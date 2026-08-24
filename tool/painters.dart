// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Which routines actually put pixels in the frame buffer.
//
// The start-up clears the buffer and the display init programmes the
// controller, but neither draws anything. This watches the frame buffer and
// reports the instructions that write to it, so the drawing primitives can
// be found rather than guessed at.
//
//   dart run tool/painters.dart <image.bin> [steps] [--after N]
//
// --after ignores writes before step N, which is how the start-up clear is
// left out of the count.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int frameBuffer = 0x040000;
const int frameEnd = 0x044B00;

String h6(int v) => "H'${v.toRadixString(16).toUpperCase().padLeft(6, '0')}";

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  final steps = int.parse(args.length > 1 && !args[1].startsWith('--')
      ? args[1]
      : '40000000');
  final afterIndex = args.indexOf('--after');
  final after = afterIndex >= 0 ? int.parse(args[afterIndex + 1]) : 0;

  // Watching every byte would be a huge set; a scattering across the buffer
  // catches any routine that fills or blits a meaningful area.
  for (var a = frameBuffer; a < frameEnd; a += 97) {
    cpu.dataBreaks.add(a);
  }

  final writers = <int, int>{};
  final firstAt = <int, int>{};

  for (var i = 0; i < steps; i++) {
    cpu.clearBreakHit();
    cpu.step();
    if (cpu.breakHit && i >= after) {
      final pc = cpu.breakPc ?? 0;
      writers[pc] = (writers[pc] ?? 0) + 1;
      firstAt.putIfAbsent(pc, () => i);
    }
  }

  final sorted = writers.entries.toList()
    ..sort((x, y) => y.value.compareTo(x.value));
  print('instructions touching the frame buffer after step $after: '
      '${sorted.length}');
  for (final e in sorted.take(20)) {
    print('  ${h6(e.key)}  x${e.value}  first at step ${firstAt[e.key]}');
  }
}
