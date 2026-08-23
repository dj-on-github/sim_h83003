// The Embroidery Relay TCP wire format.

import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/emb_protocol.dart';

void main() {
  group('framing', () {
    test('a message is type, request id, hex length, payload', () {
      final m = EmbProtocol.encodeText(EmbType.read, '0000000A', '210000');
      expect(latin1.decode(m), 'READ0000000A00000006210000');
    });

    test('the length is uppercase hex, not decimal', () {
      final m = EmbProtocol.encode(EmbType.largeData, '00000001',
          List<int>.filled(256, 0));
      expect(latin1.decode(m.sublist(12, 20)), '00000100');
      expect(m.length, 20 + 256);
    });

    test('round trip, including binary payloads', () {
      final payload = Uint8List.fromList([0, 1, 0x0D, 0x0A, 0xFF, 0x80]);
      final wire = EmbProtocol.encode(EmbType.largeData, 'DEADBEEF', payload);
      final back = EmbProtocol.decode(wire)!;
      expect(back.type, EmbType.largeData);
      expect(back.requestId, 'DEADBEEF');
      expect(back.payload, payload);
      expect(back.totalLength, wire.length);
    });

    test('an incomplete message decodes to null rather than throwing', () {
      final wire = EmbProtocol.encodeText(EmbType.read, '00000001', '210000');
      for (var n = 0; n < wire.length; n++) {
        expect(EmbProtocol.decode(wire.sublist(0, n)), isNull, reason: '$n');
      }
      expect(EmbProtocol.decode(wire), isNotNull);
    });

    test('two messages in one buffer are taken one at a time', () {
      final a = EmbProtocol.encodeText(EmbType.read, '00000001', '210000');
      final b = EmbProtocol.encodeText(EmbType.read, '00000002', '210100');
      final buffer = [...a, ...b];

      final first = EmbProtocol.decode(buffer)!;
      expect(first.requestId, '00000001');
      final second = EmbProtocol.decode(buffer.sublist(first.totalLength))!;
      expect(second.requestId, '00000002');
    });

    test('a length that is not hex is rejected', () {
      final bad = latin1.encode('READ00000001ZZZZZZZZ');
      expect(() => EmbProtocol.decode(bad), throwsA(isA<EmbFormatException>()));
    });
  });

  group('payloads', () {
    test('addresses are six hex characters, upper-cased', () {
      expect(EmbProtocol.parseAddress(latin1.encode('2a0100')), '2A0100');
      expect(() => EmbProtocol.parseAddress(latin1.encode('2A01')),
          throwsA(isA<EmbFormatException>()));
      expect(() => EmbProtocol.parseAddress(latin1.encode('ZZ0100')),
          throwsA(isA<EmbFormatException>()));
    });

    test('a write is an address then whole bytes of hex', () {
      final w = EmbProtocol.parseWrite(latin1.encode('0201E14142'));
      expect(w.address, '0201E1');
      expect(w.data, '4142');
      expect(() => EmbProtocol.parseWrite(latin1.encode('0201E1414')),
          throwsA(isA<EmbFormatException>()));
    });

    test('an upload is a four-character page and exactly 256 bytes', () {
      final payload = Uint8List(260)..setRange(0, 4, latin1.encode('028F'));
      final u = EmbProtocol.parseUpload(payload);
      expect(u.address, '028F');
      expect(u.data.length, 256);
      expect(() => EmbProtocol.parseUpload(Uint8List(259)),
          throwsA(isA<EmbFormatException>()));
    });

    test('a checksum is an address and a length', () {
      final c = EmbProtocol.parseChecksum(latin1.encode('240D50000360'));
      expect(c.address, '240D50');
      expect(c.length, '000360');
    });

    test('only the two rates the machine supports are accepted', () {
      expect(EmbProtocol.parseBaud(latin1.encode('19200')), 19200);
      expect(EmbProtocol.parseBaud(latin1.encode('57600')), 57600);
      expect(() => EmbProtocol.parseBaud(latin1.encode('115200')),
          throwsA(isA<EmbFormatException>()));
    });
  });
}
