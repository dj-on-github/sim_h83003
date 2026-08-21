// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Calls one of the boot ROM's vector-slot routines the way the application
// does: the first argument in ER6, any others on the stack, the result read
// back from R6. That is the original ROM's convention, so this exercises the
// rebuilt ROM's entry shims as well as the routine behind them.
//
// Usage:
//   dart run tool/vector_call.dart <image.bin> <slot> [er6] [arg1] [arg2]
//                                  [--flash BASE:SIZE] [--dump ADDR:LEN]
//                                  [--rx0 TEXT] [--rx1 TEXT] [--steps N]
// All numbers hex.

import 'dart:io';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int sentinel = 0x00FFF800; // nothing executes here; used as a landing pad
const int scratchSp = 0x00FFF700;

int hex(String s) => int.parse(s, radix: 16);

void main(List<String> args) {
  String? opt(String name) {
    final i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : null;
  }

  final positional = <String>[];
  for (var i = 0; i < args.length; i++) {
    if (args[i].startsWith('--')) {
      i++;
      continue;
    }
    positional.add(args[i]);
  }

  final cpu = H8Cpu();
  loadRawBinary(File(positional[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.mem.poke(0x200000, 0x01); // stay in the boot ROM
  final flashSpec = opt('--flash');
  if (flashSpec != null) {
    final p = flashSpec.split(':');
    cpu.attachFlash(JedecFlash(base: hex(p[0]), size: hex(p[1])));
  }
  cpu.reset();
  for (var i = 0; i < 1500000; i++) {
    cpu.step();
  }

  final slot = hex(positional[1]);
  final target = (cpu.mem.peek(slot * 4) << 24) |
      (cpu.mem.peek(slot * 4 + 1) << 16) |
      (cpu.mem.peek(slot * 4 + 2) << 8) |
      cpu.mem.peek(slot * 4 + 3);

  void poke32(int a, int v) {
    for (var i = 0; i < 4; i++) {
      cpu.mem.poke(a + i, (v >> (24 - 8 * i)) & 0xFF);
    }
  }

  poke32(scratchSp, sentinel);
  if (positional.length > 3) poke32(scratchSp + 4, hex(positional[3]));
  if (positional.length > 4) poke32(scratchSp + 8, hex(positional[4]));

  cpu.er[7] = scratchSp;
  cpu.er[6] = positional.length > 2 ? hex(positional[2]) : 0;
  cpu.pc = target;

  print("slot $slot -> H'${target.toRadixString(16).toUpperCase()}"
      "   page buffer at H'${_peek32(cpu, 0xFFFD10).toRadixString(16).toUpperCase()}");

  final rx0 = opt('--rx0');
  final rx1 = opt('--rx1');
  if (rx0 != null) cpu.sci0.receive(rx0.codeUnits);
  if (rx1 != null) cpu.sci1.receive(rx1.codeUnits);
  cpu.sci0.txLog.clear();
  cpu.sci1.txLog.clear();

  var steps = 0;
  final limit = int.parse(opt('--steps') ?? '40000000');
  while (cpu.pc != sentinel && steps < limit) {
    cpu.step();
    steps++;
  }
  if (steps >= limit) {
    print('did not return within $limit steps');
  } else {
    print('returned after $steps steps, r6 = '
        "H'${(cpu.er[6] & 0xFFFF).toRadixString(16).toUpperCase()}");
  }

  if (rx0 != null || rx1 != null) {
    String show(List<int> b) => b
        .map((c) => c >= 32 && c < 127 ? String.fromCharCode(c) : '.')
        .join();
    print('sci0 out "${show(cpu.sci0.txLog)}"');
    print('sci1 out "${show(cpu.sci1.txLog)}"');
  }

  final dump = opt('--dump');
  if (dump != null) {
    final p = dump.split(':');
    final a = hex(p[0]), n = hex(p[1]);
    final bytes = [for (var i = 0; i < n; i++) cpu.mem.peek(a + i)];
    print('${p[0]}: ${bytes.map((b) => b.toRadixString(16).toUpperCase().padLeft(2, '0')).join(' ')}');
  }
}

int _peek32(H8Cpu cpu, int a) =>
    (cpu.mem.peek(a) << 24) |
    (cpu.mem.peek(a + 1) << 16) |
    (cpu.mem.peek(a + 2) << 8) |
    cpu.mem.peek(a + 3);
