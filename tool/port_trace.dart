// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Records what a routine puts on an I/O port, by calling it and sampling the
// port after every instruction.
//
// A bit-banged bus is defined by its waveform, not by its code, so this is
// how a rebuilt driver is checked against the original: call each, record the
// sequence of pin states, and compare. The two are compiled differently and
// take their arguments differently, so nothing else about them will match --
// but what appears on the wire has to.
//
//   dart run tool/port_trace.dart <image.bin> <funcHex> <portHex> <ddrHex>
//        [--boot N] [--r6l N] [--r0l N] [--r1l N] [--pushw N]
//        [--poke ADDR:VALUE ...]
//
// --poke forces memory or a register to a known value after the boot and
// before the call, so two images can be compared from the same starting
// state rather than from wherever each happened to be.
//
// All numbers hex.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int sentinel = 0x00FFF800;
const int scratchSp = 0x00FFF700;

int hex(String s) => int.parse(s, radix: 16);

void main(List<String> args) {
  String? opt(String name) {
    final i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : null;
  }

  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  // Let the machine come up far enough that the ports and the DDR shadow
  // hold whatever the routine expects to find.
  final boot = int.parse(opt('--boot') ?? '0');
  for (var i = 0; i < boot; i++) {
    cpu.step();
  }

  // Force any starting state the routine depends on, so both images begin
  // the same way.
  for (var i = 0; i < args.length; i++) {
    if (args[i] != '--poke' || i + 1 >= args.length) continue;
    final p = args[i + 1].split(':');
    cpu.writeB(hex(p[0]), hex(p[1]));
  }

  final func = hex(args[1]);
  final port = hex(args[2]);
  final ddr = hex(args[3]);

  void poke32(int a, int v) {
    for (var i = 0; i < 4; i++) {
      cpu.mem.poke(a + i, (v >> (24 - 8 * i)) & 0xFF);
    }
  }

  var sp = scratchSp;
  final pushw = opt('--pushw');
  if (pushw != null) {
    // A word argument the original's convention passes on the stack, below
    // the return address.
    cpu.mem.poke(sp + 4, (hex(pushw) >> 8) & 0xFF);
    cpu.mem.poke(sp + 5, hex(pushw) & 0xFF);
  }
  poke32(sp, sentinel);
  cpu.er[7] = sp;

  if (opt('--r6l') != null) {
    cpu.er[6] = (cpu.er[6] & ~0xFF) | hex(opt('--r6l')!);
  }
  if (opt('--r0l') != null) {
    cpu.er[0] = (cpu.er[0] & ~0xFF) | hex(opt('--r0l')!);
  }
  if (opt('--r1l') != null) {
    cpu.er[1] = (cpu.er[1] & ~0xFF) | hex(opt('--r1l')!);
  }
  cpu.pc = func;

  final trace = <String>[];
  var lastPort = -1, lastDdr = -1, steps = 0;
  const limit = 4000000;
  while (cpu.pc != sentinel && steps < limit) {
    cpu.step();
    steps++;
    final p = cpu.peekBus(port);
    final d = cpu.peekBus(ddr);
    if (p != lastPort || d != lastDdr) {
      lastPort = p;
      lastDdr = d;
      trace.add('${p.toRadixString(16).toUpperCase().padLeft(2, '0')}/'
          '${d.toRadixString(16).toUpperCase().padLeft(2, '0')}');
    }
  }

  if (steps >= limit) {
    print('did not return within $limit steps');
    exit(1);
  }
  print('steps: $steps  transitions: ${trace.length}  '
      "r6l=H'${(cpu.er[6] & 0xFF).toRadixString(16).toUpperCase()} "
      "r0l=H'${(cpu.er[0] & 0xFF).toRadixString(16).toUpperCase()}");
  print(trace.join(' '));
}
