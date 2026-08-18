// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Which instruction trips a data breakpoint, and on which address.
//
// Answers a specific question: with breakpoints on the four A/D result
// registers and none on ADCSR, does anything stop on an ADCSR access?

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/h8disasm.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');

class ProbeCpu extends H8Cpu {
  /// Addresses inside the breakpoint set that this instruction touched.
  final List<int> touched = [];
  bool recording = false;

  @override
  int readB(int addr) {
    if (recording && dataBreaks.contains(addr & 0xFFFFFF)) {
      touched.add(addr & 0xFFFFFF);
    }
    return super.readB(addr);
  }

  @override
  void writeB(int addr, int value) {
    if (recording && dataBreaks.contains(addr & 0xFFFFFF)) {
      touched.add(addr & 0xFFFFFF);
    }
    super.writeB(addr, value);
  }
}

void main(List<String> args) {
  final cpu = ProbeCpu();
  loadRawBinary(File(args.first).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  // Warm up without breakpoints so the machine reaches its idle state.
  final warmup = int.parse(args.length > 1 ? args[1] : '40000000');
  for (var i = 0; i < warmup && !(cpu.halted && !cpu.sleeping); i++) {
    cpu.step();
  }

  // Exactly what the user set: the four result registers, not ADCSR.
  for (final a in [0xFFFFE0, 0xFFFFE2, 0xFFFFE4, 0xFFFFE6]) {
    cpu.dataBreaks.add(a);
  }
  cpu.recording = true;
  print('breakpoints: ${cpu.dataBreaks.map((a) => "H'${hex6(a)}").join(', ')}');
  print('');

  var hits = 0;
  for (var i = 0; i < 4000000 && hits < 12; i++) {
    final pcBefore = cpu.pc;
    cpu.breakHit = false;
    cpu.touched.clear();
    cpu.step();
    if (!cpu.breakHit) continue;
    hits++;
    final d = disassembleH8(cpu.mem.peek, pcBefore);
    final after = disassembleH8(cpu.mem.peek, cpu.pc);
    print('hit $hits');
    print("  instruction that tripped it : H'${hex6(pcBefore)}  ${d.text}");
    print('  addresses it touched        : '
        "${cpu.touched.map((a) => "H'${hex6(a)}").join(', ')}");
    print("  PC now reads                : H'${hex6(cpu.pc)}  ${after.text}");
    print('');
  }
}
