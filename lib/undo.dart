/// Stepping backwards: what each instruction changed, kept so it can be
/// put back.
///
/// Every bug in this project has been found the same way -- run to the
/// fault, guess what to instrument, run again. Stepping back replaces the
/// guess with looking: stop at the fault and walk backwards through what
/// actually happened.
///
/// Most H8 instructions change one register and the flags, and touch no
/// memory at all, so an undo record is small. It is kept as flat words
/// rather than an object per instruction: an object per instruction costs
/// well over a hundred bytes and puts a ceiling of a few tens of thousands
/// of steps on a machine that takes twenty-five million to boot.
///
/// ## The peripherals
///
/// The journal alone puts back the registers, the flags, the cycle count
/// and memory. It cannot put back a peripheral: a timer counts into its own
/// counter rather than working it out from the clock, so winding the clock
/// back leaves the timer where it was. Undoing a run without dealing with
/// that gives a machine that looks right and then takes an interrupt that
/// never happened.
///
/// So the whole machine outside memory -- which is small, and which the CPU
/// already knows how to save -- is kept every [checkpointEvery]
/// instructions. A rewind goes back to the checkpoint at or before where it
/// is heading, puts that state back, and replays forward. Memory and the
/// registers come from the journal, the peripherals from the checkpoint,
/// and what comes out is the machine as it actually was.
///
/// A step is still marked [UndoStep.inexact] when it wrote to a peripheral,
/// for the case where there is no checkpoint to fall back on -- the history
/// having been trimmed past it.
library;

import 'dart:typed_data';

/// One instruction's changes, ready to be put back.
class UndoStep {
  const UndoStep({
    required this.pc,
    required this.ccr,
    required this.cycles,
    required this.halted,
    required this.sleeping,
    required this.inexact,
    required this.registers,
    required this.memory,
  });

  /// Where the machine was before the instruction ran.
  final int pc;
  final int ccr;

  /// What the cycle count was before it ran.
  final int cycles;

  final bool halted;
  final bool sleeping;

  /// True when the instruction touched a peripheral, so putting the CPU and
  /// memory back does not put the machine back.
  final bool inexact;

  /// Which registers changed, and what they held before. Index and value.
  final List<(int, int)> registers;

  /// Which memory bytes changed, and what they held before, in the order
  /// they were written.
  final List<(int, int)> memory;
}

/// The undo records, newest last.
class UndoJournal {
  UndoJournal({this.limit = 200000});

  /// How many instructions to keep. The oldest are dropped in batches once
  /// this is passed, so the journal holds a little under it.
  int limit;

  // The records, flat. Each one is:
  //
  //   w0  header: nRegs (0-3), nMem (4-19), ccr (20-27), halted (28),
  //               sleeping (29), inexact (30)
  //   w1  pc before
  //   w2  cycles before, low word
  //   w3  cycles before, high word
  //   then nRegs pairs of (index, value before)
  //   then nMem words of (addr << 8 | byte before)
  //
  // Cycles are kept whole rather than as a delta because the count runs
  // past what a single word holds within a few seconds of simulated time.
  Uint32List _words = Uint32List(4096);
  int _used = 0;

  /// Where each record starts in [_words], so the newest can be found
  /// without walking from the front.
  Uint32List _starts = Uint32List(1024);
  int _count = 0;

  /// How often to keep the machine's state outside memory. Small enough
  /// that a rewind replays only a few hundred instructions, big enough that
  /// keeping them is not the expensive part.
  int checkpointEvery = 128;

  /// Instructions recorded since the journal began, including ones since
  /// dropped: where the machine stands in its own history.
  int get position => _position;
  int _position = 0;

  final List<int> _cpPos = [];
  final List<List<int>> _cpState = [];

  /// The oldest instruction still in the journal.
  int get oldestPosition => _position - _count;

  /// How many instructions can be stepped back.
  int get length => _count;
  bool get isEmpty => _count == 0;
  bool get isNotEmpty => _count != 0;

  /// Roughly how much is being held, in bytes.
  int get bytesHeld => _used * 4 + _count * 4;

  void clear() {
    _used = 0;
    _count = 0;
    _openMem = 0;
    _open = false;
    _position = 0;
    _cpPos.clear();
    _cpState.clear();
  }

  // ---- the machine outside memory ---------------------------------------

  /// True when the next instruction is one whose state should be kept.
  bool get wantsCheckpoint =>
      checkpointEvery > 0 && _position % checkpointEvery == 0;

  /// Keeps the machine's state outside memory as it stands now.
  void addCheckpoint(List<int> state) {
    // A replay passes over its own starting point again; keeping a second
    // copy of it would leave two checkpoints claiming the same instruction.
    if (_cpPos.isNotEmpty && _cpPos.last == _position) return;
    _cpPos.add(_position);
    _cpState.add(state);
  }

  /// The newest kept state at or before [pos], or null when there is none
  /// the journal can still reach back to.
  (int, List<int>)? checkpointAtOrBefore(int pos) {
    final floor = oldestPosition;
    for (var i = _cpPos.length - 1; i >= 0; i--) {
      if (_cpPos[i] <= pos) {
        return _cpPos[i] < floor ? null : (_cpPos[i], _cpState[i]);
      }
    }
    return null;
  }

  /// Forgets the states kept after [pos], which a replay is about to make
  /// again.
  void dropCheckpointsAfter(int pos) {
    while (_cpPos.isNotEmpty && _cpPos.last > pos) {
      _cpPos.removeLast();
      _cpState.removeLast();
    }
  }

  void _pruneCheckpoints() {
    final floor = oldestPosition;
    var drop = 0;
    while (drop < _cpPos.length && _cpPos[drop] < floor) {
      drop++;
    }
    if (drop == 0) return;
    _cpPos.removeRange(0, drop);
    _cpState.removeRange(0, drop);
  }

  // ---- recording one instruction ----------------------------------------

  bool _open = false;
  int _pc = 0, _ccr = 0, _cycles = 0;
  bool _halted = false, _sleeping = false, _inexact = false;

  /// The bytes this instruction has changed so far, packed as they will be
  /// stored. A field rather than a local so that recording a write costs no
  /// allocation.
  Uint32List _mem = Uint32List(512);
  int _openMem = 0;

  /// The maximum number of bytes one instruction may change. EEPMOV.W can
  /// move 65535, which is the largest a single instruction can manage.
  static const int maxBytesPerStep = 0xFFFF;

  /// Starts recording the instruction about to run.
  void beginStep({
    required int pc,
    required int ccr,
    required int cycles,
    required bool halted,
    required bool sleeping,
  }) {
    _pc = pc;
    _ccr = ccr;
    _cycles = cycles;
    _halted = halted;
    _sleeping = sleeping;
    _inexact = false;
    _openMem = 0;
    _open = true;
  }

  /// Records a byte about to be overwritten. [was] is what the plain memory
  /// held: a device-backed address has nothing there to put back, which is
  /// what [noteDevice] is for.
  void noteWrite(int addr, int was) {
    if (!_open) return;
    if (_openMem >= maxBytesPerStep) {
      // More than one instruction can change, and more than the count field
      // holds. Rather than keep a record that is not the whole story, give
      // up on the history: a gap in it is worse than not having it, because
      // stepping back through a gap lands silently somewhere that never
      // happened.
      clear();
      return;
    }
    if (_openMem == _mem.length) {
      _mem = Uint32List(_mem.length * 2)..setRange(0, _openMem, _mem);
    }
    _mem[_openMem++] = ((addr & 0xFFFFFF) << 8) | (was & 0xFF);
  }

  /// Records that a peripheral was written, so this step cannot be put back
  /// exactly.
  void noteDevice() => _inexact = true;

  /// Finishes the record. [before] and [after] are the register file either
  /// side of the instruction; only what changed is kept.
  void endStep(Uint32List before, Uint32List after) {
    if (!_open) return;
    _open = false;
    if (limit <= 0) return;

    var nRegs = 0;
    for (var i = 0; i < 8; i++) {
      if (before[i] != after[i]) nRegs++;
    }
    final need = 4 + nRegs * 2 + _openMem;
    _reserve(need);

    if (_count == _starts.length) {
      _starts = Uint32List(_starts.length * 2)..setRange(0, _count, _starts);
    }
    _starts[_count++] = _used;

    _words[_used++] = nRegs |
        (_openMem << 4) |
        (_ccr << 20) |
        (_halted ? 1 << 28 : 0) |
        (_sleeping ? 1 << 29 : 0) |
        (_inexact ? 1 << 30 : 0);
    _words[_used++] = _pc;
    _words[_used++] = _cycles & 0xFFFFFFFF;
    _words[_used++] = (_cycles ~/ 0x100000000) & 0xFFFFFFFF;
    for (var i = 0; i < 8; i++) {
      if (before[i] != after[i]) {
        _words[_used++] = i;
        _words[_used++] = before[i];
      }
    }
    for (var i = 0; i < _openMem; i++) {
      _words[_used++] = _mem[i];
    }
    _position++;

    if (_count > limit) {
      _dropOldest();
      _pruneCheckpoints();
    }
  }

  /// Abandons the record being built, leaving what is already held alone.
  /// Used when an instruction did not in fact run.
  void abandonStep() {
    _open = false;
    _openMem = 0;
  }

  // ---- stepping back -----------------------------------------------------

  /// Takes the newest record off, or null when there is nothing to undo.
  UndoStep? pop() {
    if (_count == 0) return null;
    _position--;
    var at = _starts[--_count];
    final header = _words[at++];
    final nRegs = header & 0xF;
    final nMem = (header >> 4) & 0xFFFF;
    final pc = _words[at++];
    // Written out rather than left to argument order: the two halves going
    // the wrong way round would be a cycle count off by four billion.
    final cyclesLow = _words[at++];
    final cyclesHigh = _words[at++];
    final cycles = cyclesLow + cyclesHigh * 0x100000000;

    final registers = <(int, int)>[];
    for (var i = 0; i < nRegs; i++) {
      final index = _words[at++];
      registers.add((index, _words[at++]));
    }
    final memory = <(int, int)>[];
    for (var i = 0; i < nMem; i++) {
      final packed = _words[at++];
      memory.add(((packed >> 8) & 0xFFFFFF, packed & 0xFF));
    }

    _used = _starts[_count];
    return UndoStep(
      pc: pc,
      ccr: (header >> 20) & 0xFF,
      cycles: cycles,
      halted: (header >> 28) & 1 == 1,
      sleeping: (header >> 29) & 1 == 1,
      inexact: (header >> 30) & 1 == 1,
      registers: registers,
      memory: memory,
    );
  }

  /// Whether the newest record can be put back exactly, without taking it.
  /// Null when there is nothing to undo.
  bool? get newestIsExact =>
      _count == 0 ? null : (_words[_starts[_count - 1]] >> 30) & 1 == 0;

  // ---- keeping it to size ------------------------------------------------

  void _reserve(int words) {
    if (_used + words <= _words.length) return;
    var size = _words.length;
    while (size < _used + words) {
      size *= 2;
    }
    _words = Uint32List(size)..setRange(0, _used, _words);
  }

  /// Drops the oldest eighth in one go. One at a time would mean moving the
  /// whole journal down on every instruction once it was full.
  void _dropOldest() {
    final drop = (limit ~/ 8).clamp(1, _count);
    if (drop >= _count) {
      // A limit small enough that a batch is the whole journal.
      _used = 0;
      _count = 0;
      return;
    }
    final from = _starts[drop];
    _words.setRange(0, _used - from, _words, from);
    _used -= from;
    for (var i = 0; i < _count - drop; i++) {
      _starts[i] = _starts[i + drop] - from;
    }
    _count -= drop;
  }
}
