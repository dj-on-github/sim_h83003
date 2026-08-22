// Joining a simulated SCI channel to a host serial port.
//
// The part that touches real hardware is a thin FFI wrapper and cannot run
// without a port attached; everything above it — the rate the channel is
// programmed to, and the movement of bytes in both directions — is here,
// driven against a loopback stand-in for the port.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/sci.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci_bridge.dart';

/// Stands in for the host port: remembers the rates it was set to and the
/// bytes written, and hands back whatever has been queued for it.
class FakePort {
  final List<int> baudHistory = [];
  final List<int> written = [];
  final List<int> pending = [];
  int baud = 0;

  void setBaud(int b) {
    if (b == baud) return; // a real port ignores a rate it is already at
    baud = b;
    baudHistory.add(b);
  }

  List<int> drain() {
    final out = List<int>.from(pending);
    pending.clear();
    return out;
  }

  void send(List<int> data) => written.addAll(data);
}

/// Runs the channel forward, pumping the bridge the way a frame timer does.
void run(H8Cpu cpu, SciChannel ch, FakePort port, List<int> out, int steps,
    {int phiHz = 11059200}) {
  for (var i = 0; i < steps; i++) {
    cpu.step();
    if (i % 500 == 0) {
      pumpSciBridge(
        channel: ch,
        outgoing: out,
        phiHz: phiHz,
        setBaud: port.setBaud,
        drain: port.drain,
        send: port.send,
      );
    }
  }
  pumpSciBridge(
    channel: ch,
    outgoing: out,
    phiHz: phiHz,
    setBaud: port.setBaud,
    drain: port.drain,
    send: port.send,
  );
}

void main() {
  test('the real download protocol runs over the bridge', () {
    final dump = File('bernina_artista180/Bernina180_20260816.bin');
    if (!dump.existsSync()) {
      markTestSkipped('memory dump not present');
      return;
    }

    final cpu = H8Cpu();
    loadRawBinary(dump.readAsBytesSync(), 0, cpu.mem.poke);
    cpu.mem.poke(0x200000, 0x01); // keep the boot ROM in charge of the link
    cpu.reset();

    final ch = cpu.sci[1];
    final port = FakePort();
    final out = <int>[];
    ch.onTransmit = out.add;

    void pump() => pumpSciBridge(
          channel: ch,
          outgoing: out,
          phiHz: 11059200,
          setBaud: port.setBaud,
          drain: port.drain,
          send: port.send,
        );

    // Boot, then ask who it is. Nothing here touches the SCI directly: the
    // question goes in through the port and the answer comes back out of it.
    for (var i = 0; i < 1500000; i++) {
      cpu.step();
      if (i % 1000 == 0) pump();
    }
    port.written.clear();
    port.pending.addAll('I'.codeUnits);
    for (var i = 0; i < 2000000; i++) {
      cpu.step();
      if (i % 1000 == 0) pump();
    }
    pump();

    expect(String.fromCharCodes(port.written), contains('BERNINA'));
    expect(port.baud, 19200, reason: 'the rate the boot ROM programs');
  });

  group('the rate the channel is programmed to', () {
    test("the boot ROM's default divisor is 19200 baud at 11.0592 MHz", () {
      final ch = SciChannel(name: 'SCI1', base: 0xFFFFB8, vectorBase: 56);
      ch.brr = 0x11; // what sci_init_channel() writes
      ch.smr = 0x00; // async, 8N1, phi/1
      expect(sciBaudAt(ch, 11059200), 19200);
    });

    test('the clock select divides it', () {
      final ch = SciChannel(name: 'SCI1', base: 0xFFFFB8, vectorBase: 56);
      ch.brr = 0x11;
      for (final entry in {0: 19200, 1: 4800, 2: 1200, 3: 300}.entries) {
        ch.smr = entry.key;
        expect(sciBaudAt(ch, 11059200), entry.value,
            reason: 'CKS ${entry.key}');
      }
    });

    test('a nonsensical clock gives no rate rather than a wrong one', () {
      final ch = SciChannel(name: 'SCI1', base: 0xFFFFB8, vectorBase: 56);
      ch.brr = 0x11;
      expect(sciBaudAt(ch, 0), 0);
      expect(sciBaudAt(ch, -1), 0);
    });
  });

  group('moving bytes', () {
    late H8Cpu cpu;
    late SciChannel ch;
    late FakePort port;
    late List<int> out;

    setUp(() {
      cpu = H8Cpu();
      ch = cpu.sci[1];
      port = FakePort();
      out = <int>[];
      ch.onTransmit = out.add;
      ch.brr = 0x11;
      ch.smr = 0x00;
      ch.scr = SciControl.te | SciControl.re;
    });

    test('what the channel transmits reaches the port', () {
      // Load TDR and clear TDRE, which is how the firmware starts a byte.
      ch.tdr = 0x41;
      ch.ssr &= ~SciStatus.tdre;
      run(cpu, ch, port, out, 4000);

      expect(port.written, contains(0x41));
    });

    test('what arrives at the port reaches the receiver', () {
      port.pending.addAll('EB'.codeUnits);

      // Read the bytes out the way the firmware does: when RDRF is set,
      // read RDR and then clear the flag by writing a zero to it.
      final got = <int>[];
      for (var i = 0; i < 60000; i++) {
        cpu.step();
        if (i % 200 == 0) {
          pumpSciBridge(
            channel: ch,
            outgoing: out,
            phiHz: 11059200,
            setBaud: port.setBaud,
            drain: port.drain,
            send: port.send,
          );
        }
        if ((ch.ssr & SciStatus.rdrf) != 0) {
          got.add(cpu.readB(ch.rdrAddr));
          cpu.writeB(ch.ssrAddr, 0xFF & ~SciStatus.rdrf);
        }
      }
      expect(String.fromCharCodes(got), 'EB');
    });

    test('the port is not told a rate it is already at', () {
      run(cpu, ch, port, out, 2000);
      expect(port.baudHistory, [19200]);
    });

    test('a rate change follows the channel', () {
      run(cpu, ch, port, out, 1000);
      expect(port.baud, 19200);

      ch.brr = 0x08; // what 'J08' would program
      run(cpu, ch, port, out, 1000);
      expect(port.baud, sciBaudAt(ch, 11059200));
      expect(port.baudHistory.length, 2);
    });

    test('nothing is sent when the channel has said nothing', () {
      run(cpu, ch, port, out, 2000);
      expect(port.written, isEmpty);
    });
  });
}
