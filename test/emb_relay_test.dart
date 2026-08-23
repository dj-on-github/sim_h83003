// The relay driving the real firmware.
//
// Each request becomes the machine's character-at-a-time serial exchange, so
// these run the reconstructed ROM in the simulator and check the answers
// against the memory image itself — not against the relay's own idea of what
// it should have said.

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/emb_protocol.dart';
import 'package:sim_h83003/emb_relay.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci_bridge.dart';

/// A machine booted into download mode with the relay attached to SCI1, and
/// a ticker keeping the CPU running so the relay's awaits can be satisfied.
class Harness {
  Harness(List<int> image) {
    loadRawBinary(image, 0, cpu.mem.poke);
    cpu.mem.poke(0x200000, 0x01); // the boot ROM keeps the link
    cpu.reset();
    for (var i = 0; i < 1500000; i++) {
      cpu.step();
    }
    link = SciEmbLink(cpu.sci[1]);
    stack = EmbSerialStack(link, charTimeout: const Duration(seconds: 5),
        slowTimeout: const Duration(seconds: 10));
    relay = EmbRelay(stack, describeLink: () => 'simulated SCI1');
    _ticker = Timer.periodic(const Duration(milliseconds: 1), (_) {
      for (var i = 0; i < 40000; i++) {
        cpu.step();
      }
    });
  }

  final H8Cpu cpu = H8Cpu();
  late final SciEmbLink link;
  late final EmbSerialStack stack;
  late final EmbRelay relay;
  late final Timer _ticker;

  var _id = 0;
  Future<EmbMessage> request(String type, [List<int> payload = const []]) async {
    _id++;
    final wire = await relay.handle(EmbMessage(
        type, _id.toRadixString(16).toUpperCase().padLeft(8, '0'),
        Uint8List.fromList(payload)));
    return EmbProtocol.decode(wire)!;
  }

  Future<void> dispose() async {
    _ticker.cancel();
    link.dispose();
    await stack.dispose();
  }
}

void main() {
  final dump = File('bernina_artista180/Bernina180_20260816.bin');
  if (!dump.existsSync()) {
    test('relay against the firmware', () {
      markTestSkipped('memory dump not present');
    });
    return;
  }
  final image = dump.readAsBytesSync();

  late Harness h;
  setUp(() => h = Harness(image));
  tearDown(() => h.dispose());

  test('READ returns the 32 bytes that are really there', () async {
    final r = await h.request(EmbType.read, 'FFFB00'.codeUnits);
    expect(r.type, EmbType.readData);
    expect(r.payload.length, 64, reason: '32 bytes as hex');
  }, timeout: const Timeout(Duration(minutes: 2)));

  test('READ, LRED and CSUM agree with the image and each other', () async {
    const address = '210000';
    final read = await h.request(EmbType.read, address.codeUnits);
    final large = await h.request(EmbType.largeRead, address.codeUnits);
    final sum = await h.request(
        EmbType.checksum, '${address}000100'.codeUnits);

    expect(read.type, EmbType.readData);
    expect(large.type, EmbType.largeData);
    expect(sum.type, EmbType.sumResponse);
    expect(large.payload.length, 256);

    // Against the image on disk, which is the ground truth.
    final expected = image.sublist(0x210000, 0x210100);
    expect(large.payload, expected);
    expect(read.payloadText,
        expected.take(32).map((b) => b.toRadixString(16).toUpperCase()
            .padLeft(2, '0')).join());
    expect(int.parse(sum.payloadText, radix: 16),
        expected.fold<int>(0, (a, b) => a + b));
  }, timeout: const Timeout(Duration(minutes: 3)));

  test('WRIT changes memory and READ sees it', () async {
    final w = await h.request(EmbType.write, 'FFFB004142'.codeUnits);
    expect(w.type, EmbType.writeAck);
    expect(w.payloadText, 'O');

    final r = await h.request(EmbType.read, 'FFFB00'.codeUnits);
    expect(r.payloadText.substring(0, 4), '4142');
  }, timeout: const Timeout(Duration(minutes: 2)));

  test('UPLD programs a page and LRED reads it back', () async {
    final page = List<int>.generate(256, (i) => (i * 7) & 0xFF);
    final u = await h.request(
        EmbType.upload, [...'FFFB'.codeUnits, ...page]);
    expect(u.type, EmbType.uploadAck);

    final back = await h.request(EmbType.largeRead, 'FFFB00'.codeUnits);
    expect(back.payload, page);
  }, timeout: const Timeout(Duration(minutes: 3)));

  test('RSET leaves the machine able to take the next command', () async {
    final reset = await h.request(EmbType.reset);
    expect(reset.type, EmbType.resetAck);

    final r = await h.request(EmbType.read, 'FFFB00'.codeUnits);
    expect(r.type, EmbType.readData);
  }, timeout: const Timeout(Duration(minutes: 2)));

  test('a bad address is refused without disturbing the machine', () async {
    final bad = await h.request(EmbType.read, 'ZZZZZZ'.codeUnits);
    expect(bad.type, EmbType.error);
    expect(bad.payloadText, contains('1006'));

    final good = await h.request(EmbType.read, 'FFFB00'.codeUnits);
    expect(good.type, EmbType.readData);
  }, timeout: const Timeout(Duration(minutes: 2)));

  test('an unknown message type is reported, not ignored', () async {
    final r = await h.request('XXXX');
    expect(r.type, EmbType.error);
    expect(r.payloadText, contains('1001'));
  }, timeout: const Timeout(Duration(minutes: 2)));

  test('GCFG and STAT describe the link', () async {
    final cfg = await h.request(EmbType.getConfig);
    expect(cfg.type, EmbType.configResponse);
    expect(cfg.payloadText, contains('simulated SCI1'));

    final stat = await h.request(EmbType.getStatus);
    expect(stat.type, EmbType.statusResponse);
    expect(stat.payloadText, contains('"connected":true'));
  }, timeout: const Timeout(Duration(minutes: 2)));
}
