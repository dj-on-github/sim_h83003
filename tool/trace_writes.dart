// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// The call sequence of one comparison case, on both images, side by side.
//
// compare_routines says *that* two runs of a routine ended up with different
// memory; when the routine is a long one that is not enough to say where they
// parted company. This runs the same case and records every subroutine call
// each side makes, then prints the point at which the two sequences stop
// agreeing -- which is nearly always the call that went wrong, or the one
// just before it.
//
//   dart run tool/trace_case.dart <spec.json> <case name>
//        [--depth N] [--hide SYMBOL] [--watch FROM:LEN] [--all]
//
// --watch takes a checksum of a range of memory before every call counted
// and once at the end, and says which call the two sides stopped agreeing
// after -- which is the one that did something different, whatever its
// arguments looked like.
//
// --depth counts calls that deep and no deeper (1, the default, is the calls
// the routine makes itself). --hide drops one routine from the list and
// lifts its own calls in its place, which is what lines the two sides up
// -- give it a symbol for the rebuilt side and a bare address for the
// original's, one --hide each
// when the original reaches a screen body through its jump table while the
// rebuild calls it as a function.
//
// Names on the rebuilt side come from the symbol file; the original's are
// bare addresses, which is what the disassembler wants anyway.

import 'dart:convert';
import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/routine_compare.dart';

int hex(Object? v) =>
    v == null ? 0 : int.parse(v.toString().replaceAll("H'", ''), radix: 16);
String h6(int v) => "H'${v.toRadixString(16).toUpperCase().padLeft(6, '0')}";

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

({int index, int width}) parseRegister(String name) {
  final m = RegExp(r'^(e?)r(\d)([lh]?)$').firstMatch(name.toLowerCase());
  if (m == null) throw FormatException('not a register: "$name"');
  return (
    index: int.parse(m.group(2)!),
    width: m.group(3)!.isNotEmpty ? 1 : (m.group(1)!.isNotEmpty ? 4 : 2)
  );
}

RoutineSide buildSide(Map<String, dynamic> spec, Map<String, int> symbols) {
  final args = <Placement>[];
  (spec['regs'] as Map<String, dynamic>? ?? const {}).forEach((n, v) {
    args.add(RegisterArg(parseRegister(n).index, hex(v)));
  });
  (spec['stack'] as Map<String, dynamic>? ?? const {}).forEach((off, s) {
    final p = s.toString().split(':');
    args.add(StackArg(hex(off), int.parse(p[0]), hex(p[1])));
  });
  final address = spec.containsKey('symbol')
      ? (symbols[spec['symbol']] ??
          (throw FormatException('no symbol "${spec['symbol']}"')))
      : hex(spec['addr']);
  return RoutineSide(address: address, args: args);
}

/// Whether the instruction at [pc] is a call, so that the next PC is the
/// routine being entered. JSR is H'5E (absolute), H'5D (register indirect)
/// and H'5F (through the vector table); BSR is H'55 (byte) and H'5C (word).
H8Cpu boot(String path, int steps) {
  final cpu = H8Cpu();
  loadRawBinary(File(path).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();
  for (var i = 0; i < steps; i++) {
    cpu.step();
  }
  // The same masking compare_routines does: interrupts off, the five timer
  // enables cleared and the counters stopped, so that the two sides run the
  // same window rather than one of them taking a millisecond handler the
  // other does not.
  cpu.ccr |= 0x80;
  for (final tier in [0xFFFF66, 0xFFFF70, 0xFFFF7A, 0xFFFF84, 0xFFFF94]) {
    cpu.writeB(tier, 0x00);
  }
  cpu.writeB(0xFFFF60, 0x00);
  return cpu;
}

void seedFill(H8Cpu cpu, Map<String, dynamic> fill) {
  fill.forEach((key, value) {
    final p = key.split(':');
    final from = hex(p[0]);
    final span = p.length > 1 ? hex(p[1]) : 1;
    for (var i = 0; i < span; i++) {
      cpu.writeB(from + i, hex(value));
    }
  });
}

/// Every write into [from]..[from+len) in the order it happened, with the
/// program counter of the instruction that made it. This is what says which
/// routine wrote somewhere it should not have: the address diff that
/// compare_routines prints is sorted by address, and the first *address* is
/// hardly ever the first *write*.
List<List<int>> traceWrites(H8Cpu cpu, RoutineSide side, List<Seed> seed,
    int limit, int from, int len, int keep) {
  for (final s in seed) {
    cpu.writeB(s.address, s.value);
  }
  const sentinel = 0x00FFF800;
  const sp = 0x00FFF600;
  for (var i = 0; i < 4; i++) {
    cpu.mem.poke(sp + i, (sentinel >> (8 * (3 - i))) & 0xFF);
  }
  for (final a in side.args) {
    switch (a) {
      case RegisterArg(:final index, :final value):
        cpu.er[index] = value;
      case StackArg(:final offset, :final size, :final value):
        for (var i = 0; i < size; i++) {
          cpu.mem.poke(sp + offset + i, (value >> (8 * (size - 1 - i))) & 0xFF);
        }
    }
  }
  cpu.er[7] = sp;
  cpu.pc = side.address;

  final out = <List<int>>[];
  var steps = 0;
  while (cpu.pc != sentinel && steps < limit) {
    final pc = cpu.pc;
    final log = <int, int>{};
    final outer = cpu.mem.undoLog;
    cpu.mem.undoLog = log;
    cpu.step();
    cpu.mem.undoLog = outer;
    if (outer != null) {
      log.forEach((addr, was) => outer.putIfAbsent(addr, () => was));
    }
    for (final addr in log.keys) {
      if (addr >= from && addr < from + len && out.length < keep) {
        out.add([pc, addr, cpu.mem.peek(addr)]);
      }
    }
    steps++;
  }
  return out;
}

void main(List<String> argv) {
  if (argv.length < 3) {
    print('usage: dart run tool/trace_writes.dart <spec.json> <case name>'
        ' <FROM:LEN> [--keep N]');
    exit(2);
  }
  var keep = 30;
  final rest = <String>[];
  for (var i = 0; i < argv.length; i++) {
    if (argv[i] == '--keep') {
      keep = int.parse(argv[++i]);
    } else {
      rest.add(argv[i]);
    }
  }
  final spec =
      json.decode(File(rest[0]).readAsStringSync()) as Map<String, dynamic>;
  final symbols = loadSymbols(spec['symbols'] as String);
  final names = <int, String>{};
  symbols.forEach((n, a) => names[a] = n);
  final c = (spec['cases'] as List)
      .cast<Map<String, dynamic>>()
      .firstWhere((x) => x['name'] == rest[1],
          orElse: () => throw FormatException('no case "${rest[1]}"'));
  final p = rest[2].split(':');
  final from = hex(p[0]), len = hex(p[1]);

  final bootSteps = c['boot'] as int? ?? 0;
  final seed = <Seed>[];
  (c['seed'] as Map<String, dynamic>? ?? const {})
      .forEach((k, v) => seed.add(Seed(hex(k), hex(v))));

  for (final which in ['original', 'rebuilt']) {
    final path =
        (which == 'original' ? spec['originalImage'] : spec['rebuiltImage'])
            as String;
    final cpu = boot(path, bootSteps);
    seedFill(cpu, composeFill(
        c, spec['fills'] as Map<String, dynamic>? ?? const {}));
    final side = buildSide(c[which] as Map<String, dynamic>, symbols);
    final rows = traceWrites(cpu, side, seed, 40000000, from, len, keep);
    print('$which: ${rows.length} writes into ${h6(from)}');
    for (final r in rows) {
      var where = '';
      if (which == 'rebuilt') {
        var best = 0;
        names.forEach((a, n) {
          if (a <= r[0] && a > best) best = a;
        });
        if (best != 0) where = '  ${names[best]}+${r[0] - best}';
      }
      print('  pc ${h6(r[0])}  ${h6(r[1])} = '
          "H'${r[2].toRadixString(16).toUpperCase().padLeft(2, '0')}$where");
    }
  }
}
