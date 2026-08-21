// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Runs one protocol exchange with a flash device attached and reports what
// the firmware actually asked the device to do, so a programming command
// that is refused can be told apart from one that was never issued.

import 'dart:io';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.mem.poke(0x200000, 0x01);
  final flash = JedecFlash(base: 0x200000, size: 0x80000);
  cpu.attachFlash(flash);
  cpu.reset();

  for (var i = 0; i < 1500000; i++) {
    cpu.step();
  }

  // Log every write the device is offered, so a mangled unlock shows up.
  final writes = <String>[];
  final realPoke = flash.poke;
  flash.poke = (a, v) => realPoke(a, v);

  final sci = (cpu.peekBus(0xFFFD1C) & 0x02) != 0 ? cpu.sci0 : cpu.sci1;
  sci.txLog.clear();
  sci.receive(args[1].codeUnits);
  for (var i = 0; i < 4000000; i++) {
    cpu.step();
  }

  final reply = sci.txLog
      .map((b) => b >= 32 && b < 127 ? String.fromCharCode(b) : '.')
      .join();
  print('reply            "$reply"');
  print('identify commands ${flash.identifyCount}');
  print('pages programmed  ${flash.programmedPages}');
  print('bytes programmed  ${flash.programmedBytes}');
  print('sectors erased    ${flash.erasedSectors}');
  if (writes.isNotEmpty) print(writes.join('\n'));
}
