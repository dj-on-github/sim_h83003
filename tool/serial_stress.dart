// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Opens, reconfigures and closes a host port over and over.
//
// This is the shape that was crashing: a config handed to the port and then
// disposed as well (a double free), and a port freed while the reader
// isolate still held a pointer into it (a use-after-free). Both only show up
// once a real port has actually been opened, so this needs a real device
// node -- any serial device will do, nothing is sent to it.
//
//   dart run tool/serial_stress.dart /dev/cu.usbserial-XXXX [cycles] [gap_ms]
//
// A gap of 0 is the worst case: the close lands before the reader isolate
// has even finished spawning, which is the window in which it used to be
// left running against a port that was about to be freed.

import 'dart:async';

import 'package:sim_h83003/serial_link.dart';

Future<void> main(List<String> args) async {
  if (args.isEmpty) {
    print('usage: serial_stress <port> [cycles]');
    return;
  }
  final cycles = args.length > 1 ? int.parse(args[1]) : 10;
  final gapMs = args.length > 2 ? int.parse(args[2]) : 40;

  for (var i = 0; i < cycles; i++) {
    final link = SerialLink();
    final error = link.open(args[0], 19200);
    if (error != null) {
      print('cycle $i: $error');
      return;
    }
    // Walk the rate the way the download protocol does.
    for (final rate in [9600, 19200, 38400, 19200]) {
      link.setBaud(rate);
    }
    link.send(const [0x41, 0x42, 0x43]);
    link.drain();
    link.close();

    // Closing detaches immediately and frees a moment later; overlap the
    // next open with that teardown, which is the risky case.
    await Future<void>.delayed(Duration(milliseconds: gapMs));
    if (gapMs > 0 || i % 10 == 0) print('cycle $i ok');
  }

  // Outlive the last teardown so a late free still lands inside this run.
  await Future<void>.delayed(const Duration(seconds: 2));
  print('survived $cycles cycles');
}
