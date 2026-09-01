// Two machines run side by side, stopped where they part company.
//
// The question is always the same -- "the new build behaves differently from
// the old one, where does it start?" -- and answering it by hand meant
// writing the harness again each time.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/keypad.dart';
import 'package:sim_h83003/lockstep.dart';

/// A machine running [program] from H'000100.
H8Cpu machineRunning(List<int> program) {
  final cpu = H8Cpu();
  for (var i = 0; i < program.length; i++) {
    cpu.mem.poke(0x000100 + i, program[i]);
  }
  for (final (i, b) in [0x00, 0x00, 0x01, 0x00].indexed) {
    cpu.mem.poke(i, b);
  }
  cpu.reset();
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

/// The same, but summing to 9 -- one iteration fewer, and a different total.
List<int> get sumTo9 => [...sumTo10]..[15] = 0x0A;

Lockstep pair(List<int> a, List<int> b, {LockstepConfig? config}) => Lockstep(
      LockstepSide('old', machineRunning(a)),
      LockstepSide('new', machineRunning(b)),
      config: config ?? const LockstepConfig(maxSteps: 100000),
    );

void main() {
  group('two runs of the same thing', () {
    test('stay in step', () {
      final r = pair(sumTo10, sumTo10).run();
      expect(r.inStep, isTrue);
      expect(r.divergence, isNull);
      expect(r.steps, 100000);
    });

    test('and say so in words', () {
      expect(describeDivergence(pair(sumTo10, sumTo10).run()),
          contains('In step'));
    });

    test('running out of instructions is not the same as agreeing', () {
      // A run that hit its limit has not proved anything beyond it, and a
      // report that reads as though it had is a report that gets believed
      // too far.
      final r = pair(sumTo10, sumTo10).run();
      expect(r.end, LockstepEnd.limit);
      expect(describeDivergence(r), contains('reached its limit'));
      expect(describeDivergence(r), isNot(contains('Both sides')));
    });

    test('both sides falling quiet is, and says so', () {
      final r = pair(sumTo10, sumTo10,
              config: const LockstepConfig(
                  compare: LockstepCompare.writes,
                  writeRanges: [(0xFFFD10, 0xFFFD10)],
                  maxStepsBetweenWrites: 5000,
                  maxSteps: 100000))
          .run();
      expect(r.end, LockstepEnd.bothQuiet);
      final text = describeDivergence(r, writes: true);
      expect(text, contains('stopped writing'));
      expect(text, contains('1 write compared'),
          reason: 'writes, not instructions, and singular when there is one');
    });

    test('each side says how far it actually ran', () {
      final r = pair(sumTo10, sumTo10,
              config: const LockstepConfig(maxSteps: 500))
          .run();
      expect(r.a.stepsRun, 500);
      expect(r.b.stepsRun, 500);
    });
  });

  group('two runs that differ', () {
    test('part company at the branch that goes the other way', () {
      final r = pair(sumTo10, sumTo9).run();
      expect(r.inStep, isFalse);
      // The CMP is against a different immediate, so the flags differ before
      // the BNE does anything with them: state comparison catches the cause,
      // not the symptom.
      expect(r.divergence!.what, 'ccr');
      expect(r.end, LockstepEnd.diverged);
    });

    test('pc comparison catches it later, at the branch itself', () {
      final r = pair(sumTo10, sumTo9,
              config: const LockstepConfig(
                  compare: LockstepCompare.pc, maxSteps: 100000))
          .run();
      expect(r.divergence!.what, 'pc');
      expect(r.divergence!.step,
          greaterThan(pair(sumTo10, sumTo9).run().divergence!.step),
          reason: 'the flags differed before the branch acted on them');
    });

    test('the report shows how each side got there', () {
      final text = describeDivergence(pair(sumTo10, sumTo9).run());
      expect(text, contains('Parted company'));
      expect(text, contains('old:'));
      expect(text, contains('new:'));
      expect(text, contains('CMP.B'), reason: 'the run-up is disassembled');
      expect(text, contains('registers:'));
    });

    test('the report names symbols when it has them', () {
      final text = describeDivergence(pair(sumTo10, sumTo9).run(),
          symbols: {0x00010E: 'compare_limit'});
      expect(text, contains('compare_limit'));
    });
  });

  group('what is not compared', () {
    test('a settling period lets start-up code differ', () {
      // Both are asleep in the same two-instruction loop by then, so the
      // code is back in step -- though the totals they worked out are not,
      // which is why this is a pc comparison and not a state one.
      final r = pair(sumTo10, sumTo9,
              config: const LockstepConfig(
                  compare: LockstepCompare.pc,
                  settleSteps: 100,
                  maxSteps: 100000))
          .run();
      expect(r.inStep, isTrue);
    });

    test('so does an ignored range', () {
      final r = pair(sumTo10, sumTo9,
              config: const LockstepConfig(
                  compare: LockstepCompare.pc,
                  ignorePc: [(0x000100, 0x000120)],
                  maxSteps: 100000))
          .run();
      expect(r.inStep, isTrue);
    });

    test('a range is ignored when either side is inside it', () {
      // Not just the first side: the interesting case is one image jumping
      // somewhere the other never goes.
      final r = pair(sumTo10, sumTo9,
              config: const LockstepConfig(
                  ignorePc: [(0x000110, 0x000112)], maxSteps: 1000))
          .run();
      expect(r.inStep, isFalse,
          reason: 'the CMP at H\'00010E is still compared');
    });
  });

  group('comparing what was written', () {
    test('a difference in the value written is reported', () {
      final r = pair(sumTo10, sumTo9,
              config: const LockstepConfig(
                  compare: LockstepCompare.writes,
                  writeRanges: [(0xFFFD10, 0xFFFD10)],
                  maxSteps: 100000))
          .run();
      expect(r.inStep, isFalse);
      expect(r.divergence!.what, 'write');
      // H'37 is 55, the sum of 1..10; H'2D is 45, the sum of 1..9.
      expect(r.divergence!.detail, contains("H'37"));
      expect(r.divergence!.detail, contains("H'2D"));
    });

    test('the writing instruction is named on both sides', () {
      final r = pair(sumTo10, sumTo9,
              config: const LockstepConfig(
                  compare: LockstepCompare.writes,
                  writeRanges: [(0xFFFD10, 0xFFFD10)],
                  maxSteps: 100000))
          .run();
      expect(r.divergence!.detail, contains("H'000112"));
    });

    test('two runs that write the same things agree, however they got there',
        () {
      // The same program at two different speeds: one has a nop-equivalent
      // in its loop, so the instruction counts and cycle counts differ all
      // the way through, and the writes do not.
      final slow = [...sumTo10];
      slow.insertAll(10, [0x00, 0x00]); // NOP at the top of the loop
      slow[19] = 0xF6; // the BNE, now two bytes along, reaches back over it
      final r = Lockstep(
        LockstepSide('plain', machineRunning(sumTo10)),
        LockstepSide('with a nop', machineRunning(slow)),
        config: const LockstepConfig(
            compare: LockstepCompare.writes,
            writeRanges: [(0xFFFD10, 0xFFFD10)],
            maxSteps: 100000),
      ).run();
      expect(r.inStep, isTrue,
          reason: 'the point of comparing writes is that the route may differ');
    });

    test('one side that stops writing is a divergence in itself', () {
      // The second never reaches its store: the loop counter starts past the
      // limit, so the BNE never falls through.
      // Not "start past the limit": R1L wraps round to it. BRA in place of
      // BNE is the loop that genuinely never falls through to the store.
      final never = [...sumTo10]..[16] = 0x40; // BRA loop
      final r = Lockstep(
        LockstepSide('writes', machineRunning(sumTo10)),
        LockstepSide('never writes', machineRunning(never)),
        config: const LockstepConfig(
            compare: LockstepCompare.writes,
            writeRanges: [(0xFFFD10, 0xFFFD10)],
            maxStepsBetweenWrites: 20000,
            maxSteps: 100000),
      ).run();
      expect(r.inStep, isFalse);
      expect(r.divergence!.detail, contains('stopped writing'));
    });
  });

  group('in lockstep, a differing write is caught as well as differing code',
      () {
    test('even when the two sides are executing the same instruction', () {
      // Same code both sides, but one starts with a different byte in the
      // memory it adds in, so the instructions match and the write does not.
      final a = machineRunning(sumTo10);
      final b = machineRunning(sumTo10);
      b.mem.poke(0x000107, 0x05); // MOV.B #5,R0L rather than #0
      final r = Lockstep(
        LockstepSide('zero', a),
        LockstepSide('five', b),
        config: const LockstepConfig(
            compare: LockstepCompare.pc,
            writeRanges: [(0xFFFD10, 0xFFFD10)],
            maxSteps: 100000),
      ).run();
      expect(r.inStep, isFalse);
      expect(r.divergence!.what, 'write');
    });
  });

  group('against the real machine', () {
    final dump = File('bernina_artista180/Bernina180_20260816.bin');
    final have = dump.existsSync();

    H8Cpu artista({Keypad? pad}) {
      final cpu = H8Cpu();
      loadRawBinary(dump.readAsBytesSync(), 0, cpu.mem.poke);
      cpu.attachKeypad(pad ?? Keypad());
      for (final r in artista180Flash) {
        cpu.attachFlash(JedecFlash.forRegion(r));
      }
      cpu.reset();
      return cpu;
    }

    test('the same image against itself stays in step', () {
      final r = Lockstep(
        LockstepSide('a', artista()),
        LockstepSide('b', artista()),
        config: const LockstepConfig(maxSteps: 2000000),
      ).run();
      expect(r.inStep, isTrue,
          reason: 'anything else means the machine is not deterministic, '
              'and every comparison built on it would be noise');
      expect(r.steps, 2000000);
    });

    test('a byte changed in the firmware is found', () {
      final patched = artista();
      // H'208E7A is inside the application. Whatever it did, it now returns.
      patched.mem.poke(0x208E7A, 0x54);
      patched.mem.poke(0x208E7B, 0x70);
      final r = Lockstep(
        LockstepSide('stock', artista()),
        LockstepSide('patched', patched),
        config: const LockstepConfig(maxSteps: 25000000),
      ).run();
      expect(r.inStep, isFalse);
      final text = describeDivergence(r);
      expect(text, contains('Parted company'));
      expect(text, contains('stock:'));
    }, skip: have ? false : 'needs the machine dump');

    test('holding a key is a difference the machine notices', () {
      // The same image both sides, one with clr held. This is the shape of
      // "what does this button actually change?", answered by where the two
      // runs first disagree rather than by reading the key scanner.
      final pad = Keypad();
      final held = artista(pad: pad);
      pad.setDown(
          Keypad.panelKeys.firstWhere((k) => k.code == 0x77), true);
      final r = Lockstep(
        LockstepSide('nothing held', artista()),
        LockstepSide('clr held', held),
        config: const LockstepConfig(maxSteps: 25000000),
      ).run();
      expect(r.inStep, isFalse,
          reason: 'if this passes, the keypad is not reaching the firmware');
    }, skip: have ? false : 'needs the machine dump');
  });
}
