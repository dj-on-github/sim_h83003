// ignore_for_file: avoid_print — these are command-line tools; stdout is the UI.
// Runs a firmware image on the simulator core with a minimal model of the
// machine's peripherals, and reports what the code actually did.
//
// Static analysis can only see addresses that appear in an instruction
// encoding. Anything the firmware computes at runtime — a framebuffer
// pointer, a DMA destination, a device base held in a variable — is
// invisible to it. Executing the code makes those addresses observable.
//
// The peripheral model is deliberately thin: enough for the firmware to get
// past hardware polling loops (SCI transmitter always ready, timers that
// tick, ports that read back what was written) rather than an accurate
// machine. Where the model guesses, the report says so.
//
// Usage:
//   dart run tool/trace_run.dart <image.bin> [--steps N] [--from HEX]
//                                            [--rx "hex bytes"]

import 'dart:io';
import 'dart:typed_data';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/h8disasm.dart';

import 'h8_regmap.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// H8/3003 core plus just enough of the Bernina's peripherals to run.
class BerninaMachine extends H8Cpu {
  BerninaMachine();

  /// Bytes transmitted and queued for receipt live in the core's SCI model.
  List<int> get tx0 => sci0.txLog;
  List<int> get tx1 => sci1.txLog;
  List<int> get rx0 => sci0.rxQueue;

  /// Every write, bucketed by 256-byte page, so device windows stand out.
  final Map<int, int> writePages = {};

  /// Reads of on-chip registers we have no model for — these are the places
  /// the model is guessing, and where a wrong guess could derail execution.
  final Map<int, int> unmodelledReads = {};

  /// Per-register access counts, for the report.
  final Map<int, int> regReads = {};
  final Map<int, int> regWrites = {};

  /// Last value written to each on-chip register (drives DR read-back).
  final Map<int, int> regFile = {};

  /// PC histogram, bucketed by instruction address.
  final Map<int, int> pcHits = {};

  /// Optional watch window: every write inside it is logged with the PC that
  /// made it, which is how a device's access pattern gets identified.
  int watchLo = -1, watchHi = -1;
  final List<(int pc, int addr, int value)> watchLog = [];
  final Map<int, int> watchAddrCounts = {};
  final Map<int, int> watchPcCounts = {};

  /// Write counts for every individual address (not just pages).
  final Map<int, int> writeAddrs = {};

  /// Reads from outside ROM/RAM/flash, by page — candidate device windows.
  final Map<int, int> deviceReads = {};

  /// Every write to a DMAC register, with the instruction that made it.
  final List<(int pc, int addr, int value)> dmacLog = [];

  /// The PC that made the write, for the busiest addresses.
  final Map<int, Map<int, int>> writersOf = {};

  bool _isReg(int a) => a >= 0xFFFF00;

  // ---- DMAC, short address mode ----------------------------------------
  // Channel halves live at H'FFFF20 + n*16 (A) and +8 (B):
  //   +1..+3 MAR (24-bit memory address)   +4..+5 ETCR (transfer count)
  //   +6     IOAR (low byte of H'FFFFxx)   +7     DTCR
  // DTCR: bit7 DTE, bit6 DTSZ, bit5 DTID, bit4 RPE, bit3 DTIE, bits2-0 DTS.
  // DTS=100 transmit-data-empty from an SCI, DTS=101 receive-data-full.

  int _mar(int b) =>
      ((regFile[b + 1] ?? 0) << 16) |
      ((regFile[b + 2] ?? 0) << 8) |
      (regFile[b + 3] ?? 0);

  void _setMar(int b, int v) {
    regFile[b + 1] = (v >> 16) & 0xFF;
    regFile[b + 2] = (v >> 8) & 0xFF;
    regFile[b + 3] = v & 0xFF;
  }

  int _etcr(int b) => ((regFile[b + 4] ?? 0) << 8) | (regFile[b + 5] ?? 0);

  void _setEtcr(int b, int v) {
    regFile[b + 4] = (v >> 8) & 0xFF;
    regFile[b + 5] = v & 0xFF;
  }

  /// True when the given channel half is enabled for an SCI0 transfer.
  bool _dmaArmed(int b, int wantDts) {
    final dtcr = regFile[b + 7] ?? 0;
    if ((dtcr & 0x80) == 0) return false; // DTE clear: disabled
    if ((dtcr & 0x07) != wantDts) return false;
    final ioar = regFile[b + 6] ?? 0;
    // Only model the SCI0 data registers.
    return ioar == 0xB3 || ioar == 0xB5;
  }

  int dmaTxBytes = 0, dmaRxBytes = 0, dmaCompletions = 0;

  /// Performs one byte of DMA if a channel is armed and its trigger is
  /// ready. Returns the DEND vector to raise, or null.
  int? serviceDma() {
    for (final ch in [0, 1, 2, 3]) {
      for (final half in [0, 8]) {
        final b = 0xFFFF20 + ch * 0x10 + half;
        final dtcr = regFile[b + 7] ?? 0;
        final dtid = (dtcr & 0x20) != 0 ? -1 : 1;
        // Transmit: memory -> TDR0.
        if (_dmaArmed(b, 4)) {
          final mar = _mar(b);
          final byte = mem.peek(mar);
          // Push it through the SCI so the transmit flags follow.
          writeB(0xFFFFB3, byte);
          writeB(0xFFFFB4, 0x7F); // clear TDRE: start the transmission
          dmaTxBytes++;
          _setMar(b, (mar + dtid) & 0xFFFFFF);
          final n = (_etcr(b) - 1) & 0xFFFF;
          _setEtcr(b, n);
          if (n == 0) {
            regFile[b + 7] = dtcr & 0x7F; // clear DTE
            dmaCompletions++;
            if ((dtcr & 0x08) != 0) return 44 + ch * 2 + (half == 0 ? 0 : 1);
          }
          return null;
        }
        // Receive: RDR0 -> memory, only when a byte is waiting.
        if (_dmaArmed(b, 5) && rx0.isNotEmpty) {
          final mar = _mar(b);
          mem.poke(mar, rx0.removeAt(0));
          dmaRxBytes++;
          _setMar(b, (mar + dtid) & 0xFFFFFF);
          final n = (_etcr(b) - 1) & 0xFFFF;
          _setEtcr(b, n);
          if (n == 0) {
            regFile[b + 7] = dtcr & 0x7F;
            dmaCompletions++;
            if ((dtcr & 0x08) != 0) return 44 + ch * 2 + (half == 0 ? 0 : 1);
          }
          return null;
        }
      }
    }
    return null;
  }

  /// True when SCI0 transfers are being handled by the DMAC, in which case
  /// the CPU does not also get the RXI/TXI interrupt.
  bool get sciDmaActive =>
      _dmaArmed(0xFFFF30, 4) ||
      _dmaArmed(0xFFFF30, 5) ||
      _dmaArmed(0xFFFF38, 4) ||
      _dmaArmed(0xFFFF38, 5);

  @override
  int readB(int addr) {
    addr &= 0xFFFFFF;
    if (_isReg(addr)) {
      regReads[addr] = (regReads[addr] ?? 0) + 1;
      // The SCI, ITU and DMAC are all modelled in the CPU core.
      if (addr >= 0xFFFFB0 && addr <= 0xFFFFBD) return super.readB(addr);
      if (addr >= 0xFFFF60 && addr <= 0xFFFF9F) return super.readB(addr);
      if (addr >= 0xFFFF20 && addr <= 0xFFFF5F) return super.readB(addr);
      // Ports and everything else: read back what was written, which is what
      // the hardware does for pins configured as outputs.
      if (regFile.containsKey(addr)) return regFile[addr]!;
      unmodelledReads[addr] = (unmodelledReads[addr] ?? 0) + 1;
      // Input ports read as all-ones (pulled up, nothing pressed).
      return 0xFF;
    }
    // Reads outside the boot ROM/RAM (area 0) and the application flash
    // (area 1) are candidate memory-mapped devices.
    if (addr >= 0x60000 && !(addr >= 0x200000 && addr < 0x400000)) {
      deviceReads[addr >> 8] = (deviceReads[addr >> 8] ?? 0) + 1;
    }
    return super.readB(addr);
  }

  @override
  void writeB(int addr, int value) {
    addr &= 0xFFFFFF;
    value &= 0xFF;
    if (_isReg(addr)) {
      regWrites[addr] = (regWrites[addr] ?? 0) + 1;
      final lo = addr & 0xFF;
      // The SCI and ITU are modelled properly in the CPU core; let them
      // handle their own registers, including the clear-only status flags.
      if ((addr >= 0xFFFFB0 && addr <= 0xFFFFBD) ||
          (addr >= 0xFFFF60 && addr <= 0xFFFF9F) ||
          (addr >= 0xFFFF20 && addr <= 0xFFFF5F)) {
        super.writeB(addr, value);
        return;
      }
      regFile[addr] = value;
      // Keep every DMAC register write: the channel's memory address (MAR)
      // and I/O address (IOAR) registers reveal what the DMA moves and to
      // which device — traffic the CPU never issues itself.
      if (lo >= 0x20 && lo <= 0x5F) dmacLog.add((pc, addr, value));
      return;
    }
    writePages[addr >> 8] = (writePages[addr >> 8] ?? 0) + 1;
    writeAddrs[addr] = (writeAddrs[addr] ?? 0) + 1;
    if (addr >= watchLo && addr <= watchHi) {
      if (watchLog.length < 4000) watchLog.add((pc, addr, value));
      watchAddrCounts[addr] = (watchAddrCounts[addr] ?? 0) + 1;
      watchPcCounts[pc] = (watchPcCounts[pc] ?? 0) + 1;
    }
    super.writeB(addr, value);
  }
}

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/trace_run.dart <image.bin> '
        '[--steps N] [--from HEX] [--rx "AA BB"]');
    exit(2);
  }
  String? opt(String n) {
    final i = args.indexOf(n);
    return (i >= 0 && i + 1 < args.length) ? args[i + 1] : null;
  }

  final image = File(args.first).readAsBytesSync();
  final steps = int.parse(opt('--steps') ?? '20000000');
  final from = opt('--from');

  final m = BerninaMachine();
  // Load the image into the sparse memory, skipping banks that are entirely
  // 0x00 or 0xFF so unpopulated address space stays unallocated.
  var banks = 0;
  for (var b = 0; b < image.length; b += 0x10000) {
    final end = (b + 0x10000).clamp(0, image.length);
    final slice = Uint8List.sublistView(image, b, end);
    var allSame = true;
    for (var i = 1; i < slice.length; i++) {
      if (slice[i] != slice[0]) {
        allSame = false;
        break;
      }
    }
    if (allSame) continue;
    for (var i = 0; i < slice.length; i++) {
      m.mem.poke(b + i, slice[i]);
    }
    banks++;
  }
  print('loaded ${image.length} bytes, $banks populated 64K banks');

  for (final t in (opt('--rx') ?? '').split(RegExp(r'[\s,]+'))) {
    final v = int.tryParse(t, radix: 16);
    if (v != null) m.rx0.add(v);
  }

  final watch = opt('--watch');
  if (watch != null) {
    final parts = watch.split('-');
    m.watchLo = int.parse(parts[0], radix: 16);
    m.watchHi = int.parse(parts.length > 1 ? parts[1] : parts[0], radix: 16);
    print("watching writes to H'${hex6(m.watchLo)}-H'${hex6(m.watchHi)}");
  }

  m.reset();
  if (from != null) m.pc = int.parse(from, radix: 16);
  print("start PC = H'${hex6(m.pc)}, running up to $steps instructions\n");

  // Periodic on-chip interrupts. The firmware's state machines are driven by
  // the ITU compare-match handlers it installs (IMIA3/IMIA4) and by SCI0
  // receive; without them the main loop just spins. Periods here are chosen
  // to be plausible, not measured — see the caveats in the report.
  final tickEvery = int.parse(opt('--tick') ?? '20000');
  final ituVectors = <int>[
    for (final v in (opt('--itu') ?? '').split(','))
      if (int.tryParse(v) != null) int.parse(v)
  ];  // empty by default: the modelled ITU raises its own interrupts
  var taken = <int, int>{};

  // --stub: addresses to return from immediately. Used to skip calibrated
  // busy-wait delay routines, which otherwise consume the entire run without
  // advancing the firmware's state. At a function's entry the return address
  // is the longword on top of the stack, so returning is just an RTS.
  final stubs = <int>{
    for (final s in (opt('--stub') ?? '').split(RegExp(r'[\s,]+')))
      if (int.tryParse(s, radix: 16) != null) int.parse(s, radix: 16)
  };
  final stubHits = <int, int>{};
  if (stubs.isNotEmpty) {
    print('stubbed (immediate return): '
        '${stubs.map((s) => "H'${hex6(s)}").join(', ')}');
  }
  // --rxloop: a reply pattern the simulated peer sends over and over.
  final rxLoop = <int>[
    for (final t in (opt('--rxloop') ?? '').split(RegExp(r'[\s,]+')))
      if (int.tryParse(t, radix: 16) != null) int.parse(t, radix: 16)
  ];
  if (rxLoop.isNotEmpty) {
    print('simulated SCI0 peer replies with: '
        '${rxLoop.map(hex2).join(' ')} (repeating)');
  }

  int peekL(int a) =>
      (m.mem.peek(a) << 24) |
      (m.mem.peek(a + 1) << 16) |
      (m.mem.peek(a + 2) << 8) |
      m.mem.peek(a + 3);

  // Instructions per received byte (~8000 ≈ one byte at 19200 baud with a
  // 16 MHz clock).
  final rxInterval = int.parse(opt('--rxrate') ?? '8000');
  var nextRxByte = 0;

  var executed = 0;
  var nextTick = tickEvery;
  var vi = 0;
  final sw = Stopwatch()..start();
  while (executed < steps && !m.halted) {
    if (stubs.contains(m.pc)) {
      stubHits[m.pc] = (stubHits[m.pc] ?? 0) + 1;
      final sp = m.er[7];
      m.pc = peekL(sp) & 0xFFFFFF;
      m.er[7] = (sp + 4) & 0xFFFFFFFF;
      executed++;
      continue;
    }
    m.pcHits[m.pc] = (m.pcHits[m.pc] ?? 0) + 1;
    m.step();
    executed++;
    if (executed >= nextTick && ituVectors.isNotEmpty) {
      nextTick = executed + tickEvery;
      final v = ituVectors[vi++ % ituVectors.length];
      if (m.interrupt(v)) taken[v] = (taken[v] ?? 0) + 1;
    }
    // SCI0 interrupts, gated on the enable bits the firmware sets in SCR0,
    // as the real controller would: TIE (bit 7) -> TXI when the transmit
    // register is empty, RIE (bit 6) -> RXI when a byte has arrived.
    if (executed % 64 == 0) {
      // Feed the peer's bytes at something like line rate. At 19200 baud one
      // byte takes ~520us, which is a few thousand instructions — injecting
      // them every few dozen instructions instead drowns the firmware in
      // receive interrupts and is not a realistic test.
      if (rxLoop.isNotEmpty && m.rx0.isEmpty && executed >= nextRxByte) {
        nextRxByte = executed + rxInterval * rxLoop.length;
        m.rx0.addAll(rxLoop);
      }
      // The DMAC gets first refusal on the SCI0 triggers; only when it is
      // not driving the channel does the CPU see RXI/TXI.
      final dend = m.serviceDma();
      if (dend != null && m.interrupt(dend)) {
        taken[dend] = (taken[dend] ?? 0) + 1;
      }
    }
  }
  sw.stop();
  if (stubHits.isNotEmpty) {
    print('stub returns: '
        '${stubHits.entries.map((e) => "H'${hex6(e.key)} x${e.value}").join(', ')}');
  }
  if (taken.isNotEmpty) {
    print('interrupts delivered: '
        '${taken.entries.map((e) => "vec ${e.key} x${e.value}").join(', ')}');
  }

  print('executed $executed instructions in ${sw.elapsedMilliseconds} ms'
      '${m.halted ? "  (halted: ${m.haltReason})" : ""}');
  print("final PC = H'${hex6(m.pc)}, ${m.cycles} states\n");

  // ---- where did it spend its time? -------------------------------------
  final hot = m.pcHits.entries.toList()
    ..sort((a, b) => b.value.compareTo(a.value));
  print('=== hottest instruction addresses (polling loops show up here) ===');
  for (final e in hot.take(12)) {
    final d = disassembleH8(m.mem.peek, e.key);
    print("  H'${hex6(e.key)}  ${e.value.toString().padLeft(9)}x  ${d.text}");
  }
  print('');

  // ---- what did it write, and where? ------------------------------------
  final pages = m.writePages.entries.toList()
    ..sort((a, b) => b.value.compareTo(a.value));
  print('=== most-written 256-byte pages (device windows / buffers) ===');
  for (final e in pages.take(16)) {
    final a = e.key << 8;
    final where = a >= 0xFFFD10 && a <= 0xFFFF0F
        ? 'on-chip RAM'
        : (a < 0x60000
            ? 'area 0 (ROM/RAM)'
            : (a < 0x400000 ? 'area 1' : 'external'));
    print("  H'${hex6(a)}-H'${hex6(a + 0xFF)}  "
        "${e.value.toString().padLeft(8)} writes   $where");
  }
  print('');

  // ---- peripheral registers actually exercised --------------------------
  print('=== on-chip registers touched at run time ===');
  final all = <int>{...m.regReads.keys, ...m.regWrites.keys}.toList()..sort();
  for (final a in all) {
    final lo = a & 0xFF;
    print("  H'${hex6(a)} ${(h8Registers[lo] ?? '?').padRight(9)} "
        "r=${(m.regReads[a] ?? 0).toString().padLeft(7)} "
        "w=${(m.regWrites[a] ?? 0).toString().padLeft(7)}   ${h8Module(lo)}");
  }
  print('');

  if (m.tx0.isNotEmpty || m.tx1.isNotEmpty) {
    print('=== serial output ===');
    if (m.tx0.isNotEmpty) {
      print('  SCI0: ${m.tx0.length} bytes: '
          '${m.tx0.take(32).map(hex2).join(' ')}');
    }
    if (m.tx1.isNotEmpty) {
      print('  SCI1: ${m.tx1.length} bytes: '
          '${m.tx1.take(32).map(hex2).join(' ')}');
    }
    print('');
  }

  if (m.dmaTxBytes > 0 || m.dmaRxBytes > 0) {
    print('=== DMA activity ===');
    print('  ${m.dmaTxBytes} bytes transmitted, ${m.dmaRxBytes} received, '
        '${m.dmaCompletions} completed transfers');
    print('');
  }

  if (m.dmacLog.isNotEmpty) {
    print('=== DMAC programming (${m.dmacLog.length} register writes) ===');
    // Reassemble each channel's 24-bit address registers from the byte
    // writes, so the source/destination/device addresses are readable.
    final regs = <int, int>{};
    for (final w in m.dmacLog) {
      regs[w.$2] = w.$3;
    }
    for (final ch in [0, 1, 2, 3]) {
      final b = 0xFFFF20 + ch * 0x10;
      int? mar(int o) {
        if (!regs.containsKey(b + o + 1)) return null;
        return ((regs[b + o + 1] ?? 0) << 16) |
            ((regs[b + o + 2] ?? 0) << 8) |
            (regs[b + o + 3] ?? 0);
      }

      final marA = mar(0), marB = mar(8);
      final ioarA = regs[b + 6], ioarB = regs[b + 14];
      final dtcrA = regs[b + 7], dtcrB = regs[b + 15];
      if (marA == null && marB == null && ioarA == null) continue;
      print('  channel $ch:');
      if (marA != null) print("    MAR${ch}A  = H'${hex6(marA)}");
      if (ioarA != null) print("    IOAR${ch}A = H'FFFF${hex2(ioarA)} (low byte $ioarA)");
      if (dtcrA != null) print("    DTCR${ch}A = H'${hex2(dtcrA)}");
      if (marB != null) print("    MAR${ch}B  = H'${hex6(marB)}");
      if (ioarB != null) print("    IOAR${ch}B = H'FFFF${hex2(ioarB)}");
      if (dtcrB != null) print("    DTCR${ch}B = H'${hex2(dtcrB)}");
    }
    print('  first writes:');
    for (final w in m.dmacLog.take(40)) {
      print("    H'${hex6(w.$1)} -> H'${hex6(w.$2)} "
          "${(h8Registers[w.$2 & 0xFF] ?? '?').padRight(8)} = H'${hex2(w.$3)}");
    }
    print('');
  }

  if (m.deviceReads.isNotEmpty) {
    print('=== reads outside ROM/RAM/flash (candidate devices) ===');
    final dr = m.deviceReads.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));
    for (final e in dr.take(12)) {
      print("  H'${hex6(e.key << 8)}-H'${hex6((e.key << 8) + 0xFF)}  "
          "${e.value.toString().padLeft(8)} reads");
    }
    print('');
  }

  if (m.watchAddrCounts.isNotEmpty) {
    print('=== watch window ===');
    final ac = m.watchAddrCounts.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));
    print('  ${m.watchAddrCounts.length} distinct addresses written');
    for (final e in ac.take(10)) {
      print("    H'${hex6(e.key)}  ${e.value} writes");
    }
    final pc = m.watchPcCounts.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));
    print('  written by ${m.watchPcCounts.length} distinct instruction(s):');
    for (final e in pc.take(10)) {
      final d = disassembleH8(m.mem.peek, e.key);
      print("    H'${hex6(e.key)}  ${e.value.toString().padLeft(7)}x  "
          "${d.text}");
    }
    print('  first writes (pc, addr, value):');
    for (final w in m.watchLog.take(16)) {
      print("    H'${hex6(w.$1)} -> H'${hex6(w.$2)} = H'${hex2(w.$3)}");
    }
    print('');
  }

  if (m.unmodelledReads.isNotEmpty) {
    print('=== registers read with no model (guessed 0xFF) ===');
    final u = m.unmodelledReads.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));
    for (final e in u.take(10)) {
      print("  H'${hex6(e.key)} ${h8Registers[e.key & 0xFF] ?? '?'}  "
          "${e.value} reads");
    }
  }
}
