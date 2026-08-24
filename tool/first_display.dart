// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Finds the shortest path from reset to the display coming alive.
//
// The application is being rebuilt a part at a time, and the part worth
// reaching first is whatever makes the screen show something, because a
// screen comparison is the only strong check available. This reports where
// the machine first touches the LCD controller and the frame buffer, and the
// return addresses on the stack at that moment — which is the call path that
// has to exist in the rewrite before anything can be drawn.
//
//   dart run tool/first_display.dart <image.bin> [steps]

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int lcdRegs = 0x020000;
const int frameBuffer = 0x040000;
const int codeBase = 0x200000;
const int codeEnd = 0x251000;

String h6(int v) => "H'${v.toRadixString(16).toUpperCase().padLeft(6, '0')}";

/// Return addresses sitting on the stack, outermost last. JSR pushes a
/// longword whose top byte is unused, so a plausible entry is one that lands
/// inside the code region.
List<int> callPath(H8Cpu cpu) {
  final out = <int>[];
  final sp = cpu.er[7] & 0xFFFFFF;
  for (var a = sp; a < sp + 0x200 && out.length < 16; a += 2) {
    final v = (cpu.mem.peek(a) << 24) |
        (cpu.mem.peek(a + 1) << 16) |
        (cpu.mem.peek(a + 2) << 8) |
        cpu.mem.peek(a + 3);
    final addr = v & 0xFFFFFF;
    if ((v >> 24) == 0 && addr >= codeBase && addr < codeEnd) out.add(addr);
  }
  return out;
}

void run(String path, int steps, String what, Set<int> watch) {
  final cpu = H8Cpu();
  loadRawBinary(File(path).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();
  cpu.dataBreaks.addAll(watch);

  for (var i = 0; i < steps; i++) {
    cpu.clearBreakHit();
    cpu.step();
    if (cpu.breakHit) {
      print('$what first touched at step $i');
      print('  address ${h6(cpu.breakAddr!)} from ${h6(cpu.breakPc!)}');
      final path = callPath(cpu);
      if (path.isNotEmpty) {
        print('  return addresses on the stack (innermost first):');
        for (final a in path) {
          print('    ${h6(a)}');
        }
      }
      return;
    }
  }
  print('$what never touched in $steps steps');
}

void main(List<String> args) {
  final steps = int.parse(args.length > 1 ? args[1] : '40000000');

  run(args[0], steps, 'the LCD controller',
      {for (var a = lcdRegs; a < lcdRegs + 32; a++) a});
  print('');
  run(args[0], steps, 'the frame buffer',
      {frameBuffer, frameBuffer + 1, frameBuffer + 0x1000});
}
