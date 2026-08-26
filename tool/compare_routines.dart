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
import 'dart:typed_data';

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

/// One booted machine per (image, boot step), kept and handed back to case
/// after case.
///
/// Booting is 1.9M instructions and every case used to do it twice; over a
/// suite of a thousand cases that is five billion steps, and it is the same
/// five billion every time. The machine is booted once, its state outside
/// memory saved, and afterwards each case borrows it and puts it back: the
/// memory from its own undo log, everything else from the saved state.
///
/// [_restoreBooted] is what makes that safe, and `--verify-restore` checks
/// it the expensive way.
class _Booted {
  _Booted(this.cpu, this.state);
  final H8Cpu cpu;
  final List<int> state;
  Map<int, int>? live;

  /// Only filled in under --verify-restore: the whole of memory as the boot
  /// left it, to check the undo log against.
  Map<int, Uint8List>? pristine;
}

final Map<String, _Booted> _bootCache = {};

/// --no-boot-cache boots afresh for every case, the way this worked before
/// the cache existed. --verify-restore keeps the cache but checks after every
/// case that the machine really did go back byte for byte.
bool bootCache = true;
bool verifyRestore = false;

Map<int, Uint8List> _wholeMemory(H8Cpu cpu) {
  final out = <int, Uint8List>{};
  for (final (from, to) in cpu.mem.regions()) {
    for (var base = from; base < to; base += 0x10000) {
      final bank = Uint8List(0x10000);
      for (var i = 0; i < 0x10000; i++) {
        bank[i] = cpu.mem.peek(base + i);
      }
      out[base] = bank;
    }
  }
  return out;
}

void _checkRestored(_Booted b) {
  final want = b.pristine!;
  final now = _wholeMemory(b.cpu);
  for (final base in {...want.keys, ...now.keys}) {
    final a = want[base], c = now[base];
    for (var i = 0; i < 0x10000; i++) {
      final x = a == null ? 0 : a[i];
      final y = c == null ? 0 : c[i];
      if (x != y) {
        stderr.writeln('restore left H\'${(base + i).toRadixString(16)} at '
            '${y.toRadixString(16)}, was ${x.toRadixString(16)}');
        exit(3);
      }
    }
  }
}

H8Cpu load(String path, int boot, Map<String, dynamic> analog,
    Map<String, dynamic> serial) {
  final key = '$path|$boot';
  final cached = bootCache ? _bootCache[key] : null;
  if (cached != null) {
    _restoreBooted(cached);
    if (verifyRestore) _checkRestored(cached);
    _prepare(cached.cpu, analog, serial);
    cached.live = <int, int>{};
    cached.cpu.mem.undoLog = cached.live;
    return cached.cpu;
  }

  final cpu = H8Cpu();
  loadRawBinary(File(path).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();
  for (var i = 0; i < boot; i++) {
    cpu.step();
  }
  // The state that is cached is the machine *after* the interrupt mask, the
  // timer enables and the counters have been dealt with, not before: with the
  // timers still running, restoring would leave the counters to jump the
  // moment anything read them.
  _prepare(cpu, const {}, const {});
  final entry = _Booted(cpu, cpu.saveState());
  if (verifyRestore) entry.pristine = _wholeMemory(cpu);
  if (bootCache) _bootCache[key] = entry;
  _prepare(cpu, analog, serial);
  entry.live = <int, int>{};
  cpu.mem.undoLog = entry.live;
  return cpu;
}

/// Puts a borrowed machine back the way the boot left it: every byte the
/// last case wrote goes back to what it held, and the registers, flags and
/// peripheral models go back to the saved state.
void _restoreBooted(_Booted b) {
  final live = b.live;
  b.cpu.mem.undoLog = null;
  if (live != null) {
    live.forEach(b.cpu.mem.poke);
    b.live = null;
  }
  b.cpu.restoreState(b.state);
}

/// What every case needs set before it runs, cached machine or fresh one.
void _prepare(H8Cpu cpu, Map<String, dynamic> analog,
    Map<String, dynamic> serial) {
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
  // put into the model rather than into memory. A borrowed machine has
  // already had its receive register put back to what the boot left, so only
  // the cases that ask for a byte need to do anything here.
  serial.forEach((channel, value) {
    final ch = int.parse(channel) == 0 ? cpu.sci0 : cpu.sci1;
    ch.rdr = hex(value);
  });
  for (final ch in cpu.sci) {
    ch.syncToMemory();
  }
}

void main(List<String> args) {
  String? opt(String name) {
    final i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : null;
  }

  if (args.isEmpty) {
    print('usage: compare_routines <spec.json> [--original IMAGE] '
        '[--rebuilt IMAGE] [--symbols FILE] [--only NAME] '
        '[--no-boot-cache] [--verify-restore]');
    exit(2);
  }

  final spec = json.decode(File(args[0]).readAsStringSync())
      as Map<String, dynamic>;
  final originalPath = opt('--original') ?? spec['originalImage'] as String;
  final rebuiltPath = opt('--rebuilt') ?? spec['rebuiltImage'] as String;
  final symbols = loadSymbols(opt('--symbols') ?? spec['symbols'] as String);
  final only = opt('--only');
  bootCache = !args.contains('--no-boot-cache');
  verifyRestore = args.contains('--verify-restore');
  final fills = spec['fills'] as Map<String, dynamic>? ?? const {};

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
    //
    // The fill is applied in order -- the same address named twice takes the
    // value named last -- so composing a case on its base has to give back
    // the same sequence of pairs, not merely the same set. "fillBase" names
    // a fill in the spec's "fills" table; a key the base already has keeps
    // its place and takes the case's value, and a key it does not have goes
    // on the end. A case with no "fillBase" carries its whole fill, as
    // hand-written cases do.
    (composeFill(c, fills)).forEach((range, value) {
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
    // "steps": N raises the step limit for one case. The delays the module
    // link waits out are longer than anything else in the application, and
    // the default limit stops in the middle of one.
    final stepLimit = (c['steps'] as int?) ?? 8000000;
    final comparer =
        RoutineComparer(excluded: excluded, stepLimit: stepLimit);

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
    final show = int.tryParse(
        Platform.environment['COMPARE_SHOW_DIFFS'] ?? '') ?? 12;
    for (final d in diffs.take(show)) {
      final (o, n) = d.value;
      print('        ${h6(d.key)}: original '
          '${o < 0 ? "unchanged" : h2(o)}, rebuild '
          '${n < 0 ? "unchanged" : h2(n)}');
    }
    if (diffs.length > show) {
      print('        ... and ${diffs.length - show} more addresses');
    }
  }

  print('');
  print('$passed passed, $failed failed'
      '${skipped > 0 ? ", $skipped not selected" : ""}');
  exit(failed == 0 ? 0 : 1);
}
