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
import 'h8300h.dart';
import 'hex_files.dart';
import 'itu.dart';
import 'lcd.dart';
import 'sci.dart';
import 'sparse_memory.dart';
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

  /// Top address shown in the memory window (drives the header label).
  int _memBase = 0x000100;

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
  bool _viewProfile = false;

  /// Stable keys so each pane is *moved* (not rebuilt) when the layout
  /// switches between the tabbed and side-by-side arrangements.
  final GlobalKey _memKey = GlobalKey();
  final GlobalKey _disKey = GlobalKey();
  final GlobalKey _scrKey = GlobalKey();
  final GlobalKey _sciKey = GlobalKey();
  final GlobalKey _ituKey = GlobalKey();
  final GlobalKey _dmaKey = GlobalKey();
  final GlobalKey _ioKey = GlobalKey();
  final GlobalKey _profKey = GlobalKey();

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

  @override
  void initState() {
    super.initState();
    _tab = TabController(length: 8, vsync: this);
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
    _runTimer?.cancel();
    _tab.dispose();
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
      cpu.step();
      _memRev++;
      _screenRev.value++;
      _followPc();
    });
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
            cpu.breakHit = false;
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
        }
      });
    });
  }

  void _pause() {
    _runTimer?.cancel();
    _runTimer = null;
    if (mounted) setState(() {});
  }

  void _reset() {
    _pause();
    setState(cpu.reset);
    _scrollToAddress(cpu.pc);
  }

  /// Fires the non-maskable interrupt (vector 7).
  void _nmi() {
    setState(() {
      cpu.nmi();
      _memRev++;
    });
    _scrollToAddress(cpu.pc);
  }

  /// Fires IRQ0 (vector 12). If the I bit has it masked, the CPU state is
  /// unchanged and we tell the user why nothing happened.
  void _irq() {
    final taken = cpu.irq(0);
    setState(() {
      if (taken) _memRev++;
    });
    if (taken) {
      _scrollToAddress(cpu.pc);
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
        cpu.mem.poke(addr, result);
        cpu.breakHit = false; // don't let the UI edit arm a stale break
        if (brk) {
          cpu.dataBreaks.add(addr);
        } else {
          cpu.dataBreaks.remove(addr);
        }
        _memRev++;
        _codeRev++;
      });
    }
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

  /// Picks a hex/S-record file (local storage or a cloud provider, via the
  /// OS document picker) and loads it.
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

    final format = detectProgramFormat(data);
    final HexResult result;
    if (format == ProgramFormat.raw) {
      // A flat binary carries no addresses, so ask where it goes.
      final base = await _askRawLoadAddress(file.name, data.length);
      if (base == null) return; // cancelled
      result = loadRawBinary(data, base, cpu.mem.poke);
    } else {
      // Intel HEX and S-records are plain ASCII text.
      result = parseHexFile(String.fromCharCodes(data), cpu.mem.poke);
    }

    if (result.isEmpty) {
      final why = result.errors.isNotEmpty
          ? result.errors.first
          : 'no data records found';
      _showSnack('Nothing loaded from "${file.name}" ($why).');
      return;
    }

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
    await _autoLoadSymbols(file.path);
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
          IconButton(
            tooltip: 'Open Intel HEX / S-record file',
            onPressed: _loadHexFile,
            icon: const Icon(Icons.folder_open_outlined),
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
            Tab(text: 'Profile'),
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
                  key: _profKey, child: _KeepAlive(child: _profileView())),
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
      if (_viewProfile)
        (
          pane: KeyedSubtree(
              key: _profKey, child: _KeepAlive(child: _profileView())),
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
  Widget _viewToggles() {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _viewToggle('MEM', _viewMem, (v) => _viewMem = v),
        const SizedBox(width: 4),
        _viewToggle('DIS', _viewDis, (v) => _viewDis = v),
        const SizedBox(width: 4),
        _viewToggle('SCR', _viewScr, (v) => _viewScr = v),
        const SizedBox(width: 4),
        _viewToggle('SCI', _viewSci, (v) => _viewSci = v),
        const SizedBox(width: 4),
        _viewToggle('ITU', _viewItu, (v) => _viewItu = v),
        const SizedBox(width: 4),
        _viewToggle('DMA', _viewDma, (v) => _viewDma = v),
        const SizedBox(width: 4),
        _viewToggle('IO', _viewIo, (v) => _viewIo = v),
        const SizedBox(width: 4),
        _viewToggle('PROF', _viewProfile, (v) => _viewProfile = v),
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
        (_viewProfile ? 1 : 0);
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
      if (name == 'PC') _scrollToAddress(cpu.pc);
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
                child: TextButton.icon(
                  onPressed: _gotoAddress,
                  icon: const Icon(Icons.my_location, size: 16),
                  label: Text("Go to  H'${_hex6(_memBase)}"),
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

  /// Opens a JSON symbol table and maps its addresses to labels. Accepts a
  /// `{ "label": address }` object (address as an int or a hex string), or
  /// a list of `"name = H'hhhhhh"`-style listing lines.
  Future<void> _loadSymbols() async {
    try {
      final picked =
          await FilePicker.pickFiles(type: FileType.any, withData: true);
      if (picked == null || picked.files.isEmpty) return;
      final bytes = picked.files.first.bytes;
      if (bytes == null) {
        _showSnack('Could not read that file.');
        return;
      }
      final n = _applySymbolBytes(bytes);
      if (n == 0) {
        _showSnack('No symbols found in that file.');
        return;
      }
      _showSnack('Loaded $n symbols.');
    } catch (e) {
      _showSnack('Symbol load failed: $e');
    }
  }

  /// Parses a symbol-table JSON from [bytes] and installs it, returning
  /// the number of symbols loaded.
  int _applySymbolBytes(List<int> bytes) {
    final map = <int, String>{};
    try {
      final decoded = json.decode(utf8.decode(bytes));
      if (decoded is Map) {
        decoded.forEach((k, v) {
          final addr = _asAddress(v);
          if (addr != null) map[addr] = k.toString();
        });
      } else if (decoded is List) {
        final re = RegExp(
            r"^\s*([A-Za-z_.$][\w.$]*)\s*=\s*(?:H'|\$|0x)?([0-9A-Fa-f]+)");
        for (final item in decoded) {
          final m = re.firstMatch(item.toString());
          if (m != null) {
            map[int.parse(m.group(2)!, radix: 16) & SparseMemory.addrMask] =
                m.group(1)!;
          }
        }
      }
    } catch (_) {
      return 0;
    }
    if (map.isEmpty) return 0;
    setState(() => _symbols = map);
    return map.length;
  }

  /// After loading a hex file at [hexPath], look for a sibling symbol
  /// table `<name>_sym.json` and load it automatically.
  Future<void> _autoLoadSymbols(String? hexPath) async {
    if (hexPath == null) return;
    final dot = hexPath.lastIndexOf('.');
    if (dot <= 0) return;
    final symPath = '${hexPath.substring(0, dot)}_sym.json';
    final bytes = await readBytesFromPath(symPath);
    if (bytes == null) return; // no sibling symbol file
    final n = _applySymbolBytes(bytes);
    if (n > 0) _showSnack('Auto-loaded $n symbols from ${_baseName(symPath)}.');
  }

  String _baseName(String path) => path.split(RegExp(r'[\\/]')).last;

  /// Coerces a symbol-table value to a 24-bit address.
  int? _asAddress(dynamic v) {
    if (v is int) return v & SparseMemory.addrMask;
    if (v is String) {
      var s = v.trim();
      if (s.startsWith("H'")) s = s.substring(2);
      if (s.startsWith('\$')) s = s.substring(1);
      if (s.startsWith('0x') || s.startsWith('0X')) s = s.substring(2);
      final hex = int.tryParse(s, radix: 16);
      if (hex != null) return hex & SparseMemory.addrMask;
      final dec = int.tryParse(s);
      if (dec != null) return dec & SparseMemory.addrMask;
    }
    return null;
  }

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
                    TextButton.icon(
                      onPressed: _loadSymbols,
                      icon: const Icon(Icons.label_outline, size: 16),
                      label: Text(_symbols.isEmpty
                          ? 'Symbols'
                          : 'Symbols (${_symbols.length})'),
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
                child: _LcdPane(
                  cpu: cpu,
                  base: _screenBase,
                  invert: _screenInvert,
                  memRev: _memRev,
                  tick: _screenRev,
                ),
              ),
            ),
          ),
        ),
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
            ],
          ),
        ),
      ],
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
            Text(
              ch.txCount == log.length
                  ? '${ch.txCount} bytes'
                  : '${ch.txCount} bytes (showing the last ${log.length})',
              style: TextStyle(fontSize: 11, color: _inkA(0.55)),
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
            _ioLegendSwatch(_gpioInColor, 'input'),
          ],
        ),
        const SizedBox(height: 4),
        Text(
          'DDR selects direction; DR holds the output value.',
          style: TextStyle(fontSize: 11, color: _inkA(0.5)),
        ),
        const SizedBox(height: 8),
        for (final p in H8Cpu.ports) _gpioPort(p),
      ],
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
                _gpioBitBox(
                  bit: bit,
                  hasPin: (p.pinMask >> bit) & 1 == 1,
                  // Port 7 has no DDR: every pin is an input.
                  isOutput: ddrAddr != null && (ddr >> bit) & 1 == 1,
                  value: (dr >> bit) & 1,
                  isFirst: bit == 7,
                ),
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
  }) {
    final Color? fill =
        !hasPin ? null : (isOutput ? _gpioOutColor : _gpioInColor);
    final border = Border(
      top: BorderSide(color: _inkA(0.4)),
      bottom: BorderSide(color: _inkA(0.4)),
      right: BorderSide(color: _inkA(0.4)),
      // Abutting boxes: only the leftmost box draws its own left edge.
      left: isFirst ? BorderSide(color: _inkA(0.4)) : BorderSide.none,
    );
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        // Bit number label above the box.
        Text('$bit', style: TextStyle(fontSize: 10, color: _inkA(0.6))),
        Container(
          width: 34,
          height: 30,
          alignment: Alignment.center,
          decoration: BoxDecoration(color: fill, border: border),
          // Output pins show their driven value in a contrasting colour;
          // input pins are just green; pinless positions stay empty.
          child: isOutput && hasPin
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
