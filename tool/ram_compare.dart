// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Compares what two images have done to memory at the same point in the boot.
//
// The application is rebuilt a part at a time, so most of it does not run
// yet and a screen comparison says nothing. What can be checked is narrower:
// after the startup clear, the regions it was supposed to clear should hold
// the same thing in both. Each image is run until it reaches a given
// address, so the comparison is at the same stage rather than the same
// instruction count.
//
//   dart run tool/ram_compare.dart <a.bin> <pcA> <b.bin> <pcB> RANGE...
//   RANGE is START:END in hex.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String h6(int v) => "H'${v.toRadixString(16).toUpperCase().padLeft(6, '0')}";

H8Cpu? runTo(String path, int target, int limit) {
  final cpu = H8Cpu();
  loadRawBinary(File(path).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();
  for (var i = 0; i < limit; i++) {
    if (cpu.pc == target) return cpu;
    cpu.step();
  }
  return null;
}

void main(List<String> args) {
  final a = runTo(args[0], int.parse(args[1], radix: 16), 40000000);
  final b = runTo(args[2], int.parse(args[3], radix: 16), 40000000);
  if (a == null) {
    print('${args[0]} never reached ${h6(int.parse(args[1], radix: 16))}');
    exit(1);
  }
  if (b == null) {
    print('${args[2]} never reached ${h6(int.parse(args[3], radix: 16))}');
    exit(1);
  }

  var worst = 0;
  for (final spec in args.skip(4)) {
    final p = spec.split(':');
    final from = int.parse(p[0], radix: 16);
    final to = int.parse(p[1], radix: 16);
    var differ = 0;
    int? firstDiff;
    for (var addr = from; addr < to; addr++) {
      if (a.mem.peek(addr) != b.mem.peek(addr)) {
        differ++;
        firstDiff ??= addr;
      }
    }
    if (differ > worst) worst = differ;
    print('  ${h6(from)}-${h6(to - 1)}  ${to - from} bytes  '
        '${differ == 0 ? "identical" : "$differ differ, first at "
            "${h6(firstDiff!)}"}');
  }
  exit(worst == 0 ? 0 : 1);
}
