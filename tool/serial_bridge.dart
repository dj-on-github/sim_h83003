// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Runs the machine with one simulated SCI channel joined to a real serial
// port on this computer, so software that talks to a Bernina can talk to the
// simulation instead. This is the same plumbing the SCI tab drives, without
// the window, which is also what makes it testable.
//
// Usage:
//   dart run tool/serial_bridge.dart <image.bin> <port> [--channel 0|1]
//        [--phi HZ] [--seconds N] [--running]
//
// The native library is found through LIBSERIALPORT_PATH when it is not
// installed system-wide.

import 'dart:async';
import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci_bridge.dart';
import 'package:sim_h83003/serial_link.dart';

void main(List<String> args) {
  String? opt(String name) {
    final i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : null;
  }

  if (args.length < 2) {
    print('usage: serial_bridge <image.bin> <port> [--channel 0|1] '
        '[--phi HZ] [--seconds N] [--running]');
    exit(2);
  }

  final channel = int.parse(opt('--channel') ?? '1');
  final phi = int.parse(opt('--phi') ?? '11059200');
  final seconds = double.parse(opt('--seconds') ?? '10');

  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  // Without --running the application's entry pointer is invalidated so the
  // boot ROM keeps the link and its download protocol answers.
  if (!args.contains('--running')) cpu.mem.poke(0x200000, 0x01);
  cpu.reset();

  final sci = cpu.sci[channel];
  final link = SerialLink();
  final error = link.open(args[1], sciBaudAt(sci, phi));
  if (error != null) {
    stderr.writeln('serial_bridge: $error');
    exit(1);
  }
  print('bridged ${sci.name} to ${args[1]} at ${link.baud} baud');

  final outgoing = <int>[];
  sci.onTransmit = outgoing.add;

  final deadline = Stopwatch()..start();
  var lastBaud = link.baud;

  // The same shape as the UI's frame timer: run a slice of the machine, then
  // move whatever has accumulated in each direction.
  Timer.periodic(const Duration(milliseconds: 5), (t) {
    for (var i = 0; i < 60000; i++) {
      if (cpu.halted && !cpu.sleeping) break;
      cpu.step();
    }

    pumpSciBridge(
      channel: sci,
      outgoing: outgoing,
      phiHz: phi,
      setBaud: (b) {
        if (b != lastBaud) {
          lastBaud = b;
          print('rate now $b baud');
        }
        link.setBaud(b);
      },
      drain: link.drain,
      send: link.send,
    );

    if (deadline.elapsed.inMilliseconds >= seconds * 1000) {
      t.cancel();
      link.close();
      print('in ${link.bytesIn}  out ${link.bytesOut}');
      exit(0);
    }
  });
}
