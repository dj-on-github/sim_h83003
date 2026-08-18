// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Boots the machine with a given input held and writes the resulting screen
// as raw 8-bit grey, 320x240, so it can be looked at rather than hashed.

import 'dart:io';
import 'dart:typed_data';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/lcd.dart';

void main(List<String> args) {
  final image = File(args.first).readAsBytesSync();
  final out = args[1];
  final steps = int.parse(args.length > 2 ? args[2] : '25000000');
  final pinSpec = args.length > 3 ? args[3] : '';
  // Optional: invert one conditional branch, the way branch_flip.dart does,
  // so an alternative boot path it found can be looked at.
  final patch = args.length > 4 && args[4].isNotEmpty
      ? int.parse(args[4], radix: 16)
      : null;
  // Optional ADDR:VALUE poke, for config bytes in the data flash.
  final poke = args.length > 5 && args[5].contains(':') ? args[5] : null;

  final cpu = H8Cpu();
  loadRawBinary(image, 0, cpu.mem.poke);
  if (patch != null) {
    final b0 = cpu.mem.peek(patch);
    if (b0 >= 0x41 && b0 <= 0x4F) {
      cpu.mem.poke(patch, b0 ^ 1);
    } else if (b0 == 0x58) {
      cpu.mem.poke(patch + 1, cpu.mem.peek(patch + 1) ^ 0x10);
    }
    print('inverted the branch at H\'${args[4]}');
  }
  if (poke != null) {
    final p = poke.split(':');
    cpu.mem.poke(int.parse(p[0], radix: 16), int.parse(p[1], radix: 16));
    print("poked H'${p[0]} = H'${p[1]}");
  }
  cpu.reset();
  if (pinSpec.isNotEmpty) {
    final p = pinSpec.split(':');
    cpu.setPin(int.parse(p[0], radix: 16), int.parse(p[1]), p[2] != '0');
    print('held H\'${p[0]} bit ${p[1]} ${p[2] != '0' ? 'high' : 'low'}');
  }
  for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
    cpu.step();
  }

  // Unpack the 2-bit pixels into one grey byte each, positive mode, which is
  // how the Screen tab shows them.
  final grey = Uint8List(LcdFormat.width * LcdFormat.height);
  var i = 0;
  for (var y = 0; y < LcdFormat.height; y++) {
    for (var x = 0; x < LcdFormat.width; x++) {
      final byte = cpu.mem.peek(0x040000 + y * LcdFormat.stride + (x >> 2));
      final level = (byte >> (6 - 2 * (x & 3))) & 3;
      grey[i++] = LcdFormat.levels[3 - level];
    }
  }
  File(out).writeAsBytesSync(grey);
  print('wrote $out (${LcdFormat.width}x${LcdFormat.height} grey)');
}
