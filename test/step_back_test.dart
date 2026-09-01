// Stepping backwards on a real CPU.
//
// The journal is tested on its own in undo_journal_test.dart; this is the
// part that matters in use -- that the machine you step back to is the
// machine you were in, and that going forwards again retraces the same run.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/condition.dart';
import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/keypad.dart';
import 'package:sim_h83003/undo.dart';

H8Cpu running(List<int> program, {bool history = true}) {
  final cpu = H8Cpu();
  for (var i = 0; i < program.length; i++) {
    cpu.mem.poke(0x000100 + i, program[i]);
  }
  for (final (i, b) in [0x00, 0x00, 0x01, 0x00].indexed) {
    cpu.mem.poke(i, b);
  }
  cpu.reset();
  if (history) cpu.undo = UndoJournal();
  return cpu;
}

/// Sums 1..10 and stores the total at H'FFFD10, then sleeps.
const sumTo10 = [
  0x7A, 0x07, 0x00, 0xFF, 0xFF, 0x00, //  MOV.L #H'00FFFF00,ER7
  0xF8, 0x00, //                          MOV.B #0,R0L
  0xF9, 0x01, //                          MOV.B #1,R1L
  0x08, 0x98, //                   loop:  ADD.B R1L,R0L
  0x0A, 0x09, //                          INC.B R1L
  0xA9, 0x0B, //                          CMP.B #H'0B,R1L
  0x46, 0xF8, //                          BNE   loop
  0x6A, 0xA8, 0x00, 0xFF, 0xFD, 0x10, //  MOV.B R0L,@H'FFFD10:24
  0x01, 0x80, //                   done:  SLEEP
  0x40, 0xFC, //                          BRA   done
];

/// Everything a step back has to put back, as one comparable value.
///
/// A record holding a List would compare by identity, and two machines in
/// the same state would never be equal.
String snap(H8Cpu c) {
  String h(int v) => v.toRadixString(16).toUpperCase();
  return 'pc=${h(c.pc)} ccr=${h(c.ccr)} cycles=${c.cycles} '
      'er=[${c.er.map(h).join(',')}] '
      'mem=${h(c.mem.peek(0xFFFD10))} '
      '${c.halted ? 'halted' : 'running'}';
}

void main() {
  group('with no journal', () {
    test('nothing is recorded and nothing can be undone', () {
      final cpu = running(sumTo10, history: false);
      for (var i = 0; i < 50; i++) {
        cpu.step();
      }
      expect(cpu.stepBack(), isFalse);
      expect(cpu.canStepBackExactly, isNull);
    });
  });

  group('one step back', () {
    test('puts the machine back exactly where it was', () {
      final cpu = running(sumTo10);
      for (var i = 0; i < 20; i++) {
        cpu.step();
      }
      final was = snap(cpu);
      cpu.step();
      expect(snap(cpu), isNot(was), reason: 'it did something');

      expect(cpu.stepBack(), isTrue);
      expect(snap(cpu), was);
    });

    test('at the very start there is nothing to go back to', () {
      final cpu = running(sumTo10);
      expect(cpu.stepBack(), isFalse);
    });

    test('undoes the memory the instruction wrote', () {
      final cpu = running(sumTo10);
      cpu.mem.poke(0xFFFD10, 0xEE);
      // Run right through the store.
      for (var i = 0; i < 50 && cpu.mem.peek(0xFFFD10) == 0xEE; i++) {
        cpu.step();
      }
      expect(cpu.mem.peek(0xFFFD10), 0x37, reason: 'the sum of 1..10');

      cpu.stepBack();
      expect(cpu.mem.peek(0xFFFD10), 0xEE,
          reason: 'what was displaced comes back');
    });
  });

  group('many steps back', () {
    test('walk the whole run backwards to where it started', () {
      final cpu = running(sumTo10);
      final start = snap(cpu);
      final along = <String>[];
      for (var i = 0; i < 60; i++) {
        along.add(snap(cpu));
        cpu.step();
      }
      for (var i = 59; i >= 0; i--) {
        expect(cpu.stepBack(), isTrue);
        expect(snap(cpu), along[i], reason: 'at step $i');
      }
      expect(snap(cpu), start);
      expect(cpu.stepBack(), isFalse, reason: 'and no further');
    });

    test('and then forwards again over the same ground', () {
      // The point of a rewind that is exact: what happens next is what
      // happened before, so a run can be replayed rather than re-set-up.
      final cpu = running(sumTo10);
      for (var i = 0; i < 40; i++) {
        cpu.step();
      }
      final firstTime = <int>[];
      for (var i = 0; i < 20; i++) {
        firstTime.add(cpu.pc);
        cpu.step();
      }
      for (var i = 0; i < 20; i++) {
        cpu.stepBack();
      }
      final secondTime = <int>[];
      for (var i = 0; i < 20; i++) {
        secondTime.add(cpu.pc);
        cpu.step();
      }
      expect(secondTime, firstTime);
    });
  });

  group('stepping back out of a halt', () {
    test('an illegal instruction can be stepped back from', () {
      // The reason to have this at all: you are sitting on a fault and want
      // to see what led to it. H'00FF is illegal -- NOP is the only H'00xx
      // there is -- where H'FFFF is a perfectly good MOV.B.
      final cpu = running([0x00, 0xFF]);
      cpu.step();
      expect(cpu.halted, isTrue);
      expect(cpu.haltReason, contains('Illegal'));

      // The halt itself ran no instruction, so there is nothing recorded
      // for it and nothing before it either.
      expect(cpu.stepBack(), isFalse);
    });

    test('the instructions leading up to one are still there', () {
      final cpu = running([...sumTo10.take(10), 0x00, 0xFF]);
      for (var i = 0; i < 20 && !cpu.halted; i++) {
        cpu.step();
      }
      expect(cpu.halted, isTrue);
      final at = cpu.pc;
      expect(cpu.stepBack(), isTrue);
      expect(cpu.halted, isFalse, reason: 'back before the fault');
      expect(cpu.pc, lessThan(at));
      expect(cpu.haltReason, isEmpty);
    });

    test('a sleeping machine steps back to before it slept', () {
      final cpu = running(sumTo10);
      for (var i = 0; i < 60 && !cpu.sleeping; i++) {
        cpu.step();
      }
      expect(cpu.sleeping, isTrue);
      while (cpu.sleeping) {
        expect(cpu.stepBack(), isTrue);
      }
      expect(cpu.halted, isFalse);
    });
  });

  group('the peripherals', () {
    // The part the journal cannot do on its own: a timer counts into its own
    // counter rather than working it out from the clock, so winding the
    // clock back leaves it where it was. Kept states and a replay are what
    // make the rewind true.

    /// Starts timer channel 0, then spins.
    const startsTimer = [
      0xF8, 0x01, //                          MOV.B #1,R0L
      0x6A, 0xA8, 0x00, 0xFF, 0xFF, 0x60, //  MOV.B R0L,@H'FFFF60  TSTR
      0x40, 0xFE, //                   here:  BRA here
    ];

    int tcnt(H8Cpu c) => (c.peekBus(0xFFFF68) << 8) | c.peekBus(0xFFFF69);

    test('a timer that counted is put back to what it had counted', () {
      final cpu = running(startsTimer);
      cpu.step();
      cpu.step();
      for (var i = 0; i < 100; i++) {
        cpu.step();
      }
      final wasCounting = tcnt(cpu);
      expect(wasCounting, greaterThan(0), reason: 'the timer has to be running');

      for (var i = 0; i < 100; i++) {
        cpu.step();
      }
      expect(tcnt(cpu), greaterThan(wasCounting));

      for (var i = 0; i < 100; i++) {
        expect(cpu.stepBack(), isTrue);
      }
      expect(tcnt(cpu), wasCounting,
          reason: 'a rewind that leaves the timer in the future gives a '
              'machine that then takes an interrupt that never happened');
    });

    test('and going forwards again counts the same way', () {
      final cpu = running(startsTimer);
      for (var i = 0; i < 102; i++) {
        cpu.step();
      }
      final firstTime = <int>[];
      for (var i = 0; i < 100; i++) {
        cpu.step();
        firstTime.add(tcnt(cpu));
      }
      for (var i = 0; i < 100; i++) {
        cpu.stepBack();
      }
      final again = <int>[];
      for (var i = 0; i < 100; i++) {
        cpu.step();
        again.add(tcnt(cpu));
      }
      expect(again, firstTime);
    });

    test('with no kept states, a peripheral write says it cannot be exact',
        () {
      final cpu = running(startsTimer);
      cpu.undo!.checkpointEvery = 0;
      cpu.step();
      expect(cpu.canStepBackExactly, isTrue, reason: 'the MOV.B touched nothing');
      cpu.step();
      expect(cpu.canStepBackExactly, isFalse,
          reason: 'the timer kept the byte, and there is no state to go back '
              'to that would put it back');
    });

    test('with them, the same step can be put back exactly', () {
      final cpu = running(startsTimer);
      cpu.step();
      cpu.step();
      expect(cpu.canStepBackExactly, isTrue);
    });

    test('an ordinary write needs no kept state to be exact', () {
      final cpu = running(sumTo10);
      cpu.undo!.checkpointEvery = 0;
      for (var i = 0; i < 50; i++) {
        cpu.step();
      }
      expect(cpu.canStepBackExactly, isTrue);
    });
  });

  group('the journal and the rest of the machine', () {
    test('a reset throws the history away', () {
      // Otherwise a step back crosses the reset into a machine that no
      // longer exists.
      final cpu = running(sumTo10);
      for (var i = 0; i < 30; i++) {
        cpu.step();
      }
      expect(cpu.undo!.isNotEmpty, isTrue);
      cpu.reset();
      expect(cpu.undo!.isEmpty, isTrue);
      expect(cpu.stepBack(), isFalse);
    });

    test('a condition that stopped the run uses no history', () {
      final cpu = running(sumTo10);
      for (var i = 0; i < 10; i++) {
        cpu.step();
      }
      final held = cpu.undo!.length;
      cpu.stopWhen = Condition.parse('1');
      cpu.step();
      expect(cpu.conditionHit, isTrue);
      expect(cpu.undo!.length, held,
          reason: 'no instruction ran, so no record for one');
    });

    test('the history is bounded, whatever the run does', () {
      final cpu = running(sumTo10);
      cpu.undo!.limit = 200;
      for (var i = 0; i < 20000; i++) {
        cpu.step();
      }
      expect(cpu.undo!.length, lessThanOrEqualTo(200));
      expect(cpu.stepBack(), isTrue, reason: 'and still usable');
    });
  });

  group('against the real machine', () {
    final dump = File('bernina_artista180/Bernina180_20260816.bin');
    final have = dump.existsSync();

    H8Cpu artista() {
      final cpu = H8Cpu();
      loadRawBinary(dump.readAsBytesSync(), 0, cpu.mem.poke);
      cpu.attachKeypad(Keypad());
      for (final r in artista180Flash) {
        cpu.attachFlash(JedecFlash.forRegion(r));
      }
      cpu.reset();
      return cpu;
    }

    test('a boot can be wound back a long way and replayed exactly', () {
      final cpu = artista();
      for (var i = 0; i < 2000000; i++) {
        cpu.step();
      }
      cpu.undo = UndoJournal(limit: 100000);
      final forwards = <int>[];
      for (var i = 0; i < 50000; i++) {
        forwards.add(cpu.pc);
        cpu.step();
      }
      final end = snap(cpu);

      for (var i = 0; i < 50000; i++) {
        expect(cpu.stepBack(), isTrue, reason: 'at $i');
      }
      expect(cpu.pc, forwards.first);

      final again = <int>[];
      for (var i = 0; i < 50000; i++) {
        again.add(cpu.pc);
        cpu.step();
      }
      expect(again, forwards,
          reason: 'the same fifty thousand instructions, in the same order');
      expect(snap(cpu), end);
    }, skip: have ? false : 'needs the machine dump');

    test('the firmware writes plenty that comes back', () {
      final cpu = artista();
      for (var i = 0; i < 2000000; i++) {
        cpu.step();
      }
      cpu.undo = UndoJournal(limit: 100000);
      final before = <int, int>{};
      for (var a = 0x110000; a < 0x120000; a++) {
        before[a] = cpu.mem.peek(a);
      }
      for (var i = 0; i < 50000; i++) {
        cpu.step();
      }
      final changed = before.keys.where((a) => cpu.mem.peek(a) != before[a]);
      expect(changed, isNotEmpty,
          reason: 'if nothing changed this proves nothing');

      while (cpu.stepBack()) {}
      for (final a in before.keys) {
        expect(cpu.mem.peek(a), before[a], reason: "at H'${a.toRadixString(16)}");
      }
    }, skip: have ? false : 'needs the machine dump');
  });
}
