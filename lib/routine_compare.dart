// Comparing one rebuilt routine against the original it replaces.
//
// Most of the application is too deep in the call graph for any end-to-end
// check to reach: a routine three hundred calls below the main loop cannot
// be judged by looking at the screen. What can be done instead is to call
// the original and its replacement with equivalent inputs and compare what
// each one did — the value it handed back, and every byte of memory it
// changed.
//
// "Equivalent inputs" has to be spelled out per routine, because the two do
// not agree on where arguments go. The original was compiled to put the
// first in ER6 and the rest on the stack; the rebuild is compiled by GCC,
// which uses ER0, ER1 and ER2 and then the stack. So each case says where
// each side's arguments belong, and the harness places them.
//
// Memory is compared by snapshot rather than by naming ranges, so a routine
// that writes somewhere it should not is caught without anyone having to
// predict where. The application's own code region is excluded, since the
// two images differ there by construction.


import 'h8300h.dart';

/// Where one argument goes.
sealed class Placement {
  const Placement();
}

/// Into a register: `er[index]`, whole 32 bits.
class RegisterArg extends Placement {
  const RegisterArg(this.index, this.value);
  final int index;
  final int value;
}

/// Onto the stack, [size] bytes at [offset] above the stack pointer, where
/// offset 0 is the return address.
class StackArg extends Placement {
  const StackArg(this.offset, this.size, this.value);
  final int offset;
  final int size;
  final int value;
}

/// A byte to place in memory before the call, on both sides.
class Seed {
  const Seed(this.address, this.value);
  final int address;
  final int value;
}

/// One side of a comparison: which routine, and how to call it.
class RoutineSide {
  const RoutineSide({
    required this.address,
    this.args = const [],
    this.resultRegister,
    this.resultWidth = 4,
  });

  final int address;
  final List<Placement> args;

  /// The register the result comes back in — ER6 for the original's
  /// convention, ER0 for GCC's. Null for a routine that returns nothing:
  /// the two will leave different rubbish in their scratch registers and
  /// comparing it would fail every time for no reason.
  final int? resultRegister;

  /// How much of that register is the result: 1 for a byte returned in the
  /// low half, 2 for a word, 4 for a longword. A routine returning a byte
  /// leaves the rest of the register as working space, so comparing all of
  /// it would fail on rubbish.
  final int resultWidth;
}

/// What happened when a routine ran.
class RoutineRun {
  RoutineRun({
    required this.steps,
    required this.result,
    required this.writes,
    required this.returned,
  });

  final int steps;
  final int? result;

  /// Address to value, for every byte the routine changed.
  final Map<int, int> writes;
  final bool returned;
}

/// The outcome of comparing two runs.
class RoutineComparison {
  RoutineComparison(this.name, this.original, this.rebuilt, this.differences);

  final String name;
  final RoutineRun original;
  final RoutineRun rebuilt;

  /// Addresses whose final value differs, with both values.
  final Map<int, (int, int)> differences;

  /// True when neither side reports a result, or both report the same one.
  bool get resultsMatch {
    final o = original.result, n = rebuilt.result;
    if (o == null || n == null) return true;
    return o == n;
  }
  bool get bothReturned => original.returned && rebuilt.returned;
  bool get passed => bothReturned && resultsMatch && differences.isEmpty;
}

/// Runs routines and compares them.
class RoutineComparer {
  RoutineComparer({
    this.sentinel = 0x00FFF800,
    this.stackPointer = 0x00FFF600,
    this.stepLimit = 8000000,
    this.excluded = const [(0x200000, 0x251000)],
  });

  /// Where a call is made to return to, so the harness knows it has finished.
  final int sentinel;
  final int stackPointer;
  final int stepLimit;

  /// Address ranges left out of the memory comparison. The application's
  /// code region is here by default: the two images hold different code
  /// there, and a routine that writes to itself is not what is being looked
  /// for.
  final List<(int, int)> excluded;

  /// How far below the stack pointer to ignore. A routine's own frame lives
  /// there, and the two sides push different things in different orders, so
  /// what is left behind differs for reasons that say nothing about whether
  /// the routine is right.
  static const int frameDepth = 0x2000;

  bool _isExcluded(int addr) {
    if (addr < stackPointer + 0x20 && addr >= stackPointer - frameDepth) {
      return true;
    }
    for (final (from, to) in excluded) {
      if (addr >= from && addr < to) return true;
    }
    return false;
  }

  /// What a run changed, from the memory's own undo log: every address it
  /// wrote, against the value that was there before the first write. An
  /// address written and then written back to what it held is not a change
  /// and does not appear.
  ///
  /// This used to be done by copying every allocated byte before the run and
  /// comparing every allocated byte after it -- four megabytes each way, for
  /// a routine that usually touches a few dozen bytes. The log is exact and
  /// costs nothing when nothing is written.
  Map<int, int> _writesFromLog(H8Cpu cpu, Map<int, int> log) {
    final writes = <int, int>{};
    log.forEach((addr, was) {
      final now = cpu.mem.peek(addr);
      if (now != was && !_isExcluded(addr)) writes[addr] = now;
    });
    return writes;
  }

  void _place(H8Cpu cpu, Placement p) {
    switch (p) {
      case RegisterArg(:final index, :final value):
        cpu.er[index] = value;
      case StackArg(:final offset, :final size, :final value):
        for (var i = 0; i < size; i++) {
          cpu.mem.poke(
              stackPointer + offset + i, (value >> (8 * (size - 1 - i))) & 0xFF);
        }
    }
  }

  /// Calls [side] on an already-prepared machine.
  RoutineRun run(H8Cpu cpu, RoutineSide side, List<Seed> seed) {
    for (final s in seed) {
      cpu.writeB(s.address, s.value);
    }

    for (var i = 0; i < 4; i++) {
      cpu.mem.poke(stackPointer + i, (sentinel >> (8 * (3 - i))) & 0xFF);
    }
    for (final a in side.args) {
      _place(cpu, a);
    }
    cpu.er[7] = stackPointer;
    cpu.pc = side.address;

    // A caller may already be logging -- the comparer's tool keeps one
    // running so it can put a borrowed machine back. Chain rather than
    // clobber: this run gets a log of its own for the diff, and afterwards
    // everything in it is folded into the outer one, keeping whichever old
    // value was seen first.
    final outer = cpu.mem.undoLog;
    final log = <int, int>{};
    cpu.mem.undoLog = log;

    var steps = 0;
    while (cpu.pc != sentinel && steps < stepLimit) {
      cpu.step();
      steps++;
    }
    final returned = cpu.pc == sentinel;
    cpu.mem.undoLog = outer;
    if (outer != null) {
      log.forEach((addr, was) => outer.putIfAbsent(addr, () => was));
    }

    return RoutineRun(
      steps: steps,
      result: side.resultRegister == null
          ? null
          : cpu.er[side.resultRegister!] &
              (side.resultWidth == 1
                  ? 0xFF
                  : side.resultWidth == 2
                      ? 0xFFFF
                      : 0xFFFFFF),
      writes: returned ? _writesFromLog(cpu, log) : const {},
      returned: returned,
    );
  }

  /// Compares two runs of the same routine.
  RoutineComparison compare(
      String name, RoutineRun original, RoutineRun rebuilt) {
    final differences = <int, (int, int)>{};
    final addresses = {...original.writes.keys, ...rebuilt.writes.keys};
    for (final a in addresses) {
      final o = original.writes[a];
      final n = rebuilt.writes[a];
      // A byte one side wrote and the other did not is a difference only if
      // the values differ; writing back the value that was already there is
      // invisible either way.
      if (o != n) differences[a] = (o ?? -1, n ?? -1);
    }
    return RoutineComparison(name, original, rebuilt, differences);
  }
}
