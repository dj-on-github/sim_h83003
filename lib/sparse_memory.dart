// Sparse 16-Mbyte memory for the H8/3003 simulator.
//
// The H8/300H address space is 24 bits (16 Mbytes) — far too large to
// allocate flat the way the 6502 simulator's 64K was. Instead the space is
// divided into 256 banks of 64 Kbytes, and a bank's backing store is only
// allocated the first time something is written to it. Reads from
// unallocated banks return 0x00 without allocating.
//
// The UI uses [regions] to know which parts of the space actually hold data
// (the disassembler sweeps only those), and [isAllocated] to render
// unallocated rows dimmed.

import 'dart:typed_data';

class SparseMemory {
  /// Total simulated address space: 16 Mbytes (24-bit addresses).
  static const int size = 0x1000000;
  static const int addrMask = size - 1;

  /// Bank granularity: 64 Kbytes, 256 banks.
  static const int bankBits = 16;
  static const int bankSize = 1 << bankBits;
  static const int numBanks = size >> bankBits;

  final List<Uint8List?> _banks = List<Uint8List?>.filled(numBanks, null);

  /// Number of banks currently allocated (for display: memory footprint).
  int get allocatedBankCount {
    var n = 0;
    for (final b in _banks) {
      if (b != null) n++;
    }
    return n;
  }

  /// Reads one byte. Unallocated memory reads as 0x00.
  int peek(int addr) {
    final bank = _banks[(addr & addrMask) >> bankBits];
    return bank == null ? 0 : bank[addr & (bankSize - 1)];
  }

  /// When this is set, every byte about to be written is recorded here
  /// against the value it had first, so a caller can work out afterwards
  /// exactly what a run changed -- and put it back -- without scanning the
  /// whole address space. Only the *first* value seen for an address is
  /// kept, which is what makes it the state before the run rather than the
  /// state one write ago. Null, and nothing is recorded.
  Map<int, int>? undoLog;

  /// Writes one byte, allocating the containing 64K bank on first touch.
  void poke(int addr, int value) {
    addr &= addrMask;
    final bank = _banks[addr >> bankBits] ??= Uint8List(bankSize);
    final offset = addr & (bankSize - 1);
    final log = undoLog;
    if (log != null && !log.containsKey(addr)) log[addr] = bank[offset];
    bank[offset] = value & 0xFF;
  }

  /// True when the bank containing [addr] has been allocated.
  bool isAllocated(int addr) => _banks[(addr & addrMask) >> bankBits] != null;

  /// Ensures the bank containing [addr] exists (used to pre-allocate the
  /// on-chip RAM bank so it shows as real memory from the start).
  void allocate(int addr) {
    addr &= addrMask;
    _banks[addr >> bankBits] ??= Uint8List(bankSize);
  }

  /// The allocated portions of the address space as a list of
  /// (start, end-exclusive) byte ranges, with adjacent banks merged.
  List<(int, int)> regions() {
    final out = <(int, int)>[];
    int? runStart;
    for (var i = 0; i < numBanks; i++) {
      if (_banks[i] != null) {
        runStart ??= i << bankBits;
      } else if (runStart != null) {
        out.add((runStart, i << bankBits));
        runStart = null;
      }
    }
    if (runStart != null) out.add((runStart, size));
    return out;
  }

  /// Releases every bank (memory reads as zero again).
  void clear() {
    for (var i = 0; i < numBanks; i++) {
      _banks[i] = null;
    }
  }
}

/// Sparse per-address counters used by the profiler; same 64K banking scheme
/// as [SparseMemory] so profiling a small program doesn't cost 128 Mbytes of
/// flat Uint32 arrays.
class SparseCounters {
  final List<Uint32List?> _banks =
      List<Uint32List?>.filled(SparseMemory.numBanks, null);

  void bump(int addr) {
    addr &= SparseMemory.addrMask;
    final bank = _banks[addr >> SparseMemory.bankBits] ??=
        Uint32List(SparseMemory.bankSize);
    bank[addr & (SparseMemory.bankSize - 1)]++;
  }

  int at(int addr) {
    final bank = _banks[(addr & SparseMemory.addrMask) >> SparseMemory.bankBits];
    return bank == null ? 0 : bank[addr & (SparseMemory.bankSize - 1)];
  }

  /// All nonzero (address, count) entries, unordered.
  List<MapEntry<int, int>> nonZeroEntries() {
    final out = <MapEntry<int, int>>[];
    for (var i = 0; i < _banks.length; i++) {
      final bank = _banks[i];
      if (bank == null) continue;
      final base = i << SparseMemory.bankBits;
      for (var j = 0; j < bank.length; j++) {
        if (bank[j] != 0) out.add(MapEntry(base + j, bank[j]));
      }
    }
    return out;
  }

  void reset() {
    for (var i = 0; i < _banks.length; i++) {
      _banks[i] = null;
    }
  }
}
