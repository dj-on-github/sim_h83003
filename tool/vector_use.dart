// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Counts how often the running machine enters each of the boot ROM's
// vector-slot routines, and what the original calling convention's argument
// registers held on entry. The application is the untouched binary, so it
// calls these the original's way; this says whether that matters in practice.
//
// Usage:
//   dart run tool/vector_use.dart <image.bin> <steps> NAME=HEXADDR ...

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  final steps = int.parse(args[1]);
  final watch = <int, String>{};
  for (final a in args.skip(2)) {
    final p = a.split('=');
    watch[int.parse(p[1], radix: 16)] = p[0];
  }

  final hits = <String, int>{};
  final firstArgs = <String, String>{};
  for (var i = 0; i < steps; i++) {
    final name = watch[cpu.pc];
    if (name != null) {
      hits[name] = (hits[name] ?? 0) + 1;
      firstArgs.putIfAbsent(name, () {
        final er6 = cpu.readL(0xFFFFFF & cpu.er[7]); // top of stack: return addr
        return "er6=H'${cpu.er[6].toRadixString(16).toUpperCase()}"
            "  er0=H'${cpu.er[0].toRadixString(16).toUpperCase()}"
            "  ret=H'${er6.toRadixString(16).toUpperCase()}";
      });
    }
    cpu.step();
  }

  for (final name in watch.values) {
    final n = hits[name] ?? 0;
    print('  ${name.padRight(18)} $n'
        '${n > 0 ? "   first: ${firstArgs[name]}" : ""}');
  }
}
