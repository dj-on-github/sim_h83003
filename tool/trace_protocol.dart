// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Feeds a byte stream to a boot ROM in download mode and logs every change
// of the protocol state byte alongside the bytes going in and out, so a
// command that goes astray can be located exactly.
//
// Usage:
//   dart run tool/trace_protocol.dart <image.bin> "<text>" [--settle N]

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String ch(int b) => b >= 32 && b < 127 ? String.fromCharCode(b) : '.';

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.mem.poke(0x200000, 0x01);
  cpu.reset();
  for (var i = 0; i < 1500000; i++) {
    cpu.step();
  }

  final sci = (cpu.peekBus(0xFFFD1C) & 0x02) != 0 ? cpu.sci0 : cpu.sci1;
  sci.txLog.clear();
  sci.receive(args[1].codeUnits);

  final settle = args.length > 3 && args[2] == '--settle'
      ? int.parse(args[3])
      : 4000000;

  var lastState = -1, lastTx = 0, lastQueued = -1;
  var prevRdrf = false, prevPc = 0;
  for (var i = 0; i < settle; i++) {
    prevPc = cpu.pc;
    cpu.step();
    final rdrf = (sci.ssr & 0x40) != 0;
    if (prevRdrf && !rdrf) {
      print("      RDR read at pc=H'"
          "${prevPc.toRadixString(16).toUpperCase().padLeft(6, '0')}");
    }
    prevRdrf = rdrf;
    final st = cpu.peekBus(0xFFFD1E);
    final q = sci.rxQueue.length;
    if (st != lastState || sci.txLog.length != lastTx || q != lastQueued) {
      final out = sci.txLog.skip(lastTx).map(ch).join();
      print("state H'${st.toRadixString(16).toUpperCase().padLeft(2, '0')}"
          "  queued=$q  count=${cpu.peekBus(0xFFFD1F)}"
          "  pc=H'${cpu.pc.toRadixString(16).toUpperCase().padLeft(6, '0')}"
          "  out='$out'");
      lastState = st;
      lastTx = sci.txLog.length;
      lastQueued = q;
    }
  }
}
