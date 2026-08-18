// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Finds the decision that selects a boot mode, without knowing what it reads.
//
// For each conditional branch that executes exactly once while booting, the
// condition is inverted and the machine is booted again. If a branch is the
// one that picks between the normal screen and another, inverting it puts
// the machine into the other mode — so the screen changes. That names the
// decision; what it tests can then be read out of the disassembly.
//
// Inverting is a one-byte patch: the H8's Bcc conditions are laid out in
// true/false pairs, so XOR 1 on the condition nibble swaps each for its
// opposite.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/h8disasm.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');

const int frameBuffer = 0x040000;
const int frameBytes = 0x4B00;

({int hash, int ink}) bootWithPatch(
    List<int> image, int steps, int? patchAt) {
  final cpu = H8Cpu();
  loadRawBinary(image, 0, cpu.mem.poke);
  if (patchAt != null) {
    final b0 = cpu.mem.peek(patchAt);
    if (b0 >= 0x41 && b0 <= 0x4F) {
      cpu.mem.poke(patchAt, b0 ^ 1);
    } else if (b0 == 0x58) {
      cpu.mem.poke(patchAt + 1, cpu.mem.peek(patchAt + 1) ^ 0x10);
    }
  }
  cpu.reset();
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
  return (hash: hash, ink: ink);
}

void main(List<String> args) {
  final image = File(args.first).readAsBytesSync();
  final steps = int.parse(args.length > 2 ? args[2] : '25000000');
  final candidates = File(args[1])
      .readAsLinesSync()
      .where((l) => l.trim().isNotEmpty)
      .map((l) => int.parse(l.trim(), radix: 16))
      .toList();

  final base = bootWithPatch(image, steps, null);
  print('idle boot: ${base.hash.toRadixString(16).toUpperCase()}, '
      '${base.ink} non-blank bytes');
  print('flipping ${candidates.length} once-only branches\n');

  final cpu = H8Cpu();
  loadRawBinary(image, 0, cpu.mem.poke);

  var hits = 0;
  for (var i = 0; i < candidates.length; i++) {
    final at = candidates[i];
    final r = bootWithPatch(image, steps, at);
    final changed = r.hash != base.hash;
    if (changed) {
      hits++;
      final d = disassembleH8(cpu.mem.peek, at);
      print("HIT  H'${hex6(at)}  ${d.text.padRight(22)} -> "
          '${r.hash.toRadixString(16).toUpperCase()}, ${r.ink} bytes');
    }
    if ((i + 1) % 20 == 0) {
      print('  ... ${i + 1}/${candidates.length} done, $hits so far');
    }
  }
  print('\n$hits branches changed the screen');
}
