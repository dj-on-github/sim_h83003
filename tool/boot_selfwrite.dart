// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// What happens when the boot ROM is told to program its own device?
//
// The 'M' command and the 'P' downloader both take an address from the host
// and neither checks it against the region the code is running in. Aimed at
// the application device they are the ordinary, proven path. Aimed at bank 0
// they issue a page-program to the device the CPU is fetching instructions
// from, and an AT29C-style part answers reads with toggling status for the
// length of the write rather than with data.
//
// The simulated device models that -- JedecFlash.read() returns status while
// busyReads is counting down -- so the failure reproduces here rather than
// having to be argued for. busyReads is 0 by default, which makes programming
// look instantaneous; a real part is busy for milliseconds, which is hundreds
// of instruction fetches.
//
//   dart run tool/boot_selfwrite.dart <image.bin> --target 200000|000000
//     [--busy N]

import 'dart:io';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int stateByte = 0xFFFD1E;

void main(List<String> args) {
  String? opt(String n) =>
      args.contains(n) ? args[args.indexOf(n) + 1] : null;

  final target = int.parse(opt('--target') ?? '200000', radix: 16);
  final busy = int.parse(opt('--busy') ?? '400');

  final cpu = H8Cpu();
  loadRawBinary(File(args.first).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.mem.poke(0x200000, 0x01); // keep the boot ROM in charge
  final devices = <JedecFlash>[];
  for (final r in artista180Flash) {
    final d = JedecFlash(base: r.base, size: r.size)..busyReads = busy;
    cpu.attachFlash(d);
    devices.add(d);
  }
  cpu.reset();

  final ch = cpu.sci1;
  void run(int n) {
    for (var i = 0; i < n && !(cpu.halted && !cpu.sleeping); i++) {
      cpu.step();
    }
  }

  // Settle into the service loop, then drive 'M' the way the burner does.
  // Every character needs its own pass through the service routine, and the
  // routine is only reached once per iteration of the boot loop, so each one
  // gets a generous budget rather than a guessed one.
  // The handshake offers the link for 500 rounds before giving up, so
  // reaching the download loop costs millions of instructions, not thousands.
  run(12000000);
  print('  reached the loop  : after the handshake timed out, '
      "status ${String.fromCharCodes(ch.txLog)}");
  final before = List<int>.generate(0x2300, (i) => cpu.peekBus(i));

  final addr = target.toRadixString(16).toUpperCase().padLeft(6, '0');
  final trace = <String>[];
  void feed(String text) {
    for (final c in text.codeUnits) {
      ch.receive([c]);
      run(600000);
      trace.add('${String.fromCharCode(c)}->'
          'H${cpu.peekBus(stateByte).toRadixString(16).toUpperCase()}');
    }
  }

  feed('M$addr');
  // Four bytes of payload, then a non-hex character to finish the page.
  feed('A5A5A5A5');
  print('  states            : ${trace.join(' ')}');
  ch.receive('.'.codeUnits);

  // Watch where execution goes while the program is in flight.
  final seen = <int>{};
  var left = 0;
  for (var i = 0; i < 3000000 && !(cpu.halted && !cpu.sleeping); i++) {
    cpu.step();
    seen.add(cpu.pc);
    if (cpu.pc >= 0x2300) left++;
  }

  final after = List<int>.generate(0x2300, (i) => cpu.peekBus(i));
  final clobbered = [
    for (var i = 0; i < before.length; i++)
      if (before[i] != after[i]) i
  ];

  print('  target            : H$addr');
  print('  busy reads/write  : $busy');
  print('  programmed pages  : boot=${devices[0].programmedPages}  '
      'app=${devices[1].programmedPages}');
  print('  CPU halted        : ${cpu.halted}');
  print('  PC now            : H${cpu.pc.toRadixString(16).toUpperCase()}');
  print('  distinct PCs seen : ${seen.length}');
  print('  fetches outside the ROM  : $left');
  print('  boot ROM bytes changed  : ${clobbered.length}'
      '${clobbered.isEmpty ? '' : '  (first at H'
          '${clobbered.first.toRadixString(16).toUpperCase()})'}');
  print('  protocol state    : H'
      '${cpu.peekBus(stateByte).toRadixString(16).toUpperCase()}');
}
