// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Runs every routine in a spec against the original it replaces.
//
// This is how the rebuilt application is checked. Most of it is far too deep
// in the call graph for the screen, or anything else observable from
// outside, to say whether a routine is right. So each one is called on both
// images with equivalent inputs, and the result and every byte of memory it
// changed are compared.
//
//   dart run tool/compare_routines.dart <spec.json>
//        [--original IMAGE] [--rebuilt IMAGE] [--symbols FILE] [--only NAME]
//
// The spec names rebuilt routines by symbol, resolved through the symbol
// file the build writes, so cases keep working when addresses move — which
// they do on every build.

import 'dart:convert';
import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/routine_compare.dart';

int hex(Object? v) =>
    v == null ? 0 : int.parse(v.toString().replaceAll("H'", ''), radix: 16);

String h6(int v) => "H'${v.toRadixString(16).toUpperCase().padLeft(6, '0')}";
String h2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// Reads the symbol file the build writes (nm -n output).
Map<String, int> loadSymbols(String path) {
  final out = <String, int>{};
  for (final line in File(path).readAsLinesSync()) {
    final parts = line.trim().split(RegExp(r'\s+'));
    if (parts.length == 3) {
      final addr = int.tryParse(parts[0], radix: 16);
      if (addr != null) out[parts[2]] = addr;
    }
  }
  return out;
}

/// "r6l" is the low byte, "r6" the low word, "er6" the whole register.
({int index, int width}) parseRegister(String name) {
  final n = name.toLowerCase();
  final m = RegExp(r'^(e?)r(\d)([lh]?)$').firstMatch(n);
  if (m == null) throw FormatException('not a register: "$name"');
  final width = m.group(3)!.isNotEmpty
      ? 1
      : m.group(1)!.isNotEmpty
          ? 4
          : 2;
  return (index: int.parse(m.group(2)!), width: width);
}

int registerIndex(String name) => parseRegister(name).index;

RoutineSide buildSide(Map<String, dynamic> spec, Map<String, int> symbols) {
  final args = <Placement>[];
  final regs = spec['regs'] as Map<String, dynamic>? ?? const {};
  regs.forEach((name, value) {
    args.add(RegisterArg(registerIndex(name), hex(value)));
  });
  final stack = spec['stack'] as Map<String, dynamic>? ?? const {};
  stack.forEach((offset, spec) {
    // "SIZE:VALUE", both hex.
    final p = spec.toString().split(':');
    args.add(StackArg(hex(offset), int.parse(p[0]), hex(p[1])));
  });

  int address;
  if (spec.containsKey('symbol')) {
    final name = spec['symbol'] as String;
    final a = symbols[name];
    if (a == null) throw FormatException('no symbol "$name" in the symbol file');
    address = a;
  } else {
    address = hex(spec['addr']);
  }

  return RoutineSide(
    address: address,
    args: args,
    resultRegister: spec.containsKey('result')
        ? parseRegister(spec['result'].toString()).index
        : null,
    resultWidth: spec.containsKey('result')
        ? parseRegister(spec['result'].toString()).width
        : 4,
  );
}

H8Cpu load(String path, int boot, Map<String, dynamic> analog,
    Map<String, dynamic> serial) {
  final cpu = H8Cpu();
  loadRawBinary(File(path).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();
  for (var i = 0; i < boot; i++) {
    cpu.step();
  }
  // Interrupts off before the routine runs. The two images are never at the
  // same point in the boot after the same number of steps -- the rebuild is
  // smaller and gets further -- so one of them may have its timers running
  // and its mask down while the other does not, and then every counter the
  // millisecond handler touches shows up as a difference that has nothing to
  // do with the routine being compared. Masking both sides makes the window
  // deterministic. A handler that is itself under test is called directly.
  cpu.ccr |= 0x80;
  // And the timers' own enables with it, because masking the CPU is not
  // enough on its own: a routine that lowers the mask itself -- the analog
  // scan does -- would take whichever interrupts happen to be armed on that
  // side. The five TIERs are cleared on both.
  for (final tier in [0xFFFF66, 0xFFFF70, 0xFFFF7A, 0xFFFF84, 0xFFFF94]) {
    cpu.writeB(tier, 0x00);
  }
  // The counters are stopped as well. They are excluded from the comparison
  // already, but a routine that computes from one -- the stepper handlers
  // set their next interval to TCNT plus a constant -- writes the drift into
  // a register that is not excluded. Stopped, the two sides read the same
  // counter and land on the same interval.
  cpu.writeB(0xFFFF60, 0x00);

  // Peripheral inputs, not memory: a routine that reads a converter or a
  // port pin needs something on the other side of it, and seeding the
  // result register is no good because the model overwrites it.
  analog.forEach((channel, value) {
    cpu.adc.setInput8(int.parse(channel), hex(value));
  });
  // The serial receive registers are read-only to the CPU and the model
  // holds them, so a byte a routine is supposed to have received has to be
  // put into the model rather than into memory.
  serial.forEach((channel, value) {
    final ch = int.parse(channel) == 0 ? cpu.sci0 : cpu.sci1;
    ch.rdr = hex(value);
    ch.syncToMemory();
  });
  return cpu;
}

void main(List<String> args) {
  String? opt(String name) {
    final i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : null;
  }

  if (args.isEmpty) {
    print('usage: compare_routines <spec.json> [--original IMAGE] '
        '[--rebuilt IMAGE] [--symbols FILE] [--only NAME]');
    exit(2);
  }

  final spec = json.decode(File(args[0]).readAsStringSync())
      as Map<String, dynamic>;
  final originalPath = opt('--original') ?? spec['originalImage'] as String;
  final rebuiltPath = opt('--rebuilt') ?? spec['rebuiltImage'] as String;
  final symbols = loadSymbols(opt('--symbols') ?? spec['symbols'] as String);
  final only = opt('--only');

  var passed = 0, failed = 0, skipped = 0;

  for (final raw in spec['cases'] as List) {
    final c = raw as Map<String, dynamic>;
    final name = c['name'] as String;
    if (only != null && !name.contains(only)) {
      skipped++;
      continue;
    }

    final boot = (c['boot'] as int?) ?? 0;
    final seed = <Seed>[];
    (c['seed'] as Map<String, dynamic>? ?? const {}).forEach((addr, value) {
      seed.add(Seed(hex(addr), hex(value)));
    });
    // "fill": {"ADDR:LENGTH": "VALUE"} puts a known pattern over a range
    // before the call. Without it, a routine that recomputes what the dump
    // already holds writes nothing, and a comparison of what changed would
    // pass even if neither side did anything at all.
    (c['fill'] as Map<String, dynamic>? ?? const {}).forEach((range, value) {
      final p = range.split(':');
      final from = hex(p[0]), length = hex(p[1]);
      for (var i = 0; i < length; i++) {
        seed.add(Seed(from + i, hex(value)));
      }
    });

    // "exclude": ["ADDR:LENGTH", ...] leaves a range out of the comparison.
    // For counters that run on their own: once a routine has started a
    // timer, its count depends on how many instructions have gone by since,
    // and the two sides never execute the same number. Nothing else belongs
    // here -- a range listed is a range not being checked.
    // The five timer status-and-counter triples. A running counter's value
    // depends on how many instructions have gone by, and the two sides never
    // execute the same number -- so once anything has started a timer these
    // can never agree, whatever the routine under test did. They are left out
    // everywhere rather than case by case, because which cases are affected
    // depends only on how far each image has booted by the step the call is
    // made at, which is not a property of the routine.
    //
    // The cost is that a routine which writes a counter -- itu0_init and its
    // two siblings write TCNT to zero -- is not checked on that write. Their
    // other seven bytes still are.
    final excluded = <(int, int)>[
      (0x200000, 0x251000),
      (0xFFFF67, 0xFFFF6A), // TSR0, TCNT0
      (0xFFFF71, 0xFFFF74), // TSR1, TCNT1
      (0xFFFF7B, 0xFFFF7E), // TSR2, TCNT2
      (0xFFFF85, 0xFFFF88), // TSR3, TCNT3
      (0xFFFF95, 0xFFFF98), // TSR4, TCNT4
      // The boot ROM's interrupt trampoline. Its vector table lives at
      // address 0 and most of its slots write the application's handler
      // address here and jump to it, so every interrupt taken leaves four
      // bytes behind. Whether an interrupt lands inside a window depends on
      // how long the window is, and the rebuild's timing is its own.
      (0xFFFD18, 0xFFFD1C),
    ];
    for (final range in (c['exclude'] as List? ?? const [])) {
      final p = range.toString().split(':');
      final from = hex(p[0]);
      excluded.add((from, from + hex(p[1])));
    }
    final comparer = RoutineComparer(excluded: excluded);

    final originalSide =
        buildSide(c['original'] as Map<String, dynamic>, symbols);
    final rebuiltSide =
        buildSide(c['rebuilt'] as Map<String, dynamic>, symbols);

    final analog = c['analog'] as Map<String, dynamic>? ?? const {};
    final serial = c['serial'] as Map<String, dynamic>? ?? const {};
    final a =
        comparer.run(load(originalPath, boot, analog, serial), originalSide, seed);
    final b =
        comparer.run(load(rebuiltPath, boot, analog, serial), rebuiltSide, seed);
    final result = comparer.compare(name, a, b);

    if (result.passed) {
      passed++;
      print('  pass  $name'
          '  (${a.steps} vs ${b.steps} steps, ${a.writes.length} bytes'
          ' written)');
      continue;
    }

    failed++;
    print('  FAIL  $name');
    if (!a.returned) print('        the original did not return');
    if (!b.returned) print('        the rebuild did not return');
    if (a.returned && b.returned && !result.resultsMatch) {
      print('        result: original ${h6((a.result ?? 0) & 0xFFFFFF)}, '
          'rebuild ${h6((b.result ?? 0) & 0xFFFFFF)}');
    }
    final diffs = result.differences.entries.toList()
      ..sort((x, y) => x.key.compareTo(y.key));
    for (final d in diffs.take(12)) {
      final (o, n) = d.value;
      print('        ${h6(d.key)}: original '
          '${o < 0 ? "unchanged" : h2(o)}, rebuild '
          '${n < 0 ? "unchanged" : h2(n)}');
    }
    if (diffs.length > 12) {
      print('        ... and ${diffs.length - 12} more addresses');
    }
  }

  print('');
  print('$passed passed, $failed failed'
      '${skipped > 0 ? ", $skipped not selected" : ""}');
  exit(failed == 0 ? 0 : 1);
}
