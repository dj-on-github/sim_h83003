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
  snapshotConfigTests();
  watchConfigTests();
  historyConfigTests();
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
      final c = SimConfig.parse('{"views": {"memory": false, "watch": true}}');
      expect(c.views.asList.first, isFalse);
      expect(c.views.asList.last, isTrue);
      expect(c.views.asList.length, 13);
    });
  });
}

/// A snapshot named in the config, which beats everything else that touches
/// the machine because it already holds all of it.
void snapshotConfigTests() {
  group('a snapshot in the config', () {
    test('defaults to none', () {
      final c = SimConfig.parse('{}');
      expect(c.snapshot.file, isNull);
      expect(c.snapshot.load, isFalse);
    });

    test('is read and written', () {
      final c = SimConfig.parse(
          '{"snapshot": {"file": "booted.h8snap", "load": true}}');
      expect(c.snapshot.file, 'booted.h8snap');
      expect(c.snapshot.load, isTrue);
      final back = SimConfig.parse(c.toText());
      expect(back.snapshot.file, 'booted.h8snap');
      expect(back.snapshot.load, isTrue);
    });
  });
}

/// The stop condition and the watched ranges: things you set before a run
/// rather than during one, which is what this file is for.
void watchConfigTests() {
  group('a watch in the config', () {
    test('defaults to nothing armed and nothing watched', () {
      final c = SimConfig.parse('{}');
      expect(c.watch.condition, isNull);
      expect(c.watch.armed, isFalse);
      expect(c.watch.writes, isEmpty);
    });

    test('reads a condition and the ranges to record', () {
      final c = SimConfig.parse('''
{"watch": {"condition": "[11B10E].w == 77", "armed": true,
           "writes": ["11B10E-11B10F", "H'FFFFC7"]}}''');
      expect(c.watch.condition, '[11B10E].w == 77');
      expect(c.watch.armed, isTrue);
      expect(c.watch.writes, [(0x11B10E, 0x11B10F), (0xFFFFC7, 0xFFFFC7)]);
    });

    test('survives a round trip, ranges and all', () {
      final a = SimConfig.parse('''
{"watch": {"condition": "pc == 208E7A", "armed": true,
           "writes": ["11B10E-11B10F", "200000"]}}''');
      final b = SimConfig.parse(a.toText());
      expect(b.watch.condition, 'pc == 208E7A');
      expect(b.watch.armed, isTrue);
      expect(b.watch.writes, [(0x11B10E, 0x11B10F), (0x200000, 0x200000)]);
      expect(a.toText(), contains('"11B10E-11B10F"'));
      expect(a.toText(), contains('"200000"'),
          reason: 'a single address stays a single address');
    });

    test('a condition that no longer parses is still carried through', () {
      // The app puts it back in the field so it can be seen and fixed.
      // Dropping it here would lose the user's typing with no sign of it.
      final c = SimConfig.parse('{"watch": {"condition": "pc == ", '
          '"armed": true}}');
      expect(c.watch.condition, 'pc == ');
    });

    test('a range it cannot read is dropped and the rest kept', () {
      final c = SimConfig.parse(
          '{"watch": {"writes": ["11B10E", "rubbish", "200000-200010"]}}');
      expect(c.watch.writes, [(0x11B10E, 0x11B10E), (0x200000, 0x200010)]);
    });

    test('a range written backwards is taken the right way round', () {
      final c = SimConfig.parse('{"watch": {"writes": ["200010-200000"]}}');
      expect(c.watch.writes, [(0x200000, 0x200010)]);
    });

    test('a range in H\' form parts at the right dash', () {
      expect(parseRange("H'11B10E-H'11B10F"), (0x11B10E, 0x11B10F));
      expect(parseRange('11B10E'), (0x11B10E, 0x11B10E));
      expect(parseRange('nonsense'), isNull);
    });

    test('comes back written the way the app writes addresses', () {
      expect(formatRange((0x11B10E, 0x11B10E)), '11B10E');
      expect(formatRange((0x11B10E, 0x11B10F)), '11B10E-11B10F');
    });
  });
}

/// Keeping enough of each instruction to step back over it.
void historyConfigTests() {
  group('history in the config', () {
    test('is off by default, because recording costs speed', () {
      final c = SimConfig.parse('{}');
      expect(c.history.enabled, isFalse);
      expect(c.history.steps, 200000);
    });

    test('is read and written', () {
      final c = SimConfig.parse(
          '{"history": {"enabled": true, "steps": 1000000}}');
      expect(c.history.enabled, isTrue);
      expect(c.history.steps, 1000000);
      final back = SimConfig.parse(c.toText());
      expect(back.history.enabled, isTrue);
      expect(back.history.steps, 1000000);
    });

    test('a nonsense depth falls back rather than keeping nothing', () {
      // A zero here would switch the feature on and have it hold no history,
      // which looks like it is broken rather than like it is off.
      expect(SimConfig.parse('{"history": {"steps": 0}}').history.steps, 200000);
      expect(SimConfig.parse('{"history": {"steps": -5}}').history.steps, 200000);
      expect(SimConfig.parse('{"history": {"steps": "lots"}}').history.steps,
          200000);
    });
  });
}
