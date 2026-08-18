// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Counts the conditional branches taken while booting, so a "flip one branch
// and see what changes" search can be sized before it is run.
//
// A mode chosen by a button held at power-on is decided by a branch that
// executes once. Those are the candidates.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');

/// Bcc opcodes: 0x40-0x4F short form, 0x58 long form. 0x40/0x5800 are BRA,
/// which is unconditional and therefore not a decision.
bool isConditional(int b0, int b1) {
  if (b0 >= 0x41 && b0 <= 0x4F) return true;
  if (b0 == 0x58 && (b1 >> 4) != 0) return true;
  return false;
}

void main(List<String> args) {
  final steps = int.parse(args.length > 1 ? args[1] : '25000000');
  final cpu = H8Cpu();
  loadRawBinary(File(args.first).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  final counts = <int, int>{};
  var firstInk = -1;
  for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
    final pc = cpu.pc;
    final b0 = cpu.mem.peek(pc);
    if (isConditional(b0, cpu.mem.peek(pc + 1))) {
      counts[pc] = (counts[pc] ?? 0) + 1;
    }
    cpu.step();
    if (firstInk < 0 && (i & 0xFFFF) == 0) {
      var ink = 0;
      for (var k = 0; k < 0x4B00; k += 64) {
        if (cpu.mem.peek(0x040000 + k) != 0) ink++;
      }
      if (ink > 20) firstInk = i;
    }
  }

  final once = counts.entries.where((e) => e.value == 1).toList()
    ..sort((a, b) => a.key.compareTo(b.key));
  print('conditional branches executed: ${counts.length} distinct addresses');
  print('  of those, executed exactly once: ${once.length}');
  print('  screen first had content at instruction ~$firstInk');
  final f = File('${args.length > 2 ? args[2] : 'once_branches'}.txt');
  f.writeAsStringSync(once.map((e) => hex6(e.key)).join('\n'));
  print('  wrote the once-only list to ${f.path}');
}
