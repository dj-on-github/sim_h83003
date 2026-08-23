// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Runs the machine with the Embroidery Relay TCP listener attached to one of
// its SCI channels, so EmbroideryCommunicator (or any client speaking the
// EMB-Serial TCP protocol) can drive the simulation.
//
// This is the same plumbing the SCI tab's Network Connection section drives,
// without the window — which is also what makes it testable.
//
//   dart run tool/emb_relay.dart <image.bin> [--port N] [--channel 0|1]
//        [--running] [--seconds N]

import 'dart:async';
import 'dart:io';

import 'package:sim_h83003/emb_relay.dart';
import 'package:sim_h83003/emb_server.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci_bridge.dart';

void main(List<String> args) async {
  String? opt(String name) {
    final i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : null;
  }

  if (args.isEmpty) {
    print('usage: emb_relay <image.bin> [--port N] [--channel 0|1] '
        '[--running] [--seconds N]');
    exit(2);
  }

  final port = int.parse(opt('--port') ?? '8888');
  final channel = int.parse(opt('--channel') ?? '1');
  final seconds = double.parse(opt('--seconds') ?? '60');

  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  if (!args.contains('--running')) cpu.mem.poke(0x200000, 0x01);
  cpu.reset();

  final sci = cpu.sci[channel];
  final link = SciEmbLink(sci);
  final stack = EmbSerialStack(link);
  final relay = EmbRelay(stack, describeLink: () => 'simulated ${sci.name}');
  final server = EmbServer(relay: relay);

  final error = await server.start(port);
  if (error != null) {
    stderr.writeln('emb_relay: $error');
    exit(1);
  }
  print('listening on 127.0.0.1:${server.port}, bridged to ${sci.name}');

  // Boot before announcing readiness, so the first request does not race the
  // machine's own start-up.
  for (var i = 0; i < 1500000; i++) {
    cpu.step();
  }
  print('machine booted');

  // The relay's awaits are satisfied by bytes the CPU transmits, so the CPU
  // has to keep running. This is the command-line stand-in for the UI's frame
  // timer.
  final deadline = Stopwatch()..start();
  Timer.periodic(const Duration(milliseconds: 2), (t) async {
    for (var i = 0; i < 20000; i++) {
      if (cpu.halted && !cpu.sleeping) break;
      cpu.step();
    }
    if (deadline.elapsed.inMilliseconds >= seconds * 1000) {
      t.cancel();
      await server.stop();
      link.dispose();
      await stack.dispose();
      print('handled ${server.requestsHandled} requests');
      exit(0);
    }
  });
}
