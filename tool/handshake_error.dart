// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Does a latched receive error stop the boot ROM hearing the host?
//
// V3's handshake calls serial_clear_rx_errors() on every round in which
// nothing is waiting; V2's has no such call. ORER, once set, holds RDRF down,
// so the question is whether a ROM can talk its way out of an error that was
// latched before the host said anything -- which is the ordinary state of a
// serial line at power-up, when the machine comes out of reset into whatever
// the host's transmitter was doing.
//
// The error is provoked rather than poked: bytes are delivered back to back
// while the ROM is still in bus_init, so the second overruns the first.
//
//   dart run tool/handshake_error.dart <image.bin> [--clean]

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci.dart';

void main(List<String> args) {
  final clean = args.contains('--clean');
  final cpu = H8Cpu();
  loadRawBinary(File(args.first).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.mem.poke(0x200000, 0x01); // keep the boot ROM in charge
  cpu.reset();

  final ch = cpu.sci1;
  var announced = -1, gotError = -1, answered = -1, sent = false;

  for (var i = 0; i < 40000000; i++) {
    cpu.step();

    // The announcement tells us the handshake window is open.
    if (announced < 0 &&
        ch.txLog.length >= 3 &&
        String.fromCharCodes(ch.txLog.sublist(0, 3)) == 'BOS') {
      announced = i;
    }

    // The glitch: ORER latched once the announcement is out and the ROM has
    // settled into its handshake loop. The simulated SCI does not raise
    // overruns of its own -- it holds the byte back instead -- so the flag is
    // set the way a real line would set it, and syncToMemory() makes the
    // firmware's view of SSR1 agree.
    if (!clean && !sent && announced > 0 && i == announced + 50000) {
      ch.ssr |= SciStatus.orer;
      ch.syncToMemory();
      sent = true;
    }

    if (gotError < 0 && (ch.ssr & (SciStatus.orer | SciStatus.fer)) != 0) {
      gotError = i;
    }

    // Once the window is open, say "EB" as a host would.
    if (announced > 0 && i == announced + 200000) {
      ch.txLog.clear();
      ch.receive([0x45, 0x42]);
    }
    if (announced > 0 && i > announced + 200000 && answered < 0 &&
        ch.txLog.isNotEmpty) {
      answered = i;
    }
  }

  String at(int v) => v < 0 ? 'never' : 'step $v';
  print('  line error latched : ${at(gotError)}'
      '${gotError < 0 ? '' : '   (SSR1 = H${ch.ssr.toRadixString(16).toUpperCase()})'}');
  print('  announced "BOS"    : ${at(announced)}');
  print('  answered the host  : ${at(answered)}'
      '${answered < 0 ? '' : '   ${String.fromCharCodes(ch.txLog.map((b) => b >= 32 && b < 127 ? b : 46))}'}');
}
