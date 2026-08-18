// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Every input the firmware consults while booting, and where it consults it.
//
// A mode that is chosen by holding a button at power-on has to be decided
// from something the CPU can read: a port pin, an external port, or an A/D
// channel. This lists those reads in the order they first happen, with the
// instruction that made each one, so the decision points can be found
// without guessing which pin to wiggle.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/h8disasm.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// Port data registers: the only on-chip addresses that can carry a signal
/// from outside the chip.
final Set<int> portDrs = {
  for (final p in H8Cpu.ports) p.drAddr,
};

class ProbeCpu extends H8Cpu {
  /// First time each (pc, addr) pair was read: instruction index and value.
  final Map<int, ({int addr, int firstAt, int count, Set<int> values})> reads =
      {};

  int executed = 0;
  bool logging = false;

  bool _isInput(int addr) {
    if (portDrs.contains(addr)) return true;
    if (adc.owns(addr) && addr <= 0xFFFFE7) return true; // result registers
    // External space outside the RAM and flash the image actually populates.
    if (addr >= 0x060000 && addr < 0x100000) return true;
    // The H'57xxxx window, which the firmware reads but never writes.
    if (addr >= 0x570000 && addr < 0x580000) return true;
    return false;
  }

  @override
  int readB(int addr) {
    final v = super.readB(addr);
    if (logging && _isInput(addr)) {
      final key = (_pcOfInstr << 24) | (addr & 0xFFFFFF);
      final e = reads[key];
      if (e == null) {
        reads[key] = (
          addr: addr,
          firstAt: executed,
          count: 1,
          values: {v},
        );
      } else {
        e.values.add(v);
        reads[key] = (
          addr: e.addr,
          firstAt: e.firstAt,
          count: e.count + 1,
          values: e.values,
        );
      }
    }
    return v;
  }

  int _pcOfInstr = 0;

  /// Runs one instruction, remembering where it started.
  void stepLogged() {
    _pcOfInstr = pc;
    step();
    executed++;
  }
}

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/boot_inputs.dart <image.bin> '
        '[instructions]');
    exit(2);
  }
  final steps = int.parse(args.length > 1 ? args[1] : '8000000');

  final cpu = ProbeCpu();
  loadRawBinary(File(args.first).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();
  cpu.logging = true;

  for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
    cpu.stepLogged();
  }

  print('$steps instructions from reset '
      "(final PC H'${hex6(cpu.pc)})\n");

  final entries = cpu.reads.entries.toList()
    ..sort((a, b) => a.value.firstAt.compareTo(b.value.firstAt));
  print('=== reads of anything that could carry an external signal ===');
  print('   first seen  instruction              address    reads  values');
  for (final e in entries) {
    final at = e.value.firstAt;
    final instrPc = e.key >>> 24;
    final d = disassembleH8(cpu.mem.peek, instrPc);
    final vals = (e.value.values.toList()..sort()).map(hex2).join(',');
    print('  ${at.toString().padLeft(10)}  '
        "H'${hex6(instrPc)} ${d.text.padRight(24)} "
        "H'${hex6(e.value.addr)}  "
        '${e.value.count.toString().padLeft(6)}  $vals');
  }
  print('\n${entries.length} distinct (instruction, address) pairs');
}
