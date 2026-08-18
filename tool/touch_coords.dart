// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Follows a simulated touch all the way to the pixel coordinate the firmware
// computes from it.
//
// H'210FB4 applies the calibration held in the data flash at H'57FFA0:
//
//   H'11B102 = round(H'FFFED9 * 1.325967 + 0.720993)
//   H'11B104 = round(H'FFFEDA * 1.006289 - 0.757862)
//
// which turns a byte into something around 0-320 and 0-240 — screen pixels.
// So driving the A/D and reading H'11B102/H'11B104 says, in the firmware's
// own terms, where it thinks the finger is.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// The simulator's mapping: both axes span H'00-H'F0 across the panel.
int rawFor(double fraction) => (0x00 + fraction * (0xF0 - 0x00)).round();

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args.first).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  final warmup = int.parse(args.length > 1 ? args[1] : '40000000');
  final hold = int.parse(args.length > 2 ? args[2] : '4000000');
  for (var i = 0; i < warmup && !(cpu.halted && !cpu.sleeping); i++) {
    cpu.step();
  }

  int word(int a) => (cpu.peekBus(a) << 8) | cpu.peekBus(a + 1);
  int signedWord(int a) {
    final v = word(a);
    return v >= 0x8000 ? v - 0x10000 : v;
  }

  void press(double fx, double fy) {
    final x = rawFor(fx), y = rawFor(fy);
    // X in ADDRB, Y in ADDRA, both pins of each pair.
    cpu.adc.setInput8(5, x);
    cpu.adc.setInput8(1, x);
    cpu.adc.setInput8(4, y);
    cpu.adc.setInput8(0, y);
    for (var i = 0; i < hold && !(cpu.halted && !cpu.sleeping); i++) {
      cpu.step();
    }
    print('  panel (${(fx * 320).round().toString().padLeft(3)}, '
        '${(fy * 240).round().toString().padLeft(3)})  '
        "raw X H'${hex2(x)} Y H'${hex2(y)}"
        "   H'11A80C=H'${hex2(cpu.peekBus(0x11A80C))} "
        "H'11A80D=H'${hex2(cpu.peekBus(0x11A80D))}"
        "   H'FFFED9=H'${hex2(cpu.peekBus(0xFFFED9))} "
        "H'FFFEDA=H'${hex2(cpu.peekBus(0xFFFEDA))}"
        '   calibrated (${signedWord(0x11B102)}, ${signedWord(0x11B104)})');
  }

  void pressRaw(int x, int y, String label) {
    cpu.adc.setInput8(5, x);
    cpu.adc.setInput8(1, x);
    cpu.adc.setInput8(4, y);
    cpu.adc.setInput8(0, y);
    for (var i = 0; i < hold && !(cpu.halted && !cpu.sleeping); i++) {
      cpu.step();
    }
    print("  $label  raw X H'${hex2(x)} Y H'${hex2(y)}"
        "   H'FFFED9=H'${hex2(cpu.peekBus(0xFFFED9))} "
        "H'FFFEDA=H'${hex2(cpu.peekBus(0xFFFEDA))}"
        '   calibrated (${signedWord(0x11B102)}, ${signedWord(0x11B104)})');
  }

  print('idle:');
  press(-0.0, -0.0);
  print('\npressing across the panel:');
  for (final p in [
    [0.0, 0.0],
    [0.5, 0.0],
    [1.0, 0.0],
    [0.0, 0.5],
    [0.5, 0.5],
    [1.0, 0.5],
    [0.0, 1.0],
    [0.5, 1.0],
    [1.0, 1.0],
  ]) {
    press(p[0], p[1]);
  }

  print('\nlow X readings, to find where the left edge starts registering:');
  for (final x in [0, 1, 2, 3, 4, 6, 8, 0x10]) {
    // Release between presses so each is a fresh contact.
    pressRaw(0, 0, 'release        ');
    pressRaw(x, 0x78, 'X=${x.toString().padLeft(3)}      ');
  }
}
