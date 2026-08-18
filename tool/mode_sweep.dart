// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Boots the machine once per combination of the three inputs the mode
// selector at H'20A47C reads, and reports what each one produces.
//
// That routine builds a 3-bit code:  bit 2 <- PB0 (H'FFFFD6 bit 0),
// bit 1 <- H'080000 bit 1, bit 0 <- H'080000 bit 2, and stores it in
// mode_FFFEC0. H'080000 is an external input byte, not a port, so it cannot
// be reached from the IO tab — which is why wiggling port pins alone never
// found it.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

const int pbdr = 0xFFFFD6;
const int digitalInputs = 0x080000;
const int frameBuffer = 0x040000;
const int frameBytes = 0x4B00;

/// Value the dump captured at H'080000, which the page returns everywhere.
const int inputsIdle = 0x70;

({int fbHash, int fbInk, int mode, Set<int> pcs}) boot(
  List<int> image,
  int steps, {
  required bool pb0,
  required bool in1,
  required bool in2,
  required bool tracePcs,
}) {
  final cpu = H8Cpu();
  loadRawBinary(image, 0, cpu.mem.poke);

  var byte = inputsIdle;
  byte = in1 ? (byte | 0x02) : (byte & ~0x02);
  byte = in2 ? (byte | 0x04) : (byte & ~0x04);
  // The page decodes without its low address bits, so every byte in it
  // returns the same value on the real board.
  for (var a = digitalInputs; a < digitalInputs + 0x100; a++) {
    cpu.mem.poke(a, byte);
  }
  cpu.reset();
  if (pb0) cpu.setPin(pbdr, 0, true);

  final pcs = <int>{};
  for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
    if (tracePcs) pcs.add(cpu.pc);
    cpu.step();
  }

  var hash = 0x811C9DC5;
  var ink = 0;
  for (var i = 0; i < frameBytes; i++) {
    final b = cpu.mem.peek(frameBuffer + i);
    if (b != 0) ink++;
    hash = ((hash ^ b) * 0x01000193) & 0xFFFFFFFF;
  }
  return (fbHash: hash, fbInk: ink, mode: cpu.peekBus(0xFFFEC0), pcs: pcs);
}

void main(List<String> args) {
  final steps = int.parse(args.length > 1 ? args[1] : '25000000');
  final image = File(args.first).readAsBytesSync();

  print('booting $steps instructions per combination\n');
  print('  PB0  in1  in2  mode  screen hash  non-blank bytes');
  final results = <String, ({int fbHash, int fbInk, int mode, Set<int> pcs})>{};
  for (var combo = 0; combo < 8; combo++) {
    final pb0 = (combo & 4) != 0;
    final in1 = (combo & 2) != 0;
    final in2 = (combo & 1) != 0;
    final r = boot(image, steps,
        pb0: pb0, in1: in1, in2: in2, tracePcs: combo == 0 || true);
    results['$combo'] = r;
    print('  ${pb0 ? ' 1 ' : ' 0 '}  ${in1 ? ' 1 ' : ' 0 '}'
        '  ${in2 ? ' 1 ' : ' 0 '}  '
        "  H'${hex2(r.mode)}  "
        '${r.fbHash.toRadixString(16).toUpperCase().padLeft(8, '0')}     '
        '${r.fbInk.toString().padLeft(6)}');
  }

  final base = results['0']!;
  print('\ncode reached that the idle boot never reaches:');
  for (var combo = 1; combo < 8; combo++) {
    final r = results['$combo']!;
    final fresh = r.pcs.difference(base.pcs);
    final same = r.fbHash == base.fbHash;
    print('  combo $combo: ${fresh.length} new instruction addresses, '
        'screen ${same ? 'identical to idle' : 'DIFFERENT'}');
    if (fresh.isNotEmpty && fresh.length < 4000) {
      final sorted = fresh.toList()..sort();
      print('    first: '
          '${sorted.take(8).map((a) => "H'${hex6(a)}").join(', ')}');
    }
  }
}
