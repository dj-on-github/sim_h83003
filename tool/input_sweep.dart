// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Boots the machine once per single-input change and reports which ones
// change what ends up on the screen.
//
// A mode entered by holding a button at power-on must be decided from
// something readable at reset. This sweeps every candidate one at a time —
// each bit of the external input latch at H'080000, and every port pin —
// and compares the frame buffer against an untouched boot.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

const int digitalInputs = 0x080000;
const int frameBuffer = 0x040000;
const int frameBytes = 0x4B00;
const int inputsIdle = 0x70;

typedef Result = ({int hash, int ink, int mode});

Result boot(
  List<int> image,
  int steps, {
  int inputsByte = inputsIdle,
  int? pinDr,
  int pinBit = 0,
  bool pinHigh = false,
  int? adcChannel,
  int adcValue = 0,
  int? pokeAddr,
  int pokeValue = 0,
}) {
  final cpu = H8Cpu();
  loadRawBinary(image, 0, cpu.mem.poke);
  for (var a = digitalInputs; a < digitalInputs + 0x100; a++) {
    cpu.mem.poke(a, inputsByte);
  }
  if (pokeAddr != null) cpu.mem.poke(pokeAddr, pokeValue);
  cpu.reset();
  if (pinDr != null) cpu.setPin(pinDr, pinBit, pinHigh);
  if (adcChannel != null) {
    // Both pins that feed a result register, so it does not alternate with
    // the other half of the multiplexer.
    cpu.adc.setInput8(adcChannel, adcValue);
    cpu.adc.setInput8(adcChannel ^ 4, adcValue);
  }

  for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
    cpu.step();
  }
  var hash = 0x811C9DC5;
  var ink = 0;
  for (var i = 0; i < frameBytes; i++) {
    final b = cpu.mem.peek(frameBuffer + i);
    if (b != 0) ink++;
    hash = ((hash ^ b) * 0x01000193) & 0xFFFFFFFF;
  }
  return (hash: hash, ink: ink, mode: cpu.peekBus(0xFFFEC0));
}

void main(List<String> args) {
  final steps = int.parse(args.length > 1 ? args[1] : '25000000');
  final only = args.length > 2 ? args[2] : 'all';
  final image = File(args.first).readAsBytesSync();

  final base = boot(image, steps);
  print('idle boot: screen hash '
      '${base.hash.toRadixString(16).toUpperCase()}, '
      '${base.ink} non-blank bytes\n');

  void report(String what, Result r) {
    final same = r.hash == base.hash;
    print('  ${what.padRight(28)} '
        '${r.hash.toRadixString(16).toUpperCase().padLeft(8, '0')}  '
        '${r.ink.toString().padLeft(6)}  '
        "mode H'${hex2(r.mode)}  "
        '${same ? '' : '<<< DIFFERENT SCREEN'}');
  }

  if (only == 'all' || only == 'latch') {
    print("=== each bit of the input latch at H'080000 (idle H'70) ===");
    for (var bit = 0; bit < 8; bit++) {
      final flipped = inputsIdle ^ (1 << bit);
      report("H'080000 bit $bit -> ${(flipped >> bit) & 1}",
          boot(image, steps, inputsByte: flipped));
    }
    print('');
  }

  if (only == 'all' || only == 'adc') {
    print('=== every analog input, held at three levels ===');
    for (var ch = 0; ch < 8; ch++) {
      for (final v in [0x40, 0x80, 0xFF]) {
        report("AN$ch -> H'${hex2(v)}",
            boot(image, steps, adcChannel: ch, adcValue: v));
      }
    }
    print('');
  }

  if (only == 'config') {
    // The configuration block in the data flash at H'57xxxx, which carries
    // the firmware version string and an H'A5 validity marker.
    print("=== configuration bytes H'57FF80-H'57FF9F ===");
    for (var a = 0x57FF80; a <= 0x57FF9F; a++) {
      for (final v in [0x00, 0xFF]) {
        report("H'${hex6(a)} -> H'${hex2(v)}",
            boot(image, steps, pokeAddr: a, pokeValue: v));
      }
    }
    print('');
  }

  if (only == 'all' || only == 'pins') {
    print('=== every port pin, held low then high ===');
    for (final p in H8Cpu.ports) {
      for (var bit = 0; bit < 8; bit++) {
        if ((p.pinMask >> bit) & 1 == 0) continue;
        for (final high in [false, true]) {
          report(
              'P${p.name} bit $bit -> ${high ? 1 : 0}',
              boot(image, steps,
                  pinDr: p.drAddr, pinBit: bit, pinHigh: high));
        }
      }
    }
  }
}
