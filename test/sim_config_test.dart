// ~/.h8simrc: the settings a session needs before it is any use.
//
// The file is meant to be edited by hand as well as written by the app, so
// the parsing has to be forgiving in the ways a hand-written file needs --
// missing fields, addresses written however the user writes them elsewhere --
// and unforgiving about the one thing worth complaining about, which is a
// file that is not the shape it claims to be.

import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/sim_config.dart';

void main() {
  group('addresses', () {
    test('are read in the three ways they get written', () {
      expect(parseAddress('11B10E'), 0x11B10E);
      expect(parseAddress('0x11B10E'), 0x11B10E);
      expect(parseAddress("H'11B10E"), 0x11B10E);
      expect(parseAddress("h'11b10e"), 0x11B10E);
    });

    test('take a plain number too, for anyone generating the file', () {
      expect(parseAddress(0x1234), 0x1234);
    });

    test('come back as hex, which is how the app writes them', () {
      expect(formatAddress(0x11B10E), '11B10E');
      expect(formatAddress(0x77, 2), '77');
    });

    test('a nonsense one is dropped rather than guessed at', () {
      expect(parseAddress('not an address'), isNull);
      expect(parseAddress(''), isNull);
      expect(parseAddress(null), isNull);
    });
  });

  group('an empty file', () {
    test('gives the built-in defaults', () {
      final c = SimConfig.parse('{}');
      expect(c.views.memory, isTrue);
      expect(c.views.disassembly, isTrue);
      expect(c.views.buttons, isFalse);
      expect(c.memory.followPc, isTrue);
      expect(c.memory.address, 0x000100);
      expect(c.sci.channel, 1);
      expect(c.sci.transport, 'serial');
      expect(c.sci.phiHz, 11059200);
      expect(c.profiling, isFalse);
      expect(c.eeprom.file, 'eeprom.json');
      expect(c.appearance.darkMode, isTrue);
      expect(c.heldKeys, isEmpty);
    });

    test('so does a file naming only one thing', () {
      final c = SimConfig.parse('{"profiling": true}');
      expect(c.profiling, isTrue);
      expect(c.memory.followPc, isTrue, reason: 'everything else is default');
      expect(c.sci.tcpPort, 5555);
    });
  });

  group('what the file can say', () {
    const text = '''
{
  "views": { "memory": true, "screen": true, "buttons": true,
             "disassembly": false },
  "memory": { "followPc": false, "address": "11B10E" },
  "sci": { "channel": 0, "transport": "tcp", "tcpPort": 5556,
           "bridge": true, "phiHz": 12000000 },
  "pins": [ { "port": "4", "bit": 5, "level": "low" },
            { "port": "C", "bit": 1, "level": "high" } ],
  "traces": [ { "target": "11B10E", "bits": 16, "bigEndian": true },
              { "target": "key_scan", "bits": 8 } ],
  "profiling": true,
  "flash": { "file": "flash.bin", "load": true, "enable": true },
  "eeprom": { "file": "eeprom.json", "enable": true,
              "verifyFriendly": true },
  "heldKeys": [ "77" ],
  "image": { "file": "dump.bin", "load": true },
  "dataBreakpoints": [ "11B10E", "0x11A6D3" ],
  "instructionBreakpoints": [ "H'208E7A" ],
  "appearance": { "darkMode": false, "fontFamily": "serif", "cpuHz": 1000000 }
}
''';

    test('reads every section', () {
      final c = SimConfig.parse(text);
      expect(c.views.screen, isTrue);
      expect(c.views.disassembly, isFalse);
      expect(c.memory.followPc, isFalse);
      expect(c.memory.address, 0x11B10E);
      expect(c.sci.channel, 0);
      expect(c.sci.useTcp, isTrue);
      expect(c.sci.tcpPort, 5556);
      expect(c.sci.bridge, isTrue);
      expect(c.sci.phiHz, 12000000);
      expect(c.pins.length, 2);
      expect(c.pins[0].port, '4');
      expect(c.pins[0].bit, 5);
      expect(c.pins[0].isHigh, isFalse);
      expect(c.pins[1].isHigh, isTrue);
      expect(c.traces.length, 2);
      expect(c.traces[0].bits, 16);
      expect(c.traces[1].target, 'key_scan');
      expect(c.traces[1].bits, 8);
      expect(c.profiling, isTrue);
      expect(c.flash.file, 'flash.bin');
      expect(c.flash.enable, isTrue);
      expect(c.eeprom.verifyFriendly, isTrue);
      expect(c.heldKeys, [0x77]);
      expect(c.image.file, 'dump.bin');
      expect(c.image.load, isTrue);
      expect(c.dataBreakpoints, [0x11B10E, 0x11A6D3]);
      expect(c.instructionBreakpoints, [0x208E7A]);
      expect(c.appearance.darkMode, isFalse);
      expect(c.appearance.fontFamily, 'serif');
      expect(c.appearance.cpuHz, 1000000);
    });

    test('survives a round trip through the file', () {
      final a = SimConfig.parse(text);
      final b = SimConfig.parse(a.toText());
      expect(jsonDecode(b.toText()), jsonDecode(a.toText()));
      expect(b.memory.address, 0x11B10E);
      expect(b.heldKeys, [0x77]);
      expect(b.dataBreakpoints, [0x11B10E, 0x11A6D3]);
    });

    test('is written indented, to be edited by hand', () {
      final t = const SimConfig().toText();
      expect(t, contains('\n  "views"'));
      expect(t.endsWith('\n'), isTrue);
    });
  });

  group('a file that is wrong', () {
    test('says so rather than doing nothing', () {
      expect(() => SimConfig.parse('not json'), throwsFormatException);
      expect(() => SimConfig.parse('[1, 2, 3]'), throwsFormatException,
          reason: 'the top level has to be an object');
    });

    test('ignores fields it does not know', () {
      final c = SimConfig.parse('{"somethingNew": 42, "profiling": true}');
      expect(c.profiling, isTrue);
    });

    test('drops a breakpoint it cannot read and keeps the rest', () {
      final c = SimConfig.parse(
          '{"dataBreakpoints": ["11B10E", "rubbish", "200000"]}');
      expect(c.dataBreakpoints, [0x11B10E, 0x200000]);
    });

    test('a wrongly typed field falls back rather than throwing', () {
      final c = SimConfig.parse('{"profiling": "yes please"}');
      expect(c.profiling, isFalse, reason: 'not a bool, so the default');
    });
  });

  group('the view list', () {
    test('keeps its order, which is the order of the tabs', () {
      final c = SimConfig.parse('{"views": {"memory": false, "buttons": true}}');
      expect(c.views.asList.first, isFalse);
      expect(c.views.asList.last, isTrue);
      expect(c.views.asList.length, 12);
    });
  });
}
