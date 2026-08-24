// The per-routine comparison harness.
//
// Two tiny routines are assembled into memory and compared: the point is
// that the harness notices when they disagree and does not notice things
// that do not matter, such as the different rubbish each leaves in its own
// stack frame.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/routine_compare.dart';

const int codeA = 0x300000;
const int codeB = 0x310000;
const int target = 0x320000;

/// Writes a routine that stores [value] at [target] and returns [ret] in ER6,
/// optionally pushing and popping a register first so it leaves a frame.
void writeRoutine(H8Cpu cpu, int at, int value, int ret,
    {bool usesStack = false}) {
  var p = at;
  void b(List<int> bytes) {
    for (final x in bytes) {
      cpu.mem.poke(p++, x);
    }
  }

  if (usesStack) b([0x6D, 0xF5]); // push.w r5
  b([0xFE, value]); //              mov.b #value,r6l
  // MOV.B R6L,@aa:24 -- the address occupies four bytes, top one unused.
  b([0x6A, 0xAE, 0x00, (target >> 16) & 0xFF, (target >> 8) & 0xFF,
     target & 0xFF]);
  b([0xF9, ret]); //                mov.b #ret,r1l  (scratch)
  b([0xFE, ret]); //                mov.b #ret,r6l
  if (usesStack) b([0x6D, 0x75]); // pop.w r5
  b([0x54, 0x70]); //               rts
}

H8Cpu machine(int at, int value, int ret, {bool usesStack = false}) {
  final cpu = H8Cpu();
  cpu.reset();
  writeRoutine(cpu, at, value, ret, usesStack: usesStack);
  return cpu;
}

void main() {
  final comparer = RoutineComparer(excluded: const []);

  test('two routines that do the same thing agree', () {
    final a = comparer.run(machine(codeA, 0x5A, 1),
        const RoutineSide(address: codeA, resultRegister: 6, resultWidth: 1),
        const []);
    final b = comparer.run(machine(codeB, 0x5A, 1, usesStack: true),
        const RoutineSide(address: codeB, resultRegister: 6, resultWidth: 1),
        const []);

    final r = comparer.compare('same', a, b);
    expect(r.bothReturned, isTrue);
    expect(r.resultsMatch, isTrue);
    expect(r.differences, isEmpty,
        reason: 'the frame one of them pushes is not a difference');
    expect(r.passed, isTrue);
  });

  test('a different value written is caught, with both values', () {
    final a = comparer.run(machine(codeA, 0x5A, 1),
        const RoutineSide(address: codeA, resultRegister: 6, resultWidth: 1),
        const []);
    final b = comparer.run(machine(codeB, 0x5B, 1),
        const RoutineSide(address: codeB, resultRegister: 6, resultWidth: 1),
        const []);

    final r = comparer.compare('value', a, b);
    expect(r.passed, isFalse);
    expect(r.differences[target], (0x5A, 0x5B));
  });

  test('a different result is caught', () {
    final a = comparer.run(machine(codeA, 0x5A, 1),
        const RoutineSide(address: codeA, resultRegister: 6, resultWidth: 1),
        const []);
    final b = comparer.run(machine(codeB, 0x5A, 2),
        const RoutineSide(address: codeB, resultRegister: 6, resultWidth: 1),
        const []);

    final r = comparer.compare('result', a, b);
    expect(r.resultsMatch, isFalse);
    expect(r.passed, isFalse);
  });

  test('a routine with no result is not judged on its scratch registers', () {
    // Same store, different leftovers in ER6.
    final a = comparer.run(
        machine(codeA, 0x5A, 1), const RoutineSide(address: codeA), const []);
    final b = comparer.run(
        machine(codeB, 0x5A, 9), const RoutineSide(address: codeB), const []);

    expect(comparer.compare('void', a, b).passed, isTrue);
  });

  test('only the named width of the result is compared', () {
    // Both leave 1 in R6L; the rest of ER6 differs and must be ignored.
    final wide = machine(codeB, 0x5A, 1);
    wide.er[6] = 0xFFFF0000;
    final a = comparer.run(machine(codeA, 0x5A, 1),
        const RoutineSide(address: codeA, resultRegister: 6, resultWidth: 1),
        const []);
    final b = comparer.run(wide,
        const RoutineSide(address: codeB, resultRegister: 6, resultWidth: 1),
        const []);
    expect(comparer.compare('byte', a, b).resultsMatch, isTrue);
  });

  test('a routine that never returns is reported rather than hanging', () {
    final cpu = H8Cpu()..reset();
    cpu.mem.poke(codeA, 0x40); // bra .
    cpu.mem.poke(codeA + 1, 0xFE);
    final short = RoutineComparer(stepLimit: 5000, excluded: const []);
    final run =
        short.run(cpu, const RoutineSide(address: codeA), const []);
    expect(run.returned, isFalse);

    final other = short.run(machine(codeB, 0x5A, 1),
        const RoutineSide(address: codeB), const []);
    expect(short.compare('hang', run, other).passed, isFalse);
  });

  test('seeded memory is placed before the call, on both sides', () {
    final cpu = machine(codeA, 0x5A, 1);
    comparer.run(cpu, const RoutineSide(address: codeA),
        const [Seed(0x330000, 0x77)]);
    expect(cpu.mem.peek(0x330000), 0x77);
  });
}
