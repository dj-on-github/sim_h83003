/// Two machines, run side by side, stopped at the first instruction where
/// they part company.
///
/// A third of tool/ is one program written again with a different test in
/// it, and a good share of the rest is this: run the old image and the new
/// one and work out where they stopped agreeing. Doing it by hand finds the
/// answer in one run, which is the point -- the awkward part was never the
/// idea, it was writing the harness again each time.
///
/// Three things can be compared, and which one to use depends on how alike
/// the two images are meant to be:
///
///   [LockstepCompare.pc]      the same code in the same order. The loosest,
///                             and the right one when the two images share
///                             their application and differ somewhere else.
///   [LockstepCompare.state]   the registers and flags too, so a difference
///                             is caught at the instruction that caused it
///                             rather than at the branch that revealed it.
///   [LockstepCompare.writes]  not the instructions at all -- only what each
///                             side writes, in order. Two different
///                             implementations of the same routine make the
///                             same writes, so this is the comparison that
///                             survives the code being rewritten, which is
///                             the case of "does my C version behave the
///                             same".
///
/// Nothing here loads anything. The caller builds both machines, which is
/// what lets the same runner compare two images, or one image against
/// itself with a different key held down.
library;

import 'h8300h.dart';
import 'h8disasm.dart';

/// What has to match for the two runs to be in step.
enum LockstepCompare { pc, state, writes }

/// One side's position, kept for the few instructions before a divergence so
/// the report can show how each side got there.
typedef LockstepStep = ({int pc, int cycles});

/// A machine and what to call it in the report.
class LockstepSide {
  LockstepSide(this.name, this.cpu);

  final String name;
  final H8Cpu cpu;

  /// Instructions this side has actually run. In [LockstepCompare.writes]
  /// the two sides run at their own pace, so these differ -- and how far
  /// apart they are is itself worth knowing.
  int stepsRun = 0;

  /// The last few instructions, oldest first. A ring, so a forty-million
  /// instruction run does not keep forty million entries.
  final List<LockstepStep> history = [];

  void _remember(int depth) {
    history.add((pc: cpu.pc, cycles: cpu.cycles));
    if (history.length > depth) history.removeRange(0, history.length - depth);
  }
}

/// How to run the comparison.
class LockstepConfig {
  const LockstepConfig({
    this.compare = LockstepCompare.state,
    this.maxSteps = 40000000,
    this.settleSteps = 0,
    this.ignorePc = const [],
    this.writeRanges = const [],
    this.historyDepth = 16,
    this.maxStepsBetweenWrites = 2000000,
  });

  final LockstepCompare compare;

  /// How far to run before giving up and reporting that they stayed in step.
  final int maxSteps;

  /// Instructions to run before comparing anything. Two images that differ
  /// in their start-up code agree about everything that matters afterwards,
  /// and comparing from instruction zero reports the difference you already
  /// know about.
  final int settleSteps;

  /// Ranges in which a difference is not a divergence, for the same reason.
  /// A step is skipped when *either* side's PC is inside one of them.
  final List<(int, int)> ignorePc;

  /// The addresses whose writes are compared. Required by
  /// [LockstepCompare.writes]; optional for the other two, where a
  /// difference in what was written is reported as well as one in the code.
  final List<(int, int)> writeRanges;

  /// How many instructions of run-up to keep for the report.
  final int historyDepth;

  /// In [LockstepCompare.writes], how far one side may run without producing
  /// a write before it is called a divergence in itself: one side has stopped
  /// doing the work the other is still doing.
  final int maxStepsBetweenWrites;
}

/// Why the run ended.
enum LockstepEnd {
  /// The two parted company.
  diverged,

  /// The instruction limit was reached with them still in step.
  limit,

  /// Both sides stopped writing, in [LockstepCompare.writes]. They agreed
  /// about everything either of them wrote.
  bothQuiet,
}

/// Where and how the two runs stopped agreeing.
class Divergence {
  const Divergence({
    required this.step,
    required this.what,
    required this.detail,
    required this.a,
    required this.b,
  });

  /// Which instruction, counting from the start of the run. In
  /// [LockstepCompare.writes] this counts writes, not instructions.
  final int step;

  /// Which thing differed: 'pc', a register name, 'ccr', 'halted', 'write'.
  final String what;

  /// The two values, already written the way the machine writes them.
  final String detail;

  final LockstepSide a, b;
}

/// What the run found.
class LockstepResult {
  const LockstepResult({
    required this.divergence,
    required this.steps,
    required this.end,
    required this.a,
    required this.b,
  });

  /// Null when the two stayed in step for the whole run.
  final Divergence? divergence;

  /// How far the comparison got: instructions, or in
  /// [LockstepCompare.writes] the number of writes compared.
  final int steps;

  final LockstepEnd end;

  final LockstepSide a, b;

  bool get inStep => divergence == null;
}

/// Runs two machines side by side.
class Lockstep {
  Lockstep(this.a, this.b, {this.config = const LockstepConfig()}) {
    if (config.writeRanges.isNotEmpty) {
      for (final side in [a, b]) {
        side.cpu.writeWatches.addAll(config.writeRanges);
        // The comparison consumes the log as it goes, so it must not be
        // trimmed out from under it.
        side.cpu.writeLogLimit = 1 << 30;
      }
    }
  }

  final LockstepSide a, b;
  final LockstepConfig config;

  bool _ignored(int pc) {
    for (final (first, last) in config.ignorePc) {
      if (pc >= first && pc <= last) return true;
    }
    return false;
  }

  LockstepResult run() => config.compare == LockstepCompare.writes
      ? _runWrites()
      : _runInStep();

  LockstepResult _runInStep() {
    final full = config.compare == LockstepCompare.state;
    final watching = config.writeRanges.isNotEmpty;
    var step = 0;
    for (; step < config.maxSteps; step++) {
      a._remember(config.historyDepth);
      b._remember(config.historyDepth);

      final compare = step >= config.settleSteps &&
          !_ignored(a.cpu.pc) &&
          !_ignored(b.cpu.pc);

      if (compare) {
        final d = _difference(step, full: full);
        if (d != null) return _stopped(d, step);
      }

      // Emptied each time rather than left to grow: over forty million
      // instructions a watch on a busy address would hold every write the
      // run ever made, and only this instruction's are wanted.
      if (watching) {
        a.cpu.writeLog.clear();
        b.cpu.writeLog.clear();
      }
      a.cpu.step();
      b.cpu.step();
      a.stepsRun++;
      b.stepsRun++;

      if (compare && watching) {
        final d = _writesDiffer(step);
        if (d != null) return _stopped(d, step);
      }
    }
    return LockstepResult(
        divergence: null, steps: step, end: LockstepEnd.limit, a: a, b: b);
  }

  /// The first thing that differs about where the two machines are standing.
  /// The PC comes first because it is the one that reads as "they parted
  /// company"; a register difference with the same PC is the earlier cause
  /// of a PC difference that has not happened yet.
  Divergence? _difference(int step, {required bool full}) {
    if (a.cpu.halted != b.cpu.halted) {
      return _at(step, 'halted',
          '${a.name} ${_halt(a)}, ${b.name} ${_halt(b)}');
    }
    if (a.cpu.pc != b.cpu.pc) {
      return _at(step, 'pc', '${_h6(a.cpu.pc)} against ${_h6(b.cpu.pc)}');
    }
    if (!full) return null;
    for (var i = 0; i < 8; i++) {
      if (a.cpu.er[i] != b.cpu.er[i]) {
        return _at(step, 'er$i',
            '${_h8(a.cpu.er[i])} against ${_h8(b.cpu.er[i])}');
      }
    }
    if (a.cpu.ccr != b.cpu.ccr) {
      return _at(step, 'ccr',
          '${_ccr(a.cpu.ccr)} against ${_ccr(b.cpu.ccr)}');
    }
    return null;
  }

  /// Whether the instruction just executed wrote different things.
  Divergence? _writesDiffer(int step) {
    final newA = a.cpu.writeLog;
    final newB = b.cpu.writeLog;
    if (newA.isEmpty && newB.isEmpty) return null;
    if (newA.length != newB.length) {
      return _at(step, 'write',
          '${a.name} made ${newA.length} write(s) here, '
          '${b.name} made ${newB.length}');
    }
    for (var i = 0; i < newA.length; i++) {
      if (newA[i].addr != newB[i].addr || newA[i].value != newB[i].value) {
        return _at(step, 'write',
            "${a.name} wrote H'${_h2(newA[i].value)} to ${_h6(newA[i].addr)}, "
            "${b.name} wrote H'${_h2(newB[i].value)} to ${_h6(newB[i].addr)}");
      }
    }
    return null;
  }

  /// Compares what each side writes rather than how it gets there.
  ///
  /// Each side is run on its own until it produces its next write, so the
  /// two are lined up by write and not by instruction. A side that stops
  /// writing altogether is a divergence in itself.
  LockstepResult _runWrites() {
    var written = 0;
    var stepsA = 0, stepsB = 0;
    while (stepsA < config.maxSteps && stepsB < config.maxSteps) {
      final gotA = _runToNextWrite(a, () => stepsA++);
      final gotB = _runToNextWrite(b, () => stepsB++);

      if (!gotA || !gotB) {
        if (gotA == gotB) break; // both ran out: they agreed to the end
        final quiet = gotA ? b : a;
        final busy = gotA ? a : b;
        final next = busy.cpu.writeLog.removeAt(0);
        return LockstepResult(
          divergence: _at(
              written,
              'write',
              '${quiet.name} stopped writing; ${busy.name} went on to write '
              "H'${_h2(next.value)} to ${_h6(next.addr)} "
              'from ${_h6(next.pc)}'),
          steps: written,
          end: LockstepEnd.diverged,
          a: a,
          b: b,
        );
      }

      final wa = a.cpu.writeLog.removeAt(0);
      final wb = b.cpu.writeLog.removeAt(0);
      // The PC is not compared: the whole point is that the two sides are
      // allowed to reach the same write by different routes.
      if (wa.addr != wb.addr || wa.value != wb.value) {
        a.history.add((pc: wa.pc, cycles: wa.cycles));
        b.history.add((pc: wb.pc, cycles: wb.cycles));
        return LockstepResult(
          divergence: _at(
              written,
              'write',
              "${a.name} wrote H'${_h2(wa.value)} to ${_h6(wa.addr)} "
              'from ${_h6(wa.pc)}, '
              "${b.name} wrote H'${_h2(wb.value)} to ${_h6(wb.addr)} "
              'from ${_h6(wb.pc)}'),
          steps: written,
          end: LockstepEnd.diverged,
          a: a,
          b: b,
        );
      }
      a.history.add((pc: wa.pc, cycles: wa.cycles));
      b.history.add((pc: wb.pc, cycles: wb.cycles));
      if (a.history.length > config.historyDepth) {
        a.history.removeAt(0);
        b.history.removeAt(0);
      }
      written++;
    }
    return LockstepResult(
      divergence: null,
      steps: written,
      // Running out of instructions and both sides falling quiet are not the
      // same answer, and reading one as the other is how a comparison gets
      // believed further than it earned.
      end: (stepsA >= config.maxSteps || stepsB >= config.maxSteps)
          ? LockstepEnd.limit
          : LockstepEnd.bothQuiet,
      a: a,
      b: b,
    );
  }

  /// Runs [side] until its log holds a write, or until it has gone too far
  /// without one. True if there is a write waiting.
  bool _runToNextWrite(LockstepSide side, void Function() count) {
    var n = 0;
    while (side.cpu.writeLog.isEmpty) {
      if (n++ >= config.maxStepsBetweenWrites) return false;
      if (side.cpu.halted && !side.cpu.sleeping) return false;
      side.cpu.step();
      side.stepsRun++;
      count();
    }
    return true;
  }

  LockstepResult _stopped(Divergence d, int step) => LockstepResult(
      divergence: d,
      steps: step,
      end: LockstepEnd.diverged,
      a: a,
      b: b);

  Divergence _at(int step, String what, String detail) =>
      Divergence(step: step, what: what, detail: detail, a: a, b: b);

  String _halt(LockstepSide s) =>
      s.cpu.halted ? 'halted (${s.cpu.haltReason})' : 'still running';
}

/// The run-up and the parting, written out.
///
/// A step number on its own says a difference happened; this says what each
/// side was doing, which is the part that answers the question.
String describeDivergence(LockstepResult r,
    {Map<int, String> symbols = const {}, bool writes = false}) {
  final d = r.divergence;
  final out = StringBuffer();
  if (d == null) {
    final unit = writes ? 'write' : 'instruction';
    out.writeln('In step for all ${r.steps} $unit'
        '${r.steps == 1 ? '' : 's'} compared.');
    out.writeln(switch (r.end) {
      LockstepEnd.bothQuiet => 'Both sides then stopped writing, having '
          'agreed about everything either of them wrote '
          '(${r.a.stepsRun} and ${r.b.stepsRun} instructions run).',
      _ => 'The run reached its limit; they had not parted company by then, '
          'which is not the same as their agreeing for ever.',
    });
    return out.toString();
  }

  out.writeln('Parted company at step ${d.step}, on ${describeWhat(d)}');
  out.writeln('  ${d.detail}');
  out.writeln();
  for (final side in [r.a, r.b]) {
    out.writeln('${side.name}:');
    for (final h in side.history) {
      final dis = disassembleH8(side.cpu.peekBus, h.pc);
      final name = symbols[h.pc];
      out.writeln('  ${_h6(h.pc)}  ${dis.text.padRight(30)}'
          '${name == null ? '' : '  $name'}');
    }
    out.writeln('  registers: ${_registers(side.cpu)}');
    out.writeln();
  }
  return out.toString();
}

/// The differing thing, said in words rather than as a field name.
String describeWhat(Divergence d) => switch (d.what) {
      'pc' => 'the program counter',
      'ccr' => 'the flags',
      'write' => 'what was written',
      'halted' => 'one side stopping',
      _ => d.what,
    };

String _registers(H8Cpu cpu) {
  final parts = <String>[
    for (var i = 0; i < 8; i++) 'ER$i=${_h8(cpu.er[i])}',
    'CCR=${_ccr(cpu.ccr)}',
  ];
  return parts.join(' ');
}

String _ccr(int v) {
  const names = ['I', 'UI', 'H', 'U', 'N', 'Z', 'V', 'C'];
  final on = <String>[];
  for (var i = 0; i < 8; i++) {
    if ((v >> (7 - i)) & 1 == 1) on.add(names[i]);
  }
  return "H'${_h2(v)}${on.isEmpty ? '' : ' (${on.join(' ')})'}";
}

String _h2(int v) => (v & 0xFF).toRadixString(16).toUpperCase().padLeft(2, '0');
String _h6(int v) =>
    "H'${(v & 0xFFFFFF).toRadixString(16).toUpperCase().padLeft(6, '0')}";
String _h8(int v) =>
    (v & 0xFFFFFFFF).toRadixString(16).toUpperCase().padLeft(8, '0');
