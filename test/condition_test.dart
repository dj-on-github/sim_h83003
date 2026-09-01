// The condition language: "stop when this is true".
//
// Numbers are hex throughout, because that is how every address in this
// machine is written everywhere else, and a condition that has to be
// translated on the way in is a condition that gets typed wrong.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/condition.dart';

/// A machine made of a map, so the language can be tested without a CPU.
class FakeMachine implements MachineView {
  FakeMachine({Map<int, int>? memory, Map<String, int>? registers})
      : memory = memory ?? {},
        registers = registers ?? {};

  final Map<int, int> memory;
  final Map<String, int> registers;

  @override
  int readByte(int addr) => memory[addr] ?? 0;

  @override
  int? register(String name) => registers[name];
}

void main() {
  final m = FakeMachine(
    memory: {
      0x11B10E: 0x00, 0x11B10F: 0x77,
      0xFFFFC7: 0x04,
      0x200004: 0x00, 0x200005: 0x3F, 0x200006: 0x00, 0x200007: 0x00,
    },
    registers: {
      'pc': 0x000400,
      'er0': 0x12345678,
      'er6': 0,
      'cycles': 25000000,
      'z': 1,
      'c': 0,
    },
  );

  bool ask(String text) => Condition.parse(text).test(m);
  int value(String text) => Condition.parse(text).value(m);

  group('numbers', () {
    test('are hex without being asked', () {
      expect(value('10'), 0x10);
      expect(value('FF'), 0xFF);
    });

    test('0x is allowed, for anyone who cannot help it', () {
      expect(value('0x20'), 0x20);
    });

    test('a # makes one decimal', () {
      expect(value('#10'), 10);
      expect(ask('cycles > #1000000'), isTrue);
    });

    test('a bare # is refused with a reason', () {
      expect(() => Condition.parse('# '), throwsA(isA<ConditionError>()));
    });
  });

  group('memory', () {
    test('reads a byte by default', () {
      expect(value('[FFFFC7]'), 0x04);
    });

    test('and a word or a long, big-endian as the H8 is', () {
      expect(value('[11B10E].w'), 0x0077);
      expect(value('[200004].l'), 0x003F0000);
    });

    test('the address can be worked out', () {
      expect(value('[11B10E + 1]'), 0x77);
    });

    test('an unknown width says which ones there are', () {
      expect(() => Condition.parse('[10].q'),
          throwsA(predicate((e) => '$e'.contains('.b, .w or .l'))));
    });

    test('an unclosed bracket is reported where it opened', () {
      expect(() => Condition.parse('[11B10E'),
          throwsA(predicate((e) => '$e'.contains('not closed'))));
    });
  });

  group('registers', () {
    test('are read by name', () {
      expect(value('pc'), 0x000400);
      expect(value('er0'), 0x12345678);
    });

    test('a name that is also hex is still a register', () {
      // "e0" is both a legal hex number and a register name.
      expect(() => value('e0'), throwsA(isA<ConditionError>()),
          reason: 'this fake has no e0, which proves it was read as one');
    });

    test('an unknown one says so rather than reading as zero', () {
      expect(() => value('sausage'),
          throwsA(predicate((e) => '$e'.contains('no register'))));
    });
  });

  group('the questions people actually ask', () {
    test('the panel is asking for clr', () {
      expect(ask('[11B10E].w == 77'), isTrue);
      expect(ask('[11B10E].w == 73'), isFalse);
    });

    test('execution has entered a range', () {
      expect(ask('pc >= 400 && pc < 800'), isTrue);
      expect(ask('pc >= 800 && pc < 2400'), isFalse);
    });

    test('a bit is set', () {
      expect(ask('[FFFFC7] & 4'), isTrue, reason: 'the sewing light');
      expect(ask('[FFFFC7] & 8'), isFalse);
    });

    test('two things at once', () {
      expect(ask('er0 != 0 && [200004].l == 3F0000'), isTrue);
    });

    test('either of two things', () {
      expect(ask('pc == 0 || [11B10E].w == 77'), isTrue);
    });
  });

  group('how it reads', () {
    test('a bare value is true when it is not zero', () {
      expect(ask('1'), isTrue);
      expect(ask('0'), isFalse);
      expect(ask('[FFFFC7]'), isTrue);
    });

    test('! turns it round', () {
      expect(ask('![FFFFC7]'), isFalse);
      expect(ask('!0'), isTrue);
    });

    test('brackets group', () {
      expect(value('(1 + 2) & F'), 3);
      expect(value('(FF & F0) >> 4'), 0x0F);
    });

    test('&& short-circuits, so a condition can guard its own reads', () {
      // The right-hand side names a register this machine has not got; if it
      // were evaluated the whole thing would throw.
      expect(ask('0 && sausage'), isFalse);
    });
  });

  group('a condition that is wrong', () {
    test('says where', () {
      try {
        Condition.parse('pc == ');
        fail('should not parse');
      } on ConditionError catch (e) {
        expect(e.message, contains('stops short'));
      }
    });

    test('complains about leftovers rather than ignoring them', () {
      expect(() => Condition.parse('pc == 400 400'),
          throwsA(predicate((e) => '$e'.contains('left over'))));
    });

    test('rejects a character that means nothing here', () {
      expect(() => Condition.parse(r'pc $ 4'),
          throwsA(predicate((e) => '$e'.contains('does not belong'))));
    });

    test('keeps what was typed, to show back', () {
      expect(Condition.parse('  pc == 400  ').source, 'pc == 400');
    });
  });
}
