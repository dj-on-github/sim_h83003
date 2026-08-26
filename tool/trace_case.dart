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
bool isCall(H8Cpu cpu, int pc) {
  final b = cpu.mem.peek(pc);
  return b == 0x55 || b == 0x5C || b == 0x5D || b == 0x5E || b == 0x5F;
}

List<List<int>> trace(H8Cpu cpu, RoutineSide side, List<Seed> seed, int limit,
    int maxDepth, Set<int> hidden, int watchFrom, int watchLen) {
  for (final s in seed) {
    cpu.writeB(s.address, s.value);
  }
  const sentinel = 0x00FFFFF0;
  final sp = 0x00FFF000;
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

  // Only the calls the routine under test makes itself. Anything deeper is
  // another routine's business and has its own cases; counting it would just
  // fill the trace with the difference between one side's helpers and the
  // other's. A hidden routine keeps a frame but does not count towards the
  // depth, so its own calls stand where the call to it would have.
  //
  // Depth is followed by the stack pointer rather than by watching for RTS:
  // several routines here leave through a jump rather than a return, and an
  // RTS count goes wrong the moment one of them does. A frame is finished
  // when the stack has come back above where the call left it.
  final calls = <List<int>>[];
  final frames = <({int sp, bool transparent})>[];
  var effective = 0;
  var steps = 0;
  while (cpu.pc != sentinel && steps < limit) {
    while (frames.isNotEmpty && cpu.er[7] > frames.last.sp) {
      if (!frames.removeLast().transparent) effective--;
    }
    final was = cpu.pc;
    final call = isCall(cpu, was);
    cpu.step();
    steps++;
    if (call) {
      final transparent = hidden.contains(cpu.pc);
      frames.add((sp: cpu.er[7], transparent: transparent));
      if (!transparent) {
        effective++;
        if (effective <= maxDepth) {
          int w(int a) => (cpu.mem.peek(a) << 8) | cpu.mem.peek(a + 1);
          final sp = cpu.er[7];
          var sum = 0;
          for (var k = 0; k < watchLen; k++) {
            sum = (sum * 31 + cpu.mem.peek(watchFrom + k)) & 0x7FFFFFFF;
          }
          calls.add([
            cpu.pc,
            cpu.er[0] & 0xFFFFFF, cpu.er[1] & 0xFFFFFF, cpu.er[2] & 0xFFFFFF,
            cpu.er[6] & 0xFFFFFF,
            w(sp + 4), w(sp + 6), w(sp + 8), w(sp + 10),
            w(sp + 12), w(sp + 14), sum,
          ]);
        }
      }
    }
  }
  return calls;
}

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

void main(List<String> argv) {
  var args = argv;
  if (args.length < 2) {
    print('usage: dart run tool/trace_case.dart <spec.json> <case name>');
    exit(2);
  }
  var maxDepth = 1;
  var showAll = false;
  var watchFrom = 0, watchLen = 0;
  final hideNames = <String>[];
  final rest = <String>[];
  for (var i = 0; i < args.length; i++) {
    if (args[i] == '--depth') {
      maxDepth = int.parse(args[++i]);
    } else if (args[i] == '--watch') {
      final p = args[++i].split(':');
      watchFrom = hex(p[0]);
      watchLen = hex(p[1]);
    } else if (args[i] == '--all') {
      showAll = true;
    } else if (args[i] == '--hide') {
      hideNames.add(args[++i]);
    } else {
      rest.add(args[i]);
    }
  }
  args = rest;
  final spec = json.decode(File(args[0]).readAsStringSync())
      as Map<String, dynamic>;
  final symbols = loadSymbols(spec['symbols'] as String);
  final c = (spec['cases'] as List)
      .cast<Map<String, dynamic>>()
      .firstWhere((x) => x['name'] == args[1],
          orElse: () => throw FormatException('no case "${args[1]}"'));

  final bootSteps = c['boot'] as int? ?? 0;
  final seed = <Seed>[];
  (c['seed'] as Map<String, dynamic>? ?? const {})
      .forEach((k, v) => seed.add(Seed(hex(k), hex(v))));

  final names = <int, String>{};
  symbols.forEach((n, a) => names[a] = n);
  // The original's addresses get names too, taken from the spec itself:
  // every case pairs an address with the symbol that replaced it, so the
  // suite is already the map from one to the other.
  final originalNames = <int, String>{
    // The soft-float library, which has no comparison cases of its own and
    // so is not in the map the spec builds. Without these the two sides'
    // sequences differ by name at the first float and the diff is useless.
    0x200700: '__floatsisf',
    0x20070C: '__floatunsisf',
    0x20046C: '__mulsf3',
    0x200608: '__addsf3',
    0x200530: '__divsf3',
    0x2006C8: '__fixsfsi',
  };
  for (final other in (spec['cases'] as List).cast<Map<String, dynamic>>()) {
    final o = other['original'] as Map<String, dynamic>;
    final r = other['rebuilt'] as Map<String, dynamic>;
    if (o.containsKey('addr') && r.containsKey('symbol')) {
      originalNames[hex(o['addr'])] = r['symbol'] as String;
    }
  }

  final hidden = <int>{};
  for (final n in hideNames) {
    // A symbol for the rebuilt side, a bare hex address for the original's.
    final a = symbols[n] ?? int.tryParse(n.replaceAll("H'", ''), radix: 16);
    if (a != null) hidden.add(a);
  }

  final out = <List<List<int>>>[];
  for (final path in [spec['originalImage'], spec['rebuiltImage']]) {
    final cpu = boot(path as String, bootSteps);
    seedFill(cpu, c['fill'] as Map<String, dynamic>? ?? const {});
    final side = buildSide(
        c[path == spec['originalImage'] ? 'original' : 'rebuilt']
            as Map<String, dynamic>,
        symbols);
    out.add(trace(cpu, side, seed, 40000000, maxDepth, hidden,
        watchFrom, watchLen));
  }

  final a = out[0], b = out[1];
  print('original made ${a.length} calls, rebuild ${b.length}');
  String bare(String n) => n.replaceFirst(RegExp(r'^_+'), '');
  String left(int i) => i < a.length
      ? bare(originalNames[a[i][0]] ?? h6(a[i][0]))
      : '(end)';
  String right(int i) =>
      i < b.length ? bare(names[b[i][0]] ?? h6(b[i][0])) : '(end)';
  String hx(int v) => v.toRadixString(16).toUpperCase();
  // The original's first argument is in ER6 and the rest are on the stack;
  // the rebuild's first three are in ER0, ER1 and ER2 and the rest follow.
  String leftArgs(int i) => i >= a.length
      ? ''
      : 'er6=${hx(a[i][4])} sp+4=${hx(a[i][5])},${hx(a[i][6])},'
          '${hx(a[i][7])},${hx(a[i][8])},${hx(a[i][9])},${hx(a[i][10])}';
  String rightArgs(int i) => i >= b.length
      ? ''
      : 'er0=${hx(b[i][1])} er1=${hx(b[i][2])} er2=${hx(b[i][3])} '
          'sp+4=${hx(b[i][5])},${hx(b[i][6])},${hx(b[i][7])},'
          '${hx(b[i][8])},${hx(b[i][9])},${hx(b[i][10])}';

  var first = -1;
  for (var i = 0; i < (a.length > b.length ? a.length : b.length); i++) {
    if (left(i) != right(i)) {
      first = i;
      break;
    }
  }
  if (showAll) first = -1;
  if (first < 0) {
    print('the two call sequences agree all the way through');
    for (var i = 0; i < a.length; i++) {
      final same = watchLen == 0 || a[i][11] == b[i][11];
      print('${same ? " " : "*"}${i.toString().padLeft(4)}  '
          '${left(i).padRight(30)}${leftArgs(i)}');
      print('       ${''.padRight(30)}${rightArgs(i)}');
    }
    if (watchLen != 0) {
      print('(* marks a call the two sides reached with the watched range '
          'already differing, so the call before it is the one that '
          'diverged.)');
    }
    return;
  }
  print('they part company at call $first:');
  final from = first - 12 < 0 ? 0 : first - 12;
  for (var i = from; i < first + 12; i++) {
    print('${i == first ? ">" : " "}${i.toString().padLeft(5)}  '
        '${left(i).padRight(34)}${right(i)}');
  }
}
