// Mobile/desktop UI for the H8/3003 (H8/300H) simulator.
//
// This file owns only presentation and user interaction. It instantiates a
// single [H8Cpu] model and renders: the register/PC/CCR panel, a scrollable
// window into the 16-Mbyte sparse memory, a disassembly of the allocated
// regions, a profiler view, and a control bar (Pause / Step / Run, NMI,
// IRQ) along the bottom. A hex loader and tap-to-edit memory let the user
// get a program into memory. Follows the same scheme as sim_6502.

import 'dart:async';
import 'dart:convert';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart' show ValueListenable;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:file_picker/file_picker.dart';
import 'dmac.dart';
import 'emb_relay.dart';
import 'emb_server.dart';
import 'flash.dart';
import 'h8300h.dart';
import 'hex_files.dart';
import 'i2c_eeprom.dart';
import 'itu.dart';
import 'lcd.dart';
import 'sci.dart';
import 'sci_bridge.dart';
import 'serial_link.dart';
import 'sparse_memory.dart';
import 'symbols.dart';
import 'keypad.dart';
import 'panel_view.dart';
// Re-exported so existing importers of main.dart keep working.
export 'symbols.dart' show parseSymbolTable, symbolAddress;
// Writes a file to a chosen path on desktop/mobile; no-op stub on web.
import 'file_save_stub.dart' if (dart.library.io) 'file_save_io.dart';
// Reads a hex file named on the command line. Uses dart:io on desktop/mobile,
// and a no-op stub on web (which has no command-line file access).
import 'hex_startup_stub.dart' if (dart.library.io) 'hex_startup_io.dart';

void main(List<String> args) {
  // On the desktop builds the runner forwards command line arguments here,
  // so `sim_h83003 program.mot` loads that file at startup.
  final startup = readStartupHexFile(args);
  runApp(SimH8App(startup: startup));
}

/// Shown in the in-app About dialog. Keep [kAppVersion] in step with the
/// `version:` field in pubspec.yaml.
const String kAppVersion = '1.0.0';
const String kAppCopyright = 'Copyright © 2026 David Johnston';

/// User-adjustable settings (see the gear button in the toolbar).
class AppSettings {
  const AppSettings({
    this.darkMode = true,
    this.fontFamily = 'monospace',
    this.cpuHz = 0,
  });

  final bool darkMode;
  final String fontFamily;

  /// Target CPU clock in Hz used to pace Run mode. 0 means "max speed"
  /// (run flat-out within each frame's time budget).
  final int cpuHz;

  AppSettings copyWith({
    bool? darkMode,
    String? fontFamily,
    int? cpuHz,
  }) =>
      AppSettings(
        darkMode: darkMode ?? this.darkMode,
        fontFamily: fontFamily ?? this.fontFamily,
        cpuHz: cpuHz ?? this.cpuHz,
      );
}

class SimH8App extends StatefulWidget {
  const SimH8App({super.key, this.startup});

  /// A hex file named on the command line (path/contents/error), if any.
  final StartupHex? startup;

  @override
  State<SimH8App> createState() => _SimH8AppState();
}

class _SimH8AppState extends State<SimH8App> {
  AppSettings _settings = const AppSettings();

  ThemeData _theme(Brightness b) => ThemeData(
        useMaterial3: true,
        brightness: b,
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF2A7FFF),
          brightness: b,
        ),
      );

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'H8/3003 Simulator',
      debugShowCheckedModeBanner: false,
      theme: _theme(Brightness.light),
      darkTheme: _theme(Brightness.dark),
      themeMode: _settings.darkMode ? ThemeMode.dark : ThemeMode.light,
      home: SimulatorPage(
        startup: widget.startup,
        settings: _settings,
        onSettingsChanged: (s) => setState(() => _settings = s),
      ),
    );
  }
}

class SimulatorPage extends StatefulWidget {
  const SimulatorPage({
    super.key,
    this.startup,
    required this.settings,
    required this.onSettingsChanged,
  });

  /// A hex file named on the command line, to load once at startup.
  final StartupHex? startup;

  /// Current user settings and a callback to change them.
  final AppSettings settings;
  final ValueChanged<AppSettings> onSettingsChanged;

  @override
  State<SimulatorPage> createState() => _SimulatorPageState();
}

/// One row of the disassembly list: an instruction, or a gap marker
/// between allocated regions.
typedef _DisasmEntry = ({int addr, int len, String text, bool gap});

class _SimulatorPageState extends State<SimulatorPage>
    with SingleTickerProviderStateMixin {
  final H8Cpu cpu = H8Cpu();

  /// Whether the memory window scrolls to keep the PC in view. Off lets a
  /// chosen region stay put while the program runs, which is the only way to
  /// watch one address change.
  bool _followPcInMemory = true;

  /// The front panel shown in the Buttons tab, wired to the CPU as the real
  /// one is: a scanned matrix on port C and the latch at H'060000, reverse on
  /// a port 8 pin, and the two knobs on their quadrature pairs.
  final Keypad _keypad = Keypad();

  /// Top address shown in the memory window (drives the header label).
  int _memBase = 0x000100;

  /// Path of the last program loaded, used to prefill "Open by path".
  String? _lastProgramPath;

  /// Likewise for the last symbol table.
  String? _lastSymbolPath;

  /// Scroll controllers for the two heavy views.
  final ScrollController _memScroll = ScrollController();
  final ScrollController _disasmScroll = ScrollController();

  /// Tab controller for the Memory / Disassembly / Profile tabs.
  late final TabController _tab;

  /// Cached linear-sweep disassembly of the *allocated* memory regions,
  /// rebuilt only when the memory contents change (tracked by [_memRev]).
  List<_DisasmEntry> _disasm = const [];
  /// Bumped to repaint the views. This happens on every Run tick, so it
  /// must stay cheap — nothing expensive may key off it.
  int _memRev = 0;

  /// Bumped only when the *contents* of memory change in a way that could
  /// alter the code: an edit, a file load, a reset. The disassembly sweep is
  /// keyed off this rather than [_memRev], because sweeping a loaded firmware
  /// image takes hundreds of milliseconds and rebuilding it every repaint
  /// makes the app unresponsive.
  int _codeRev = 0;

  int _disasmRev = -1; // _codeRev the cache was built for

  /// Optional address -> label map loaded from an assembler symbol-table
  /// JSON. When present, the disassembly view shows labels at their
  /// addresses and substitutes symbolic names for operand addresses.
  Map<int, String> _symbols = const {};

  /// Name-to-address, rebuilt whenever [_symbols] is replaced, so the Trace
  /// view can resolve an entry typed as a symbol name.
  Map<String, int> _symbolsByName = const {};

  /// Locations shown in the Trace view.
  final List<TraceEntry> _traces = [];

  /// Base address of the LCD pixel buffer. The Bernina artista 180 keeps its
  /// frame buffer here; editable from the Screen tab because the SED1351's
  /// start-address registers can move it.
  int _screenBase = 0x040000;

  /// The panel is positive-mode by default (0 = light background, 3 = black),
  /// which matches the real machine; this flips it the other way up.
  bool _screenInvert = false;

  /// Drives the (cheap) screen repaint independently of the heavier memory
  /// and disassembly views. Bumped every Run tick so the display animates
  /// smoothly even while those views are throttled to 300ms.
  final ValueNotifier<int> _screenRev = ValueNotifier<int>(0);

  /// Periodic ticker used by Run mode.
  Timer? _runTimer;
  bool get _running => _runTimer != null;

  /// Measured effective clock speed (Hz) of the most recent Run.
  double _effHz = 0;

  static const int _bytesPerRow = 8;

  /// Fixed pixel height of one memory/disassembly row.
  static const double _rowExtent = 22.0;

  /// Width of each character cell in the memory view's ASCII column.
  static const double _asciiCharW = 13.0;

  /// Background colour for breakpoint cells/rows.
  static const Color _breakColor = Color(0xFFC62828);

  /// Below this many logical pixels for two panes, only one view fits and
  /// the UI falls back to a single tabbed view.
  static const double _minViewWidth = 380.0;

  /// True when the window is wide enough to show more than one view.
  bool _wide = false;

  /// Which views are shown in the wide (side-by-side) layout.
  bool _viewMem = true;
  bool _viewDis = true;
  bool _viewScr = false;
  bool _viewSci = false;
  bool _viewItu = false;
  bool _viewDma = false;
  bool _viewIo = false;
  bool _viewTrace = false;
  bool _viewProfile = false;
  bool _viewFlash = false;
  bool _viewEeprom = false;
  bool _viewPanel = false;

  /// Stable keys so each pane is *moved* (not rebuilt) when the layout
  /// switches between the tabbed and side-by-side arrangements.
  final GlobalKey _memKey = GlobalKey();
  final GlobalKey _disKey = GlobalKey();
  final GlobalKey _scrKey = GlobalKey();
  final GlobalKey _sciKey = GlobalKey();
  final GlobalKey _ituKey = GlobalKey();
  final GlobalKey _dmaKey = GlobalKey();
  final GlobalKey _ioKey = GlobalKey();
  final GlobalKey _traceKey = GlobalKey();
  final GlobalKey _profKey = GlobalKey();
  final GlobalKey _flashKey = GlobalKey();
  final GlobalKey _eepromKey = GlobalKey();
  final GlobalKey _panelKey = GlobalKey();

  /// SCI tab, host serial bridge. One simulated channel can be joined to a
  /// real port on this machine, so the software that talks to a Bernina can
  /// talk to the simulation instead.
  final SerialLink _serial = SerialLink();
  List<String> _serialPorts = const [];

  /// Port names to the labels shown in the menu, worked out when the list is
  /// refreshed. Never during a build: each lookup allocates and frees a
  /// native handle, and the menu rebuilds every frame.
  Map<String, String> _serialPortLabels = const {};
  String? _serialPortName;
  int _serialChannelIndex = 1; // SCI1 is the machine's PC port
  bool _serialOn = false;
  String _serialStatus = '';

  /// Bytes the simulation has transmitted, waiting to go out on the wire.
  /// Collected as they leave the shift register and flushed once a frame,
  /// so a burst costs one write rather than one per byte.
  final List<int> _serialOut = [];

  /// The system clock the SCI's divisors are measured against.
  ///
  /// The machine's crystal is not recorded anywhere in the dump, but two
  /// divisors pin it down: the boot ROM's default of H'11 gives exactly 19200
  /// baud at 11.0592 MHz, and the H'05 that the download protocol switches to
  /// gives exactly 57600 at the same clock. EMB-Serial, written from captures
  /// of a real machine, uses those two rates for those two divisors, which
  /// makes this a corroborated figure rather than a guess. Still editable.
  static const int _defaultPhiHz = 11059200;
  int _serialPhiHz = _defaultPhiHz;
  final TextEditingController _serialPhiCtl =
      TextEditingController(text: '$_defaultPhiHz');

  /// SCI tab, Embroidery Relay. Stands up the TCP protocol that
  /// EmbroideryCommunicator speaks and turns each request into the machine's
  /// character-at-a-time serial protocol, on the same channel the host-port
  /// bridge would use.
  EmbServer? _embServer;
  SciEmbLink? _embLink;
  EmbSerialStack? _embStack;
  EmbRelay? _embRelay;
  bool _netOn = false;
  String _netStatus = '';
  static const int _defaultRelayPort = 8888;
  final TextEditingController _netPortCtl =
      TextEditingController(text: '$_defaultRelayPort');

  /// Flash tab. With the model off nothing is attached and every address
  /// behaves as ordinary RAM, which is how the simulator has always worked.
  /// With it on, the two devices answer the JEDEC command sequences and
  /// their contents can only be changed by a proper erase or program.
  bool _flashOn = false;
  final TextEditingController _flashPathCtl = TextEditingController();
  String _flashStatus = '';

  /// Whether the last image was read as a picture of the address space
  /// rather than as the two devices concatenated. Shown in the table.
  bool _flashAddressed = false;

  /// EEPROM tab. The machine keeps its settings in a serial EEPROM on two
  /// port pins; with the model off those pins are ordinary port bits and
  /// the firmware's writes go nowhere. The array is kept in a JSON file so
  /// that what the machine wrote in one session is there in the next.
  static const String _eepromDefaultFile = 'eeprom.json';
  final I2cEeprom _eeprom = I2cEeprom();
  bool _eepromOn = false;
  final TextEditingController _eepromPathCtl =
      TextEditingController(text: _eepromDefaultFile);
  String _eepromStatus = '';

  /// Set when the machine has written and the file has not caught up yet.
  /// A write arrives from inside CPU execution, so the file is written
  /// after the fact rather than in the middle of a step.
  bool _eepromSaving = false;
  bool _eepromSaveAgain = false;

  /// True only when [c] is attached to exactly one scrollable.
  bool _attached(ScrollController c) => c.positions.length == 1;

  /// The monospace-style font family chosen in settings.
  String get _font => widget.settings.fontFamily;

  /// Theme-aware "ink" colour for text on the current background.
  Color get _ink => Theme.of(context).colorScheme.onSurface;
  Color _inkA(double a) =>
      Theme.of(context).colorScheme.onSurface.withValues(alpha: a);

  /// Accent used for register values and address columns.
  Color get _accent => Theme.of(context).brightness == Brightness.dark
      ? const Color(0xFF7FB5FF)
      : const Color(0xFF1A5FC4);

  static const Color _accentFixed = Color(0xFF2A7FFF);

  /// For notes the user needs to act on, readable on either background.
  Color get _warn => Theme.of(context).brightness == Brightness.dark
      ? const Color(0xFFFFB74D)
      : const Color(0xFFB45309);

  @override
  void initState() {
    super.initState();
    _tab = TabController(length: 12, vsync: this);
    // The front panel stays on the bus for the life of the app: it answers
    // for the key latch and holds the knob pins, and an unattached one would
    // leave the firmware scanning a dead matrix.
    cpu.attachKeypad(_keypad);
    _serialPorts = SerialLink.availablePorts();
    _serialPortLabels = SerialLink.describePorts(_serialPorts);
    _loadDemo();
    _applyStartupHex(); // override the demo if a hex file was given on CLI
    _memScroll.addListener(_onMemScroll);
    WidgetsBinding.instance
        .addPostFrameCallback((_) => _scrollToAddress(_memBase));
  }

  /// If a program file was named on the command line, load it over the demo.
  /// Intel HEX, S-records and flat binaries are all accepted; a binary has no
  /// address of its own, so it is loaded at 0 — which is what a full
  /// address-space dump wants.
  void _applyStartupHex() {
    final startup = widget.startup;
    if (startup == null) return;

    final data = startup.bytes;
    if (data == null) {
      if (startup.path != null) {
        WidgetsBinding.instance.addPostFrameCallback((_) => _showSnack(
            'Could not read ${startup.path}: ${startup.error ?? "unreadable"}.'));
      }
      return;
    }

    final result = detectProgramFormat(data) == ProgramFormat.raw
        ? loadRawBinary(data, 0, cpu.mem.poke)
        : parseHexFile(String.fromCharCodes(data), cpu.mem.poke);
    if (result.isEmpty) {
      WidgetsBinding.instance.addPostFrameCallback((_) => _showSnack(
          'Could not load ${startup.path} as Intel HEX, S-records or a '
          'binary image.'));
      return;
    }
    _memRev++;
    _codeRev++;
    if (result.startAddress != null) cpu.pc = result.startAddress!;
    _memBase =
        ((result.startAddress ?? result.minAddress ?? _memBase) & 0xFFFFF8);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _scrollToAddress(cpu.pc);
      _showSnack('Loaded ${result.bytesLoaded} bytes from ${startup.path}.');
    });
  }

  @override
  void dispose() {
    _detachSerial();
    unawaited(_detachNetwork());
    _runTimer?.cancel();
    _tab.dispose();
    _eepromPathCtl.dispose();
    _memScroll.removeListener(_onMemScroll);
    _memScroll.dispose();
    _disasmScroll.dispose();
    _screenRev.dispose();
    super.dispose();
  }

  /// Keep the header label in sync with the top visible row.
  void _onMemScroll() {
    if (!_attached(_memScroll)) return;
    final topAddr = ((_memScroll.position.pixels / _rowExtent).floor() *
            _bytesPerRow) &
        SparseMemory.addrMask;
    if (topAddr != _memBase) setState(() => _memBase = topAddr);
  }

  /// Scrolls both views so [addr] is at (or near) the top.
  void _scrollToAddress(int addr) {
    addr &= SparseMemory.addrMask;
    if (_attached(_memScroll)) {
      final row = addr ~/ _bytesPerRow;
      final target =
          (row * _rowExtent).clamp(0.0, _memScroll.position.maxScrollExtent);
      _memScroll.jumpTo(target);
    }
    _scrollDisasmTo(addr);
  }

  /// Scroll to the PC because the CPU moved it there itself -- a reset, an
  /// interrupt, or the register being set by hand -- rather than because the
  /// user asked to go somewhere.
  ///
  /// Only the memory pane honours Follow PC: with it off the view stays where
  /// it was parked, which is the point of pinning it. The disassembly always
  /// follows, since the toggle is about the memory view and a disassembly of
  /// somewhere the CPU is not is no use to anyone.
  void _scrollToPc() {
    final addr = cpu.pc & SparseMemory.addrMask;
    if (_followPcInMemory) {
      _scrollToAddress(addr);
    } else {
      _scrollDisasmTo(addr);
    }
  }

  /// Scrolls the disassembly list so the instruction at/just before [addr]
  /// is at the top.
  void _scrollDisasmTo(int addr) {
    if (!_attached(_disasmScroll)) return;
    _rebuildDisasmIfNeeded();
    final i = _disasmIndexForAddr(addr & SparseMemory.addrMask);
    if (i < 0) return;
    final target =
        (i * _rowExtent).clamp(0.0, _disasmScroll.position.maxScrollExtent);
    _disasmScroll.jumpTo(target);
  }

  /// Rebuilds the cached disassembly when memory has changed. Only the
  /// allocated regions of the sparse memory are swept; a gap row marks
  /// each skipped unallocated stretch.
  void _rebuildDisasmIfNeeded() {
    if (_disasmRev == _codeRev && _disasm.isNotEmpty) return;
    final list = <_DisasmEntry>[];
    var lastEnd = 0;
    for (final (start, end) in cpu.mem.regions()) {
      if (start > lastEnd) {
        list.add((addr: lastEnd, len: start - lastEnd, text: '', gap: true));
      }
      var a = start;
      while (a < end) {
        final d = cpu.disassemble(a);
        list.add((addr: a, len: d.length, text: d.text, gap: false));
        a += d.length;
      }
      lastEnd = end;
    }
    if (lastEnd < SparseMemory.size) {
      list.add((
        addr: lastEnd,
        len: SparseMemory.size - lastEnd,
        text: '',
        gap: true
      ));
    }
    _disasm = list;
    _disasmRev = _codeRev;
  }

  /// Binary-searches the (address-sorted) disassembly for the entry whose
  /// range contains [addr].
  int _disasmIndexForAddr(int addr) {
    if (_disasm.isEmpty) return -1;
    var lo = 0, hi = _disasm.length - 1, ans = 0;
    while (lo <= hi) {
      final mid = (lo + hi) >> 1;
      if (_disasm[mid].addr <= addr) {
        ans = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    return ans;
  }

  // --------------------------------------------------------------------
  // A small demo program: sums 1..10 (= 55 / H'37), stores the result at
  // the start of the on-chip RAM (H'FFFD10), drives it out on port 4
  // (visible in the IO tab), then sleeps.
  // --------------------------------------------------------------------
  void _loadDemo() {
    cpu.mem.clear();

    // Reset vector (vector 0): program start H'000100.
    const resetVector = [0x00, 0x00, 0x01, 0x00];
    for (var i = 0; i < resetVector.length; i++) {
      cpu.mem.poke(i, resetVector[i]);
    }

    const program = <int>[
      0x7A, 0x07, 0x00, 0xFF, 0xFF, 0x00, //       MOV.L  #H'00FFFF00,ER7  (SP)
      0xF8, 0x00, //                               MOV.B  #H'00,R0L        sum
      0xF9, 0x01, //                               MOV.B  #H'01,R1L        i
      0x08, 0x98, //                        loop:  ADD.B  R1L,R0L
      0x0A, 0x09, //                               INC.B  R1L
      0xA9, 0x0B, //                               CMP.B  #H'0B,R1L
      0x46, 0xF8, //                               BNE    loop
      0x6A, 0xA8, 0x00, 0xFF, 0xFD, 0x10, //       MOV.B  R0L,@H'FFFD10:24
      0xF1, 0xFF, //                               MOV.B  #H'FF,R1H
      0x31, 0xC5, //                               MOV.B  R1H,@H'FFFFC5:8  P4DDR
      0x38, 0xC7, //                               MOV.B  R0L,@H'FFFFC7:8  P4DR
      0x01, 0x80, //                        done:  SLEEP
      0x40, 0xFC, //                               BRA    done
    ];
    for (var i = 0; i < program.length; i++) {
      cpu.mem.poke(0x000100 + i, program[i]);
    }

    // Pre-allocate the bank holding the on-chip RAM (H'FFFD10-H'FFFF0F)
    // so it shows as real memory from the start, like the actual chip.
    cpu.mem.allocate(0xFFFD10);

    cpu.reset();
    _memBase = 0x000100;
    _memRev++;
    _codeRev++;
  }

  // --------------------------------------------------------------------
  // Simulator controls
  // --------------------------------------------------------------------

  void _step() {
    setState(() {
      cpu.clearBreakHit();
      cpu.step();
      _memRev++;
      _screenRev.value++;
      _followPc();
    });
    if (cpu.breakAddr != null) _reportDataBreak();
  }

  /// Says which address stopped the run and which instruction touched it.
  ///
  /// A data breakpoint stops *after* the access completes, so the PC is
  /// already past the instruction responsible — and in code that reads a
  /// register and immediately writes a neighbouring one, the instruction
  /// left under the cursor belongs to an address that was never watched.
  void _reportDataBreak() {
    final addr = cpu.breakAddr;
    if (addr == null) return;
    final at = cpu.breakPc;
    final name = _symbols[addr];
    final where = at == null ? '' : " by H'${_hex6(at)}";
    _showSnack("Data breakpoint: H'${_hex6(addr)}"
        '${name == null ? '' : ' ($name)'}$where');
  }

  /// Effective clock speed of the current/last Run.
  String _speedLabel() {
    if (_effHz <= 0) return '—';
    final mhz = _effHz / 1e6;
    return mhz >= 1
        ? '${mhz.toStringAsFixed(1)} MHz'
        : '${(mhz * 1000).toStringAsFixed(0)} kHz';
  }

  void _run() {
    if (_running) return;
    // Allow the very first instruction to execute even if it sits on an
    // instruction breakpoint, so Run resumes from where it was paused.
    var firstStep = true;
    // The CPU runs decoupled from the 16ms frame timer: each tick executes
    // as many instructions as fit in a wall-clock budget, optionally paced
    // to a target clock speed. The views repaint at most once per 300ms.
    final wall = Stopwatch()..start();
    final startCycles = cpu.cycles;
    var lastPaintMs = -300; // ensures the first tick repaints
    _effHz = 0;
    setState(() {
      _runTimer = Timer.periodic(const Duration(milliseconds: 16), (_) {
        _pumpSerial();
        final targetHz = widget.settings.cpuHz; // 0 == max speed
        const budgetUs = 10000; // up to ~10ms of CPU work per 16ms frame
        final tickStartUs = wall.elapsedMicroseconds;
        var paused = false;
        var stopped = false;
        while (!stopped) {
          if (targetHz > 0) {
            final allowed = (wall.elapsedMicroseconds * targetHz) ~/ 1000000;
            if (cpu.cycles - startCycles >= allowed) break;
          }
          if (wall.elapsedMicroseconds - tickStartUs >= budgetUs) break;
          // Run a small batch between clock reads to amortise their cost.
          // The batch is short so that a slow instruction — one that drives
          // a peripheral, say — cannot overrun the frame budget and make the
          // window unresponsive.
          for (var i = 0; i < 64; i++) {
            if (!firstStep &&
                cpu.instrBreaks.isNotEmpty &&
                cpu.instrBreaks.contains(cpu.pc)) {
              paused = true;
              stopped = true;
              break;
            }
            firstStep = false;
            cpu.clearBreakHit();
            cpu.step();
            // A sleeping CPU is still waiting for an interrupt, so keep
            // running; only an illegal-instruction halt ends the run.
            if (cpu.halted && !cpu.sleeping) {
              stopped = true;
              break;
            }
            if (cpu.breakHit) {
              paused = true;
              stopped = true;
              break;
            }
            // Bail out mid-batch if this frame's time is already spent.
            if ((i & 15) == 15 &&
                wall.elapsedMicroseconds - tickStartUs >= budgetUs) {
              break;
            }
          }
        }
        _pumpSerial(); // and again, so this frame's output leaves now
        final stopping = (cpu.halted && !cpu.sleeping) || paused;
        // Repaint the screen every tick so it animates smoothly. This drives
        // only the Screen pane, not the heavy memory/disassembly views.
        _screenRev.value++;
        final nowMs = wall.elapsedMilliseconds;
        if (stopping || nowMs - lastPaintMs >= 300) {
          final secs = wall.elapsedMicroseconds / 1e6;
          _effHz = secs > 0 ? (cpu.cycles - startCycles) / secs : 0;
          lastPaintMs = nowMs;
          setState(() {
            _memRev++;
            _followPc();
            if (stopping) _pause();
          });
          if (paused && cpu.breakAddr != null) _reportDataBreak();
        }
      });
    });
  }

  /// The channel the host port is joined to.
  SciChannel get _bridged => cpu.sci[_serialChannelIndex];

  /// What the simulation has currently programmed the channel to, in baud.
  int get _bridgedBaud => sciBaudAt(_bridged, _serialPhiHz);

  /// Moves bytes between the simulated channel and the host port, once a
  /// frame. Also follows the bit rate: the download protocol changes it
  /// mid-conversation, and the far end has already followed, so the host
  /// port has to as well or the link turns to noise.
  void _pumpSerial() {
    if (!_serialOn) return;
    pumpSciBridge(
      channel: _bridged,
      outgoing: _serialOut,
      phiHz: _serialPhiHz,
      setBaud: _serial.setBaud,
      drain: _serial.drain,
      send: _serial.send,
    );
  }

  void _refreshSerialPorts() {
    final ports = SerialLink.availablePorts();
    final labels = SerialLink.describePorts(ports);
    setState(() {
      _serialPorts = ports;
      _serialPortLabels = labels;
      if (_serialPortName != null && !ports.contains(_serialPortName)) {
        _serialPortName = null;
      }
    });
  }

  void _setSerialOn(bool on) {
    if (!on) {
      _detachSerial();
      setState(() => _serialStatus = 'Bridge off.');
      return;
    }

    final port = _serialPortName;
    if (port == null) {
      setState(() => _serialStatus = 'Choose a serial port first.');
      return;
    }

    final baud = _bridgedBaud;
    if (baud <= 0) {
      setState(() => _serialStatus =
          'The channel has no usable bit rate yet — run the machine until it '
          'has set up its SCI.');
      return;
    }

    final error = _serial.open(port, baud);
    if (error != null) {
      setState(() => _serialStatus = error);
      return;
    }
    // Both want the channel's transmit hook; only one can have it.
    if (_netOn) unawaited(_detachNetwork());

    _serialOut.clear();
    _bridged.onTransmit = _serialOut.add;
    setState(() {
      _serialOn = true;
      _serialStatus = 'Bridged ${_bridged.name} to $port at $baud baud.';
    });
  }

  /// Starts or stops the relay.
  ///
  /// The relay and the host-port bridge both take over the channel's
  /// transmit hook, so only one of them can be running; turning this on
  /// turns that off.
  Future<void> _setNetworkOn(bool on) async {
    if (!on) {
      await _detachNetwork();
      setState(() => _netStatus = 'Relay stopped.');
      return;
    }

    final port = int.tryParse(_netPortCtl.text.trim());
    if (port == null || port < 1 || port > 65535) {
      setState(() => _netStatus = 'That is not a usable port number.');
      return;
    }

    if (_serialOn) _detachSerial();

    final link = SciEmbLink(_bridged);
    final stack = EmbSerialStack(link);
    final relay =
        EmbRelay(stack, describeLink: () => 'simulated ${_bridged.name}');
    final server = EmbServer(relay: relay)
      ..onChanged = () {
        if (mounted) setState(() {});
      };

    final error = await server.start(port);
    if (error != null) {
      link.dispose();
      await stack.dispose();
      setState(() => _netStatus = error);
      return;
    }

    _embLink = link;
    _embStack = stack;
    _embRelay = relay;
    _embServer = server;
    setState(() {
      _netOn = true;
      _serialOn = false;
      _netStatus = 'Listening on 127.0.0.1:${server.port}, '
          'bridged to ${_bridged.name}.';
    });
  }

  Future<void> _detachNetwork() async {
    final server = _embServer;
    final link = _embLink;
    final stack = _embStack;
    _embServer = null;
    _embLink = null;
    _embStack = null;
    _embRelay = null;
    _netOn = false;
    await server?.stop();
    link?.dispose();
    await stack?.dispose();
  }

  void _detachSerial() {
    for (final c in cpu.sci) {
      c.onTransmit = null;
    }
    _serialOut.clear();
    _serial.close();
    _serialOn = false;
  }

  void _pause() {
    _runTimer?.cancel();
    _runTimer = null;
    if (mounted) setState(() {});
  }

  void _reset() {
    _pause();
    setState(cpu.reset);
    _scrollToPc();
  }

  /// Fires the non-maskable interrupt (vector 7).
  void _nmi() {
    setState(() {
      cpu.nmi();
      _memRev++;
    });
    _scrollToPc();
  }

  /// Fires IRQ0 (vector 12). If the I bit has it masked, the CPU state is
  /// unchanged and we tell the user why nothing happened.
  void _irq() {
    final taken = cpu.irq(0);
    setState(() {
      if (taken) _memRev++;
    });
    if (taken) {
      _scrollToPc();
    } else {
      _showSnack('IRQ0 ignored — the I (interrupt mask) bit is set.');
    }
  }

  /// Keep the PC visible in whichever view(s) are showing while running.
  void _followPc() {
    if (_wide) {
      if (_viewMem) _ensurePcVisibleHex();
      if (_viewDis) _ensurePcVisibleDisasm();
    } else if (_tab.index == 0) {
      _ensurePcVisibleHex();
    } else if (_tab.index == 1) {
      _ensurePcVisibleDisasm();
    }
  }

  void _ensurePcVisibleHex() {
    if (!_followPcInMemory) return;
    if (!_attached(_memScroll)) return;
    final pos = _memScroll.position;
    final firstRow = (pos.pixels / _rowExtent).floor();
    final visibleRows = (pos.viewportDimension / _rowExtent).floor();
    final pcRow = cpu.pc ~/ _bytesPerRow;
    if (pcRow < firstRow || pcRow >= firstRow + visibleRows) {
      final target = ((pcRow - 2) * _rowExtent).clamp(0.0, pos.maxScrollExtent);
      _memScroll.jumpTo(target);
    }
  }

  void _ensurePcVisibleDisasm() {
    if (!_attached(_disasmScroll)) return;
    _rebuildDisasmIfNeeded();
    final idx = _disasmIndexForAddr(cpu.pc);
    if (idx < 0) return;
    final pos = _disasmScroll.position;
    final firstRow = (pos.pixels / _rowExtent).floor();
    final visibleRows = (pos.viewportDimension / _rowExtent).floor();
    if (idx < firstRow || idx >= firstRow + visibleRows) {
      final target = ((idx - 2) * _rowExtent).clamp(0.0, pos.maxScrollExtent);
      _disasmScroll.jumpTo(target);
    }
  }

  // --------------------------------------------------------------------
  // Editing & navigation dialogs
  // --------------------------------------------------------------------

  /// A text controller pre-populated with [text] and with all of it
  /// selected, so the first keystroke replaces the existing value.
  TextEditingController _selectedController(String text) {
    return TextEditingController(text: text)
      ..selection = TextSelection(baseOffset: 0, extentOffset: text.length);
  }

  Future<void> _editByte(int addr) async {
    final controller = _selectedController(_hex2(cpu.peekBus(addr)));
    var brk = cpu.dataBreaks.contains(addr);
    final result = await showDialog<int>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setLocal) => AlertDialog(
          title: Text("Edit H'${_hex6(addr)}"),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                controller: controller,
                autofocus: true,
                textCapitalization: TextCapitalization.characters,
                inputFormatters: [
                  LengthLimitingTextInputFormatter(2),
                  FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F]')),
                ],
                decoration: const InputDecoration(
                  labelText: 'Hex byte (00-FF)',
                  prefixText: "H'",
                ),
                onSubmitted: (_) => Navigator.pop(
                    ctx, int.tryParse(controller.text, radix: 16)),
              ),
              CheckboxListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('Data breakpoint'),
                subtitle: const Text('Pause Run on read or write here'),
                value: brk,
                onChanged: (v) => setLocal(() => brk = v ?? false),
              ),
            ],
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('Cancel'),
            ),
            FilledButton(
              onPressed: () =>
                  Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
              child: const Text('Set'),
            ),
          ],
        ),
      ),
    );
    if (result != null) {
      setState(() {
        // An on-chip register belongs to its peripheral model, so write it
        // through the bus rather than poking the memory mirror: poking would
        // change nothing the CPU or the views can see. Plain memory is poked
        // directly, so that an address a peripheral does not own still takes
        // whatever byte the user typed.
        if (_ownedByPeripheral(addr)) {
          cpu.writeB(addr, result);
        } else {
          cpu.mem.poke(addr, result);
        }
        cpu.clearBreakHit(); // don't let the UI edit arm a stale break
        if (brk) {
          cpu.dataBreaks.add(addr);
        } else {
          cpu.dataBreaks.remove(addr);
        }
        _memRev++;
        _codeRev++;
      });
      // Registers that are read-only in hardware silently ignore the write.
      // Say so rather than leaving the old value sitting there unexplained.
      final now = cpu.peekBus(addr);
      if (now != result) {
        _showSnack("H'${_hex6(addr)} did not take that value — it still "
            "reads H'${_hex2(now)}. Read-only or reserved bits.");
      }
    }
  }

  /// True when an on-chip peripheral model owns [addr], rather than memory.
  bool _ownedByPeripheral(int addr) {
    if (addr >= 0xFFFFB0 && addr <= 0xFFFFBD) return true; // SCI0/SCI1
    return cpu.itu.owns(addr) || cpu.dmac.owns(addr) || cpu.adc.owns(addr);
  }

  Future<void> _gotoAddress() async {
    final controller = _selectedController(_hex6(_memBase));
    final result = await showDialog<int>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Go to address'),
        content: TextField(
          controller: controller,
          autofocus: true,
          textCapitalization: TextCapitalization.characters,
          inputFormatters: [
            LengthLimitingTextInputFormatter(6),
            FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F]')),
          ],
          decoration: const InputDecoration(
            labelText: 'Hex address (000000-FFFFFF)',
            prefixText: "H'",
          ),
          onSubmitted: (_) =>
              Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              final v = int.tryParse(controller.text, radix: 16);
              Navigator.pop(ctx, v);
            },
            child: const Text('Go'),
          ),
        ],
      ),
    );
    if (result != null) {
      _scrollToAddress(result & SparseMemory.addrMask);
    }
  }

  Future<void> _loadHex() async {
    final addrCtrl = _selectedController(_hex6(_memBase));
    final bytesCtrl = TextEditingController();
    final result = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Load hex bytes'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: addrCtrl,
              textCapitalization: TextCapitalization.characters,
              inputFormatters: [
                LengthLimitingTextInputFormatter(6),
                FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F]')),
              ],
              decoration: const InputDecoration(
                labelText: 'Start address',
                prefixText: "H'",
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: bytesCtrl,
              autofocus: true,
              maxLines: 4,
              textCapitalization: TextCapitalization.characters,
              inputFormatters: [
                FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F \n,]')),
              ],
              decoration: const InputDecoration(
                labelText: 'Bytes, e.g. F8 00 F9 01',
                border: OutlineInputBorder(),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Load'),
          ),
        ],
      ),
    );
    if (result == true) {
      final start = int.tryParse(addrCtrl.text, radix: 16) ?? _memBase;
      final tokens =
          bytesCtrl.text.split(RegExp(r'[\s,]+')).where((t) => t.isNotEmpty);
      setState(() {
        var addr = start & SparseMemory.addrMask;
        for (final t in tokens) {
          final v = int.tryParse(t, radix: 16);
          if (v != null) cpu.mem.poke(addr++, v);
        }
        _memRev++;
        _codeRev++;
      });
      _scrollToAddress(start & SparseMemory.addrMask);
    }
  }

  /// Picks a program file (local storage or a cloud provider, via the OS
  /// document picker) and loads it.
  Future<void> _loadHexFile() async {
    _pause();
    FilePickerResult? picked;
    try {
      picked = await FilePicker.pickFiles(
        // FileType.any keeps cloud sources selectable; some providers hide
        // files when a custom extension filter is applied.
        type: FileType.any,
        withData: true,
      );
    } catch (e) {
      _showSnack('Could not open the file picker: $e');
      return;
    }
    if (picked == null || picked.files.isEmpty) return; // user cancelled

    final file = picked.files.first;
    final data = file.bytes;
    if (data == null) {
      _showSnack('Could not read "${file.name}".');
      return;
    }
    await _loadProgramBytes(file.name, data, file.path);
  }

  /// Loads a program from a path the user typed, without going through the
  /// document picker. The picker is an OS component and can misbehave (a
  /// panel that will not let anything be selected, a provider that hides
  /// files); this route needs nothing but a readable path, and it is also
  /// the quickest way to reopen a dump you have open in a terminal.
  Future<void> _loadHexFileByPath() async {
    _pause();
    final path = await _askProgramPath();
    if (path == null || path.trim().isEmpty) return;
    final clean = path.trim();
    final bytes = await readBytesFromPath(clean);
    if (!mounted) return;
    if (bytes == null) {
      _showSnack('Could not read "$clean".');
      return;
    }
    await _loadProgramBytes(_baseName(clean), bytes, clean);
  }

  /// Asks for a filesystem path. Defaults to the last one loaded.
  Future<String?> _askProgramPath({
    String title = 'Open by path',
    String hint = '/Users/you/dumps/memory.bin',
    String help = 'Full path to an Intel HEX, S-record or raw binary file. '
        'The format is detected from the contents.',
    String? initial,
  }) {
    final controller = _selectedController(initial ?? _lastProgramPath ?? '');
    return showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(title),
        content: SizedBox(
          width: 520,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                help,
                style: TextStyle(fontSize: 11, color: _inkA(0.6)),
              ),
              const SizedBox(height: 12),
              TextField(
                controller: controller,
                autofocus: true,
                decoration: InputDecoration(
                  labelText: 'Path',
                  hintText: hint,
                ),
                onSubmitted: (v) => Navigator.pop(ctx, v),
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, controller.text),
            child: const Text('Open'),
          ),
        ],
      ),
    );
  }

  /// Everything after the bytes are in hand, shared by both open routes.
  Future<void> _loadProgramBytes(
      String name, List<int> data, String? path) async {
    final format = detectProgramFormat(data);
    final HexResult result;
    if (format == ProgramFormat.raw) {
      // A flat binary carries no addresses, so ask where it goes.
      final base = await _askRawLoadAddress(name, data.length);
      if (base == null || !mounted) return; // cancelled
      result = loadRawBinary(data, base, cpu.mem.poke);
    } else {
      // Intel HEX and S-records are plain ASCII text.
      result = parseHexFile(String.fromCharCodes(data), cpu.mem.poke);
    }

    if (result.isEmpty) {
      final why = result.errors.isNotEmpty
          ? result.errors.first
          : 'no data records found';
      _showSnack('Nothing loaded from "$name" ($why).');
      return;
    }
    _lastProgramPath = path ?? _lastProgramPath;

    setState(() {
      _memRev++;
      _codeRev++;
      if (result.startAddress != null) cpu.pc = result.startAddress!;
    });

    final target = result.startAddress ?? result.minAddress ?? _memBase;
    _scrollToAddress(target);

    final msg = StringBuffer('Loaded ${result.bytesLoaded} bytes');
    if (result.minAddress != null) {
      msg.write(
          " (H'${_hex6(result.minAddress!)}–H'${_hex6(result.maxAddress!)})");
    }
    if (result.startAddress != null) {
      msg.write(", start H'${_hex6(result.startAddress!)}");
    }
    if (result.errors.isNotEmpty) {
      msg.write(' — ${result.errors.length} warning(s)');
    }
    _showSnack(msg.toString());

    // If the assembler wrote a "<name>_sym.json" beside the file, load it
    // so the disassembly shows labels without picking the file by hand.
    await _autoLoadSymbols(path);
  }

  /// Asks where a flat binary should be loaded. A file exactly the size of
  /// the address space is a full dump and defaults to H'000000; anything
  /// else defaults to the address the memory view is showing.
  Future<int?> _askRawLoadAddress(String name, int length) async {
    final wholeSpace = length == SparseMemory.size;
    final suggested = wholeSpace ? 0 : _memBase;
    final controller = _selectedController(_hex6(suggested));
    return showDialog<int>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Load binary'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              '"$name" is a flat binary of $length bytes '
              "(H'${length.toRadixString(16).toUpperCase()}), so it carries "
              'no load address of its own.',
              style: const TextStyle(fontSize: 12),
            ),
            if (wholeSpace) ...[
              const SizedBox(height: 8),
              Text(
                'That is exactly the size of the 16-Mbyte address space, so '
                'it looks like a full dump — load it at 0.',
                style: TextStyle(fontSize: 11, color: _inkA(0.6)),
              ),
            ],
            const SizedBox(height: 12),
            TextField(
              controller: controller,
              autofocus: true,
              textCapitalization: TextCapitalization.characters,
              inputFormatters: [
                LengthLimitingTextInputFormatter(6),
                FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F]')),
              ],
              decoration: const InputDecoration(
                labelText: 'Load address',
                prefixText: "H'",
              ),
              onSubmitted: (_) =>
                  Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () =>
                Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
            child: const Text('Load'),
          ),
        ],
      ),
    );
  }

  /// Saves the allocated memory regions as an Intel HEX file.
  Future<void> _saveHexFile() async {
    _pause();
    final text = memoryToIntelHex(cpu.mem);
    final bytes = Uint8List.fromList(text.codeUnits);
    try {
      final path = await FilePicker.saveFile(
        dialogTitle: 'Save memory as Intel HEX',
        fileName: 'memory.hex',
        bytes: bytes,
      );
      if (path == null) {
        _showSnack('Save cancelled.');
        return;
      }
      // On desktop, saveFile returns a path without writing — write here.
      await writeBytesToPath(path, bytes);
      _showSnack('Saved allocated memory to $path');
    } catch (e) {
      _showSnack('Save failed: $e');
    }
  }

  void _showSnack(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context)
      ..clearSnackBars()
      ..showSnackBar(SnackBar(content: Text(message)));
  }

  void _openAbout() {
    showAboutDialog(
      context: context,
      applicationName: 'sim_h83003',
      applicationVersion: 'Version $kAppVersion',
      applicationIcon: const Icon(Icons.memory, size: 40),
      applicationLegalese: kAppCopyright,
      children: const [
        SizedBox(height: 12),
        Text(
          'An interactive H8/3003 (H8/300H core, advanced mode) '
          'microcontroller simulator: live ER0-ER7 registers and CCR '
          'flags, a hex memory view, a disassembler, and a profiler, over '
          'a sparse 16-Mbyte address space where 64K banks are allocated '
          'as they are touched. On-chip RAM lives at H\'FFFD10-H\'FFFF0F; '
          'the exception vector table starts at H\'000000 with the reset '
          'vector.',
        ),
      ],
    );
  }

  /// Opens the settings dialog (theme, font, run speed).
  Future<void> _openSettings() async {
    await showDialog<void>(
      context: context,
      builder: (ctx) {
        var s = widget.settings;
        var clearBreakpoints = false;
        return StatefulBuilder(
          builder: (ctx, setLocal) {
            void apply(AppSettings ns) {
              setLocal(() => s = ns);
              widget.onSettingsChanged(ns);
            }

            return AlertDialog(
              title: const Text('Settings'),
              content: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Text('Theme'),
                      const Spacer(),
                      SegmentedButton<bool>(
                        segments: const [
                          ButtonSegment(
                              value: false,
                              label: Text('Light'),
                              icon: Icon(Icons.light_mode_outlined)),
                          ButtonSegment(
                              value: true,
                              label: Text('Dark'),
                              icon: Icon(Icons.dark_mode_outlined)),
                        ],
                        selected: {s.darkMode},
                        onSelectionChanged: (sel) =>
                            apply(s.copyWith(darkMode: sel.first)),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  Row(
                    children: [
                      const Text('Font'),
                      const Spacer(),
                      DropdownButton<String>(
                        value: s.fontFamily,
                        items: const [
                          DropdownMenuItem(
                              value: 'monospace', child: Text('Monospace')),
                          DropdownMenuItem(
                              value: 'sans-serif', child: Text('Sans-serif')),
                          DropdownMenuItem(value: 'serif', child: Text('Serif')),
                        ],
                        onChanged: (v) =>
                            v == null ? null : apply(s.copyWith(fontFamily: v)),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  Row(
                    children: [
                      const Text('Run speed'),
                      const Spacer(),
                      DropdownButton<int>(
                        value: s.cpuHz,
                        items: const [
                          DropdownMenuItem(value: 0, child: Text('Max')),
                          DropdownMenuItem(
                              value: 16000000, child: Text('16 MHz')),
                          DropdownMenuItem(
                              value: 8000000, child: Text('8 MHz')),
                          DropdownMenuItem(
                              value: 2000000, child: Text('2 MHz')),
                          DropdownMenuItem(
                              value: 1000000, child: Text('1 MHz')),
                        ],
                        onChanged: (v) =>
                            v == null ? null : apply(s.copyWith(cpuHz: v)),
                      ),
                    ],
                  ),
                  const SizedBox(height: 4),
                  CheckboxListTile(
                    contentPadding: EdgeInsets.zero,
                    title: const Text('Delete all breakpoints'),
                    subtitle: Text(
                        'Clears the ${cpu.instrBreaks.length + cpu.dataBreaks.length} '
                        'breakpoint(s) when you press OK'),
                    value: clearBreakpoints,
                    onChanged: (v) =>
                        setLocal(() => clearBreakpoints = v ?? false),
                  ),
                ],
              ),
              actions: [
                TextButton(
                  onPressed: () {
                    if (clearBreakpoints) {
                      setState(() {
                        cpu.instrBreaks.clear();
                        cpu.dataBreaks.clear();
                        _memRev++;
                      });
                    }
                    Navigator.pop(ctx);
                  },
                  child: const Text('OK'),
                ),
              ],
            );
          },
        );
      },
    );
  }

  // --------------------------------------------------------------------
  // Build
  // --------------------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    _wide = MediaQuery.of(context).size.width >= 2 * _minViewWidth;
    return Scaffold(
      appBar: AppBar(
        title: const Text('H8/3003 Simulator'),
        actions: [
          Tooltip(
            message: cpu.profiling
                ? 'Profiling on — turn off to keep the counts for viewing'
                : 'Profiling off — turn on to zero the counts and record',
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(Icons.insights,
                    size: 18, color: cpu.profiling ? _accentFixed : null),
                Switch(
                  value: cpu.profiling,
                  onChanged: _setProfiling,
                  materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                ),
              ],
            ),
          ),
          IconButton(
            tooltip: 'Settings',
            onPressed: _openSettings,
            icon: const Icon(Icons.settings),
          ),
          IconButton(
            tooltip: 'Paste hex bytes',
            onPressed: _loadHex,
            icon: const Icon(Icons.file_upload_outlined),
          ),
          PopupMenuButton<int>(
            tooltip: 'Open a program file (Intel HEX, S-record or raw binary)',
            icon: const Icon(Icons.folder_open_outlined),
            onSelected: (v) => v == 0 ? _loadHexFile() : _loadHexFileByPath(),
            itemBuilder: (_) => const [
              PopupMenuItem(value: 0, child: Text('Open file…')),
              PopupMenuItem(value: 1, child: Text('Open by path…')),
            ],
          ),
          IconButton(
            tooltip: 'Save allocated memory to .hex file (Intel HEX)',
            onPressed: _saveHexFile,
            icon: const Icon(Icons.save_outlined),
          ),
          IconButton(
            tooltip: "Reset (load reset vector at H'000000)",
            onPressed: _reset,
            icon: const Icon(Icons.restart_alt),
          ),
          IconButton(
            tooltip: 'Reload demo',
            onPressed: () {
              _pause();
              setState(_loadDemo);
              _scrollToAddress(_memBase);
            },
            icon: const Icon(Icons.refresh),
          ),
          IconButton(
            tooltip: 'About sim_h83003',
            onPressed: _openAbout,
            icon: const Icon(Icons.info_outline),
          ),
        ],
      ),
      body: Column(
        children: [
          _registerPanel(),
          _nextInstruction(),
          Expanded(child: _wide ? _multiView() : _tabbedView()),
        ],
      ),
      bottomNavigationBar: _controlBar(),
    );
  }

  /// Narrow layout: a tab bar with one view visible at a time.
  Widget _tabbedView() {
    return Column(
      children: [
        TabBar(
          controller: _tab,
          isScrollable: true,
          tabAlignment: TabAlignment.start,
          tabs: const [
            Tab(text: 'Memory'),
            Tab(text: 'Disassembly'),
            Tab(text: 'Screen'),
            Tab(text: 'SCI'),
            Tab(text: 'ITU'),
            Tab(text: 'DMA'),
            Tab(text: 'IO'),
            Tab(text: 'Trace'),
            Tab(text: 'Profile'),
            Tab(text: 'Flash'),
            Tab(text: 'EEPROM'),
            Tab(text: 'Buttons'),
          ],
        ),
        Expanded(
          child: TabBarView(
            controller: _tab,
            physics: const NeverScrollableScrollPhysics(),
            children: [
              KeyedSubtree(
                  key: _memKey, child: _KeepAlive(child: _memoryView())),
              KeyedSubtree(
                  key: _disKey, child: _KeepAlive(child: _disasmView())),
              KeyedSubtree(
                  key: _scrKey, child: _KeepAlive(child: _screenView())),
              KeyedSubtree(key: _sciKey, child: _KeepAlive(child: _sciView())),
              KeyedSubtree(key: _ituKey, child: _KeepAlive(child: _ituView())),
              KeyedSubtree(key: _dmaKey, child: _KeepAlive(child: _dmaView())),
              KeyedSubtree(key: _ioKey, child: _KeepAlive(child: _ioView())),
              KeyedSubtree(
                  key: _traceKey, child: _KeepAlive(child: _traceView())),
              KeyedSubtree(
                  key: _profKey, child: _KeepAlive(child: _profileView())),
              KeyedSubtree(
                  key: _flashKey, child: _KeepAlive(child: _flashView())),
              KeyedSubtree(
                  key: _eepromKey, child: _KeepAlive(child: _eepromView())),
              KeyedSubtree(
                  key: _panelKey, child: _KeepAlive(child: _panelView())),
            ],
          ),
        ),
      ],
    );
  }

  /// Wide layout: the checked views shown side by side. Most panes share
  /// the space by flex weight; the IO pane takes a fixed [_ioViewWidth],
  /// which is exactly what its GPIO diagram needs — so the other views
  /// keep all the remaining room instead of the window having to be huge
  /// for the diagram to fit.
  Widget _multiView() {
    const wideFlex = 2;
    final entries = <({Widget pane, int flex, double? width})>[
      if (_viewMem)
        (
          pane: KeyedSubtree(
              key: _memKey, child: _KeepAlive(child: _memoryView())),
          flex: wideFlex,
          width: null
        ),
      if (_viewDis)
        (
          pane: KeyedSubtree(
              key: _disKey, child: _KeepAlive(child: _disasmView())),
          flex: wideFlex,
          width: null
        ),
      if (_viewScr)
        (
          pane: KeyedSubtree(
              key: _scrKey, child: _KeepAlive(child: _screenView())),
          flex: wideFlex,
          width: null
        ),
      if (_viewSci)
        (
          pane:
              KeyedSubtree(key: _sciKey, child: _KeepAlive(child: _sciView())),
          flex: 1,
          width: null
        ),
      if (_viewItu)
        (
          pane:
              KeyedSubtree(key: _ituKey, child: _KeepAlive(child: _ituView())),
          flex: 1,
          width: null
        ),
      if (_viewDma)
        (
          pane:
              KeyedSubtree(key: _dmaKey, child: _KeepAlive(child: _dmaView())),
          flex: 1,
          width: null
        ),
      if (_viewIo)
        (
          pane: KeyedSubtree(key: _ioKey, child: _KeepAlive(child: _ioView())),
          flex: 1,
          width: _ioViewWidth
        ),
      if (_viewTrace)
        (
          pane: KeyedSubtree(
              key: _traceKey, child: _KeepAlive(child: _traceView())),
          flex: 1,
          width: null
        ),
      if (_viewProfile)
        (
          pane: KeyedSubtree(
              key: _profKey, child: _KeepAlive(child: _profileView())),
          flex: 1,
          width: null
        ),
      if (_viewFlash)
        (
          pane: KeyedSubtree(
              key: _flashKey, child: _KeepAlive(child: _flashView())),
          flex: 1,
          width: null
        ),
      if (_viewEeprom)
        (
          pane: KeyedSubtree(
              key: _eepromKey, child: _KeepAlive(child: _eepromView())),
          flex: 1,
          width: null
        ),
      if (_viewPanel)
        (
          pane: KeyedSubtree(
              key: _panelKey, child: _KeepAlive(child: _panelView())),
          flex: 1,
          width: null
        ),
    ];
    if (entries.isEmpty) {
      return const Center(child: Text('Select a view above'));
    }
    final hasFlexible = entries.any((e) => e.width == null);
    return LayoutBuilder(
      builder: (context, constraints) {
        // Room taken by the dividers between panes.
        final dividers = (entries.length - 1).toDouble();
        // Never let a fixed pane push past the available width.
        final maxFixed = (constraints.maxWidth - dividers).clamp(0.0, 1e9);
        final children = <Widget>[];
        for (var i = 0; i < entries.length; i++) {
          if (i > 0) children.add(const VerticalDivider(width: 1, thickness: 1));
          final e = entries[i];
          children.add(e.width == null
              ? Expanded(flex: e.flex, child: e.pane)
              : SizedBox(width: e.width!.clamp(0.0, maxFixed), child: e.pane));
        }
        // With only fixed-width panes selected, soak up the leftover space
        // so they stay left-aligned rather than stretching.
        if (!hasFlexible) children.add(const Expanded(child: SizedBox()));
        return Row(children: children);
      },
    );
  }

  // ---- Registers -------------------------------------------------------

  Widget _registerPanel() {
    return Container(
      width: double.infinity,
      color: Theme.of(context).colorScheme.surfaceContainerHighest,
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Wrap(
            spacing: 14,
            runSpacing: 6,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              _reg('PC', _hex6(cpu.pc),
                  onTap: () => _editRegister(
                      'PC', cpu.pc, 6, (v) => cpu.pc = v & 0xFFFFFF)),
              for (var i = 0; i < 8; i++)
                _reg(i == 7 ? 'ER7/SP' : 'ER$i', _hex8(cpu.er[i]),
                    onTap: () => _editRegister(i == 7 ? 'ER7 (SP)' : 'ER$i',
                        cpu.er[i], 8, (v) => cpu.er[i] = v)),
              _viewToggles(),
              _cpuChip(),
              // Cycle count last: it is decimal and variable-width, so
              // keeping it rightmost stops the pills jittering.
              _reg('CYC', cpu.cycles.toString(), hex: false),
              _reg('SPD', _speedLabel(), hex: false),
            ],
          ),
          const SizedBox(height: 8),
          _flagsRow(),
        ],
      ),
    );
  }

  /// Toggles selecting which views are shown in the wide layout.
  ///
  /// A Wrap rather than a Row: there are more views than fit across a narrow
  /// window, and a second line of toggles is better than a clipped one.
  Widget _viewToggles() {
    return Wrap(
      spacing: 4,
      runSpacing: 4,
      crossAxisAlignment: WrapCrossAlignment.center,
      children: [
        _viewToggle('MEM', _viewMem, (v) => _viewMem = v),
        _viewToggle('DIS', _viewDis, (v) => _viewDis = v),
        _viewToggle('SCR', _viewScr, (v) => _viewScr = v),
        _viewToggle('SCI', _viewSci, (v) => _viewSci = v),
        _viewToggle('ITU', _viewItu, (v) => _viewItu = v),
        _viewToggle('DMA', _viewDma, (v) => _viewDma = v),
        _viewToggle('IO', _viewIo, (v) => _viewIo = v),
        _viewToggle('TRACE', _viewTrace, (v) => _viewTrace = v),
        _viewToggle('PROF', _viewProfile, (v) => _viewProfile = v),
        _viewToggle('FLASH', _viewFlash, (v) => _viewFlash = v),
        _viewToggle('EEPROM', _viewEeprom, (v) => _viewEeprom = v),
        _viewToggle('BTN', _viewPanel, (v) => _viewPanel = v),
      ],
    );
  }

  Widget _viewToggle(String label, bool on, void Function(bool) apply) {
    final enabled = _wide;
    final selectedCount = (_viewMem ? 1 : 0) +
        (_viewDis ? 1 : 0) +
        (_viewScr ? 1 : 0) +
        (_viewSci ? 1 : 0) +
        (_viewItu ? 1 : 0) +
        (_viewDma ? 1 : 0) +
        (_viewIo ? 1 : 0) +
        (_viewTrace ? 1 : 0) +
        (_viewProfile ? 1 : 0) +
        (_viewFlash ? 1 : 0) +
        (_viewEeprom ? 1 : 0) +
        (_viewPanel ? 1 : 0);
    return Tooltip(
      message: enabled
          ? 'Show the $label view'
          : 'Widen the window to show more than one view',
      child: InkWell(
        onTap: enabled
            ? () {
                if (on && selectedCount <= 1) return;
                setState(() => apply(!on));
              }
            : null,
        borderRadius: BorderRadius.circular(4),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 4),
          decoration: BoxDecoration(
            color: (enabled && on)
                ? _accentFixed
                : Theme.of(context).colorScheme.surfaceContainerLow,
            borderRadius: BorderRadius.circular(4),
            border: Border.all(color: Colors.black26),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                on ? Icons.check_box : Icons.check_box_outline_blank,
                size: 13,
                color:
                    !enabled ? _inkA(0.25) : (on ? Colors.white : _inkA(0.55)),
              ),
              const SizedBox(width: 3),
              Text(
                label,
                style: TextStyle(
                  fontSize: 11,
                  fontWeight: FontWeight.bold,
                  color: !enabled
                      ? _inkA(0.25)
                      : (on ? Colors.white : _inkA(0.7)),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  /// A pill showing the emulated CPU and memory footprint.
  Widget _cpuChip() {
    final banks = cpu.mem.allocatedBankCount;
    return Tooltip(
      message: 'H8/3003 — H8/300H CPU, advanced mode, 16-Mbyte address '
          'space. $banks of ${SparseMemory.numBanks} 64K banks allocated.',
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
        decoration: BoxDecoration(
          color: _accentFixed,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: Colors.black26),
        ),
        child: Text(
          'H8/3003 · $banks×64K',
          style: const TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.bold,
            color: Colors.white,
          ),
        ),
      ),
    );
  }

  Widget _reg(String label, String value,
      {VoidCallback? onTap, bool hex = true}) {
    final content = Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Text('$label ',
            style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
        Text(hex ? "H'$value" : value,
            style: TextStyle(fontFamily: _font, fontSize: 14, color: _accent)),
        if (onTap != null) ...[
          const SizedBox(width: 2),
          Icon(Icons.edit, size: 11, color: _inkA(0.3)),
        ],
      ],
    );
    if (onTap == null) return content;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(4),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
        child: content,
      ),
    );
  }

  /// Pops a hex-entry dialog to edit a register. [digits] is 6 for the
  /// 24-bit PC and 8 for the 32-bit ERn registers.
  Future<void> _editRegister(
    String name,
    int current,
    int digits,
    void Function(int) apply,
  ) async {
    final controller = _selectedController(
      current.toRadixString(16).toUpperCase().padLeft(digits, '0'),
    );
    final result = await showDialog<int>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Edit $name'),
        content: TextField(
          controller: controller,
          autofocus: true,
          textCapitalization: TextCapitalization.characters,
          inputFormatters: [
            LengthLimitingTextInputFormatter(digits),
            FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F]')),
          ],
          decoration: InputDecoration(
            labelText: 'Hex ($digits digits)',
            prefixText: "H'",
          ),
          onSubmitted: (_) =>
              Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () =>
                Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
            child: const Text('Set'),
          ),
        ],
      ),
    );
    if (result != null) {
      setState(() => apply(result));
      if (name == 'PC') _scrollToPc();
    }
  }

  Widget _flagsRow() {
    // H8/300H condition-code register: I UI H U N Z V C.
    const flags = [
      ('I', H8Flag.i),
      ('UI', H8Flag.ui),
      ('H', H8Flag.h),
      ('U', H8Flag.u),
      ('N', H8Flag.n),
      ('Z', H8Flag.z),
      ('V', H8Flag.v),
      ('C', H8Flag.c),
    ];
    return Row(
      children: [
        const Text('CCR ',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
        for (final f in flags)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 3),
            child: _flagChip(f.$1, cpu.getFlag(f.$2),
                onTap: () => _toggleFlag(f.$2)),
          ),
      ],
    );
  }

  void _toggleFlag(int mask) {
    setState(() => cpu.setFlag(mask, !cpu.getFlag(mask)));
  }

  Widget _flagChip(String name, bool on, {VoidCallback? onTap}) {
    final chip = Container(
      width: name.length > 1 ? 28 : 22,
      height: 22,
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: on
            ? _accentFixed
            : Theme.of(context).colorScheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: Colors.black26),
      ),
      child: Text(
        name,
        style: TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.bold,
          color: on ? Colors.white : _inkA(0.45),
        ),
      ),
    );
    if (onTap == null) return chip;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(4),
      child: chip,
    );
  }

  Widget _nextInstruction() {
    final d = cpu.disassemble(cpu.pc);
    final label = cpu.halted ? (cpu.sleeping ? 'SLEEPING' : 'HALTED') : 'NEXT';
    return Container(
      width: double.infinity,
      color: Theme.of(context).colorScheme.surfaceContainerHighest,
      padding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
      child: Row(
        children: [
          Text(label,
              style: TextStyle(
                  fontSize: 11,
                  fontWeight: FontWeight.bold,
                  color: cpu.halted ? Colors.orangeAccent : _inkA(0.55))),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              cpu.halted && !cpu.sleeping
                  ? "H'${_hex6(cpu.pc)}:  ${cpu.haltReason}"
                  : "H'${_hex6(cpu.pc)}:  ${d.text}",
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontFamily: _font, fontSize: 14, color: _ink),
            ),
          ),
        ],
      ),
    );
  }

  // ---- Memory window -----------------------------------------------------

  Widget _memoryView() {
    return Column(
      children: [
        _memHeader(),
        Expanded(
          child: ListView.builder(
            controller: _memScroll,
            itemExtent: _rowExtent,
            itemCount: SparseMemory.size ~/ _bytesPerRow,
            itemBuilder: (ctx, row) => _memRow(row * _bytesPerRow),
          ),
        ),
      ],
    );
  }

  Widget _memHeader() {
    return Container(
      color: Theme.of(context).colorScheme.surfaceContainer,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      child: Row(
        children: [
          const Icon(Icons.memory, size: 18),
          const SizedBox(width: 6),
          const Text('Memory', style: TextStyle(fontWeight: FontWeight.bold)),
          Expanded(
            child: Align(
              alignment: Alignment.centerRight,
              child: FittedBox(
                fit: BoxFit.scaleDown,
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Tooltip(
                      message: _followPcInMemory
                          ? 'Following the PC: the view scrolls to it as the '
                              'program runs'
                          : 'Not following: the view stays where you put it',
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Text('Follow PC',
                              style: TextStyle(
                                  fontSize: 12,
                                  color: _inkA(
                                      _followPcInMemory ? 0.85 : 0.45))),
                          const SizedBox(width: 2),
                          Switch(
                            key: const Key('followPcSwitch'),
                            value: _followPcInMemory,
                            materialTapTargetSize:
                                MaterialTapTargetSize.shrinkWrap,
                            onChanged: (v) => setState(() {
                              _followPcInMemory = v;
                              // Turning it back on should catch up at once
                              // rather than waiting for the PC to move.
                              if (v) _ensurePcVisibleHex();
                            }),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(width: 4),
                    TextButton.icon(
                      onPressed: _gotoAddress,
                      icon: const Icon(Icons.my_location, size: 16),
                      label: Text("Go to  H'${_hex6(_memBase)}"),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _memRow(int rowAddr) {
    final pcInRow = cpu.pc >= rowAddr && cpu.pc < rowAddr + _bytesPerRow;
    final allocated = cpu.mem.isAllocated(rowAddr);
    return Container(
      color: pcInRow ? const Color(0x222A7FFF) : Colors.transparent,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 1),
      child: Row(
        children: [
          SizedBox(
            width: 76,
            child: Text("H'${_hex6(rowAddr)}",
                style: TextStyle(
                    fontFamily: _font,
                    fontSize: 13,
                    color: allocated ? _accent : _inkA(0.3))),
          ),
          for (var i = 0; i < _bytesPerRow; i++)
            _memCell(rowAddr + i, allocated),
          const SizedBox(width: 8),
          _asciiColumn(rowAddr, allocated),
        ],
      ),
    );
  }

  /// The character column on the right (plain ASCII; other bytes show as
  /// a dot). Fixed one-em cells keep the hex columns aligned.
  Widget _asciiColumn(int rowAddr, bool allocated) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        for (var i = 0; i < _bytesPerRow; i++)
          SizedBox(
            width: _asciiCharW,
            child: Text(
              allocated ? _asciiGlyph(cpu.peekBus(rowAddr + i)) : '·',
              textAlign: TextAlign.center,
              style: TextStyle(
                fontFamily: _font,
                fontSize: 13,
                color: _inkA(allocated ? 0.6 : 0.2),
              ),
            ),
          ),
      ],
    );
  }

  static String _asciiGlyph(int code) =>
      (code >= 0x20 && code < 0x7F) ? String.fromCharCode(code) : '·';

  Widget _memCell(int addr, bool allocated) {
    final isPc = addr == cpu.pc;
    final isBreak = cpu.dataBreaks.contains(addr);
    final Color? bg = isPc ? _accentFixed : (isBreak ? _breakColor : null);
    return Expanded(
      child: GestureDetector(
        onTap: () => _editByte(addr),
        child: Container(
          alignment: Alignment.center,
          padding: const EdgeInsets.symmetric(vertical: 1),
          margin: const EdgeInsets.symmetric(horizontal: 1),
          decoration: bg != null
              ? BoxDecoration(
                  color: bg,
                  borderRadius: BorderRadius.circular(3),
                  // PC sitting on a data breakpoint: fill + red outline.
                  border: (isPc && isBreak)
                      ? Border.all(color: _breakColor, width: 1.5)
                      : null,
                )
              : null,
          child: Text(
            _hex2(cpu.peekBus(addr)),
            style: TextStyle(
              fontFamily: _font,
              fontSize: 13,
              color: isPc
                  ? Colors.white
                  : (isBreak
                      ? Colors.white
                      : (allocated ? _ink : _inkA(0.25))),
              fontWeight:
                  (isPc || isBreak) ? FontWeight.bold : FontWeight.normal,
            ),
          ),
        ),
      ),
    );
  }

  // ---- Disassembly view ----------------------------------------------------

  /// Opens a symbol table and maps its addresses to labels. Accepts a JSON
  /// `{ "label": address }` object (address as an int or a hex string), a
  /// JSON list of `"name = H'hhhhhh"`-style listing lines, or a plain-text
  /// `.sym` file of those lines with `;`, `#` or `//` comments.
  Future<void> _loadSymbols() async {
    // Don't leave the emulator running flat out behind a modal file panel.
    _pause();
    try {
      final picked =
          await FilePicker.pickFiles(type: FileType.any, withData: true);
      if (picked == null || picked.files.isEmpty) return;
      final bytes = picked.files.first.bytes;
      if (bytes == null) {
        _showSnack('Could not read that file.');
        return;
      }
      _applyLoadedSymbols(bytes);
    } catch (e) {
      _showSnack('Symbol load failed: $e');
    }
  }

  /// Loads a symbol table from a path the user typed, bypassing the document
  /// picker entirely — the same escape hatch the program loader has.
  Future<void> _loadSymbolsByPath() async {
    _pause();
    final path = await _askProgramPath(
      title: 'Open symbols by path',
      hint: '/Users/you/dumps/memory.sym',
      help: 'Full path to a symbol table: a plain-text .sym of '
          "NAME = H'ADDRESS lines, or the JSON form.",
      initial: _lastSymbolPath,
    );
    if (path == null || path.trim().isEmpty) return;
    final clean = path.trim();
    final bytes = await readBytesFromPath(clean);
    if (!mounted) return;
    if (bytes == null) {
      _showSnack('Could not read "$clean".');
      return;
    }
    if (_applyLoadedSymbols(bytes)) _lastSymbolPath = clean;
  }

  /// Installs a symbol table and reports the outcome. True when it took.
  bool _applyLoadedSymbols(List<int> bytes) {
    final n = _applySymbolBytes(bytes);
    if (n == 0) {
      _showSnack('No symbols found in that file.');
      return false;
    }
    _showSnack('Loaded $n symbols.');
    return true;
  }

  /// Parses a symbol table from [bytes] and installs it, returning the
  /// number of symbols loaded.
  int _applySymbolBytes(List<int> bytes) {
    final String text;
    try {
      text = utf8.decode(bytes);
    } catch (_) {
      return 0;
    }
    final map = parseSymbolTable(text);
    if (map.isEmpty) return 0;
    setState(() {
      _symbols = map;
      _symbolsByName = {
        for (final e in map.entries) e.value: e.key,
      };
    });
    return map.length;
  }

  /// After loading a hex file at [hexPath], look for a sibling symbol
  /// table — `<name>_sym.json` or `<name>.sym` — and load it automatically.
  Future<void> _autoLoadSymbols(String? hexPath) async {
    if (hexPath == null) return;
    final dot = hexPath.lastIndexOf('.');
    if (dot <= 0) return;
    final stem = hexPath.substring(0, dot);
    for (final symPath in ['${stem}_sym.json', '$stem.sym']) {
      final bytes = await readBytesFromPath(symPath);
      if (bytes == null) continue; // no sibling symbol file
      final n = _applySymbolBytes(bytes);
      if (n > 0) {
        _showSnack('Auto-loaded $n symbols from ${_baseName(symPath)}.');
        return;
      }
    }
  }

  String _baseName(String path) => path.split(RegExp(r'[\\/]')).last;

  /// Replaces `H'xxxxxx` operand addresses in a disassembly line with
  /// their symbol names (leaving `#H'xx` immediates alone).
  String _subSymbols(String text) {
    if (_symbols.isEmpty) return text;
    return text.replaceAllMapped(
      RegExp(r"(?<!#)H'([0-9A-Fa-f]{4}|[0-9A-Fa-f]{6})(?![0-9A-Fa-f])"),
      (m) {
        final addr = int.parse(m.group(1)!, radix: 16) & SparseMemory.addrMask;
        return _symbols[addr] ?? m.group(0)!;
      },
    );
  }

  Widget _disasmView() {
    _rebuildDisasmIfNeeded();
    return Column(
      children: [
        _disasmHeader(),
        Expanded(
          child: ListView.builder(
            controller: _disasmScroll,
            itemExtent: _rowExtent,
            itemCount: _disasm.length,
            itemBuilder: (ctx, i) => _disasmRow(_disasm[i]),
          ),
        ),
      ],
    );
  }

  Widget _disasmHeader() {
    return Container(
      color: Theme.of(context).colorScheme.surfaceContainer,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      child: Row(
        children: [
          const Icon(Icons.list_alt, size: 18),
          const SizedBox(width: 6),
          const Text('Disassembly',
              style: TextStyle(fontWeight: FontWeight.bold)),
          Expanded(
            child: Align(
              alignment: Alignment.centerRight,
              child: FittedBox(
                fit: BoxFit.scaleDown,
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    PopupMenuButton<int>(
                      tooltip: 'Load a symbol table',
                      onSelected: (v) =>
                          v == 0 ? _loadSymbols() : _loadSymbolsByPath(),
                      itemBuilder: (_) => const [
                        PopupMenuItem(value: 0, child: Text('Open file…')),
                        PopupMenuItem(value: 1, child: Text('Open by path…')),
                      ],
                      child: Padding(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 8, vertical: 8),
                        child: Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            const Icon(Icons.label_outline, size: 16),
                            const SizedBox(width: 6),
                            Text(_symbols.isEmpty
                                ? 'Symbols'
                                : 'Symbols (${_symbols.length})'),
                          ],
                        ),
                      ),
                    ),
                    TextButton.icon(
                      onPressed: _gotoAddress,
                      icon: const Icon(Icons.my_location, size: 16),
                      label: const Text('Go to'),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _disasmRow(_DisasmEntry e) {
    if (e.gap) {
      final endAddr = e.addr + e.len - 1;
      return Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 1),
        alignment: Alignment.centerLeft,
        child: Text(
          "····  H'${_hex6(e.addr)}–H'${_hex6(endAddr)}  unallocated",
          style: TextStyle(
              fontFamily: _font,
              fontSize: 12,
              fontStyle: FontStyle.italic,
              color: _inkA(0.35)),
        ),
      );
    }
    final isPc = cpu.pc >= e.addr && cpu.pc < e.addr + e.len;
    final isBreak = cpu.instrBreaks.contains(e.addr);
    // The raw bytes for this instruction (long ones get an ellipsis).
    final shown = e.len > 6 ? 5 : e.len;
    var bytes = [
      for (var i = 0; i < shown; i++) _hex2(cpu.mem.peek(e.addr + i)),
    ].join(' ');
    if (shown < e.len) bytes = '$bytes …';
    return GestureDetector(
      // Tap a row to set/clear an instruction breakpoint at its address.
      onTap: () => _toggleInstrBreak(e.addr),
      child: Container(
        color: isBreak
            ? _breakColor
            : (isPc ? const Color(0x222A7FFF) : Colors.transparent),
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 1),
        child: Row(
          children: [
            // PC marker.
            SizedBox(
              width: 14,
              child: isPc
                  ? Icon(Icons.play_arrow,
                      size: 13, color: isBreak ? Colors.white : _accentFixed)
                  : null,
            ),
            SizedBox(
              width: 72,
              child: Text("H'${_hex6(e.addr)}",
                  style: TextStyle(
                      fontFamily: _font,
                      fontSize: 13,
                      color: isBreak ? Colors.white : _accent)),
            ),
            SizedBox(
              width: 132,
              child: Text(bytes,
                  style: TextStyle(
                      fontFamily: _font,
                      fontSize: 13,
                      color: isBreak ? Colors.white70 : _inkA(0.45))),
            ),
            const SizedBox(width: 6),
            Expanded(
              child: Text.rich(
                TextSpan(children: [
                  // Show the label defined at this address, if any.
                  if (_symbols[e.addr] != null)
                    TextSpan(
                      text: '${_symbols[e.addr]}: ',
                      style: TextStyle(
                          color:
                              isBreak ? Colors.white : const Color(0xFFE0A030),
                          fontWeight: FontWeight.bold),
                    ),
                  TextSpan(text: _subSymbols(e.text)),
                ]),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                    fontFamily: _font,
                    fontSize: 13,
                    fontWeight: isPc ? FontWeight.bold : FontWeight.normal,
                    color: isBreak ? Colors.white : _ink),
              ),
            ),
          ],
        ),
      ),
    );
  }

  void _toggleInstrBreak(int addr) {
    setState(() {
      if (cpu.instrBreaks.contains(addr)) {
        cpu.instrBreaks.remove(addr);
      } else {
        cpu.instrBreaks.add(addr);
      }
    });
  }

  // ---- Screen view ---------------------------------------------------------

  // ---- Touch panel ---------------------------------------------------------
  //
  // The artista 180 reads a 4-wire resistive panel through the CPU's A/D
  // converter: the X axis on AN4 and the Y axis on AN6, taking the top eight
  // bits of each conversion. Clicking the simulated screen feeds those two
  // channels, so the firmware sees a finger press.
  //
  // The panel's calibration is not known, so the pixel-to-reading mapping is
  // linear between adjustable endpoints. The firmware treats a reading below
  // H'4C as no touch, so the defaults sit above that and releasing drives
  // both channels to zero.

  // Channel assignment, from traces taken on the machine itself: the Y
  // position arrives in result register A and the X position in register B.
  // Driving X into B rather than C is what made the screen start responding.
  // Register C swings between zero and full scale and looks like a finger
  // detect, so it can be driven too, but that is a guess and it is off by
  // default.
  //
  // Each result register is fed by two pins — ADDRA by AN0 or AN4, ADDRB by
  // AN1 or AN5 — and this firmware scans all eight, so a register carries
  // whichever of its pair converted most recently. Driving only one of a
  // pair therefore makes the register alternate between the injected value
  // and whatever the other pin reads; [_touchDrivePairs] drives both.
  int _touchChannelX = 5; // AN5 -> ADDRB
  int _touchChannelY = 4; // AN4 -> ADDRA
  int _touchChannelDetect = 6; // AN6 -> ADDRC
  bool _touchDetectEnabled = false;
  // On by default: with only one pin of a pair driven the result register
  // alternates between the injected value and zero, and the firmware never
  // sees a steady reading. Driving both is what makes a click register — the
  // screen redraws on a press with it on, and does nothing with it off.
  bool _touchDrivePairs = true;
  int _touchDetectOn = 0xFF;

  /// Reading that corresponds to the left/top edge and the right/bottom edge.
  ///
  /// Both axes run from zero, so a click maps straight onto the panel with
  /// no offset. The firmware's H'4C floor on the Y reading is not a
  /// no-touch threshold to be avoided but a deliberate dead band: a press
  /// above roughly the top third of the screen reads below it and is
  /// ignored, which is what the machine does.
  int _touchXMin = 0x00, _touchXMax = 0xF0;
  int _touchYMin = 0x00, _touchYMax = 0xF0;

  /// Where the pointer is, in panel pixels, while pressed.
  Offset? _touchPoint;

  int _touchRawX = 0, _touchRawY = 0;

  /// Converts a point on the panel into the two channel readings and applies
  /// them. A null point lifts the finger.
  void _applyTouch(Offset? panelPoint) {
    setState(() {
      _touchPoint = panelPoint;
      if (panelPoint == null) {
        _touchRawX = 0;
        _touchRawY = 0;
      } else {
        final fx = (panelPoint.dx / (_LcdPane.width - 1)).clamp(0.0, 1.0);
        final fy = (panelPoint.dy / (_LcdPane.height - 1)).clamp(0.0, 1.0);
        _touchRawX = (_touchXMin + fx * (_touchXMax - _touchXMin)).round();
        _touchRawY = (_touchYMin + fy * (_touchYMax - _touchYMin)).round();
      }
      _driveTouchChannel(_touchChannelX, _touchRawX);
      _driveTouchChannel(_touchChannelY, _touchRawY);
      if (_touchDetectEnabled) {
        _driveTouchChannel(
            _touchChannelDetect, panelPoint == null ? 0 : _touchDetectOn);
      }
    });
  }

  /// Sets one analog input, and its partner on the same result register when
  /// [_touchDrivePairs] is on, so the register does not alternate between the
  /// injected value and the other pin of the pair.
  void _driveTouchChannel(int channel, int value) {
    cpu.adc.setInput8(channel, value);
    if (_touchDrivePairs) cpu.adc.setInput8(channel ^ 4, value);
  }

  /// Maps a pointer position inside the pane onto panel pixels.
  Offset _toPanel(Offset local, Size paneSize) => Offset(
        local.dx / paneSize.width * _LcdPane.width,
        local.dy / paneSize.height * _LcdPane.height,
      );

  /// The Buttons tab. The panel drives the keypad model, which the firmware
  /// reads through the matrix and the port pins for itself.
  Widget _panelView() {
    return PanelView(
      keypad: _keypad,
      repaint: _screenRev,
      askedFor: () =>
          (cpu.peekBus(0x11B10E) << 8) | cpu.peekBus(0x11B10F),
    );
  }

  Widget _screenView() {
    return Column(
      children: [
        _screenHeader(),
        Expanded(
          child: Container(
            color: const Color(0xFF101214),
            padding: const EdgeInsets.all(12),
            child: Center(
              child: AspectRatio(
                aspectRatio: _LcdPane.width / _LcdPane.height,
                child: LayoutBuilder(
                  builder: (context, constraints) {
                    final paneSize =
                        Size(constraints.maxWidth, constraints.maxHeight);
                    return GestureDetector(
                      behavior: HitTestBehavior.opaque,
                      onTapDown: (d) =>
                          _applyTouch(_toPanel(d.localPosition, paneSize)),
                      onTapUp: (_) => _applyTouch(null),
                      onTapCancel: () => _applyTouch(null),
                      onPanDown: (d) =>
                          _applyTouch(_toPanel(d.localPosition, paneSize)),
                      onPanUpdate: (d) =>
                          _applyTouch(_toPanel(d.localPosition, paneSize)),
                      onPanEnd: (_) => _applyTouch(null),
                      onPanCancel: () => _applyTouch(null),
                      child: Stack(
                        fit: StackFit.expand,
                        children: [
                          _LcdPane(
                            cpu: cpu,
                            base: _screenBase,
                            invert: _screenInvert,
                            memRev: _memRev,
                            tick: _screenRev,
                          ),
                          if (_touchPoint != null)
                            Positioned(
                              left: _touchPoint!.dx /
                                      _LcdPane.width *
                                      paneSize.width -
                                  9,
                              top: _touchPoint!.dy /
                                      _LcdPane.height *
                                      paneSize.height -
                                  9,
                              child: IgnorePointer(
                                child: Container(
                                  width: 18,
                                  height: 18,
                                  decoration: BoxDecoration(
                                    shape: BoxShape.circle,
                                    border: Border.all(
                                        color: _accentFixed, width: 2),
                                  ),
                                ),
                              ),
                            ),
                        ],
                      ),
                    );
                  },
                ),
              ),
            ),
          ),
        ),
        _touchStrip(),
        if (!cpu.mem.isAllocated(_screenBase))
          Container(
            width: double.infinity,
            color: Theme.of(context).colorScheme.surfaceContainerHigh,
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            child: Text(
              "Nothing has been written at H'${_hex6(_screenBase)} yet — "
              'the frame buffer is unallocated, so the panel reads as all '
              'zeroes.',
              style: TextStyle(fontSize: 11, color: _inkA(0.6)),
            ),
          ),
      ],
    );
  }

  /// The touch readout under the panel: what the click is feeding the A/D,
  /// and a way to adjust the mapping while calibrating.
  Widget _touchStrip() {
    final touching = _touchPoint != null;
    return Container(
      width: double.infinity,
      color: Theme.of(context).colorScheme.surfaceContainerHigh,
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      child: Row(
        children: [
          Icon(touching ? Icons.touch_app : Icons.touch_app_outlined,
              size: 16, color: touching ? _accentFixed : _inkA(0.4)),
          const SizedBox(width: 6),
          Text(
            touching
                ? 'touch  (${_touchPoint!.dx.round()}, '
                    '${_touchPoint!.dy.round()})'
                : 'click the panel to press',
            style: TextStyle(
                fontSize: 11,
                color: touching ? _ink : _inkA(0.55)),
          ),
          const SizedBox(width: 12),
          Text(
            "X AN$_touchChannelX H'${_hex2(_touchRawX)}"
            "   Y AN$_touchChannelY H'${_hex2(_touchRawY)}"
            "${_touchDetectEnabled ? '   det AN$_touchChannelDetect' : ''}",
            style: TextStyle(
                fontFamily: _font,
                fontSize: 11,
                color: touching ? _accent : _inkA(0.45)),
          ),
          const Spacer(),
          TextButton.icon(
            onPressed: _editTouchCalibration,
            icon: const Icon(Icons.tune, size: 15),
            label: const Text('Calibrate'),
            style: TextButton.styleFrom(visualDensity: VisualDensity.compact),
          ),
        ],
      ),
    );
  }

  /// The panel's real calibration is unknown, so the endpoints of the linear
  /// pixel-to-reading map are adjustable.
  Future<void> _editTouchCalibration() async {
    final xMin = _selectedController(_hex2(_touchXMin));
    final xMax = _selectedController(_hex2(_touchXMax));
    final yMin = _selectedController(_hex2(_touchYMin));
    final yMax = _selectedController(_hex2(_touchYMax));
    final detOn = _selectedController(_hex2(_touchDetectOn));
    var chX = _touchChannelX;
    var chY = _touchChannelY;
    var chDet = _touchChannelDetect;
    var detEnabled = _touchDetectEnabled;
    var drivePairs = _touchDrivePairs;

    Widget field(String label, TextEditingController c) => SizedBox(
          width: 96,
          child: TextField(
            controller: c,
            textCapitalization: TextCapitalization.characters,
            inputFormatters: [
              LengthLimitingTextInputFormatter(2),
              FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F]')),
            ],
            decoration: InputDecoration(labelText: label, prefixText: "H'"),
          ),
        );

    // Which result register a channel lands in, which is the thing the
    // machine's own traces were taken against.
    String reg(int ch) => 'ADDR${'ABCD'[ch & 3]}';

    Widget channelPicker(String label, int value, void Function(int) set) =>
        Row(
          children: [
            SizedBox(
                width: 68,
                child: Text(label, style: const TextStyle(fontSize: 12))),
            DropdownButton<int>(
              value: value,
              isDense: true,
              onChanged: (v) => v == null ? null : set(v),
              items: [
                for (var ch = 0; ch < 8; ch++)
                  DropdownMenuItem(
                      value: ch, child: Text('AN$ch  -> ${reg(ch)}')),
              ],
            ),
          ],
        );

    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setLocal) => AlertDialog(
          title: const Text('Touch calibration'),
          content: SizedBox(
            width: 460,
            child: SingleChildScrollView(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'A click is converted to A/D readings, linearly between '
                    'these endpoints. Traces from the machine put the Y '
                    'position in ADDRA and the X position in ADDRB, both '
                    "running from zero. The firmware's H'4C floor on Y is a "
                    'dead band, not a threshold to clear: a press near the '
                    'top of the screen reads below it and is ignored, as on '
                    'the machine.',
                    style: TextStyle(fontSize: 11, color: _inkA(0.65)),
                  ),
                  const SizedBox(height: 14),
                  channelPicker('X input', chX, (v) => setLocal(() => chX = v)),
                  channelPicker('Y input', chY, (v) => setLocal(() => chY = v)),
                  const SizedBox(height: 10),
                  Row(children: [
                    field('X at left', xMin),
                    const SizedBox(width: 10),
                    field('X at right', xMax)
                  ]),
                  const SizedBox(height: 10),
                  Row(children: [
                    field('Y at top', yMin),
                    const SizedBox(width: 10),
                    field('Y at bottom', yMax)
                  ]),
                  const Divider(height: 24),
                  CheckboxListTile(
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                    value: drivePairs,
                    onChanged: (v) =>
                        setLocal(() => drivePairs = v ?? false),
                    title: const Text('Drive both inputs of each pair',
                        style: TextStyle(fontSize: 13)),
                    subtitle: Text(
                      'ADDRA is fed by AN0 or AN4 and ADDRB by AN1 or AN5, '
                      'and this firmware scans all eight — so driving one of '
                      'a pair leaves the register alternating with the other '
                      'pin.',
                      style: TextStyle(fontSize: 10, color: _inkA(0.6)),
                    ),
                  ),
                  CheckboxListTile(
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                    value: detEnabled,
                    onChanged: (v) => setLocal(() => detEnabled = v ?? false),
                    title: const Text('Also drive a finger-detect input',
                        style: TextStyle(fontSize: 13)),
                    subtitle: Text(
                      'ADDRC swings between zero and full scale on the real '
                      'machine, which may be a contact detect. This is a '
                      'guess, so it is off unless you turn it on.',
                      style: TextStyle(fontSize: 10, color: _inkA(0.6)),
                    ),
                  ),
                  if (detEnabled) ...[
                    channelPicker('Detect', chDet,
                        (v) => setLocal(() => chDet = v)),
                    const SizedBox(height: 6),
                    field('Value when touched', detOn),
                  ],
                ],
              ),
            ),
          ),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(ctx, false),
                child: const Text('Cancel')),
            FilledButton(
                onPressed: () => Navigator.pop(ctx, true),
                child: const Text('Apply')),
          ],
        ),
      ),
    );
    if (ok != true) return;
    setState(() {
      // Let go of any input the old settings were holding, so a channel that
      // is no longer used does not keep its last value for ever.
      for (var ch = 0; ch < 8; ch++) {
        cpu.adc.setInput8(ch, 0);
      }
      _touchChannelX = chX;
      _touchChannelY = chY;
      _touchChannelDetect = chDet;
      _touchDetectEnabled = detEnabled;
      _touchDrivePairs = drivePairs;
      _touchDetectOn = int.tryParse(detOn.text, radix: 16) ?? _touchDetectOn;
      _touchXMin = int.tryParse(xMin.text, radix: 16) ?? _touchXMin;
      _touchXMax = int.tryParse(xMax.text, radix: 16) ?? _touchXMax;
      _touchYMin = int.tryParse(yMin.text, radix: 16) ?? _touchYMin;
      _touchYMax = int.tryParse(yMax.text, radix: 16) ?? _touchYMax;
    });
    _applyTouch(_touchPoint);
  }

  Widget _screenHeader() {
    return Container(
      color: Theme.of(context).colorScheme.surfaceContainer,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      child: Row(
        children: [
          const Icon(Icons.tv, size: 18),
          const SizedBox(width: 6),
          const Text('Screen', style: TextStyle(fontWeight: FontWeight.bold)),
          const SizedBox(width: 8),
          Text('${_LcdPane.width}×${_LcdPane.height}, 2bpp',
              style: TextStyle(fontSize: 11, color: _inkA(0.55))),
          Expanded(
            child: Align(
              alignment: Alignment.centerRight,
              child: FittedBox(
                fit: BoxFit.scaleDown,
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    TextButton.icon(
                      onPressed: () =>
                          setState(() => _screenInvert = !_screenInvert),
                      icon: Icon(
                          _screenInvert
                              ? Icons.invert_colors
                              : Icons.invert_colors_off,
                          size: 16),
                      label: const Text('Invert'),
                    ),
                    TextButton.icon(
                      onPressed: _editScreenBase,
                      icon: const Icon(Icons.my_location, size: 16),
                      label: Text("Base  H'${_hex6(_screenBase)}"),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _editScreenBase() async {
    final controller = _selectedController(_hex6(_screenBase));
    final result = await showDialog<int>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Frame buffer address'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            TextField(
              controller: controller,
              autofocus: true,
              textCapitalization: TextCapitalization.characters,
              inputFormatters: [
                LengthLimitingTextInputFormatter(6),
                FilteringTextInputFormatter.allow(RegExp('[0-9a-fA-F]')),
              ],
              decoration: const InputDecoration(
                labelText: 'Hex address',
                prefixText: "H'",
              ),
              onSubmitted: (_) =>
                  Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
            ),
            const SizedBox(height: 10),
            Text(
              '${_LcdPane.width}×${_LcdPane.height} pixels, 2 bits each, '
              '${_LcdPane.stride} bytes per line — '
              '${_LcdPane.bytes} (H\'${_LcdPane.bytes.toRadixString(16).toUpperCase()}) bytes in all.',
              style: TextStyle(fontSize: 11, color: _inkA(0.6)),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () =>
                Navigator.pop(ctx, int.tryParse(controller.text, radix: 16)),
            child: const Text('Set'),
          ),
        ],
      ),
    );
    if (result != null) {
      setState(() => _screenBase = result & SparseMemory.addrMask);
    }
  }

  // ---- SCI view ------------------------------------------------------------

  /// Which channel's detail the SCI tab is showing.
  int _sciChannel = 0;

  /// System clock assumed when converting the bit rate to baud. The divisor
  /// is exact; the crystal is not, so this is a display aid only.
  static const double _assumedPhi = 11059200; // gives 19200 at BRR = 17

  Widget _sciView() {
    final ch = cpu.sci[_sciChannel];
    return Column(
      children: [
        _sciHeader(),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.fromLTRB(12, 8, 12, 12),
            children: [
              _sciSummary(ch),
              const SizedBox(height: 10),
              _sciRegisters(ch),
              const SizedBox(height: 12),
              _sciFlagRow('SCR', ch.scr, const [
                ('TIE', SciControl.tie),
                ('RIE', SciControl.rie),
                ('TE', SciControl.te),
                ('RE', SciControl.re),
                ('MPIE', SciControl.mpie),
                ('TEIE', SciControl.teie),
                ('CKE1', SciControl.cke1),
                ('CKE0', SciControl.cke0),
              ]),
              const SizedBox(height: 6),
              _sciFlagRow('SSR', ch.ssr, const [
                ('TDRE', SciStatus.tdre),
                ('RDRF', SciStatus.rdrf),
                ('ORER', SciStatus.orer),
                ('FER', SciStatus.fer),
                ('PER', SciStatus.per),
                ('TEND', SciStatus.tend),
                ('MPB', SciStatus.mpb),
                ('MPBT', SciStatus.mpbt),
              ]),
              const SizedBox(height: 14),
              _sciTransmitted(ch),
              const SizedBox(height: 18),
              _sciHostPort(),
              const SizedBox(height: 12),
              _sciNetworkRelay(),
            ],
          ),
        ),
      ],
    );
  }

  /// Bridge to a real serial port on this machine. Sits at the bottom of the
  /// SCI tab because it belongs to the channel above it rather than to the
  /// machine as a whole.
  Widget _sciHostPort() {
    final baud = _bridgedBaud;
    final label = TextStyle(fontFamily: _font, color: _ink);
    final dim = TextStyle(fontFamily: _font, color: _inkA(0.7));

    return Container(
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        border: Border.all(color: _inkA(0.25)),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('HOST SERIAL PORT', style: label),
          const SizedBox(height: 8),
          Wrap(
            spacing: 12,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              SizedBox(
                width: 300,
                child: DropdownButtonFormField<String>(
                  key: const Key('serialPortDropdown'),
                  initialValue: _serialPortName,
                  isExpanded: true,
                  decoration: const InputDecoration(
                    isDense: true,
                    border: OutlineInputBorder(),
                    labelText: 'Port',
                  ),
                  items: [
                    for (final p in _serialPorts)
                      DropdownMenuItem(
                        value: p,
                        child: Text(_serialPortLabels[p] ?? p,
                            overflow: TextOverflow.ellipsis, style: dim),
                      ),
                  ],
                  onChanged: _serialOn
                      ? null
                      : (v) => setState(() => _serialPortName = v),
                ),
              ),
              OutlinedButton(
                key: const Key('serialRefreshButton'),
                onPressed: _serialOn ? null : _refreshSerialPorts,
                child: const Text('Refresh'),
              ),
              SizedBox(
                width: 130,
                child: DropdownButtonFormField<int>(
                  key: const Key('serialChannelDropdown'),
                  initialValue: _serialChannelIndex,
                  decoration: const InputDecoration(
                    isDense: true,
                    border: OutlineInputBorder(),
                    labelText: 'Channel',
                  ),
                  items: [
                    for (var i = 0; i < cpu.sci.length; i++)
                      DropdownMenuItem(
                          value: i, child: Text(cpu.sci[i].name, style: dim)),
                  ],
                  onChanged: _serialOn
                      ? null
                      : (v) =>
                          setState(() => _serialChannelIndex = v ?? 1),
                ),
              ),
              SizedBox(
                width: 150,
                child: TextField(
                  key: const Key('serialPhiField'),
                  controller: _serialPhiCtl,
                  style: dim,
                  decoration: const InputDecoration(
                    isDense: true,
                    border: OutlineInputBorder(),
                    labelText: 'Clock φ (Hz)',
                  ),
                  onChanged: (t) {
                    final v = int.tryParse(t.trim());
                    if (v != null && v > 0) setState(() => _serialPhiHz = v);
                  },
                ),
              ),
              Row(mainAxisSize: MainAxisSize.min, children: [
                Switch(
                  key: const Key('serialEnableSwitch'),
                  value: _serialOn,
                  onChanged: _setSerialOn,
                ),
                Text('Bridge', style: label),
              ]),
            ],
          ),
          const SizedBox(height: 10),
          Text(
            'The channel is set to '
            '${baud > 0 ? "$baud baud" : "no usable rate yet"} '
            '(BRR H\'${_hex2b(_bridged.brr)}, CKS ${_bridged.smr & SciMode.cks}'
            ' at $_serialPhiHz Hz). The host port follows this while the '
            'bridge is on, so a protocol that changes rate mid-conversation '
            'keeps working.',
            style: dim,
          ),
          if (_serialStatus.isNotEmpty) ...[
            const SizedBox(height: 6),
            Text(_serialStatus, style: label),
          ],
          if (_serialOn) ...[
            const SizedBox(height: 6),
            Text(
              'in ${_serial.bytesIn}  out ${_serial.bytesOut}'
              '${_serial.lastError != null ? "   ${_serial.lastError}" : ""}',
              style: dim,
            ),
          ],
        ],
      ),
    );
  }

  static String _hex2b(int v) =>
      (v & 0xFF).toRadixString(16).toUpperCase().padLeft(2, '0');

  /// The Embroidery Relay listener. Below the host-port section because it is
  /// the other way of reaching the same channel: one puts the machine on a
  /// real wire, the other puts it on a socket.
  Widget _sciNetworkRelay() {
    final server = _embServer;
    final label = TextStyle(fontFamily: _font, color: _ink);
    final dim = TextStyle(fontFamily: _font, color: _inkA(0.7));

    return Container(
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        border: Border.all(color: _inkA(0.25)),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('NETWORK CONNECTION', style: label),
          const SizedBox(height: 8),
          Wrap(
            spacing: 12,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              SizedBox(
                width: 130,
                child: TextField(
                  key: const Key('relayPortField'),
                  controller: _netPortCtl,
                  enabled: !_netOn,
                  style: dim,
                  decoration: const InputDecoration(
                    isDense: true,
                    border: OutlineInputBorder(),
                    labelText: 'TCP port',
                  ),
                ),
              ),
              FilledButton(
                key: const Key('relayEnableButton'),
                onPressed: () => _setNetworkOn(!_netOn),
                child: Text(_netOn ? 'Stop relay' : 'Start relay'),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Text(
            'Speaks the Embroidery Relay TCP protocol, so EmbroideryCommunicator '
            'can drive the simulated machine as if it were a real one. Each '
            'request becomes the character-at-a-time serial exchange on '
            '${_bridged.name}, which is why the machine has to be running for '
            'the relay to answer.',
            style: dim,
          ),
          if (_netStatus.isNotEmpty) ...[
            const SizedBox(height: 6),
            Text(_netStatus, style: label),
          ],
          if (_netOn && server != null) ...[
            const SizedBox(height: 6),
            Text(
              server.client == null
                  ? 'No client connected.  ${server.requestsHandled} requests '
                      'handled.'
                  : 'Client ${server.client}.  ${server.requestsHandled} '
                      'requests handled.'
                      '${_embRelay?.lastRequest.isNotEmpty == true ? "  Last: ${_embRelay!.lastRequest}" : ""}'
                      '${_embStack?.lastCommand.isNotEmpty == true ? " -> ${_embStack!.lastCommand}" : ""}',
              style: dim,
            ),
            if (!_running) ...[
              const SizedBox(height: 6),
              Text(
                'The machine is paused, so the relay cannot get an answer out '
                'of it. Press Run.',
                style: TextStyle(fontFamily: _font, color: _warn),
              ),
            ],
            if (server.lastError != null) ...[
              const SizedBox(height: 6),
              Text(server.lastError!, style: dim),
            ],
          ],
        ],
      ),
    );
  }

  Widget _sciHeader() {
    return Container(
      color: Theme.of(context).colorScheme.surfaceContainer,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      child: Row(
        children: [
          const Icon(Icons.cable, size: 18),
          const SizedBox(width: 6),
          const Text('Serial', style: TextStyle(fontWeight: FontWeight.bold)),
          const Spacer(),
          SegmentedButton<int>(
            segments: [
              for (var i = 0; i < cpu.sci.length; i++)
                ButtonSegment(value: i, label: Text(cpu.sci[i].name)),
            ],
            selected: {_sciChannel},
            showSelectedIcon: false,
            style: ButtonStyle(
              visualDensity: VisualDensity.compact,
              textStyle: WidgetStatePropertyAll(TextStyle(fontSize: 12)),
            ),
            onSelectionChanged: (s) => setState(() => _sciChannel = s.first),
          ),
        ],
      ),
    );
  }

  /// The framing and bit rate the registers currently describe.
  Widget _sciSummary(SciChannel ch) {
    final enabled = <String>[
      if ((ch.scr & SciControl.te) != 0) 'TX',
      if ((ch.scr & SciControl.re) != 0) 'RX',
    ];
    final baud = ch.baudAt(_assumedPhi);
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(6),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(ch.framing,
                  style: TextStyle(
                      fontFamily: _font,
                      fontSize: 15,
                      fontWeight: FontWeight.bold,
                      color: _accent)),
              const SizedBox(width: 12),
              Text(
                enabled.isEmpty ? 'disabled' : enabled.join(' + '),
                style: TextStyle(
                    fontSize: 12,
                    color: enabled.isEmpty ? _inkA(0.45) : _accent),
              ),
              const Spacer(),
              if (ch.transmitting)
                Text('sending…',
                    style: TextStyle(fontSize: 11, color: _accent)),
            ],
          ),
          const SizedBox(height: 4),
          Text(
            '${ch.statesPerBit} states/bit, ${ch.bitsPerChar} bits per '
            'character — ${baud.toStringAsFixed(0)} baud at '
            '${(_assumedPhi / 1e6).toStringAsFixed(4)} MHz',
            style: TextStyle(fontSize: 11, color: _inkA(0.6)),
          ),
        ],
      ),
    );
  }

  Widget _sciRegisters(SciChannel ch) {
    Widget row(String name, int addr, int value, String note) => Padding(
          padding: const EdgeInsets.symmetric(vertical: 2),
          child: Row(
            children: [
              SizedBox(
                width: 52,
                child: Text(name,
                    style: const TextStyle(
                        fontSize: 12, fontWeight: FontWeight.bold)),
              ),
              SizedBox(
                width: 78,
                child: Text("H'${_hex6(addr)}",
                    style: TextStyle(
                        fontFamily: _font, fontSize: 12, color: _inkA(0.5))),
              ),
              SizedBox(
                width: 46,
                child: Text("H'${_hex2(value)}",
                    style: TextStyle(
                        fontFamily: _font, fontSize: 13, color: _accent)),
              ),
              Expanded(
                child: Text(note,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(fontSize: 11, color: _inkA(0.6))),
              ),
            ],
          ),
        );

    return Column(
      children: [
        row('SMR', ch.smrAddr, ch.smr, 'mode: ${ch.framing}'),
        row('BRR', ch.brrAddr, ch.brr, 'bit rate divisor'),
        row('SCR', ch.scrAddr, ch.scr, 'control / interrupt enables'),
        row('TDR', ch.tdrAddr, ch.tdr, 'transmit data'),
        row('SSR', ch.ssrAddr, ch.ssr, 'status'),
        row('RDR', ch.rdrAddr, ch.rdr, 'receive data'),
      ],
    );
  }

  Widget _sciFlagRow(String label, int value, List<(String, int)> bits) {
    return Row(
      children: [
        SizedBox(
          width: 44,
          child: Text(label,
              style:
                  const TextStyle(fontWeight: FontWeight.bold, fontSize: 12)),
        ),
        Expanded(
          child: Wrap(
            spacing: 4,
            runSpacing: 4,
            children: [
              for (final b in bits) _sciFlagChip(b.$1, (value & b.$2) != 0),
            ],
          ),
        ),
      ],
    );
  }

  Widget _sciFlagChip(String name, bool on) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
      decoration: BoxDecoration(
        color: on
            ? _accentFixed
            : Theme.of(context).colorScheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: Colors.black26),
      ),
      child: Text(
        name,
        style: TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.bold,
          color: on ? Colors.white : _inkA(0.45),
        ),
      ),
    );
  }

  /// The bytes the firmware has put on the wire, as a hex dump.
  Widget _sciTransmitted(SciChannel ch) {
    final log = ch.txLog;
    const perRow = 16;
    final rows = (log.length + perRow - 1) ~/ perRow;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            const Text('Transmitted',
                style: TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
            const SizedBox(width: 8),
            // Flexible, not fixed: in the side-by-side layout this pane can
            // be narrow enough that the count and the button together do not
            // fit, and an unbounded Text overflows the row.
            Flexible(
              child: Text(
                ch.txCount == log.length
                    ? '${ch.txCount} bytes'
                    : '${ch.txCount} bytes (showing the last ${log.length})',
                style: TextStyle(fontSize: 11, color: _inkA(0.55)),
                overflow: TextOverflow.ellipsis,
              ),
            ),
            const Spacer(),
            TextButton.icon(
              onPressed: log.isEmpty
                  ? null
                  : () => setState(() {
                        ch.txLog.clear();
                        ch.txCount = 0;
                      }),
              icon: const Icon(Icons.clear_all, size: 16),
              label: const Text('Clear'),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Container(
          height: 220,
          decoration: BoxDecoration(
            color: Theme.of(context).colorScheme.surfaceContainerLow,
            borderRadius: BorderRadius.circular(6),
          ),
          child: log.isEmpty
              ? Center(
                  child: Text(
                    'Nothing sent yet.\nBytes appear here as the program '
                    'writes them to TDR.',
                    textAlign: TextAlign.center,
                    style: TextStyle(fontSize: 12, color: _inkA(0.5)),
                  ),
                )
              : ListView.builder(
                  padding: const EdgeInsets.all(8),
                  itemCount: rows,
                  itemExtent: 20,
                  itemBuilder: (_, r) => _sciDumpRow(log, r * perRow, perRow),
                ),
        ),
      ],
    );
  }

  Widget _sciDumpRow(List<int> log, int start, int perRow) {
    final end = (start + perRow) > log.length ? log.length : start + perRow;
    final slice = log.sublist(start, end);
    final hex = slice.map(_hex2).join(' ');
    final ascii = slice
        .map((b) => (b >= 0x20 && b < 0x7F) ? String.fromCharCode(b) : '·')
        .join();
    return Row(
      children: [
        SizedBox(
          width: 56,
          child: Text(_hex4(start),
              style: TextStyle(
                  fontFamily: _font, fontSize: 12, color: _inkA(0.45))),
        ),
        SizedBox(
          width: 372,
          child: Text(hex,
              style:
                  TextStyle(fontFamily: _font, fontSize: 12, color: _ink)),
        ),
        Text(ascii,
            style: TextStyle(
                fontFamily: _font, fontSize: 12, color: _inkA(0.55))),
      ],
    );
  }

  static String _hex4(int v) =>
      (v & 0xFFFF).toRadixString(16).toUpperCase().padLeft(4, '0');

  // ---- ITU view ------------------------------------------------------------

  Widget _ituView() {
    final itu = cpu.itu;
    return Column(
      children: [
        _ituHeader(),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.fromLTRB(12, 8, 12, 12),
            children: [
              _ituSharedRegisters(itu),
              const SizedBox(height: 12),
              for (final ch in itu.channels) ...[
                _ituChannelCard(ch),
                const SizedBox(height: 8),
              ],
            ],
          ),
        ),
      ],
    );
  }

  Widget _ituHeader() {
    final running = cpu.itu.channels.where((c) => c.running).length;
    return Container(
      color: Theme.of(context).colorScheme.surfaceContainer,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      child: Row(
        children: [
          const Icon(Icons.timer_outlined, size: 18),
          const SizedBox(width: 6),
          const Text('Timers (ITU)',
              style: TextStyle(fontWeight: FontWeight.bold)),
          const SizedBox(width: 10),
          Text(
            running == 0
                ? 'all stopped'
                : '$running of ${cpu.itu.channels.length} running',
            style: TextStyle(fontSize: 11, color: _inkA(0.55)),
          ),
          const Spacer(),
        ],
      ),
    );
  }

  /// TSTR/TSNC/TMDR/TFCR/TOER/TOCR, which apply across the channels.
  Widget _ituSharedRegisters(Itu itu) {
    Widget reg(String name, int addr, int value) => Padding(
          padding: const EdgeInsets.only(right: 14, bottom: 2),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text('$name ',
                  style: const TextStyle(
                      fontSize: 11, fontWeight: FontWeight.bold)),
              Text("H'${_hex2(value)}",
                  style: TextStyle(
                      fontFamily: _font, fontSize: 12, color: _accent)),
            ],
          ),
        );

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(6),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Shared',
              style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12)),
          const SizedBox(height: 6),
          Wrap(
            children: [
              reg('TSTR', 0xFFFF60, itu.tstr),
              reg('TSNC', 0xFFFF61, itu.tsnc),
              reg('TMDR', 0xFFFF62, itu.tmdr),
              reg('TFCR', 0xFFFF63, itu.tfcr),
              reg('TOER', 0xFFFF90, itu.toer),
              reg('TOCR', 0xFFFF91, itu.tocr),
            ],
          ),
          const SizedBox(height: 6),
          Row(
            children: [
              Text('start bits ',
                  style: TextStyle(fontSize: 11, color: _inkA(0.6))),
              for (var i = cpu.itu.channels.length - 1; i >= 0; i--)
                Padding(
                  padding: const EdgeInsets.only(right: 4),
                  child: _sciFlagChip('STR$i', cpu.itu.channels[i].running),
                ),
            ],
          ),
        ],
      ),
    );
  }

  /// One channel: counter, general registers, clock, and its flags.
  Widget _ituChannelCard(ItuChannel ch) {
    final pending = ch.pendingVector() != null;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(6),
        border: Border.all(
          color: ch.running ? _accentFixed.withValues(alpha: 0.6) : Colors.black26,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(ch.name,
                  style: const TextStyle(
                      fontWeight: FontWeight.bold, fontSize: 13)),
              const SizedBox(width: 8),
              Text(ch.running ? 'running' : 'stopped',
                  style: TextStyle(
                      fontSize: 11,
                      color: ch.running ? _accent : _inkA(0.45))),
              const SizedBox(width: 10),
              Text('${ch.clockSource} · ${ch.clearSource}',
                  style: TextStyle(fontSize: 11, color: _inkA(0.55))),
              const Spacer(),
              if (pending)
                Text('interrupt pending',
                    style: TextStyle(fontSize: 11, color: _accent)),
            ],
          ),
          const SizedBox(height: 6),
          // The counter, big enough to read at a glance, with its targets.
          Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Text('TCNT ',
                  style: TextStyle(fontSize: 11, color: _inkA(0.6))),
              Text("H'${_hex4(ch.tcnt)}",
                  style: TextStyle(
                      fontFamily: _font,
                      fontSize: 16,
                      fontWeight: FontWeight.bold,
                      color: _accent)),
              const SizedBox(width: 14),
              Text("GRA H'${_hex4(ch.gra)}   GRB H'${_hex4(ch.grb)}"
                  "${ch.hasBuffers ? "   BRA H'${_hex4(ch.bra)}   BRB H'${_hex4(ch.brb)}" : ''}",
                  style: TextStyle(
                      fontFamily: _font, fontSize: 12, color: _inkA(0.75))),
            ],
          ),
          const SizedBox(height: 4),
          // Progress towards the GRA compare match, when there is one.
          if (ch.gra > 0)
            LinearProgressIndicator(
              value: (ch.tcnt / (ch.gra == 0 ? 0xFFFF : ch.gra)).clamp(0.0, 1.0),
              minHeight: 3,
              backgroundColor: _inkA(0.12),
            ),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: Text(
                  "TCR H'${_hex2(ch.tcr)}  TIOR H'${_hex2(ch.tior)}  "
                  "TIER H'${_hex2(ch.tier)}  TSR H'${_hex2(ch.tsr)}",
                  style: TextStyle(
                      fontFamily: _font, fontSize: 11, color: _inkA(0.6)),
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          Row(
            children: [
              Text('flags ', style: TextStyle(fontSize: 11, color: _inkA(0.6))),
              _sciFlagChip('IMFA', (ch.tsr & ItuStatus.imfa) != 0),
              const SizedBox(width: 4),
              _sciFlagChip('IMFB', (ch.tsr & ItuStatus.imfb) != 0),
              const SizedBox(width: 4),
              _sciFlagChip('OVF', (ch.tsr & ItuStatus.ovf) != 0),
              const SizedBox(width: 14),
              Text('enables ',
                  style: TextStyle(fontSize: 11, color: _inkA(0.6))),
              _sciFlagChip('IMIEA', (ch.tier & ItuInterrupt.imiea) != 0),
              const SizedBox(width: 4),
              _sciFlagChip('IMIEB', (ch.tier & ItuInterrupt.imieb) != 0),
              const SizedBox(width: 4),
              _sciFlagChip('OVIE', (ch.tier & ItuInterrupt.ovie) != 0),
            ],
          ),
        ],
      ),
    );
  }

  // ---- DMA view ------------------------------------------------------------

  Widget _dmaView() {
    return Column(
      children: [
        _dmaHeader(),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.fromLTRB(12, 8, 12, 12),
            children: [
              for (final ch in cpu.dmac.channels) ...[
                _dmaChannelCard(ch),
                const SizedBox(height: 8),
              ],
            ],
          ),
        ),
      ],
    );
  }

  Widget _dmaHeader() {
    final active = cpu.dmac.channels
        .where((c) => c.fullAddress
            ? c.fullEnabled
            : (c.a.enabled || c.b.enabled))
        .length;
    return Container(
      color: Theme.of(context).colorScheme.surfaceContainer,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      child: Row(
        children: [
          const Icon(Icons.swap_horiz, size: 18),
          const SizedBox(width: 6),
          const Text('DMA', style: TextStyle(fontWeight: FontWeight.bold)),
          const SizedBox(width: 10),
          Text(
            active == 0 ? 'no channel enabled' : '$active enabled',
            style: TextStyle(fontSize: 11, color: _inkA(0.55)),
          ),
          const Spacer(),
          Text('${cpu.dmac.transferCount} transfers',
              style: TextStyle(fontSize: 11, color: _inkA(0.55))),
        ],
      ),
    );
  }

  Widget _dmaChannelCard(DmacChannel ch) {
    final full = ch.fullAddress;
    final on = full ? ch.fullEnabled : (ch.a.enabled || ch.b.enabled);
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(6),
        border: Border.all(
            color: on ? _accentFixed.withValues(alpha: 0.6) : Colors.black26),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text('Channel ${ch.index}',
                  style: const TextStyle(
                      fontWeight: FontWeight.bold, fontSize: 13)),
              const SizedBox(width: 8),
              Text(ch.modeName,
                  style: TextStyle(fontSize: 11, color: _inkA(0.6))),
              const SizedBox(width: 10),
              Text(on ? 'enabled' : 'disabled',
                  style: TextStyle(
                      fontSize: 11, color: on ? _accent : _inkA(0.45))),
              const Spacer(),
              Text("H'${_hex6(ch.base)}",
                  style: TextStyle(
                      fontFamily: _font, fontSize: 11, color: _inkA(0.4))),
            ],
          ),
          const SizedBox(height: 6),
          if (full) _dmaFullBody(ch) else _dmaShortBody(ch),
        ],
      ),
    );
  }

  /// Full address mode: MARA is the source, MARB the destination.
  Widget _dmaFullBody(DmacChannel ch) {
    final total = ch.a.etcrReload == 0 ? 1 : ch.a.etcrReload;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text("source H'${_hex6(ch.a.mar)}",
                style: TextStyle(
                    fontFamily: _font, fontSize: 13, color: _accent)),
            Icon(Icons.arrow_right_alt, size: 18, color: _inkA(0.6)),
            Text("dest H'${_hex6(ch.b.mar)}",
                style: TextStyle(
                    fontFamily: _font, fontSize: 13, color: _accent)),
            const SizedBox(width: 14),
            Text('${ch.a.etcr} left of $total',
                style: TextStyle(fontSize: 11, color: _inkA(0.6))),
          ],
        ),
        const SizedBox(height: 4),
        LinearProgressIndicator(
          value: (1 - ch.a.etcr / total).clamp(0.0, 1.0),
          minHeight: 3,
          backgroundColor: _inkA(0.12),
        ),
        const SizedBox(height: 6),
        Text(
          "DTCRA H'${_hex2(ch.a.dtcr)}   DTCRB H'${_hex2(ch.b.dtcr)}   "
          '${ch.a.wordSize ? 'word' : 'byte'} transfers',
          style: TextStyle(fontFamily: _font, fontSize: 11, color: _inkA(0.6)),
        ),
        const SizedBox(height: 6),
        Row(children: [
          _sciFlagChip('DTE', ch.a.enabled),
          const SizedBox(width: 4),
          _sciFlagChip('DTME', (ch.b.dtcr & DmacControlB.dtme) != 0),
          const SizedBox(width: 4),
          _sciFlagChip('SAIDE', (ch.a.dtcr & DmacControlA.saide) != 0),
          const SizedBox(width: 4),
          _sciFlagChip('DAIDE', (ch.b.dtcr & DmacControlB.daide) != 0),
          const SizedBox(width: 4),
          _sciFlagChip('DTIE', (ch.a.dtcr & DmacControl.dtie) != 0),
        ]),
      ],
    );
  }

  /// Short address mode: the two halves run independently.
  Widget _dmaShortBody(DmacChannel ch) {
    return Column(
      children: [
        _dmaHalfRow(ch.a),
        const SizedBox(height: 6),
        _dmaHalfRow(ch.b),
      ],
    );
  }

  Widget _dmaHalfRow(DmacHalf h) {
    final total = h.etcrReload == 0 ? 1 : h.etcrReload;
    final receiving = h.source == DmacSource.sciReceive;
    return Opacity(
      opacity: h.enabled ? 1.0 : 0.55,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              SizedBox(
                width: 30,
                child: Text(h.label,
                    style: const TextStyle(
                        fontWeight: FontWeight.bold, fontSize: 12)),
              ),
              // Direction follows the activation source: a receive channel
              // reads the device, everything else writes it.
              Text(
                receiving
                    ? "H'${_hex6(h.ioAddress)} → H'${_hex6(h.mar)}"
                    : "H'${_hex6(h.mar)} → H'${_hex6(h.ioAddress)}",
                style: TextStyle(
                    fontFamily: _font, fontSize: 12, color: _accent),
              ),
              const SizedBox(width: 10),
              Text('${h.etcr} left of $total',
                  style: TextStyle(fontSize: 11, color: _inkA(0.6))),
              const Spacer(),
              Text('${h.sourceName} · ${h.modeName}',
                  style: TextStyle(fontSize: 11, color: _inkA(0.55))),
            ],
          ),
          const SizedBox(height: 3),
          Row(
            children: [
              const SizedBox(width: 30),
              Text(
                "DTCR H'${_hex2(h.dtcr)}  IOAR H'${_hex2(h.ioar)}  "
                '${h.wordSize ? 'word' : 'byte'}, '
                '${(h.dtcr & DmacControl.dtid) != 0 ? 'decrement' : 'increment'}',
                style: TextStyle(
                    fontFamily: _font, fontSize: 11, color: _inkA(0.55)),
              ),
              const SizedBox(width: 8),
              _sciFlagChip('DTE', h.enabled),
              const SizedBox(width: 4),
              _sciFlagChip('DTIE', (h.dtcr & DmacControl.dtie) != 0),
            ],
          ),
        ],
      ),
    );
  }

  // ---- IO view -------------------------------------------------------------

  /// The width the IO view is laid out at: the eight 34px bit boxes plus
  /// the list padding, which is all the GPIO diagram needs. In the
  /// side-by-side layout the pane is pinned here (see [_multiView]) so the
  /// other views keep the rest of the window. If the pane is ever narrower
  /// than this (a narrow window on the IO tab), the content scrolls
  /// sideways instead of being squeezed.
  static const double _ioViewWidth = 300.0;

  Widget _ioView() {
    return Column(
      children: [
        _ioHeader(),
        Expanded(
          child: LayoutBuilder(
            builder: (context, constraints) {
              final content = SizedBox(
                key: const Key('ioPane'),
                width: _ioViewWidth,
                height: constraints.maxHeight,
                child: _ioContent(),
              );
              if (constraints.maxWidth >= _ioViewWidth) {
                // Keep the diagram at its natural width, left-aligned.
                return Align(alignment: Alignment.topLeft, child: content);
              }
              return SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: content,
              );
            },
          ),
        ),
      ],
    );
  }

  Widget _ioContent() {
    return ListView(
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 16),
      children: [
        const Text('GPIO',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 15)),
        const SizedBox(height: 6),
        // Wrap, not Row: the labels flow onto a second line rather than
        // overflowing if the text metrics come out wider than expected.
        Wrap(
          spacing: 12,
          runSpacing: 4,
          children: [
            _ioLegendSwatch(_gpioOutColor, 'output (value shown)'),
            _ioLegendSwatch(_gpioInColor, 'input, floating'),
            _ioLegendSwatch(_gpioDrivenColor, 'input, held'),
          ],
        ),
        const SizedBox(height: 4),
        Text(
          'DDR selects direction; DR holds the output value. Click an input '
          'to cycle it: floating, held low, held high. A held pin is what '
          'the CPU reads, so this is how a button or switch is pressed.',
          style: TextStyle(fontSize: 11, color: _inkA(0.5)),
        ),
        const SizedBox(height: 8),
        for (final p in H8Cpu.ports) _gpioPort(p),
        const Divider(height: 20),
        const Text('External inputs',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 15)),
        const SizedBox(height: 4),
        Text(
          'Digital inputs latched into the address space rather than onto a '
          'CPU pin. Until a bit is driven the window reads out of memory — '
          'for a full dump, the byte the machine was holding when it was '
          'taken.',
          style: TextStyle(fontSize: 11, color: _inkA(0.5)),
        ),
        const SizedBox(height: 8),
        for (final e in cpu.externalInputs) _externalInputs(e),
      ],
    );
  }

  /// One memory-mapped input latch, drawn like a port so the two read the
  /// same way.
  Widget _externalInputs(ExternalInputs e) {
    // What the CPU would read right now: the override if there is one, and
    // otherwise whatever the loaded image holds there.
    final value = e.value ?? cpu.mem.peek(e.base);
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Tooltip(
            message: "${e.name}\nH'${_hex6(e.base)}-"
                "H'${_hex6(e.base + e.size - 1)}\n"
                'Every address in the window reads the same byte.',
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                Text("H'${_hex6(e.base)}",
                    style: const TextStyle(
                        fontWeight: FontWeight.bold, fontSize: 13)),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    "${e.driven ? 'held' : 'from memory'}  H'${_hex2(value)}",
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                        fontFamily: _font, fontSize: 11, color: _inkA(0.5)),
                  ),
                ),
                if (e.driven)
                  IconButton(
                    tooltip: 'Release: read from memory again',
                    visualDensity: VisualDensity.compact,
                    icon: const Icon(Icons.undo, size: 16),
                    onPressed: () => setState(() {
                      e.value = null;
                      _memRev++;
                    }),
                  ),
              ],
            ),
          ),
          const SizedBox(height: 2),
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              for (var bit = 7; bit >= 0; bit--)
                _gpioBitBox(
                  bit: bit,
                  hasPin: true,
                  isOutput: false,
                  value: (value >> bit) & 1,
                  isFirst: bit == 7,
                  driven: e.driven,
                  onTap: () => setState(() {
                    // Taking the override from what is being read keeps the
                    // other seven bits as they were.
                    e.value = value ^ (1 << bit);
                    _memRev++;
                  }),
                ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _ioHeader() {
    return Container(
      color: Theme.of(context).colorScheme.surfaceContainer,
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      child: const Row(
        children: [
          Icon(Icons.settings_input_component, size: 18),
          SizedBox(width: 6),
          Text('I/O', style: TextStyle(fontWeight: FontWeight.bold)),
          Spacer(),
        ],
      ),
    );
  }

  static const Color _gpioOutColor = Color(0xFFC62828); // output pins: red
  static const Color _gpioInColor = Color(0xFF2E7D32); // input pins: green
  // An input the user is holding, so it reads as a level rather than as
  // whatever the data register happens to contain.
  static const Color _gpioDrivenColor = Color(0xFF1565C0);

  Widget _ioLegendSwatch(Color color, String label) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 12,
          height: 12,
          decoration: BoxDecoration(
            color: color,
            borderRadius: BorderRadius.circular(2),
          ),
        ),
        const SizedBox(width: 4),
        Flexible(
          child: Text(label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 11, color: _inkA(0.7))),
        ),
      ],
    );
  }

  /// One GPIO port: a name/register line and a row of eight abutting bit
  /// boxes (bit 7 on the left), each labelled above with its bit number.
  /// Output bits (DDR = 1) are red with the DR value inside; input bits
  /// are green; positions without a pin are dimmed.
  Widget _gpioPort(H8Port p) {
    final ddrAddr = p.ddrAddr;
    final ddr = ddrAddr == null ? 0 : cpu.peekBus(ddrAddr);
    final dr = cpu.peekBus(p.drAddr);
    // Values inline, register addresses in the tooltip — at this width
    // there is no room for both.
    final regs = StringBuffer();
    if (ddrAddr != null) regs.write("DDR H'${_hex2(ddr)}  ");
    regs.write("DR H'${_hex2(dr)}");
    final tip = StringBuffer('Port ${p.name}');
    if (p.inputOnly) tip.write(' (input only, no DDR)');
    if (ddrAddr != null) {
      tip.write("\nDDR H'${_hex2(ddr)} at H'${_hex6(ddrAddr)}");
    }
    tip.write("\nDR H'${_hex2(dr)} at H'${_hex6(p.drAddr)}");
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Tooltip(
            message: tip.toString(),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                Text('Port ${p.name}',
                    style: const TextStyle(
                        fontWeight: FontWeight.bold, fontSize: 13)),
                if (p.inputOnly)
                  Text(' (in)',
                      style: TextStyle(fontSize: 11, color: _inkA(0.5))),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    regs.toString(),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                        fontFamily: _font, fontSize: 11, color: _inkA(0.5)),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 2),
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              for (var bit = 7; bit >= 0; bit--)
                () {
                  final hasPin = (p.pinMask >> bit) & 1 == 1;
                  // Port 7 has no DDR: every pin is an input.
                  final isOutput = ddrAddr != null && (ddr >> bit) & 1 == 1;
                  final driven = cpu.pinIsDriven(p.drAddr, bit);
                  return _gpioBitBox(
                    bit: bit,
                    hasPin: hasPin,
                    isOutput: isOutput,
                    value: (dr >> bit) & 1,
                    isFirst: bit == 7,
                    driven: driven && !isOutput,
                    onTap: hasPin && !isOutput ? () => _cyclePin(p, bit) : null,
                  );
                }(),
            ],
          ),
        ],
      ),
    );
  }

  Widget _gpioBitBox({
    required int bit,
    required bool hasPin,
    required bool isOutput,
    required int value,
    required bool isFirst,
    bool driven = false,
    VoidCallback? onTap,
  }) {
    final Color? fill = !hasPin
        ? null
        : (isOutput
            ? _gpioOutColor
            : (driven ? _gpioDrivenColor : _gpioInColor));
    final border = Border(
      top: BorderSide(color: _inkA(0.4)),
      bottom: BorderSide(color: _inkA(0.4)),
      right: BorderSide(color: _inkA(0.4)),
      // Abutting boxes: only the leftmost box draws its own left edge.
      left: isFirst ? BorderSide(color: _inkA(0.4)) : BorderSide.none,
    );
    final box = Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        // Bit number label above the box.
        Text('$bit', style: TextStyle(fontSize: 10, color: _inkA(0.6))),
        Container(
          width: 34,
          height: 30,
          alignment: Alignment.center,
          decoration: BoxDecoration(color: fill, border: border),
          // Output pins show their driven value in a contrasting colour, as
          // do input pins the user is holding; a floating input is just
          // green, and pinless positions stay empty.
          child: hasPin && (isOutput || driven)
              ? Text(
                  '$value',
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.bold,
                    color: Colors.white,
                    fontFamily: 'monospace',
                  ),
                )
              : null,
        ),
      ],
    );
    if (onTap == null) return box;
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(onTap: onTap, child: box),
    );
  }

  /// Cycles one input pin: floating -> held low -> held high -> floating.
  /// Three states rather than two, because "floating" is the only one that
  /// reproduces the old behaviour of reading back the data register, and
  /// firmware that writes a port and reads it back depends on it.
  void _cyclePin(H8Port p, int bit) {
    setState(() {
      if (!cpu.pinIsDriven(p.drAddr, bit)) {
        cpu.setPin(p.drAddr, bit, false);
      } else if (!cpu.pinIsHigh(p.drAddr, bit)) {
        cpu.setPin(p.drAddr, bit, true);
      } else {
        cpu.releasePin(p.drAddr, bit);
      }
      _memRev++;
    });
  }

  // ---- Trace view ----------------------------------------------------------

  /// Resolves an entry's target: a loaded symbol name first, then a hex
  /// number. Symbol names win, so a location keeps its meaning if a symbol
  /// happens to look like hex (ADD, FACE, and so on).
  int? _resolveTrace(String target) {
    final t = target.trim();
    if (t.isEmpty) return null;
    final bySymbol = _symbolsByName[t];
    if (bySymbol != null) return bySymbol;
    return symbolAddress(t);
  }

  /// Puts the two flash devices on the bus, or takes them off again.
  ///
  /// Turning the model on replaces the contents of both regions with what is
  /// in the image file: that is the point of it, so the CPU is stopped and
  /// reset rather than left running on code that has just changed underneath
  /// it. Turning it off leaves memory as it stands and simply stops the
  /// devices intercepting, so every address is ordinary RAM again.
  Future<void> _setFlashOn(bool on) async {
    if (!on) {
      cpu.flash.clear();
      setState(() {
        _flashOn = false;
        _flashStatus = 'Off. Both regions are plain memory again, holding '
            'whatever is in them now.';
      });
      return;
    }
    await _loadFlashImage();
  }

  /// Reads the image file and, if it can be read, attaches the devices.
  Future<void> _loadFlashImage() async {
    final path = _flashPathCtl.text.trim();
    if (path.isEmpty) {
      setState(() {
        _flashOn = false;
        _flashStatus = 'Choose a flash image file first.';
      });
      return;
    }

    final bytes = await readBytesFromPath(path);
    if (bytes == null) {
      setState(() {
        _flashOn = false;
        _flashStatus = 'Could not read "$path".';
      });
      return;
    }

    _pause();
    final addressed = flashImageIsAddressed(bytes.length);

    cpu.flash.clear();
    final padded = applyFlashImage(bytes, cpu.mem.poke, addressed: addressed);
    for (final region in artista180Flash) {
      cpu.attachFlash(JedecFlash(base: region.base, size: region.size));
    }
    _reset();

    final shape = addressed
        ? 'read as a memory dump, each device taken from its own address'
        : 'read as the two devices back to back';
    setState(() {
      _flashOn = true;
      _flashAddressed = addressed;
      _flashStatus = '${bytes.length} bytes $shape.'
          '${padded > 0 ? "  $padded bytes past the end of the file were "
              "filled with H'FF." : ""}'
          '  CPU reset.';
    });
  }

  /// Writes what is in the two flash regions to a file in the plain
  /// two-devices-back-to-back form, whichever way the loaded image was read.
  ///
  /// The bytes come from memory directly rather than through the devices, so
  /// this is the array as it stands and not whatever a device in the middle
  /// of an autoselect would answer with. It works with the model off as well
  /// as on, which is how a starting image gets made from a memory dump.
  Future<void> _saveFlashImage() async {
    _pause();
    final image = buildFlashImage(cpu.mem.peek);

    try {
      final path = await FilePicker.saveFile(
        dialogTitle: 'Save flash image',
        fileName: 'flash.bin',
        bytes: image,
      );
      if (path == null) {
        _showSnack('Save cancelled.');
        return;
      }
      // On desktop, saveFile returns a path without writing — write here.
      await writeBytesToPath(path, image);
      if (_flashPathCtl.text.trim().isEmpty) _flashPathCtl.text = path;
      setState(() {
        _flashStatus = 'Wrote ${image.length} bytes to $path.';
      });
    } catch (e) {
      _showSnack('Save failed: $e');
    }
  }

  /// Locates the image file. The bytes come back from the picker itself, but
  /// the path is what the field keeps, so the file can be re-read later
  /// after it has been changed outside the simulator.
  Future<void> _pickFlashFile() async {
    FilePickerResult? picked;
    try {
      picked = await FilePicker.pickFiles(type: FileType.any);
    } catch (e) {
      _showSnack('Could not open the file picker: $e');
      return;
    }
    if (picked == null || picked.files.isEmpty) return; // cancelled

    final path = picked.files.first.path;
    if (path == null) {
      _showSnack('That file has no path this platform can re-read.');
      return;
    }
    _flashPathCtl.text = path;
    if (_flashOn) {
      await _loadFlashImage();
    } else {
      setState(() => _flashStatus = 'File chosen. Switch the model on to '
          'load it.');
    }
  }

  Widget _flashView() {
    final rows = <TableRow>[
      TableRow(children: [
        _flashCell('Device', bold: true),
        _flashCell('From', bold: true),
        _flashCell('To', bold: true),
        _flashCell('Size', bold: true),
        _flashCell('In file at', bold: true),
      ]),
    ];
    final addressed = _flashOn && _flashAddressed;
    for (final r in artista180Flash) {
      rows.add(TableRow(children: [
        _flashCell(r.name),
        _flashCell("H'${_addr6(r.base)}"),
        _flashCell("H'${_addr6(r.end - 1)}"),
        _flashCell("H'${_addr6(r.size)}"),
        _flashCell("H'${_addr6(flashImageOffset(r, addressed))}"),
      ]));
    }

    return SingleChildScrollView(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            Switch(
              key: const Key('flashEnableSwitch'),
              value: _flashOn,
              onChanged: (v) => _setFlashOn(v),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                _flashOn
                    ? 'Flash model on: these regions answer only to a proper '
                        'erase or program sequence.'
                    : 'Flash model off: every address behaves as RAM.',
                style: TextStyle(fontFamily: _font, color: _ink),
              ),
            ),
          ]),
          const SizedBox(height: 12),
          Row(children: [
            Expanded(
              child: TextField(
                key: const Key('flashPathField'),
                controller: _flashPathCtl,
                style: TextStyle(fontFamily: _font, color: _ink),
                decoration: const InputDecoration(
                  isDense: true,
                  border: OutlineInputBorder(),
                  labelText: 'Flash image file',
                ),
                onSubmitted: (_) {
                  if (_flashOn) _loadFlashImage();
                },
              ),
            ),
            const SizedBox(width: 8),
            OutlinedButton(
              key: const Key('flashBrowseButton'),
              onPressed: _pickFlashFile,
              child: const Text('Browse…'),
            ),
          ]),
          const SizedBox(height: 8),
          Wrap(spacing: 8, runSpacing: 8, children: [
            OutlinedButton(
              key: const Key('flashReloadButton'),
              onPressed: _flashOn ? _loadFlashImage : null,
              child: const Text('Reload'),
            ),
            OutlinedButton(
              key: const Key('flashSaveButton'),
              onPressed: _saveFlashImage,
              child: const Text('Save Flash Image…'),
            ),
          ]),
          const SizedBox(height: 12),
          Text(
            _flashStatus.isEmpty
                ? 'A plain binary file holding both devices back to back, '
                    "H'${_addr6(flashImageSize())} bytes. A full memory dump is "
                    'also accepted, and each device is then taken from its '
                    'own address.'
                : _flashStatus,
            style: TextStyle(fontFamily: _font, color: _inkA(0.75)),
          ),
          const SizedBox(height: 16),
          Table(
            columnWidths: const {
              0: IntrinsicColumnWidth(),
              1: IntrinsicColumnWidth(),
              2: IntrinsicColumnWidth(),
              3: IntrinsicColumnWidth(),
              4: IntrinsicColumnWidth(),
            },
            children: rows,
          ),
        ],
      ),
    );
  }

  static String _addr6(int v) =>
      (v & 0xFFFFFF).toRadixString(16).toUpperCase().padLeft(6, '0');

  Widget _flashCell(String text, {bool bold = false}) => Padding(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
        child: Text(
          text,
          style: TextStyle(
            fontFamily: _font,
            color: bold ? _ink : _inkA(0.85),
            fontWeight: bold ? FontWeight.bold : FontWeight.normal,
          ),
        ),
      );


  // ---- the serial EEPROM on port 4 --------------------------------------
  //
  // The machine bit-bangs I2C at a small serial EEPROM on two port pins and
  // keeps its presser-foot calibration there. With the model off those pins
  // are ordinary port bits and the firmware's writes go nowhere; with it on
  // the device answers, and the array is kept in a JSON file so that what
  // one session wrote is there in the next.

  Future<void> _setEepromOn(bool on) async {
    if (!on) {
      cpu.detachEeprom();
      _eeprom.onCommit = null;
      setState(() {
        _eepromOn = false;
        _eepromStatus = 'Off. The two pins are ordinary port bits again, and '
            'what the machine writes goes nowhere.';
      });
      return;
    }
    await _loadEepromFile(attach: true);
  }

  /// Reads the file, and with [attach] puts the device on the bus.
  ///
  /// A file that is not there yet is not an error: the array starts blank and
  /// the file is written the first time the machine puts something in it, so
  /// a fresh tree needs no setting up. A file that is there but cannot be
  /// read *is* an error, and stops the model being switched on, because
  /// carrying on would overwrite it with a blank array on the next write.
  Future<void> _loadEepromFile({bool attach = false}) async {
    var path = _eepromPathCtl.text.trim();
    if (path.isEmpty) {
      path = _eepromDefaultFile;
      _eepromPathCtl.text = path;
    }

    final bytes = await readBytesFromPath(path);
    String note;
    if (bytes == null) {
      _eeprom.data.fillRange(0, _eeprom.size, 0xFF);
      note = 'No file at "$path" yet. Starting blank; it will be written the '
          'first time the machine puts something in.';
    } else {
      final failure =
          _eeprom.fromJson(utf8.decode(bytes, allowMalformed: true));
      if (failure != null) {
        // The array is left as it was, but the device comes off the bus:
        // leaving it on with the switch saying off would be a lie, and
        // leaving it on with a file that cannot be read means the next
        // write overwrites that file.
        cpu.detachEeprom();
        _eeprom.onCommit = null;
        setState(() {
          _eepromOn = false;
          _eepromStatus = 'Could not read "$path": $failure  '
              'Nothing has been changed.';
        });
        return;
      }
      note = 'Loaded ${_eeprom.size} bytes from "$path".';
    }

    if (attach) {
      cpu.attachEeprom(_eeprom);
      _eeprom.onCommit = _eepromCommitted;
      _eeprom.log.clear();
      _eeprom.writeCount = 0;
      _eeprom.readCount = 0;
    }
    setState(() {
      if (attach) _eepromOn = true;
      _eepromStatus = note;
    });
  }

  /// The machine has finished a write. This runs from inside a CPU step, so
  /// it must not touch the widget tree; the file is written after the fact.
  void _eepromCommitted() => unawaited(_writeEepromFile());

  /// Writes the array out. Overlapping calls collapse into one more pass,
  /// so a burst of writes cannot start a pile of overlapping file writes.
  Future<void> _writeEepromFile() async {
    if (_eepromSaving) {
      _eepromSaveAgain = true;
      return;
    }
    _eepromSaving = true;
    try {
      var again = true;
      while (again) {
        again = false;
        final path = _eepromPathCtl.text.trim();
        if (path.isEmpty) return;
        await writeBytesToPath(path, utf8.encode(_eeprom.toJson()));
        if (_eepromSaveAgain) {
          _eepromSaveAgain = false;
          again = true;
        }
      }
    } finally {
      _eepromSaving = false;
    }
  }

  /// Locates the file. As on the flash tab it is the path that is kept, not
  /// the bytes, so the file can be re-read after something else has changed
  /// it -- and so the machine's own writes have somewhere to go.
  Future<void> _pickEepromFile() async {
    FilePickerResult? picked;
    try {
      picked = await FilePicker.pickFiles(type: FileType.any);
    } catch (e) {
      _showSnack('Could not open the file picker: $e');
      return;
    }
    if (picked == null || picked.files.isEmpty) return; // cancelled

    final path = picked.files.first.path;
    if (path == null) {
      _showSnack('That file has no path this platform can re-read.');
      return;
    }
    _eepromPathCtl.text = path;
    await _loadEepromFile(attach: _eepromOn);
  }

  /// Writes the array somewhere new, and keeps that as the file from now on.
  Future<void> _saveEepromAs() async {
    final doc = utf8.encode(_eeprom.toJson());
    try {
      final path = await FilePicker.saveFile(
        dialogTitle: 'Save EEPROM contents',
        fileName: _eepromDefaultFile,
        bytes: doc,
      );
      if (path == null) {
        _showSnack('Save cancelled.');
        return;
      }
      await writeBytesToPath(path, doc);
      _eepromPathCtl.text = path;
      setState(() => _eepromStatus = 'Wrote ${doc.length} bytes to $path.');
    } catch (e) {
      _showSnack('Save failed: $e');
    }
  }

  /// Puts every byte back to H'FF, as an unwritten part reads.
  Future<void> _eraseEeprom() async {
    _eeprom.data.fillRange(0, _eeprom.size, 0xFF);
    _eeprom.log.clear();
    await _writeEepromFile();
    setState(() => _eepromStatus = 'Erased: every byte back to H\'FF.');
  }

  static String _h2(int v) =>
      (v & 0xFF).toRadixString(16).toUpperCase().padLeft(2, '0');

  /// The array as a hex dump with the printable characters beside it.
  String _eepromDump() {
    final out = StringBuffer();
    for (var a = 0; a < _eeprom.size; a += 16) {
      out.write('${_h2(a)}  ');
      for (var i = 0; i < 16; i++) {
        out.write('${_h2(_eeprom.data[a + i])} ');
        if (i == 7) out.write(' ');
      }
      out.write(' ');
      for (var i = 0; i < 16; i++) {
        final c = _eeprom.data[a + i];
        out.write(c >= 0x20 && c < 0x7F ? String.fromCharCode(c) : '.');
      }
      if (a + 16 < _eeprom.size) out.writeln();
    }
    return out.toString();
  }

  Widget _eepromView() {
    final fieldRows = <TableRow>[
      TableRow(children: [
        _flashCell('At', bold: true),
        _flashCell('What it holds', bold: true),
        _flashCell('Now', bold: true),
      ]),
    ];
    for (final f in I2cEeprom.knownFields) {
      fieldRows.add(TableRow(children: [
        _flashCell("H'${_h2(f.address)}"),
        _flashCell(f.name),
        _flashCell(f.describe(_eeprom.data[f.address])),
      ]));
    }

    final busy = _eeprom.phase != I2cPhase.idle;
    final activity = _eeprom.log.isEmpty
        ? 'Nothing on the bus yet.'
        : _eeprom.log.reversed.map((t) => t.toString()).join('\n');

    return SingleChildScrollView(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            Switch(
              key: const Key('eepromEnableSwitch'),
              value: _eepromOn,
              onChanged: (v) => _setEepromOn(v),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                _eepromOn
                    ? 'EEPROM model on: the device answers on P4 bit 7 (SDA) '
                        'and bit 6 (SCL), and every write is kept in the file '
                        'below.'
                    : 'EEPROM model off: both pins are ordinary port bits, '
                        'and what the machine writes goes nowhere.',
                style: TextStyle(fontFamily: _font, color: _ink),
              ),
            ),
          ]),
          const SizedBox(height: 12),
          Row(children: [
            Expanded(
              child: TextField(
                key: const Key('eepromPathField'),
                controller: _eepromPathCtl,
                style: TextStyle(fontFamily: _font, color: _ink),
                decoration: const InputDecoration(
                  isDense: true,
                  border: OutlineInputBorder(),
                  labelText: 'EEPROM contents file (JSON)',
                ),
                onSubmitted: (_) => _loadEepromFile(attach: _eepromOn),
              ),
            ),
            const SizedBox(width: 8),
            OutlinedButton(
              key: const Key('eepromBrowseButton'),
              onPressed: _pickEepromFile,
              child: const Text('Browse…'),
            ),
          ]),
          const SizedBox(height: 8),
          Wrap(spacing: 8, runSpacing: 8, children: [
            OutlinedButton(
              key: const Key('eepromReloadButton'),
              onPressed: () => _loadEepromFile(attach: _eepromOn),
              child: const Text('Reload'),
            ),
            OutlinedButton(
              key: const Key('eepromSaveButton'),
              onPressed: _saveEepromAs,
              child: const Text('Save As…'),
            ),
            OutlinedButton(
              key: const Key('eepromEraseButton'),
              onPressed: _eraseEeprom,
              child: const Text('Erase'),
            ),
          ]),
          const SizedBox(height: 12),
          Text(
            _eepromStatus.isEmpty
                ? 'The array lives in a JSON file -- sixteen bytes to a row, '
                    'hexadecimal, meant to be readable and editable by hand. '
                    'Switch the model on to load it; the machine\'s own '
                    'writes are saved back as they happen.'
                : _eepromStatus,
            style: TextStyle(fontFamily: _font, color: _inkA(0.75)),
          ),
          const SizedBox(height: 16),
          Text('What the firmware keeps here',
              style: TextStyle(
                  fontFamily: _font, color: _ink, fontWeight: FontWeight.bold)),
          const SizedBox(height: 6),
          Table(
            columnWidths: const {
              0: IntrinsicColumnWidth(),
              1: IntrinsicColumnWidth(),
              2: FlexColumnWidth(),
            },
            children: fieldRows,
          ),
          const SizedBox(height: 6),
          for (final f in I2cEeprom.knownFields)
            Padding(
              padding: const EdgeInsets.only(left: 10, bottom: 6),
              child: Text("H'${_h2(f.address)}: ${f.detail}",
                  style: TextStyle(fontFamily: _font, color: _inkA(0.65))),
            ),
          const SizedBox(height: 10),
          CheckboxListTile(
            key: const Key('eepromVerifyFriendlyCheck'),
            dense: true,
            contentPadding: EdgeInsets.zero,
            controlAffinity: ListTileControlAffinity.leading,
            value: _eeprom.verifyFriendly,
            onChanged: (v) =>
                setState(() => _eeprom.verifyFriendly = v ?? false),
            title: Text('Leave the address counter on the byte just written',
                style: TextStyle(fontFamily: _font, color: _ink)),
            subtitle: Text(
                "A real part leaves it one further on, so the firmware's "
                "write-and-verify reads the next byte and its check says no "
                "-- which is why none of its twelve callers looks at the "
                "answer. Tick this to make the verify agree instead.",
                style: TextStyle(fontFamily: _font, color: _inkA(0.65))),
          ),
          const SizedBox(height: 10),
          Text(
            'Bus: SCL ${_eeprom.sclHigh ? "high" : "low"}, '
            'SDA ${_eeprom.sdaHigh ? "high" : "low"}, '
            '${busy ? _eeprom.phase.name : "idle"}.  '
            "Address counter H'${_h2(_eeprom.pointer)}.  "
            '${_eeprom.writeCount} bytes written, '
            '${_eeprom.readCount} read.',
            style: TextStyle(fontFamily: _font, color: _inkA(0.75)),
          ),
          const SizedBox(height: 16),
          Text('Contents',
              style: TextStyle(
                  fontFamily: _font, color: _ink, fontWeight: FontWeight.bold)),
          const SizedBox(height: 6),
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: SelectableText(
              _eepromDump(),
              key: const Key('eepromDump'),
              style: TextStyle(fontFamily: 'monospace', color: _inkA(0.9)),
            ),
          ),
          const SizedBox(height: 16),
          Text('On the bus, newest first',
              style: TextStyle(
                  fontFamily: _font, color: _ink, fontWeight: FontWeight.bold)),
          const SizedBox(height: 6),
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Text(
              activity,
              style: TextStyle(fontFamily: 'monospace', color: _inkA(0.75)),
            ),
          ),
        ],
      ),
    );
  }

  Widget _traceView() {
    return TraceView(
      cpu: cpu,
      entries: _traces,
      resolve: _resolveTrace,
      font: _font,
      onAdd: () => _editTrace(),
      onEdit: (i) => _editTrace(index: i),
      onDelete: (i) => setState(() => _traces.removeAt(i)),
    );
  }

  /// Adds a trace, or edits the one at [index].
  Future<void> _editTrace({int? index}) async {
    final existing = index == null ? null : _traces[index];
    final controller = _selectedController(existing?.target ?? '');
    var bits = existing?.bits ?? 8;
    var bigEndian = existing?.bigEndian ?? true;

    final saved = await showDialog<TraceEntry>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setLocal) {
          // Byte order is meaningless for a single byte, so the control is
          // shown disabled rather than hidden — the layout stays put as the
          // width changes.
          final endianEnabled = bits != 8;
          final resolved = _resolveTrace(controller.text);
          return AlertDialog(
            title: Text(index == null ? 'Add trace' : 'Edit trace'),
            content: SizedBox(
              width: 420,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  TextField(
                    controller: controller,
                    autofocus: true,
                    decoration: InputDecoration(
                      labelText: 'Address or symbol',
                      hintText: "FFFEF0  or  touch_x_raw",
                      helperText: resolved == null
                          ? 'Not a loaded symbol or a hex address'
                          : "Resolves to H'${_hex6(resolved)}",
                      helperStyle: TextStyle(
                        color: resolved == null
                            ? Theme.of(ctx).colorScheme.error
                            : null,
                      ),
                    ),
                    onChanged: (_) => setLocal(() {}),
                  ),
                  const SizedBox(height: 16),
                  Text('Width',
                      style: TextStyle(fontSize: 12, color: _inkA(0.6))),
                  RadioGroup<int>(
                    groupValue: bits,
                    onChanged: (v) => setLocal(() => bits = v ?? 8),
                    child: Row(
                      children: [
                        for (final w in [8, 16, 32])
                          Expanded(
                            child: RadioListTile<int>(
                              value: w,
                              title: Text('$w'),
                              dense: true,
                              contentPadding: EdgeInsets.zero,
                              visualDensity: VisualDensity.compact,
                            ),
                          ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 8),
                  Opacity(
                    opacity: endianEnabled ? 1 : 0.4,
                    child: Row(
                      children: [
                        Text('Byte order',
                            style: TextStyle(fontSize: 12, color: _inkA(0.6))),
                        const SizedBox(width: 12),
                        Switch(
                          value: bigEndian,
                          onChanged: endianEnabled
                              ? (v) => setLocal(() => bigEndian = v)
                              : null,
                        ),
                        const SizedBox(width: 6),
                        Text(bigEndian ? 'big endian' : 'little endian',
                            style: const TextStyle(fontSize: 13)),
                      ],
                    ),
                  ),
                ],
              ),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(ctx),
                child: const Text('Cancel'),
              ),
              FilledButton(
                onPressed: controller.text.trim().isEmpty
                    ? null
                    : () => Navigator.pop(
                          ctx,
                          TraceEntry(
                            target: controller.text.trim(),
                            bits: bits,
                            bigEndian: bigEndian,
                          ),
                        ),
                child: Text(index == null ? 'Add' : 'Save'),
              ),
            ],
          );
        },
      ),
    );
    if (saved == null || !mounted) return;
    setState(() {
      if (index == null) {
        _traces.add(saved);
      } else {
        // Edit in place: the list widget keys its sampled state off the
        // entry object, so mutating keeps the row's history.
        _traces[index]
          ..target = saved.target
          ..bits = saved.bits
          ..bigEndian = saved.bigEndian;
      }
    });
  }

  // ---- Profiling -----------------------------------------------------------

  /// Turns profiling on or off. Switching on zeroes the counters first;
  /// switching off keeps them so the Profile view can still be read.
  void _setProfiling(bool on) {
    setState(() {
      if (on && !cpu.profiling) cpu.resetProfile();
      cpu.profiling = on;
      _memRev++;
    });
  }

  /// The nonzero entries of [counts] as (address, count), most frequent
  /// first, capped at [limit] rows.
  List<MapEntry<int, int>> _topCounts(SparseCounters counts, int limit) {
    final entries = counts.nonZeroEntries();
    entries.sort((x, y) => y.value.compareTo(x.value));
    return entries.length > limit ? entries.sublist(0, limit) : entries;
  }

  Widget _profileView() {
    final data = _topCounts(cpu.dataAccessCount, 512);
    final instr = _topCounts(cpu.instrExecCount, 512);

    if (data.isEmpty && instr.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Text(
            cpu.profiling
                ? 'Profiling is on — run the program to collect counts.'
                : 'No profile data yet.\n\nTurn on the Profile switch at the '
                    'top, then Run. The counts are zeroed each time you '
                    'switch profiling on, and kept when you switch it off.',
            textAlign: TextAlign.center,
            style: TextStyle(color: _inkA(0.6)),
          ),
        ),
      );
    }

    Widget section(String title, List<MapEntry<int, int>> items,
        String Function(int addr) detail) {
      return Column(
        children: [
          Container(
            width: double.infinity,
            color: Theme.of(context).colorScheme.surfaceContainerHighest,
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            child: Text('$title  (${items.length})',
                style: const TextStyle(fontWeight: FontWeight.bold)),
          ),
          Expanded(
            child: items.isEmpty
                ? Center(child: Text('—', style: TextStyle(color: _inkA(0.4))))
                : ListView.builder(
                    itemCount: items.length,
                    itemExtent: _rowExtent,
                    itemBuilder: (_, i) =>
                        _profileRow(items[i].key, items[i].value, detail),
                  ),
          ),
        ],
      );
    }

    return Column(
      children: [
        Expanded(
          child: section(
              'Data accesses', data, (a) => "= H'${_hex2(cpu.peekBus(a))}"),
        ),
        const Divider(height: 1, thickness: 1),
        Expanded(
          child: section(
              'Instruction executions', instr, (a) => cpu.disassemble(a).text),
        ),
      ],
    );
  }

  Widget _profileRow(int addr, int count, String Function(int addr) detail) {
    final mono = TextStyle(fontFamily: _font, fontSize: 13, color: _ink);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 2),
      child: Row(
        children: [
          SizedBox(width: 72, child: Text("H'${_hex6(addr)}", style: mono)),
          SizedBox(
            width: 92,
            child: Text(count.toString(),
                textAlign: TextAlign.right,
                style: mono.copyWith(color: _accent)),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Text(detail(addr),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: mono.copyWith(color: _inkA(0.75))),
          ),
        ],
      ),
    );
  }

  // ---- Control bar ---------------------------------------------------------

  Widget _controlBar() {
    return SafeArea(
      top: false,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 8),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.surfaceContainerHigh,
          border: Border(
              top: BorderSide(color: Colors.black.withValues(alpha: 0.3))),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceEvenly,
          children: [
            _ctrlButton(
              icon: Icons.pause,
              label: 'Pause',
              onPressed: _running ? _pause : null,
            ),
            _ctrlButton(
              icon: Icons.skip_next,
              label: 'Step',
              onPressed:
                  (_running || (cpu.halted && !cpu.sleeping)) ? null : _step,
            ),
            _ctrlButton(
              icon: Icons.play_arrow,
              label: 'Run',
              onPressed:
                  (_running || (cpu.halted && !cpu.sleeping)) ? null : _run,
              primary: true,
            ),
            _ctrlButton(
              icon: Icons.flash_on,
              label: 'NMI',
              onPressed: _nmi,
            ),
            _ctrlButton(
              icon: Icons.bolt,
              label: 'IRQ0',
              onPressed: _irq,
            ),
          ],
        ),
      ),
    );
  }

  Widget _ctrlButton({
    required IconData icon,
    required String label,
    required VoidCallback? onPressed,
    bool primary = false,
  }) {
    final child = Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 26),
        const SizedBox(height: 2),
        Text(label, style: const TextStyle(fontSize: 12)),
      ],
    );
    return Expanded(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 4),
        child: primary
            ? FilledButton(
                onPressed: onPressed,
                style: FilledButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 8)),
                child: child,
              )
            : OutlinedButton(
                onPressed: onPressed,
                style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 8)),
                child: child,
              ),
      ),
    );
  }

  static String _hex2(int v) =>
      (v & 0xFF).toRadixString(16).toUpperCase().padLeft(2, '0');
  static String _hex6(int v) =>
      (v & 0xFFFFFF).toRadixString(16).toUpperCase().padLeft(6, '0');
  static String _hex8(int v) =>
      (v & 0xFFFFFFFF).toRadixString(16).toUpperCase().padLeft(8, '0');
}

/// The LCD panel: 320x240 pixels at 2 bits each, read straight out of the
/// simulated memory.
///
/// Each byte holds four pixels, most significant bits first, so bits 7-6 are
/// the leftmost pixel of the group. The two-bit value selects one of four
/// grey levels, 0 = black through 3 = white (invert swaps the ends).
///
/// The bitmap is converted to a [ui.Image] once per revision rather than
/// painted pixel by pixel — 76800 individual rectangles per frame would not
/// keep up with Run mode.
class _LcdPane extends StatefulWidget {
  const _LcdPane({
    required this.cpu,
    required this.base,
    required this.invert,
    required this.memRev,
    required this.tick,
  });

  final H8Cpu cpu;
  final int base;
  final bool invert;

  /// Bumped when memory may have changed (edits, loads, stepping).
  final int memRev;

  /// Bumped every Run tick, so the panel animates while the heavier views
  /// stay throttled.
  final ValueListenable<int> tick;

  static const int width = LcdFormat.width;
  static const int height = LcdFormat.height;
  static const int stride = LcdFormat.stride;
  static const int bytes = LcdFormat.bytes;

  @override
  State<_LcdPane> createState() => _LcdPaneState();
}

class _LcdPaneState extends State<_LcdPane> {
  ui.Image? _image;

  /// True while an image conversion is in flight; further requests are
  /// coalesced rather than queued.
  bool _converting = false;
  bool _dirty = false;

  @override
  void initState() {
    super.initState();
    widget.tick.addListener(_request);
    _request();
  }

  @override
  void didUpdateWidget(_LcdPane old) {
    super.didUpdateWidget(old);
    if (old.tick != widget.tick) {
      old.tick.removeListener(_request);
      widget.tick.addListener(_request);
    }
    if (old.memRev != widget.memRev ||
        old.base != widget.base ||
        old.invert != widget.invert) {
      _request();
    }
  }

  @override
  void dispose() {
    widget.tick.removeListener(_request);
    _image?.dispose();
    super.dispose();
  }

  void _request() {
    if (_converting) {
      _dirty = true; // fold into the conversion already running
      return;
    }
    _converting = true;
    _convert();
  }

  void _convert() {
    const w = _LcdPane.width, h = _LcdPane.height;
    final rgba = lcdToRgba(widget.cpu.mem.peek, widget.base,
        invert: widget.invert);
    ui.decodeImageFromPixels(rgba, w, h, ui.PixelFormat.rgba8888, (img) {
      if (!mounted) {
        img.dispose();
        return;
      }
      setState(() {
        _image?.dispose();
        _image = img;
      });
      _converting = false;
      if (_dirty) {
        _dirty = false;
        _request();
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      painter: _LcdPainter(_image),
      child: const SizedBox.expand(),
    );
  }
}

class _LcdPainter extends CustomPainter {
  _LcdPainter(this.image);

  final ui.Image? image;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    canvas.drawRect(rect, Paint()..color = const Color(0xFF000000));
    final img = image;
    if (img == null) return;
    canvas.drawImageRect(
      img,
      Rect.fromLTWH(0, 0, img.width.toDouble(), img.height.toDouble()),
      rect,
      // Nearest-neighbour: keep the pixels crisp when scaled up.
      Paint()..filterQuality = FilterQuality.none,
    );
  }

  @override
  bool shouldRepaint(_LcdPainter old) => old.image != image;
}

/// Keeps its child mounted (and therefore its scroll position intact)
/// while it is off-screen inside a [TabBarView].
class _KeepAlive extends StatefulWidget {
  const _KeepAlive({required this.child});

  final Widget child;

  @override
  State<_KeepAlive> createState() => _KeepAliveState();
}

class _KeepAliveState extends State<_KeepAlive>
    with AutomaticKeepAliveClientMixin {
  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context); // required by AutomaticKeepAliveClientMixin
    return widget.child;
  }
}

// ---- Trace view ------------------------------------------------------------

/// One watched location: where to look, how wide, and which way round.
class TraceEntry {
  TraceEntry({
    required this.target,
    this.bits = 8,
    this.bigEndian = true,
  });

  /// What the user typed: a symbol name, or an address in hex.
  String target;

  /// 8, 16 or 32.
  int bits;

  /// Byte order for the 16- and 32-bit widths. Ignored at 8 bits; the
  /// H8/300H itself is big-endian, so that is the default.
  bool bigEndian;

  int get bytes => bits ~/ 8;
}

/// The Trace tab: a live list of watched locations.
///
/// It samples on its own timer rather than repainting with the rest of the
/// UI, so a value that changes while the CPU is paused (a single Step) is
/// caught just the same as one that changes mid-run, and the flash that
/// marks a change is timed independently of the simulator's frame rate.
class TraceView extends StatefulWidget {
  const TraceView({
    super.key,
    required this.cpu,
    required this.entries,
    required this.resolve,
    required this.onAdd,
    required this.onEdit,
    required this.onDelete,
    required this.font,
  });

  final H8Cpu cpu;
  final List<TraceEntry> entries;

  /// Turns an entry's target text into an address, or null if it names
  /// neither a loaded symbol nor a hex number.
  final int? Function(String target) resolve;

  final VoidCallback onAdd;
  final void Function(int index) onEdit;
  final void Function(int index) onDelete;
  final String font;

  @override
  State<TraceView> createState() => _TraceViewState();
}

class _TraceViewState extends State<TraceView> {
  /// Sampling period. Fast enough to look live, slow enough to be free.
  static const Duration period = Duration(milliseconds: 60);

  /// How many samples a changed value stays highlighted for — about 420 ms.
  /// Counted in ticks rather than wall-clock time so the highlight is driven
  /// by the same clock as the sampling.
  static const int flashTicks = 7;

  Timer? _timer;

  /// Last sampled value per entry, and how many ticks of highlight remain.
  final Map<TraceEntry, int?> _values = {};
  final Map<TraceEntry, int> _flash = {};

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(period, (_) => _sample());
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  /// Reads one entry, or null when its target does not resolve.
  int? _read(TraceEntry e) {
    final addr = widget.resolve(e.target);
    if (addr == null) return null;
    var v = 0;
    for (var i = 0; i < e.bytes; i++) {
      final b = widget.cpu.peekBus((addr + i) & SparseMemory.addrMask);
      if (e.bigEndian || e.bits == 8) {
        v = (v << 8) | b;
      } else {
        v |= b << (8 * i);
      }
    }
    return v;
  }

  void _sample() {
    if (!mounted) return;
    var repaint = false;
    for (final e in widget.entries) {
      final v = _read(e);
      if (!_values.containsKey(e)) {
        _values[e] = v; // first sight is not a change
        repaint = true;
        continue;
      }
      if (_values[e] != v) {
        _values[e] = v;
        _flash[e] = flashTicks;
        repaint = true;
      } else {
        final left = _flash[e] ?? 0;
        if (left > 0) {
          _flash[e] = left - 1;
          repaint = true; // repaint again when the highlight ends
        }
      }
    }
    // Drop state for entries that have been deleted.
    if (_values.length > widget.entries.length) {
      _values.removeWhere((k, _) => !widget.entries.contains(k));
      _flash.removeWhere((k, _) => !widget.entries.contains(k));
    }
    if (repaint) setState(() {});
  }

  bool _flashing(TraceEntry e) => (_flash[e] ?? 0) > 0;

  String _hexValue(TraceEntry e) {
    final v = _values[e];
    if (v == null) return '--';
    return v.toRadixString(16).toUpperCase().padLeft(e.bytes * 2, '0');
  }

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final ink = DefaultTextStyle.of(context).style.color ?? scheme.onSurface;
    return Column(
      children: [
        Container(
          color: scheme.surfaceContainer,
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
          child: Row(
            children: [
              const Icon(Icons.visibility_outlined, size: 18),
              const SizedBox(width: 6),
              const Text('Trace',
                  style: TextStyle(fontWeight: FontWeight.bold)),
              const Spacer(),
              TextButton.icon(
                onPressed: widget.onAdd,
                icon: const Icon(Icons.add, size: 18),
                label: const Text('Add trace'),
              ),
            ],
          ),
        ),
        Expanded(
          child: widget.entries.isEmpty
              ? Center(
                  child: Padding(
                    padding: const EdgeInsets.all(24),
                    child: Text(
                      'No locations traced.\n\n'
                      'Add one by address (H\'FFFEF0) or by the name of a '
                      'loaded symbol (touch_x_raw). The value updates live '
                      'and flashes when it changes.',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                          fontSize: 12, color: ink.withValues(alpha: 0.6)),
                    ),
                  ),
                )
              : ListView.separated(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  itemCount: widget.entries.length,
                  separatorBuilder: (_, _) => const Divider(height: 1),
                  itemBuilder: (context, i) =>
                      _row(widget.entries[i], i, ink, scheme),
                ),
        ),
      ],
    );
  }

  Widget _row(TraceEntry e, int i, Color ink, ColorScheme scheme) {
    final addr = widget.resolve(e.target);
    final unresolved = addr == null;
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
      child: Row(
        children: [
          IconButton(
            tooltip: 'Delete',
            visualDensity: VisualDensity.compact,
            icon: const Icon(Icons.delete_outline, size: 18),
            onPressed: () => widget.onDelete(i),
          ),
          IconButton(
            tooltip: 'Edit',
            visualDensity: VisualDensity.compact,
            icon: const Icon(Icons.edit_outlined, size: 18),
            onPressed: () => widget.onEdit(i),
          ),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  e.target,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontFamily: widget.font,
                    fontSize: 13,
                    color: unresolved ? scheme.error : ink,
                  ),
                ),
                Text(
                  unresolved
                      ? 'unknown symbol or address'
                      : "H'${addr.toRadixString(16).toUpperCase().padLeft(6, '0')}"
                          '  ${e.bits}-bit'
                          '${e.bits == 8 ? '' : (e.bigEndian ? '  BE' : '  LE')}',
                  style: TextStyle(
                    fontSize: 10,
                    color: ink.withValues(alpha: 0.55),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          // The value, flashing red for a moment each time it changes.
          AnimatedContainer(
            duration: const Duration(milliseconds: 120),
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            decoration: BoxDecoration(
              color: _flashing(e)
                  ? const Color(0xFFC62828)
                  : scheme.surfaceContainerHighest,
              borderRadius: BorderRadius.circular(4),
            ),
            child: Text(
              "H'${_hexValue(e)}",
              style: TextStyle(
                fontFamily: widget.font,
                fontSize: 14,
                fontWeight: FontWeight.bold,
                color: _flashing(e) ? Colors.white : ink,
              ),
            ),
          ),
          const SizedBox(width: 4),
        ],
      ),
    );
  }
}
