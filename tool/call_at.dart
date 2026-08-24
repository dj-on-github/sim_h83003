// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Calls a routine at a given address and shows what it did to memory.
//
// The rebuilt application and the original take their arguments differently,
// so a routine can only be compared by setting each one up its own way and
// looking at the result. This places registers and stack words as asked,
// calls, and dumps whatever ranges are named.
//
//   dart run tool/call_at.dart <image.bin> <funcHex> [--boot N]
//        [--rN VALUE] [--stack OFF:SIZE:VALUE] [--poke ADDR:VALUE]
//        [--dump ADDR:LEN]
//
// --stack places a word (SIZE 2) or longword (SIZE 4) at OFF bytes above the
// stack pointer, where offset 0 is the return address. All numbers hex.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int sentinel = 0x00FFF800;
const int scratchSp = 0x00FFF600;

int hex(String s) => int.parse(s, radix: 16);

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  int? optInt(String name) {
    final i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? hex(args[i + 1]) : null;
  }

  final boot = args.indexOf('--boot');
  if (boot >= 0) {
    final n = int.parse(args[boot + 1]);
    for (var i = 0; i < n; i++) {
      cpu.step();
    }
  }

  for (var i = 0; i < args.length; i++) {
    if (args[i] == '--poke') {
      final p = args[i + 1].split(':');
      cpu.writeB(hex(p[0]), hex(p[1]));
    }
  }

  void put(int addr, int size, int value) {
    for (var i = 0; i < size; i++) {
      cpu.mem.poke(addr + i, (value >> (8 * (size - 1 - i))) & 0xFF);
    }
  }

  put(scratchSp, 4, sentinel);
  for (var i = 0; i < args.length; i++) {
    if (args[i] == '--stack') {
      final p = args[i + 1].split(':');
      put(scratchSp + hex(p[0]), hex(p[1]), hex(p[2]));
    }
  }
  cpu.er[7] = scratchSp;

  for (var r = 0; r < 8; r++) {
    final v = optInt('--r$r');
    if (v != null) cpu.er[r] = v;
  }
  cpu.pc = hex(args[1]);

  var steps = 0;
  const limit = 8000000;
  while (cpu.pc != sentinel && steps < limit) {
    cpu.step();
    steps++;
  }
  if (steps >= limit) {
    print('did not return within $limit steps');
    exit(1);
  }

  final out = StringBuffer('steps: $steps');
  for (var r = 0; r < 8; r++) {
    if (r == 6 || r == 0) {
      out.write("  er$r=H'${cpu.er[r].toRadixString(16).toUpperCase()}");
    }
  }
  print(out.toString());

  for (var i = 0; i < args.length; i++) {
    if (args[i] != '--dump') continue;
    final p = args[i + 1].split(':');
    final from = hex(p[0]), n = hex(p[1]);
    final bytes = [for (var k = 0; k < n; k++) cpu.mem.peek(from + k)];
    print('  ${p[0]}: ${bytes.map((v) =>
        v.toRadixString(16).toUpperCase().padLeft(2, '0')).join(' ')}');
  }
}
