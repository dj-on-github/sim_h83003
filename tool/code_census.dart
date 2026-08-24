// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Measures how much of the application actually runs, and where it lives.
//
// A static function list counts everything the encoding mentions, including
// code that never executes on this machine and data that happens to
// disassemble. Running the firmware and recording which addresses are
// executed separates the part that has to be got right first from the long
// tail — which is what decides the order the rewrite is done in.
//
//   dart run tool/code_census.dart <image.bin> [steps]

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int codeBase = 0x200000;
const int codeEnd = 0x251000;

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.profiling = true;
  cpu.reset();

  final steps = int.parse(args.length > 1 ? args[1] : '40000000');
  for (var i = 0; i < steps; i++) {
    cpu.step();
  }

  // Addresses that executed at least once.
  final executed = <int>{};
  var total = 0;
  for (final e in cpu.instrExecCount.nonZeroEntries()) {
    if (e.key >= codeBase && e.key < codeEnd) {
      executed.add(e.key);
      total += e.value;
    }
  }

  print('instructions retired in the application: $total');
  print('distinct instruction addresses executed: ${executed.length}');

  // Group into 4K pages so the shape of the working set is visible.
  final pages = <int, int>{};
  for (final a in executed) {
    pages[a & ~0xFFF] = (pages[a & ~0xFFF] ?? 0) + 1;
  }
  print('4K pages touched: ${pages.length} of '
      '${(codeEnd - codeBase) ~/ 0x1000}');
  print('');
  print('executed address ranges (gaps of 256 bytes or more split them):');
  final sorted = executed.toList()..sort();
  var start = sorted.first, prev = sorted.first, spans = 0, covered = 0;
  void emit(int a, int b) {
    spans++;
    covered += b - a;
    if (spans <= 24) {
      print("  H'${a.toRadixString(16).toUpperCase()}-"
          "H'${b.toRadixString(16).toUpperCase()}  ${b - a} bytes");
    }
  }

  for (final a in sorted.skip(1)) {
    if (a - prev >= 256) {
      emit(start, prev);
      start = a;
    }
    prev = a;
  }
  emit(start, prev);
  if (spans > 24) print('  ... and ${spans - 24} more');
  print('');
  print('spans: $spans, spanning $covered bytes of '
      '${codeEnd - codeBase} in the code region');
}
