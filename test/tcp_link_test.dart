// The SCI tab's TCP transport.
//
// It exists because libserialport will not open a pty: on macOS sp_list_ports
// walks the IOKit registry, which knows only real serial hardware, and
// sp_open fails on a pty even when nothing holds it -- while a plain POSIX
// open of the same device works. So the limitation is the library's, and no
// amount of typing a path into the tab would have got round it. A socket
// does, and socat joins a pty to the socket.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci_bridge.dart';
import 'package:sim_h83003/tcp_link.dart';

/// Waits for [check] to hold, so nothing races the event loop.
Future<bool> until(bool Function() check,
    {Duration limit = const Duration(seconds: 10)}) async {
  final end = DateTime.now().add(limit);
  while (DateTime.now().isBefore(end)) {
    if (check()) return true;
    await Future<void>.delayed(const Duration(milliseconds: 5));
  }
  return check();
}

void main() {
  group('the link on its own', () {
    test('binds a port and reports it', () async {
      final link = TcpLink();
      expect(await link.open(0, 19200), isNull, reason: '0 asks for any port');
      expect(link.isOpen, isTrue);
      expect(link.port, greaterThan(0));
      expect(link.isConnected, isFalse, reason: 'nothing has connected yet');
      await link.close();
      expect(link.isOpen, isFalse);
    });

    test('refuses a port already taken', () async {
      final first = TcpLink();
      await first.open(0, 19200);
      final second = TcpLink();
      final err = await second.open(first.port, 19200);
      expect(err, isNotNull);
      expect(second.isOpen, isFalse);
      await first.close();
      await second.close();
    });

    test('carries bytes both ways', () async {
      final link = TcpLink();
      await link.open(0, 19200);
      final client = await Socket.connect(
          InternetAddress.loopbackIPv4, link.port);
      final fromLink = <int>[];
      client.listen(fromLink.addAll);
      await until(() => link.isConnected);

      client.add([0x45, 0x42]); // "EB"
      await until(() => link.bytesIn >= 2);
      expect(link.drain(), [0x45, 0x42]);
      expect(link.drain(), isEmpty, reason: 'draining takes what it hands out');

      link.send([0x42, 0x4F, 0x53]); // "BOS"
      await until(() => fromLink.length >= 3);
      expect(fromLink, [0x42, 0x4F, 0x53]);
      expect(link.bytesOut, 3);

      await client.close();
      await link.close();
    });

    test('takes one caller at a time', () async {
      final link = TcpLink();
      await link.open(0, 19200);
      final a = await Socket.connect(InternetAddress.loopbackIPv4, link.port);
      await until(() => link.isConnected);

      final b = await Socket.connect(InternetAddress.loopbackIPv4, link.port);
      final told = <int>[];
      b.listen(told.addAll);
      await until(() => told.isNotEmpty);
      expect(String.fromCharCodes(told), contains('already has a connection'));

      await a.close();
      await b.close();
      await link.close();
    });

    test('a caller going away leaves the link listening', () async {
      final link = TcpLink();
      await link.open(0, 19200);
      final c = await Socket.connect(InternetAddress.loopbackIPv4, link.port);
      await until(() => link.isConnected);
      await c.close();
      await until(() => !link.isConnected);
      expect(link.isOpen, isTrue, reason: 'still listening for the next one');
      await link.close();
    });
  });

  group('with the machine on the other end', () {
    final dump = File('bernina_artista180/Bernina180_20260816.bin');

    test('the boot ROM announces itself and answers the handshake', () async {
      final cpu = H8Cpu();
      loadRawBinary(dump.readAsBytesSync(), 0, cpu.mem.poke);
      cpu.mem.poke(0x200000, 0x01); // no valid application: stay in the ROM
      cpu.reset();

      final link = TcpLink();
      await link.open(0, 19200);
      final client = await Socket.connect(
          InternetAddress.loopbackIPv4, link.port);
      final seen = <int>[];
      client.listen(seen.addAll);
      await until(() => link.isConnected);

      // The SCI tab's own pump, driven by hand.
      final outgoing = <int>[];
      final channel = cpu.sci1;
      channel.onTransmit = outgoing.add;

      var answered = false;
      for (var i = 0; i < 30000000; i++) {
        cpu.step();
        if (i % 20000 != 0) continue;
        pumpSciBridge(
          channel: channel,
          outgoing: outgoing,
          phiHz: 11059200,
          setBaud: link.setBaud,
          drain: link.drain,
          send: link.send,
        );
        await Future<void>.delayed(Duration.zero);
        if (!answered && String.fromCharCodes(seen).contains('BOS')) {
          client.add('EB'.codeUnits); // claim the link
          answered = true;
        }
        if (answered && seen.length > 4) break;
      }

      final text = String.fromCharCodes(seen);
      expect(text, contains('BOS'),
          reason: 'the boot ROM announces itself down the socket');
      expect(answered, isTrue);
      expect(link.bytesOut, greaterThan(0));

      await client.close();
      await link.close();
    }, skip: dump.existsSync() ? false : 'needs the machine dump');
  });
}
