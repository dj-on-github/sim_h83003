// The undo journal on its own: what it keeps, what it gives back, and what
// it does when asked to hold more than it can.
//
// It is flat words rather than an object per instruction, which is the only
// reason a machine that takes twenty-five million instructions to boot can
// keep a useful amount of history at all -- so the encoding is worth testing
// directly, not just through the CPU.

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/undo.dart';

Uint32List regs(List<int> v) => Uint32List.fromList(v);

/// Records one instruction that changed [before] into [after].
void step(
  UndoJournal j, {
  int pc = 0x1000,
  int ccr = 0x80,
  int cycles = 100,
  bool halted = false,
  bool sleeping = false,
  bool device = false,
  List<int> before = const [0, 0, 0, 0, 0, 0, 0, 0],
  List<int> after = const [0, 0, 0, 0, 0, 0, 0, 0],
  List<(int, int)> writes = const [],
}) {
  j.beginStep(
      pc: pc, ccr: ccr, cycles: cycles, halted: halted, sleeping: sleeping);
  for (final (addr, was) in writes) {
    j.noteWrite(addr, was);
  }
  if (device) j.noteDevice();
  j.endStep(regs(before), regs(after));
}

void main() {
  group('an empty journal', () {
    test('has nothing to give back', () {
      final j = UndoJournal();
      expect(j.isEmpty, isTrue);
      expect(j.length, 0);
      expect(j.pop(), isNull);
      expect(j.newestIsExact, isNull);
    });
  });

  group('one instruction', () {
    test('comes back as it went in', () {
      final j = UndoJournal();
      step(j,
          pc: 0x208E7A,
          ccr: 0x84,
          cycles: 123456789,
          before: [1, 2, 3, 4, 5, 6, 7, 8],
          after: [1, 2, 0xFFFF, 4, 5, 6, 7, 8]);

      final u = j.pop()!;
      expect(u.pc, 0x208E7A);
      expect(u.ccr, 0x84);
      expect(u.cycles, 123456789);
      expect(u.registers, [(2, 3)], reason: 'only what changed');
      expect(u.memory, isEmpty);
      expect(j.isEmpty, isTrue, reason: 'taking it off takes it off');
    });

    test('a cycle count past four billion survives the two halves', () {
      // The count passes what one word holds within a few seconds of
      // simulated time, so it is kept as two -- and the halves going back
      // the wrong way round would be an error of four billion.
      final j = UndoJournal();
      step(j, cycles: 0x1234_9ABC_DEF0);
      expect(j.pop()!.cycles, 0x1234_9ABC_DEF0);
    });

    test('every register changing is kept', () {
      final j = UndoJournal();
      step(j,
          before: [0, 1, 2, 3, 4, 5, 6, 7],
          after: [8, 9, 10, 11, 12, 13, 14, 15]);
      expect(j.pop()!.registers.length, 8);
    });

    test('a register value with the top bit set is not sign-mangled', () {
      final j = UndoJournal();
      step(j, before: [0xFFFFFFFF, 0, 0, 0, 0, 0, 0, 0],
          after: [0, 0, 0, 0, 0, 0, 0, 0]);
      expect(j.pop()!.registers, [(0, 0xFFFFFFFF)]);
    });

    test('the bytes it changed come back in the order they were written', () {
      final j = UndoJournal();
      step(j, writes: [(0x11B10E, 0xAA), (0x11B10F, 0xBB), (0x11B10E, 0xCC)]);
      expect(j.pop()!.memory,
          [(0x11B10E, 0xAA), (0x11B10F, 0xBB), (0x11B10E, 0xCC)]);
    });

    test('a full 24-bit address survives being packed with its byte', () {
      final j = UndoJournal();
      step(j, writes: [(0xFFFFFF, 0xFF)]);
      expect(j.pop()!.memory, [(0xFFFFFF, 0xFF)]);
    });

    test('halted and sleeping are remembered', () {
      final j = UndoJournal();
      step(j, halted: true, sleeping: true);
      final u = j.pop()!;
      expect(u.halted, isTrue);
      expect(u.sleeping, isTrue);
    });
  });

  group('a step that touched a peripheral', () {
    test('is marked, because putting memory back does not put it back', () {
      final j = UndoJournal();
      step(j, device: true);
      expect(j.newestIsExact, isFalse);
      expect(j.pop()!.inexact, isTrue);
    });

    test('and one that did not is not', () {
      final j = UndoJournal();
      step(j);
      expect(j.newestIsExact, isTrue);
      expect(j.pop()!.inexact, isFalse);
    });

    test('the mark belongs to its own step, not to the ones after it', () {
      final j = UndoJournal();
      step(j, device: true);
      step(j);
      expect(j.pop()!.inexact, isFalse);
      expect(j.pop()!.inexact, isTrue);
    });
  });

  group('many instructions', () {
    test('come back newest first', () {
      final j = UndoJournal();
      for (var i = 0; i < 100; i++) {
        step(j, pc: 0x1000 + i);
      }
      expect(j.length, 100);
      for (var i = 99; i >= 0; i--) {
        expect(j.pop()!.pc, 0x1000 + i);
      }
      expect(j.isEmpty, isTrue);
    });

    test('of differing sizes still line up', () {
      // Records are not all the same length, so finding the one before the
      // newest is the part that can go wrong.
      final j = UndoJournal();
      step(j, pc: 1, writes: [(0x100, 1), (0x101, 2), (0x102, 3)]);
      step(j, pc: 2, before: [0, 0, 0, 0, 0, 0, 0, 0],
          after: [1, 1, 1, 0, 0, 0, 0, 0]);
      step(j, pc: 3);
      step(j, pc: 4, writes: [(0x200, 9)]);

      expect(j.pop()!.pc, 4);
      expect(j.pop()!.pc, 3);
      final third = j.pop()!;
      expect(third.pc, 2);
      expect(third.registers.length, 3);
      final first = j.pop()!;
      expect(first.pc, 1);
      expect(first.memory.length, 3);
    });
  });

  group('keeping it to size', () {
    test('the oldest are dropped once the limit is passed', () {
      final j = UndoJournal(limit: 80);
      for (var i = 0; i < 400; i++) {
        step(j, pc: i);
      }
      expect(j.length, lessThanOrEqualTo(80));
      expect(j.length, greaterThan(0));
      expect(j.pop()!.pc, 399, reason: 'the newest are the ones kept');
    });

    test('and what is left still reads back correctly', () {
      final j = UndoJournal(limit: 80);
      for (var i = 0; i < 400; i++) {
        step(j, pc: i, writes: [(0x1000 + i, i & 0xFF)]);
      }
      final held = j.length;
      for (var i = 0; i < held; i++) {
        final u = j.pop()!;
        expect(u.pc, 399 - i);
        expect(u.memory, [(0x1000 + 399 - i, (399 - i) & 0xFF)]);
      }
    });

    test('a limit of one keeps one', () {
      final j = UndoJournal(limit: 1);
      for (var i = 0; i < 20; i++) {
        step(j, pc: i);
      }
      expect(j.length, lessThanOrEqualTo(1));
    });

    test('a limit of none keeps none rather than growing for ever', () {
      final j = UndoJournal(limit: 0);
      for (var i = 0; i < 100; i++) {
        step(j, pc: i);
      }
      expect(j.isEmpty, isTrue);
    });

    test('the memory it holds is bounded by the limit, not by the run', () {
      final j = UndoJournal(limit: 500);
      for (var i = 0; i < 200; i++) {
        step(j, pc: i);
      }
      final small = j.bytesHeld;
      for (var i = 0; i < 20000; i++) {
        step(j, pc: i);
      }
      expect(j.bytesHeld, lessThan(small * 10),
          reason: 'a hundred times the instructions, not a hundred times '
              'the memory');
    });
  });

  group('an instruction that changed more than can be recorded', () {
    test('drops the history rather than recording half of it', () {
      // A gap is worse than no history: stepping back through one lands
      // silently in a state the machine was never in.
      final j = UndoJournal();
      step(j, pc: 1);
      step(j, pc: 2);
      expect(j.length, 2);

      j.beginStep(pc: 3, ccr: 0, cycles: 0, halted: false, sleeping: false);
      for (var i = 0; i <= UndoJournal.maxBytesPerStep; i++) {
        j.noteWrite(0x100000 + i, i & 0xFF);
      }
      j.endStep(regs([0, 0, 0, 0, 0, 0, 0, 0]), regs([0, 0, 0, 0, 0, 0, 0, 0]));

      expect(j.isEmpty, isTrue);
    });

    test('one byte under the limit is still recorded', () {
      final j = UndoJournal();
      j.beginStep(pc: 3, ccr: 0, cycles: 0, halted: false, sleeping: false);
      for (var i = 0; i < UndoJournal.maxBytesPerStep; i++) {
        j.noteWrite(0x100000 + i, i & 0xFF);
      }
      j.endStep(regs([0, 0, 0, 0, 0, 0, 0, 0]), regs([0, 0, 0, 0, 0, 0, 0, 0]));

      expect(j.length, 1);
      final u = j.pop()!;
      expect(u.memory.length, UndoJournal.maxBytesPerStep);
      expect(u.memory.last, (0x100000 + UndoJournal.maxBytesPerStep - 1,
          (UndoJournal.maxBytesPerStep - 1) & 0xFF));
    });
  });

  group('a step that was abandoned', () {
    test('leaves what was already held alone', () {
      final j = UndoJournal();
      step(j, pc: 1);
      j.beginStep(pc: 2, ccr: 0, cycles: 0, halted: false, sleeping: false);
      j.noteWrite(0x100, 0x55);
      j.abandonStep();
      expect(j.length, 1);
      expect(j.pop()!.pc, 1);
    });

    test('and its writes do not leak into the next one', () {
      final j = UndoJournal();
      j.beginStep(pc: 1, ccr: 0, cycles: 0, halted: false, sleeping: false);
      j.noteWrite(0x100, 0x55);
      j.abandonStep();
      step(j, pc: 2);
      expect(j.pop()!.memory, isEmpty);
    });
  });

  test('clearing empties it', () {
    final j = UndoJournal();
    for (var i = 0; i < 50; i++) {
      step(j, pc: i);
    }
    j.clear();
    expect(j.isEmpty, isTrue);
    expect(j.pop(), isNull);
    step(j, pc: 99);
    expect(j.pop()!.pc, 99, reason: 'and leaves it usable');
  });
}
