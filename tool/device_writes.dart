// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Every address the firmware writes that is not RAM, flash or an on-chip
// register — i.e. every candidate device window, with the instruction that
// wrote it.
//
// A display controller's register file shows up here as a short burst of
// writes to a handful of addresses during initialisation.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/h8disasm.dart';
import 'package:sim_h83003/hex_files.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// Regions established by the memory map, which are not devices.
bool known(int a) {
  if (a >= 0xFFF710) return true; // on-chip RAM and registers
  if (a >= 0x114000 && a <= 0x11FFFF) return true; // external RAM
  if (a >= 0x200000 && a <= 0x3FFFFF) return true; // application flash
  if (a >= 0x500000 && a <= 0x5FFFFF) return true; // data flash
  // The frame buffer proper. A display controller's register window sits on
  // a different chip select, so it must NOT be excluded with it — that is
  // what hid it the first time this was looked for.
  if (a >= 0x040000 && a < 0x040000 + 0x4B00) return true;
  return false;
}

class Probe extends H8Cpu {
  final Map<int, int> counts = {};
  final Map<int, Set<int>> writers = {};
  final Map<int, Set<int>> values = {};
  final Map<int, int> firstAt = {};
  int executed = 0;
  int instrPc = 0;

  @override
  void writeB(int addr, int value) {
    final a = addr & 0xFFFFFF;
    if (!known(a)) {
      counts[a] = (counts[a] ?? 0) + 1;
      (writers[a] ??= {}).add(instrPc);
      (values[a] ??= {}).add(value & 0xFF);
      firstAt.putIfAbsent(a, () => executed);
    }
    super.writeB(addr, value);
  }
}

void main(List<String> args) {
  final steps = int.parse(args.length > 1 ? args[1] : '6000000');
  final cpu = Probe();
  loadRawBinary(File(args.first).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();
  for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
    cpu.instrPc = cpu.pc;
    cpu.step();
    cpu.executed++;
  }

  final addrs = cpu.counts.keys.toList()
    ..sort((a, b) => cpu.firstAt[a]!.compareTo(cpu.firstAt[b]!));
  print('$steps instructions from reset\n');
  print('writes to addresses outside RAM, flash and the on-chip registers,');
  print('in the order they were first written:\n');
  print('   first seen   address    writes  written by            values');
  for (final a in addrs) {
    final w = (cpu.writers[a]!.toList()..sort())
        .take(3)
        .map((p) => "H'${hex6(p)}")
        .join(' ');
    final v = (cpu.values[a]!.toList()..sort());
    final vs = v.length > 6
        ? '${v.take(6).map(hex2).join(",")},… (${v.length})'
        : v.map(hex2).join(',');
    print('  ${cpu.firstAt[a].toString().padLeft(10)}  '
        "H'${hex6(a)}  ${cpu.counts[a].toString().padLeft(7)}  "
        '${w.padRight(22)} $vs');
  }
  print('\n${addrs.length} distinct addresses');
  if (addrs.isNotEmpty) {
    final lo = addrs.reduce((x, y) => x < y ? x : y);
    final hi = addrs.reduce((x, y) => x > y ? x : y);
    print("spanning H'${hex6(lo)} to H'${hex6(hi)}");
    final d = disassembleH8(cpu.mem.peek, cpu.writers[addrs.first]!.first);
    print('first writer: ${d.text}');
  }
}
