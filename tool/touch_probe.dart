// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Asks one question: when the simulator injects a touch on the A/D inputs,
// does the firmware see it?
//
// Runs the image on the same core the app runs, with the same A/D model,
// then presses and releases the panel while watching which channels the
// firmware converts, what it reads back, and which of the touch routines it
// reaches.
//
// Usage:
//   dart run tool/touch_probe.dart <image.bin> [--warmup N] [--hold N]
//                                 [--x HEX] [--y HEX]

import 'dart:io';

import 'package:sim_h83003/adc.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// The core, with every A/D register access logged.
class ProbeCpu extends H8Cpu {
  /// Channel selected on each ADCSR write that started a conversion.
  final Map<int, int> startsByChannel = {};

  /// Reads of each result register, and the value handed back.
  final Map<int, int> resultReads = {};
  final Map<int, Set<int>> resultValues = {};

  /// Instruction addresses we want hit counts for.
  final Map<int, int> probeHits = {};
  Set<int> probes = {};

  bool logging = false;

  @override
  void writeB(int addr, int value) {
    if (logging && addr == 0xFFFFE8) {
      final wasRunning = (adc.adcsr & AdcStatus.adst) != 0;
      if (!wasRunning && (value & AdcStatus.adst) != 0) {
        final ch = value & AdcStatus.channel;
        startsByChannel[ch] = (startsByChannel[ch] ?? 0) + 1;
      }
    }
    super.writeB(addr, value);
  }

  /// An extra address to trace: which instruction reads it, and what it got.
  int traceAddr = -1;
  final Map<int, int> traceReaders = {};
  final Map<int, int> traceValues = {};

  @override
  int readB(int addr) {
    final v = super.readB(addr);
    if (logging && addr >= 0xFFFFE0 && addr <= 0xFFFFE7 && (addr & 1) == 0) {
      resultReads[addr] = (resultReads[addr] ?? 0) + 1;
      (resultValues[addr] ??= {}).add(v);
    }
    if (logging && addr == traceAddr) {
      traceReaders[pc] = (traceReaders[pc] ?? 0) + 1;
      traceValues[v] = (traceValues[v] ?? 0) + 1;
    }
    return v;
  }
}

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/touch_probe.dart <image.bin> '
        '[--warmup N] [--hold N] [--x HEX] [--y HEX]');
    exit(2);
  }
  String? opt(String n) {
    final i = args.indexOf(n);
    return (i >= 0 && i + 1 < args.length) ? args[i + 1] : null;
  }

  final warmup = int.parse(opt('--warmup') ?? '40000000');
  final hold = int.parse(opt('--hold') ?? '20000000');
  final x = int.parse(opt('--x') ?? 'A0', radix: 16);
  final y = int.parse(opt('--y') ?? 'A0', radix: 16);

  final cpu = ProbeCpu();
  final bytes = File(args.first).readAsBytesSync();
  loadRawBinary(bytes, 0, cpu.mem.poke);
  cpu.reset();

  // The addresses the analysis named, so we can see which are on the live
  // path and which only ran at startup.
  const names = {
    0x20A030: 'touch_init',
    0x2091F0: 'touch_sample_axes (entry)',
    0x209208: 'touch_sample_axes+18 (the breakpoint)',
    0x200914: 'adc_start_channel',
    0x20084A: 'adc_get_result',
    0x209C44: 'adc_scan_step',
    0x2092AA: 'touch_debounce_x',
    0x209326: 'touch_debounce_y',
    0x209262: 'touch_event_x',
    0x209286: 'touch_event_y',
    0x209C70: 'store X sample',
    0x209C7A: 'AN4 >= H\'4C (fall-through of the BCS)',
    0x209C80: 'debounce cap test',
    0x209C84: 'X over threshold: bump debounce',
    0x209CCA: 'store Y sample',
    0x209CE6: 'Y window test reached',
    0x209CFE: 'Y in window: bump debounce',
  };

  const vars = {
    0x11A814: 'x_debounce',
    0x11A815: 'x_release_count',
    0x11A816: 'y_debounce',
    0x11A817: 'y_release_count',
    0x11A818: 'adc_current_channel',
    0x11A819: 'adc_next_channel',
    0x114DC6: 'mode_114DC6',
    0x114DD7: 'latch_114DD7',
    0xFFFEC0: 'mode_FFFEC0',
    0xFFFEC1: 'flags_FFFEC1',
    0xFFFEF0: 'touch_x_raw',
    0xFFFEF1: 'touch_y_raw',
  };
  cpu.probes = names.keys.toSet();
  cpu.traceAddr = int.parse(opt('--trace') ?? '-1', radix: 16);

  void run(int steps) {
    for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
      if (cpu.probes.contains(cpu.pc)) {
        cpu.probeHits[cpu.pc] = (cpu.probeHits[cpu.pc] ?? 0) + 1;
      }
      cpu.step();
    }
  }

  /// Bytes of the frame buffer that changed since the last call, so we can
  /// tell whether a press produced any visible effect at all.
  var fbPrev = List<int>.filled(0, 0);
  int fbChanged() {
    final now = [
      for (var i = 0; i < 0x4B00; i++) cpu.mem.peek(0x040000 + i),
    ];
    if (fbPrev.isEmpty) {
      fbPrev = now;
      return -1;
    }
    var n = 0;
    for (var i = 0; i < now.length; i++) {
      if (now[i] != fbPrev[i]) n++;
    }
    fbPrev = now;
    return n;
  }

  void report(String phase) {
    print('--- $phase ---');
    final fb = fbChanged();
    print('  frame buffer: ${fb < 0 ? 'baseline' : '$fb of 19200 bytes '
        'changed since the previous phase'}');
    print('  PC = H\'${hex6(cpu.pc)}'
        '${cpu.halted ? "  HALTED: ${cpu.haltReason}" : ""}');
    final ch = cpu.startsByChannel.keys.toList()..sort();
    print('  conversions started, by channel: '
        '${ch.isEmpty ? "none" : ch.map((c) => 'AN$c='
            '${cpu.startsByChannel[c]}').join('  ')}');
    final rr = cpu.resultReads.keys.toList()..sort();
    for (final a in rr) {
      final vals = cpu.resultValues[a]!.toList()..sort();
      final shown = vals.length > 8
          ? '${vals.take(8).map(hex2).join(",")},… (${vals.length} distinct)'
          : vals.map(hex2).join(',');
      print("  read H'${hex6(a)} ${cpu.resultReads[a]}x  values: $shown");
    }
    for (final e in names.entries) {
      final n = cpu.probeHits[e.key] ?? 0;
      print("  H'${hex6(e.key)} ${e.value.padRight(38)} "
          '${n == 0 ? 'never' : '${n}x'}');
    }
    if (cpu.traceReaders.isNotEmpty) {
      final rs = cpu.traceReaders.entries.toList()
        ..sort((a, b) => b.value.compareTo(a.value));
      print("  H'${hex6(cpu.traceAddr)} read by "
          '${rs.take(6).map((e) => "H'${hex6(e.key)} (${e.value}x)").join(", ")}'
          '  values: '
          '${cpu.traceValues.keys.map(hex2).join(",")}');
    }
    final vs = vars.entries
        .map((e) => "${e.value}=H'${hex2(cpu.peekBus(e.key))}")
        .join('  ');
    print('  $vs');
    print('');
  }

  print('warming up $warmup instructions with the panel untouched...');
  run(warmup);
  cpu.logging = true;
  cpu.startsByChannel.clear();
  cpu.resultReads.clear();
  cpu.resultValues.clear();
  cpu.probeHits.clear();
  cpu.traceReaders.clear();
  cpu.traceValues.clear();
  run(hold);
  report('untouched');

  print("pressing at X=H'${hex2(x)} Y=H'${hex2(y)} on AN4/AN6...");
  cpu.adc.setInput8(4, x);
  cpu.adc.setInput8(6, y);
  cpu.startsByChannel.clear();
  cpu.resultReads.clear();
  cpu.resultValues.clear();
  cpu.probeHits.clear();
  cpu.traceReaders.clear();
  cpu.traceValues.clear();
  run(hold);
  report('pressed on AN4/AN6');

  // --force-mode: write the machine mode byte the AN4 press handler demands,
  // to find out whether the handler is what the UI is waiting on.
  final forceMode = opt('--force-mode');
  if (forceMode != null) {
    cpu.mem.poke(0xFFFEC0, int.parse(forceMode, radix: 16));
    print("forced mode_FFFEC0 = H'$forceMode");
    cpu.startsByChannel.clear();
    cpu.resultReads.clear();
    cpu.resultValues.clear();
    cpu.probeHits.clear();
    cpu.traceReaders.clear();
    cpu.traceValues.clear();
    run(hold);
    report('pressed, mode forced');
  }

  // --poke ADDR:VAL,... : drive some other candidate input and see whether
  // the screen reacts, the same way the A/D press was tested.
  final pokes = (opt('--poke') ?? '')
      .split(RegExp(r'[\s,]+'))
      .where((s) => s.contains(':'))
      .toList();
  if (pokes.isNotEmpty) {
    for (final pk in pokes) {
      final parts = pk.split(':');
      final a = int.parse(parts[0], radix: 16);
      final v = int.parse(parts[1], radix: 16);
      cpu.mem.poke(a, v);
      print("poked H'${hex6(a)} = H'${hex2(v)}");
    }
    cpu.startsByChannel.clear();
    cpu.resultReads.clear();
    cpu.resultValues.clear();
    cpu.probeHits.clear();
    cpu.traceReaders.clear();
    cpu.traceValues.clear();
    run(hold);
    report('after poke');
  }

  // --pin DRADDR:BIT:LEVEL,... : hold input pins from outside the chip and
  // see whether the screen reacts, the same way the A/D press was tested.
  final pins = (opt('--pin') ?? '')
      .split(RegExp(r'[\s,]+'))
      .where((s) => s.split(':').length == 3)
      .toList();
  if (pins.isNotEmpty) {
    for (final pin in pins) {
      final parts = pin.split(':');
      final dr = int.parse(parts[0], radix: 16);
      final bit = int.parse(parts[1]);
      final high = parts[2] != '0';
      cpu.setPin(dr, bit, high);
      final port = H8Cpu.portByDr[dr];
      final ddr = port?.ddrAddr;
      final isInput = ddr == null || (cpu.mem.peek(ddr) >> bit) & 1 == 0;
      print("held H'${hex6(dr)} bit $bit ${high ? 'high' : 'low'}"
          '  (port ${port?.name ?? '?'}, '
          '${isInput ? 'an input, so it takes effect' : 'an OUTPUT, ignored'})');
    }
    cpu.startsByChannel.clear();
    cpu.resultReads.clear();
    cpu.resultValues.clear();
    cpu.probeHits.clear();
    cpu.traceReaders.clear();
    cpu.traceValues.clear();
    run(hold);
    report('pins held');
  }

  final x2 = int.parse(opt('--x2') ?? '70', radix: 16);
  final y2 = int.parse(opt('--y2') ?? '70', radix: 16);
  print("pressing at X=H'${hex2(x2)} Y=H'${hex2(y2)}...");
  cpu.adc.setInput8(4, x2);
  cpu.adc.setInput8(6, y2);
  cpu.startsByChannel.clear();
  cpu.resultReads.clear();
  cpu.resultValues.clear();
  cpu.probeHits.clear();
  cpu.traceReaders.clear();
  cpu.traceValues.clear();
  run(hold);
  report('second press');
}
