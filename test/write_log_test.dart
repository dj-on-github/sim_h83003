// Two things that answer the questions that actually come up while
// debugging this machine: "stop when X" and "who wrote that".
//
// The breakpoint that already existed answers "stop here". Neither of those
// is the same question, and both used to mean writing another program in
// tool/ with the test compiled into it.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/condition.dart';
import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/keypad.dart';

/// A machine running a loop that stores an incrementing byte at H'FFF800.
H8Cpu counter() {
  final cpu = H8Cpu();
  const program = [
    0x79, 0x00, 0x00, 0x00, //  MOV.W #0,R0
    0x0B, 0x00, //              ADDS #1,ER0  (bumps R0L in effect)
    0x6A, 0xA8, 0x00, 0xFF, 0xF8, 0x00, // MOV.B R0L,@H'FFF800:24
    0x40, 0xF6, //              BRA back to the ADDS
  ];
  for (var i = 0; i < program.length; i++) {
    cpu.mem.poke(0x000100 + i, program[i]);
  }
  for (final (i, b) in [0x00, 0x00, 0x01, 0x00].indexed) {
    cpu.mem.poke(i, b);
  }
  cpu.reset();
  return cpu;
}

void main() {
  group('the write log', () {
    test('records nothing while nothing is watched', () {
      final cpu = counter();
      for (var i = 0; i < 200; i++) {
        cpu.step();
      }
      expect(cpu.writeLog, isEmpty);
    });

    test('records writes to a watched address', () {
      final cpu = counter();
      cpu.writeWatches.add((0xFFF800, 0xFFF800));
      for (var i = 0; i < 200; i++) {
        cpu.step();
      }
      expect(cpu.writeLog, isNotEmpty);
      expect(cpu.writeLog.every((w) => w.addr == 0xFFF800), isTrue);
    });

    test('says which instruction did it', () {
      final cpu = counter();
      cpu.writeWatches.add((0xFFF800, 0xFFF800));
      for (var i = 0; i < 60; i++) {
        cpu.step();
      }
      // The store is the third instruction of the loop, at H'000106.
      expect(cpu.writeLog.first.pc, 0x000106,
          reason: 'the address of the instruction, not of the fetch');
    });

    test('says what was displaced', () {
      final cpu = counter();
      cpu.mem.poke(0xFFF800, 0xEE);
      cpu.writeWatches.add((0xFFF800, 0xFFF800));
      for (var i = 0; i < 20; i++) {
        cpu.step();
      }
      expect(cpu.writeLog.first.was, 0xEE);
      expect(cpu.writeLog.first.changed, isTrue);
    });

    test('a write that changes nothing is marked as such', () {
      // The firmware rewrites the same value constantly -- every key scan
      // stores the same code back. A log that does not say which writes
      // changed anything is mostly noise.
      final cpu = H8Cpu();
      cpu.writeWatches.add((0xFFF800, 0xFFF800));
      cpu.writeB(0xFFF800, 0x5A);
      cpu.writeB(0xFFF800, 0x5A);
      expect(cpu.writeLog.length, 2);
      expect(cpu.writeLog[0].changed, isTrue);
      expect(cpu.writeLog[1].changed, isFalse);
    });

    test('a range is watched, not just one address', () {
      final cpu = counter();
      cpu.writeWatches.add((0xFFF000, 0xFFFFFF));
      for (var i = 0; i < 60; i++) {
        cpu.step();
      }
      expect(cpu.writeLog, isNotEmpty);
    });

    test('does not grow without end', () {
      final cpu = counter();
      cpu.writeWatches.add((0xFFF800, 0xFFF800));
      cpu.writeLogLimit = 10;
      for (var i = 0; i < 2000; i++) {
        cpu.step();
      }
      expect(cpu.writeLog.length, 10);
      expect(cpu.writeLog.last.cycles,
          greaterThan(cpu.writeLog.first.cycles),
          reason: 'the newest are kept, oldest first');
    });

    test('a word write shows as its two bytes', () {
      final cpu = H8Cpu();
      cpu.writeWatches.add((0x100000, 0x10000F));
      cpu.writeW(0x100000, 0x1234);
      expect(cpu.writeLog.length, 2);
      expect(cpu.writeLog[0].value, 0x12);
      expect(cpu.writeLog[1].value, 0x34);
    });
  });

  group('conditions on a running machine', () {
    test('stop the machine when they hold', () {
      final cpu = counter();
      cpu.stopWhen = Condition.parse('[FFF800] == 20');
      var steps = 0;
      while (!cpu.conditionHit && steps < 100000) {
        cpu.step();
        steps++;
      }
      expect(cpu.conditionHit, isTrue);
      expect(cpu.peekBus(0xFFF800), 0x20);
    });

    test('cost nothing when none is set', () {
      final cpu = counter();
      expect(cpu.stopWhen, isNull);
      for (var i = 0; i < 500; i++) {
        cpu.step();
      }
      expect(cpu.conditionHit, isFalse);
    });

    test('stop before the instruction rather than after', () {
      final cpu = counter();
      cpu.stopWhen = Condition.parse('pc == 106');
      for (var i = 0; i < 100000 && !cpu.conditionHit; i++) {
        cpu.step();
      }
      expect(cpu.conditionHit, isTrue);
      expect(cpu.pc, 0x000106,
          reason: 'stopped about to execute it, which is what a stop means');
    });

    test('one that cannot be evaluated stops rather than throwing', () {
      final cpu = counter();
      cpu.stopWhen = Condition.parse('sausage == 1');
      expect(() => cpu.step(), returnsNormally);
      expect(cpu.conditionHit, isTrue);
    });

    test('clearing the flag lets it carry on', () {
      final cpu = counter();
      cpu.stopWhen = Condition.parse('[FFF800] == 5');
      for (var i = 0; i < 100000 && !cpu.conditionHit; i++) {
        cpu.step();
      }
      expect(cpu.conditionHit, isTrue);
      cpu.conditionHit = false;
      cpu.stopWhen = null;
      final before = cpu.cycles;
      for (var i = 0; i < 100; i++) {
        cpu.step();
      }
      expect(cpu.cycles, greaterThan(before));
    });
  });

  group('carrying on from a condition stop', () {
    // The stop happens before the instruction, so the instruction it stopped
    // on has not run. Asking the condition again before it runs would stop
    // on the same instruction for ever, and Run would appear to do nothing.
    test('the instruction it stopped on is the next one to run', () {
      final cpu = counter();
      cpu.stopWhen = Condition.parse('pc == 106');
      for (var i = 0; i < 1000 && !cpu.conditionHit; i++) {
        cpu.step();
      }
      expect(cpu.pc, 0x000106);
      final at = cpu.cycles;

      cpu.stepPastCondition();
      expect(cpu.cycles, greaterThan(at), reason: 'it actually ran');
      expect(cpu.pc, 0x00010C, reason: 'past the store, not still on it');
    });

    test('the condition is armed again straight after', () {
      final cpu = counter();
      cpu.stopWhen = Condition.parse('pc == 106');
      for (var i = 0; i < 1000 && !cpu.conditionHit; i++) {
        cpu.step();
      }
      cpu.stepPastCondition();
      expect(cpu.stopWhen, isNotNull);
      expect(cpu.conditionHit, isFalse);
      // Round the loop once more and it stops on it again.
      for (var i = 0; i < 1000 && !cpu.conditionHit; i++) {
        cpu.step();
      }
      expect(cpu.conditionHit, isTrue);
      expect(cpu.pc, 0x000106);
    });

    test('stepping past when none is armed is an ordinary step', () {
      final cpu = counter();
      final at = cpu.cycles;
      cpu.stepPastCondition();
      expect(cpu.cycles, greaterThan(at));
      expect(cpu.stopWhen, isNull);
    });
  });

  group('against the real machine', () {
    final dump = File('bernina_artista180/Bernina180_20260816.bin');
    final have = dump.existsSync();

    H8Cpu machine() {
      final cpu = H8Cpu();
      loadRawBinary(dump.readAsBytesSync(), 0, cpu.mem.poke);
      cpu.attachKeypad(Keypad());
      for (final r in artista180Flash) {
        cpu.attachFlash(JedecFlash.forRegion(r));
      }
      cpu.reset();
      return cpu;
    }

    test('run until the panel asks for something', () {
      final cpu = machine();
      final pad = Keypad();
      cpu.attachKeypad(pad);
      // Boot first, then hold clr and run until the firmware reports it.
      for (var i = 0; i < 25000000; i++) {
        cpu.step();
      }
      pad.setDown(Keypad.panelKeys.firstWhere((k) => k.code == 0x77), true);

      cpu.stopWhen = Condition.parse('[11B10E].w == 77');
      var steps = 0;
      while (!cpu.conditionHit && steps < 5000000) {
        cpu.step();
        steps++;
      }
      expect(cpu.conditionHit, isTrue,
          reason: 'the condition replaces a whole program in tool/');
      expect(cpu.peekBus(0x11B10E) << 8 | cpu.peekBus(0x11B10F), 0x0077);
    }, skip: have ? false : 'needs the machine dump');

    test('who writes the screen-request word', () {
      final cpu = machine();
      cpu.writeWatches.add((0x11B10E, 0x11B10F));
      for (var i = 0; i < 25000000; i++) {
        cpu.step();
      }
      expect(cpu.writeLog, isNotEmpty);
      // Every write to it comes from key_scan, at H'21F69C-H'21F870. That
      // is the fact the whole key-code investigation turned on, and here it
      // falls out of the log rather than out of a disassembly sweep.
      final writers = cpu.writeLog.map((w) => w.pc).toSet();
      expect(writers.every((pc) => pc >= 0x21F600 && pc <= 0x21F900), isTrue,
          reason: 'only key_scan writes it; saw $writers');
    }, skip: have ? false : 'needs the machine dump');
  });
}
