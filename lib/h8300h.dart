// An emulation of the Hitachi H8/3003 microcontroller's H8/300H CPU core.
//
// Implements the full H8/300H instruction set in advanced mode (the only
// CPU mode the H8/3003 supports): eight 32-bit general registers ER0-ER7
// (ER7 doubling as the stack pointer), a 24-bit program counter, and the
// CCR (I UI H U N Z V C). Memory is the sparse 16-Mbyte model in
// sparse_memory.dart. Reference: Hitachi H8/3003 Hardware Manual
// (REN_e602055_h83003), section 2 (CPU) and appendix A (instruction set).
//
// Cycle counts are the "advanced mode" state counts from table A-1, which
// assume code and operands in on-chip memory (two-state 16-bit accesses).
//
// The class is deliberately UI-agnostic: the Flutter code instantiates it,
// drives it with [step]/[reset]/[nmi]/[irq], and reads the registers and
// memory to render its views.

import 'dart:typed_data';

import 'flash.dart';
import 'adc.dart';
import 'dmac.dart';
import 'h8disasm.dart';
import 'i2c_eeprom.dart';
import 'itu.dart';
import 'sci.dart';
import 'sparse_memory.dart';
import 'keypad.dart';
import 'undo.dart';
import 'condition.dart';

/// CCR flag bit masks (I UI H U N Z V C).
class H8Flag {
  static const int i = 0x80; // interrupt mask
  static const int ui = 0x40; // user bit / interrupt mask
  static const int h = 0x20; // half-carry
  static const int u = 0x10; // user bit
  static const int n = 0x08; // negative
  static const int z = 0x04; // zero
  static const int v = 0x02; // overflow
  static const int c = 0x01; // carry
}

/// A latch of digital inputs mapped into the address space rather than onto
/// the CPU's own port pins.
///
/// The artista 180 has one at H'080000: reading any address in the window
/// returns the same byte, because the board decodes the window without the
/// low address bits. The firmware never writes to it.
///
/// [value] is an override. While it is null the window reads out of memory,
/// which for a full memory dump means the byte the machine was holding when
/// the dump was taken — so nothing changes until an input is driven.
/// One write the machine made to a watched address.
///
/// The breakpoint answers "stop here"; this answers "who did that", which is
/// the question that actually comes up. The PC is the instruction's own
/// address, not wherever the fetch had reached.
class WriteRecord {
  const WriteRecord({
    required this.cycles,
    required this.pc,
    required this.addr,
    required this.value,
    required this.was,
  });

  final int cycles;
  final int pc;
  final int addr;
  final int value;

  /// What was there before, so a write that changed nothing is visible as
  /// such rather than looking like a change.
  final int was;

  bool get changed => value != was;
}

class ExternalInputs {
  ExternalInputs({
    required this.name,
    required this.base,
    required this.size,
  });

  final String name;
  final int base;
  final int size;

  int? value;

  bool owns(int addr) => addr >= base && addr < base + size;
  bool get driven => value != null;
}

/// Describes one of the H8/3003's I/O ports (hardware manual section 9,
/// mode 3/4 register addresses). Each port has a data direction register
/// (DDR, write-only on real hardware) and a data register (DR); port 7 is
/// input-only and has no DDR. [pinMask] marks the bits that have pins
/// (port 5's four pins are P57-P54, i.e. the high nibble).
class H8Port {
  const H8Port({
    required this.name,
    required this.pinMask,
    required this.drAddr,
    this.ddrAddr,
    this.ddrReset = 0,
    this.drReset = 0,
  });

  /// Port letter/number as shown in the manual ('4'..'9', 'A'..'C').
  final String name;

  /// Which of the 8 bits have pins.
  final int pinMask;

  /// Data register address (R/W).
  final int drAddr;

  /// Data direction register address, or null for the input-only port 7.
  final int? ddrAddr;

  /// Register values established by a reset (mode 3/4 column).
  final int ddrReset;
  final int drReset;

  bool get inputOnly => ddrAddr == null;
}

class H8Cpu {
  H8Cpu() {
    // Register values are mirrored into memory so the memory view and the
    // disassembler show what the CPU would read.
    for (final s in sci) {
      s.mirror = mem.poke;
      s.syncToMemory();
    }
    itu.mirror = mem.poke;
    itu.syncToMemory();
    adc.mirror = mem.poke;
    adc.syncToMemory();
    _wireDmac();
  }

  /// Connects the DMA controller to the bus and to the peripherals whose
  /// interrupts activate it.
  void _wireDmac() {
    dmac.mirror = mem.poke;
    dmac.readByte = readB;
    dmac.writeByte = writeB;
    dmac.ituMatchA = (ch) => (itu.channels[ch].tsr & ItuStatus.imfa) != 0;
    dmac.clearItuMatchA = (ch) {
      itu.channels[ch].tsr &= ~ItuStatus.imfa;
    };
    dmac.sciReady = (ioAddr) {
      for (final s in sci) {
        if (ioAddr == s.tdrAddr) return (s.ssr & SciStatus.tdre) != 0;
        if (ioAddr == s.rdrAddr) return (s.ssr & SciStatus.rdrf) != 0;
      }
      return false;
    };
    dmac.sciAcknowledge = (ioAddr) {
      // The DMAC writing TDR clears TDRE; reading RDR clears RDRF.
      for (final s in sci) {
        if (ioAddr == s.tdrAddr) s.ssr &= ~SciStatus.tdre;
        if (ioAddr == s.rdrAddr) s.ssr &= ~SciStatus.rdrf;
      }
    };
    dmac.syncToMemory();
  }

  /// Everything the machine holds outside its memory: the registers, the
  /// flags, the cycle count and the four peripheral models. Packed as plain
  /// integers so that a caller can put the machine back exactly where it was
  /// without booting it again -- which is what the routine comparer does
  /// between one case and the next.
  ///
  /// Memory is *not* included: it is restored from the memory's own undo log,
  /// which is cheaper than copying four megabytes.
  List<int> saveState() {
    final out = <int>[...er, pc, ccr, cycles, halted ? 1 : 0, sleeping ? 1 : 0];
    for (final s in sci) {
      final v = s.saveState();
      out..add(v.length)..addAll(v);
    }
    final i = itu.saveState();
    out..add(i.length)..addAll(i);
    final a = adc.saveState();
    out..add(a.length)..addAll(a);
    final d = dmac.saveState();
    out..add(d.length)..addAll(d);
    return out;
  }

  void restoreState(List<int> v) {
    for (var n = 0; n < 8; n++) {
      er[n] = v[n];
    }
    pc = v[8];
    ccr = v[9];
    cycles = v[10];
    halted = v[11] != 0;
    sleeping = v[12] != 0;
    var i = 13;
    for (final s in sci) {
      s.restoreState(v.sublist(i + 1, i + 1 + v[i]));
      i += 1 + v[i];
    }
    itu.restoreState(v.sublist(i + 1, i + 1 + v[i]));
    i += 1 + v[i];
    adc.restoreState(v.sublist(i + 1, i + 1 + v[i]));
    i += 1 + v[i];
    dmac.restoreState(v.sublist(i + 1, i + 1 + v[i]));
  }

  /// Sparse 16-Mbyte memory (24-bit address space).
  final SparseMemory mem = SparseMemory();

  /// The two on-chip serial channels (manual section 13). Their registers
  /// live at H'FFFFB0-H'FFFFB5 and H'FFFFB8-H'FFFFBD, and their exception
  /// vectors are ERI/RXI/TXI/TEI at 52-55 and 56-59.
  final List<SciChannel> sci = [
    SciChannel(name: 'SCI0', base: 0xFFFFB0, vectorBase: 52),
    SciChannel(name: 'SCI1', base: 0xFFFFB8, vectorBase: 56),
  ];

  SciChannel get sci0 => sci[0];
  SciChannel get sci1 => sci[1];

  /// The 16-bit integrated timer unit (manual section 10), five channels at
  /// H'FFFF60-H'FFFF9F.
  final Itu itu = Itu();

  /// The DMA controller (manual section 8), four channels at
  /// H'FFFF20-H'FFFF5F.
  final Dmac dmac = Dmac();

  /// The A/D converter (manual section 14), eight inputs at
  /// H'FFFFE0-H'FFFFE9.
  final AdConverter adc = AdConverter();

  /// The nine I/O ports of the H8/3003 (table 9-1; addresses are the
  /// mode 3/4 locations in the on-chip register area).
  static const List<H8Port> ports = [
    H8Port(name: '4', pinMask: 0xFF, ddrAddr: 0xFFFFC5, drAddr: 0xFFFFC7),
    // In modes 3/4 port 5 outputs A23-A20 and its DDR bits are fixed at 1.
    H8Port(
        name: '5',
        pinMask: 0xF0,
        ddrAddr: 0xFFFFC8,
        drAddr: 0xFFFFCA,
        ddrReset: 0xFF),
    H8Port(
        name: '6',
        pinMask: 0x07,
        ddrAddr: 0xFFFFC9,
        drAddr: 0xFFFFCB,
        ddrReset: 0x80,
        drReset: 0x80),
    H8Port(name: '7', pinMask: 0xFF, drAddr: 0xFFFFCE), // input only
    H8Port(
        name: '8',
        pinMask: 0x1F,
        ddrAddr: 0xFFFFCD,
        drAddr: 0xFFFFCF,
        ddrReset: 0xF0, // P84/CS0 defaults to output
        drReset: 0xE0),
    H8Port(
        name: '9',
        pinMask: 0x3F,
        ddrAddr: 0xFFFFD0,
        drAddr: 0xFFFFD2,
        ddrReset: 0xC0,
        drReset: 0xC0),
    H8Port(name: 'A', pinMask: 0xFF, ddrAddr: 0xFFFFD1, drAddr: 0xFFFFD3),
    H8Port(name: 'B', pinMask: 0xFF, ddrAddr: 0xFFFFD4, drAddr: 0xFFFFD6),
    H8Port(name: 'C', pinMask: 0xFF, ddrAddr: 0xFFFFD5, drAddr: 0xFFFFD7),
  ];

  /// Latches of digital inputs mapped into the address space. Reading one
  /// returns [ExternalInputs.value] when it has been driven, and whatever
  /// memory holds otherwise.
  /// Flash devices on the external bus. Empty by default: with nothing
  /// attached the address space is plain memory, which is how every tool
  /// behaved before this existed. Attach one to make a region answer the
  /// JEDEC command sequences the boot ROM's programming routines use.
  final List<JedecFlash> flash = [];

  /// Puts a flash device on the bus, backed by this CPU's memory.
  void attachFlash(JedecFlash f) {
    f.peek = mem.peek;
    f.poke = mem.poke;
    flash.add(f);
  }

  /// The serial EEPROM the artista 180 bit-bangs on port 4, when one is
  /// attached. Null by default: with nothing there the port behaves as it
  /// always did, and the firmware's writes go nowhere.
  I2cEeprom? eeprom;

  /// Puts an EEPROM on the two port pins it listens to. It reads the port
  /// registers as the CPU last wrote them -- the data register holds what
  /// the master is driving, not what the pin is at -- and holds SDA through
  /// the same external-pin layer a switch or a sensor would use.
  void attachEeprom(I2cEeprom e) {
    e.peek = mem.peek;
    e.hold = setPin;
    e.float = releasePin;
    eeprom = e;
    e.attach();
  }

  /// Takes it off again, letting SDA float back to the data register.
  void detachEeprom() {
    eeprom?.detach();
    eeprom = null;
  }

  /// The front panel, when one is attached. Null by default, which leaves
  /// the key latch reading as plain memory and the knob pins floating -- how
  /// every tool behaved before this existed.
  Keypad? keypad;

  /// Puts the panel on the bus and on its port pins. It works out the strobe
  /// state from the port registers itself, so it needs to read memory, and
  /// it drives the knob pairs and the reverse key through the same external
  /// pin layer a switch would use.
  void attachKeypad(Keypad k) {
    k.peek = mem.peek;
    k.hold = setPin;
    k.release = releasePin;
    keypad = k;
    k.driveKnobs();
  }

  void detachKeypad() {
    keypad?.releaseAll();
    keypad = null;
  }

  final List<ExternalInputs> externalInputs = [
    ExternalInputs(name: 'digital inputs', base: 0x080000, size: 0x020000),
  ];

  /// The latch that answers for [addr], if any.
  ExternalInputs? externalInputFor(int addr) {
    for (final e in externalInputs) {
      if (e.owns(addr)) return e;
    }
    return null;
  }

  /// Ports indexed by data register address, for the bus decode.
  static final Map<int, H8Port> portByDr = {
    for (final p in ports) p.drAddr: p,
  };

  // ---- external pin levels ---------------------------------------------
  // A port's data register on its own cannot represent an input: reading it
  // just hands back whatever was last written, so nothing outside the CPU
  // can ever be seen. These two maps add the missing layer — the level the
  // outside world is holding a pin at, and which pins it is holding.
  //
  // Only bits that are BOTH driven here and configured as inputs by the DDR
  // are substituted; everything else reads back the data register exactly as
  // before. Firmware that writes a port and reads it back is therefore
  // unaffected until a pin is deliberately driven.

  /// Level the outside world holds each pin at, by data register address.
  final Map<int, int> pinLevel = {};

  /// Address ranges whose writes are recorded, as (first, last) inclusive.
  /// Empty means record nothing, which costs one test per write.
  final List<(int, int)> writeWatches = [];

  /// The writes that have landed in those ranges, oldest first. Capped at
  /// [writeLogLimit]: a watch left on a busy address would otherwise grow
  /// without end.
  final List<WriteRecord> writeLog = [];

  /// The most the log will hold. It is a cap, not a size: the oldest are
  /// dropped in batches, so the log sits a little under this while a busy
  /// address is watched.
  int writeLogLimit = 5000;

  /// Forgets what has been recorded, leaving the watches armed.
  void clearWriteLog() => writeLog.clear();

  /// Stop as soon as this holds. Checked before each instruction while it is
  /// set, and nothing at all when it is not.
  Condition? stopWhen;

  /// What the armed condition works out to right now. Null when none is
  /// armed; throws the same way evaluation does, so the caller can say that
  /// a condition could not be worked out rather than showing a wrong number.
  int? conditionValue() {
    final c = stopWhen;
    return c?.value(_conditionView);
  }

  /// Set to keep an undo record per instruction, so the machine can be
  /// stepped backwards. Null -- the default -- records nothing and costs a
  /// null test per instruction.
  ///
  /// What comes back is the registers, the flags, the cycle count and
  /// memory. Not the peripherals: see [UndoJournal]. A step that touched
  /// one is marked, so an approximate rewind can be told from an exact one.
  UndoJournal? undo;

  /// The register file as it was before the instruction now running, so
  /// only what changed has to be recorded. A field, not a local, so that
  /// recording costs no allocation per instruction.
  final Uint32List _undoBefore = Uint32List(8);

  /// Puts the last instruction back. False when there is nothing to undo.
  ///
  /// The journal gives back the registers, the flags and memory. It cannot
  /// give back a peripheral -- a timer counts into its own counter rather
  /// than working it out from the clock -- so the machine is wound back to
  /// the last kept state at or before where it is going, that state is put
  /// back, and the few instructions in between are run again. What comes
  /// out is the machine as it actually was, peripherals and all.
  ///
  /// Without a checkpoint to reach (the history having been trimmed past
  /// it) the journal is used on its own, which is exact for everything but
  /// the peripherals. [canStepBackExactly] says which of the two it will be.
  bool stepBack() {
    final j = undo;
    if (j == null || j.isEmpty) return false;
    final target = j.position - 1;
    final mark = j.checkpointAtOrBefore(target);
    if (mark == null) {
      _applyUndo(j.pop()!);
      return true;
    }

    final (from, state) = mark;
    while (j.position > from) {
      _applyUndo(j.pop()!);
    }
    restoreState(state);
    j.dropCheckpointsAfter(from);
    _replay(target - from);
    return true;
  }

  /// Runs [count] instructions again over ground already covered.
  ///
  /// A replay is not a run: it must not stop on a condition, must not count
  /// towards the profile, and must not put the writes it makes into the
  /// write history a second time -- they are already in it from the first
  /// time round.
  void _replay(int count) {
    final heldCondition = stopWhen;
    final heldProfiling = profiling;
    final heldWrites = writeLog.length;
    stopWhen = null;
    profiling = false;
    for (var i = 0; i < count; i++) {
      step();
    }
    stopWhen = heldCondition;
    profiling = heldProfiling;
    if (writeLog.length > heldWrites) {
      writeLog.removeRange(heldWrites, writeLog.length);
    }
    conditionHit = false;
    clearBreakHit();
  }

  /// Puts one instruction's changes back.
  ///
  /// Memory goes back to front: an address written twice by one instruction
  /// has to end up holding what it held before the first of them, not what
  /// it held between the two.
  void _applyUndo(UndoStep u) {
    for (var i = u.memory.length - 1; i >= 0; i--) {
      mem.poke(u.memory[i].$1, u.memory[i].$2);
    }
    for (final (index, value) in u.registers) {
      er[index] = value;
    }
    pc = u.pc;
    _instrPc = u.pc;
    ccr = u.ccr;
    cycles = u.cycles;
    halted = u.halted;
    sleeping = u.sleeping;
    if (!halted) haltReason = '';
    clearBreakHit();
    conditionHit = false;
  }

  /// Whether the last instruction can be put back exactly -- peripherals
  /// and all. Null when there is nothing to put back.
  ///
  /// True when there is a kept state to wind back to, or when the step
  /// touched no peripheral and so needs none.
  bool? get canStepBackExactly {
    final j = undo;
    if (j == null || j.isEmpty) return null;
    if (j.checkpointAtOrBefore(j.position - 1) != null) return true;
    return j.newestIsExact;
  }

  /// Runs one instruction without asking the condition.
  ///
  /// This is what resuming from a condition stop means. The stop happens
  /// before the instruction, so the instruction it stopped on has not run
  /// yet; asking again before it runs would stop on it for ever. The
  /// instruction breakpoint gets the same allowance when Run is pressed.
  int stepPastCondition() {
    final held = stopWhen;
    stopWhen = null;
    conditionHit = false;
    final n = step();
    stopWhen = held;
    return n;
  }

  /// Set when [stopWhen] has held. The caller clears it.
  bool conditionHit = false;

  late final MachineView _conditionView = _CpuView(this);

  /// True when [addr] is inside a watched range.
  bool _watched(int addr) {
    for (final (first, last) in writeWatches) {
      if (addr >= first && addr <= last) return true;
    }
    return false;
  }

  /// Which pins the outside world is driving, by data register address.
  final Map<int, int> pinDriven = {};

  /// Value the CPU sees when it reads [p]'s data register.
  int portRead(H8Port p) {
    // The knobs hang off port C, and only turn when the CPU looks at them:
    // see Keypad.pollKnobs for why they are paced rather than free-running.
    if (p.drAddr == Keypad.pcDr) keypad?.pollKnobs(cycles);
    final dr = mem.peek(p.drAddr);
    final driven = (pinDriven[p.drAddr] ?? 0) & p.pinMask;
    if (driven == 0) return dr;
    // Port 7 has no DDR and is input-only, so every pin counts as an input.
    final ddr = p.ddrAddr == null ? 0 : mem.peek(p.ddrAddr!);
    final substituted = driven & ~ddr;
    if (substituted == 0) return dr;
    return (dr & ~substituted) | ((pinLevel[p.drAddr] ?? 0) & substituted);
  }

  /// Holds one pin high or low from outside the chip.
  void setPin(int drAddr, int bit, bool high) {
    final mask = 1 << (bit & 7);
    pinDriven[drAddr] = (pinDriven[drAddr] ?? 0) | mask;
    final level = pinLevel[drAddr] ?? 0;
    pinLevel[drAddr] = high ? (level | mask) : (level & ~mask);
  }

  /// Lets a pin float again, so it reads back the data register as before.
  void releasePin(int drAddr, int bit) {
    final mask = 1 << (bit & 7);
    pinDriven[drAddr] = (pinDriven[drAddr] ?? 0) & ~mask;
    pinLevel[drAddr] = (pinLevel[drAddr] ?? 0) & ~mask;
  }

  bool pinIsDriven(int drAddr, int bit) =>
      ((pinDriven[drAddr] ?? 0) >> (bit & 7)) & 1 == 1;

  bool pinIsHigh(int drAddr, int bit) =>
      ((pinLevel[drAddr] ?? 0) >> (bit & 7)) & 1 == 1;

  /// Releases every pin. Not called by [reset]: a switch keeps its position
  /// when the CPU is reset.
  void releaseAllPins() {
    pinDriven.clear();
    pinLevel.clear();
  }

  /// General registers ER0-ER7. ER7 is the stack pointer.
  final Uint32List er = Uint32List(8);

  /// 24-bit program counter.
  int pc = 0;

  /// Condition code register (I UI H U N Z V C).
  int ccr = H8Flag.i;

  /// Total elapsed states (clock cycles) since the last reset.
  int cycles = 0;

  /// True when execution has stopped: SLEEP instruction (resumable by an
  /// interrupt) or an illegal instruction (resumable only by reset).
  bool halted = false;

  /// Why the CPU halted ('SLEEP' or 'Illegal instruction ...').
  String haltReason = '';

  /// True when halted by SLEEP specifically — interrupts wake the CPU.
  bool sleeping = false;

  // ---------------------------------------------------------------------
  // Breakpoints (same contract as the 6502 simulator):
  //   instrBreaks - the Run loop pauses when the PC reaches one of these.
  //   dataBreaks  - [breakHit] is set when the CPU reads or writes one.
  // ---------------------------------------------------------------------
  final Set<int> instrBreaks = <int>{};
  final Set<int> dataBreaks = <int>{};
  bool breakHit = false;

  /// When [breakHit] is set: which address was touched, and the address of
  /// the instruction that touched it. A data breakpoint stops *after* the
  /// access completes, so the PC has already moved past that instruction —
  /// without these the user is left looking at the wrong one.
  int? breakAddr;
  int? breakPc;

  /// Address of the instruction currently executing, for [breakPc].
  int _instrPc = 0;

  // ---------------------------------------------------------------------
  // Profiling. Instruction fetches are *not* counted as data accesses (so
  // the data profile shows real operand traffic); each executed
  // instruction bumps instrExecCount at its address.
  // ---------------------------------------------------------------------
  bool profiling = false;
  final SparseCounters dataAccessCount = SparseCounters();
  final SparseCounters instrExecCount = SparseCounters();

  void resetProfile() {
    dataAccessCount.reset();
    instrExecCount.reset();
  }

  // ---------------------------------------------------------------------
  // Register access helpers.
  //
  // 4-bit register fields in instruction encodings:
  //   byte size: 0-7 = R0H-R7H, 8-15 = R0L-R7L
  //   word size: 0-7 = R0-R7,   8-15 = E0-E7
  //   long size: 3-bit field 0-7 = ER0-ER7
  // ---------------------------------------------------------------------

  int rd8(int n) =>
      n < 8 ? (er[n] >> 8) & 0xFF : er[n & 7] & 0xFF;

  void wr8(int n, int v) {
    v &= 0xFF;
    if (n < 8) {
      er[n] = (er[n] & 0xFFFF00FF) | (v << 8);
    } else {
      er[n & 7] = (er[n & 7] & 0xFFFFFF00) | v;
    }
  }

  int rd16(int n) =>
      n < 8 ? er[n] & 0xFFFF : (er[n & 7] >> 16) & 0xFFFF;

  void wr16(int n, int v) {
    v &= 0xFFFF;
    if (n < 8) {
      er[n] = (er[n] & 0xFFFF0000) | v;
    } else {
      er[n & 7] = (er[n & 7] & 0x0000FFFF) | (v << 16);
    }
  }

  int rd32(int n) => er[n & 7];

  void wr32(int n, int v) => er[n & 7] = v & 0xFFFFFFFF;

  // ---------------------------------------------------------------------
  // Bus access. Word and longword accesses force an even address (the
  // H8/300H ignores the least significant address bit; no address error).
  // ---------------------------------------------------------------------

  /// Records a data-breakpoint hit. The first address of an instruction
  /// wins, so a word access that straddles two watched bytes reports the one
  /// the programmer is most likely thinking of.
  void _noteBreak(int addr) {
    if (!breakHit) {
      breakAddr = addr;
      breakPc = _instrPc;
    }
    breakHit = true;
  }

  int readB(int addr) {
    addr &= 0xFFFFFF;
    if (profiling) dataAccessCount.bump(addr);
    if (dataBreaks.isNotEmpty && dataBreaks.contains(addr)) _noteBreak(addr);
    // On-chip peripherals answer for their own registers.
    if (addr >= 0xFFFFB0 && addr <= 0xFFFFBD) {
      for (final s in sci) {
        if (s.owns(addr)) return s.read(addr);
      }
    }
    if (itu.owns(addr)) return itu.read(addr);
    if (dmac.owns(addr)) return dmac.read(addr);
    if (adc.owns(addr)) return adc.read(addr);
    final port = portByDr[addr];
    if (port != null) return portRead(port);
    final kp = keypad;
    if (kp != null && kp.owns(addr)) return kp.read(addr);
    for (final e in externalInputs) {
      if (e.owns(addr)) {
        final v = e.value;
        if (v != null) return v;
        break;
      }
    }
    for (final f in flash) {
      if (f.owns(addr)) return f.read(addr);
    }
    return mem.peek(addr);
  }

  void writeB(int addr, int value) {
    // Recorded before the write, so the log can say what was displaced. Only
    // when something is watching: an empty list costs one test.
    if (writeWatches.isNotEmpty) {
      final a = addr & 0xFFFFFF;
      if (_watched(a)) {
        writeLog.add(WriteRecord(
          cycles: cycles,
          pc: _instrPc,
          addr: a,
          value: value & 0xFF,
          // peekBus, not readB: readB is the CPU's own read path, and a
          // watch on a receive register would eat the byte it was there to
          // watch. Looking is what the memory view does too.
          was: peekBus(a),
        ));
        // Dropped a batch at a time. removeAt(0) is O(n), so a watch on a
        // busy address would spend the whole run shuffling the list; the
        // log still never holds more than writeLogLimit.
        if (writeLog.length > writeLogLimit) {
          writeLog.removeRange(0, (writeLogLimit ~/ 8).clamp(1, writeLog.length));
        }
      }
    }
    addr &= 0xFFFFFF;
    // The byte as plain memory holds it, before it goes. A device-backed
    // address has nothing there to put back, which is what noteDevice is
    // for -- the marks are set here, next to the writes they describe, so
    // that they cannot drift away from them.
    undo?.noteWrite(addr, mem.peek(addr));
    if (profiling) dataAccessCount.bump(addr);
    if (dataBreaks.isNotEmpty && dataBreaks.contains(addr)) _noteBreak(addr);
    if (addr >= 0xFFFFB0 && addr <= 0xFFFFBD) {
      for (final s in sci) {
        if (s.owns(addr)) {
          undo?.noteDevice();
          s.write(addr, value);
          return;
        }
      }
    }
    if (itu.owns(addr)) {
      undo?.noteDevice();
      itu.write(addr, value);
      return;
    }
    if (dmac.owns(addr)) {
      undo?.noteDevice();
      dmac.write(addr, value);
      return;
    }
    if (adc.owns(addr)) {
      undo?.noteDevice();
      adc.write(addr, value);
      return;
    }
    for (final f in flash) {
      if (f.owns(addr)) {
        undo?.noteDevice();
        f.write(addr, value);
        return;
      }
    }
    mem.poke(addr, value);
    // A bit-banged bus has no controller to notice an edge, so the device
    // has to be told whenever either of the two registers it watches moves.
    final e = eeprom;
    if (e != null && (addr == e.drAddr || addr == e.ddrAddr)) {
      undo?.noteDevice();
      e.portWritten();
    }
  }

  int readW(int addr) {
    addr &= 0xFFFFFE;
    return (readB(addr) << 8) | readB(addr + 1);
  }

  void writeW(int addr, int value) {
    addr &= 0xFFFFFE;
    writeB(addr, (value >> 8) & 0xFF);
    writeB(addr + 1, value & 0xFF);
  }

  int readL(int addr) {
    addr &= 0xFFFFFE;
    return (readW(addr) << 16) | readW(addr + 2);
  }

  void writeL(int addr, int value) {
    addr &= 0xFFFFFE;
    writeW(addr, (value >> 16) & 0xFFFF);
    writeW(addr + 2, value & 0xFFFF);
  }

  /// Instruction fetch: reads the word at PC and advances. Not counted in
  /// the data-access profile and does not trigger data breakpoints.
  int _fetchW() {
    final a = pc & 0xFFFFFE;
    final w = (mem.peek(a) << 8) | mem.peek(a + 1);
    pc = (pc + 2) & 0xFFFFFF;
    return w;
  }

  // ---------------------------------------------------------------------
  // Flag helpers
  // ---------------------------------------------------------------------

  bool getFlag(int mask) => (ccr & mask) != 0;

  void setFlag(int mask, bool value) {
    if (value) {
      ccr |= mask;
    } else {
      ccr &= ~mask;
    }
  }

  /// Sets N and Z from [val] (of [bits] width) and clears V. Used by MOV,
  /// logic operations, and EXTS. C is unchanged.
  void _setNZV0(int val, int bits) {
    final sign = 1 << (bits - 1);
    ccr = (ccr & ~(H8Flag.n | H8Flag.z | H8Flag.v)) |
        ((val & sign) != 0 ? H8Flag.n : 0) |
        (val == 0 ? H8Flag.z : 0);
  }

  /// Sets N and Z only (V and C unchanged) — MULXS semantics.
  void _setNZ(int val, int bits) {
    final sign = 1 << (bits - 1);
    ccr = (ccr & ~(H8Flag.n | H8Flag.z)) |
        ((val & sign) != 0 ? H8Flag.n : 0) |
        (val == 0 ? H8Flag.z : 0);
  }

  /// Add/subtract core: computes a±b±carry at [bits] width and sets
  /// H, N, Z, V, C per the H8/300H rules (H = carry/borrow at bit 3, 11,
  /// or 27 for byte/word/long). Returns the masked result.
  int _arith(int a, int b, int cin, int bits, {required bool sub}) {
    final mask = bits == 8
        ? 0xFF
        : bits == 16
            ? 0xFFFF
            : 0xFFFFFFFF;
    final sign = bits == 8
        ? 0x80
        : bits == 16
            ? 0x8000
            : 0x80000000;
    final hbit = bits == 8
        ? 0x10
        : bits == 16
            ? 0x1000
            : 0x10000000;
    final r = sub ? a - b - cin : a + b + cin;
    final rm = r & mask;
    var f = ccr &
        ~(H8Flag.h | H8Flag.n | H8Flag.z | H8Flag.v | H8Flag.c);
    if (((a ^ b ^ r) & hbit) != 0) f |= H8Flag.h;
    if ((rm & sign) != 0) f |= H8Flag.n;
    if (rm == 0) f |= H8Flag.z;
    final overflow = sub
        ? ((a ^ b) & (a ^ r) & sign) != 0
        : ((~(a ^ b)) & (a ^ r) & sign) != 0;
    if (overflow) f |= H8Flag.v;
    if (sub ? r < 0 : r > mask) f |= H8Flag.c;
    ccr = f;
    return rm;
  }

  /// ADDX/SUBX: like [_arith] with the carry in, but Z retains its previous
  /// value when the result is zero (multi-precision chaining, table A-1
  /// note 3).
  int _arithX(int a, int b, int bits, {required bool sub}) {
    final oldZ = ccr & H8Flag.z;
    final cin = (ccr & H8Flag.c) != 0 ? 1 : 0;
    final rm = _arith(a, b, cin, bits, sub: sub);
    if (rm == 0) {
      ccr = (ccr & ~H8Flag.z) | oldZ;
    }
    return rm;
  }

  /// INC/DEC: adds [delta] (+-1 or +-2), setting N, Z, V but leaving H and
  /// C unchanged.
  int _incDec(int a, int delta, int bits) {
    final mask = bits == 8
        ? 0xFF
        : bits == 16
            ? 0xFFFF
            : 0xFFFFFFFF;
    final sign = bits == 8
        ? 0x80
        : bits == 16
            ? 0x8000
            : 0x80000000;
    final r = (a + delta) & mask;
    var f = ccr & ~(H8Flag.n | H8Flag.z | H8Flag.v);
    if ((r & sign) != 0) f |= H8Flag.n;
    if (r == 0) f |= H8Flag.z;
    final overflow =
        delta > 0 ? ((~a) & r & sign) != 0 : (a & (~r) & sign) != 0;
    if (overflow) f |= H8Flag.v;
    ccr = f;
    return r;
  }

  // ---------------------------------------------------------------------
  // Reset and exceptions
  // ---------------------------------------------------------------------

  /// Reset exception: loads the PC from the reset vector (a longword at
  /// H'000000; the low 24 bits are used) and sets the I bit. On real
  /// hardware the general registers are undefined after reset; they are
  /// zeroed here for a deterministic simulation (initialize SP yourself,
  /// as on the real chip).
  void reset() {
    for (var i = 0; i < 8; i++) {
      er[i] = 0;
    }
    ccr = H8Flag.i;
    for (final s in sci) {
      s.reset();
    }
    itu.reset();
    dmac.reset();
    adc.reset();
    // A reset initializes the on-chip I/O port registers (section 9).
    for (final p in ports) {
      final ddrAddr = p.ddrAddr;
      if (ddrAddr != null) mem.poke(ddrAddr, p.ddrReset);
      mem.poke(p.drAddr, p.drReset);
    }
    // A device part-way through a transfer would still be holding SDA
    // down, and the machine would never see the bus idle again.
    eeprom?.reset();
    pc = _peekL(0) & 0xFFFFFF;
    cycles = 0;
    halted = false;
    sleeping = false;
    haltReason = '';
    // The history is of a run that is over. Keeping it would let a step
    // back cross the reset into a machine that no longer exists.
    undo?.clear();
    clearBreakHit();
  }

  /// Clears a recorded data-breakpoint hit.
  void clearBreakHit() {
    breakHit = false;
    breakAddr = null;
    breakPc = null;
  }

  /// Side-effect-free read that consults the on-chip peripherals, for the
  /// UI. The peripherals hold the authoritative value; memory only carries a
  /// mirror updated when a register is written, so a live counter has to be
  /// read from the model itself.
  int peekBus(int addr) {
    addr &= 0xFFFFFF;
    if (addr >= 0xFFFFB0 && addr <= 0xFFFFBD) {
      for (final s in sci) {
        if (s.owns(addr)) return s.read(addr);
      }
    }
    if (itu.owns(addr)) return itu.read(addr);
    if (dmac.owns(addr)) return dmac.read(addr);
    if (adc.owns(addr)) return adc.read(addr);
    final port = portByDr[addr];
    if (port != null) return portRead(port);
    for (final e in externalInputs) {
      if (e.owns(addr)) {
        final v = e.value;
        if (v != null) return v;
        break;
      }
    }
    return mem.peek(addr);
  }

  /// Side-effect-free longword read (no profiling, no data breakpoints).
  int _peekL(int addr) {
    addr &= 0xFFFFFE;
    return (mem.peek(addr) << 24) |
        (mem.peek(addr + 1) << 16) |
        (mem.peek(addr + 2) << 8) |
        mem.peek(addr + 3);
  }

  /// Common exception entry (advanced mode): pushes CCR:PC24 as one
  /// longword, sets I, and vectors through `vector * 4`.
  void _exception(int vector) {
    final sp = (rd32(7) - 4) & 0xFFFFFFFF;
    wr32(7, sp);
    writeL(sp, ((ccr & 0xFF) << 24) | (pc & 0xFFFFFF));
    ccr |= H8Flag.i;
    pc = readL(vector * 4) & 0xFFFFFF;
    cycles += 16;
  }

  /// Non-maskable interrupt (vector 7). Wakes a sleeping CPU.
  void nmi() {
    if (sleeping) {
      halted = false;
      sleeping = false;
      haltReason = '';
    }
    if (halted) return; // stopped on an illegal instruction: only reset helps
    _exception(7);
  }

  /// Raises the interrupt with the given exception [vector] — the general
  /// form behind [nmi] and [irq], for on-chip peripheral interrupts (ITU,
  /// SCI, DMAC, A/D: vectors 20-60). Maskable interrupts are ignored while
  /// the I bit is set. A sleeping CPU wakes; one halted on an illegal
  /// instruction does not. Returns true if the interrupt was taken.
  bool interrupt(int vector, {bool maskable = true}) {
    if (maskable && (ccr & H8Flag.i) != 0) return false;
    if (sleeping) {
      halted = false;
      sleeping = false;
      haltReason = '';
    }
    if (halted) return false;
    _exception(vector);
    return true;
  }

  /// External interrupt IRQn (vector 12 + n), n = 0-7. Masked by the I bit.
  /// Returns true if the interrupt was serviced. Wakes a sleeping CPU.
  bool irq([int n = 0]) {
    if ((ccr & H8Flag.i) != 0) return false;
    if (sleeping) {
      halted = false;
      sleeping = false;
      haltReason = '';
    }
    if (halted) return false;
    _exception(12 + (n & 7));
    return true;
  }

  void _illegal(int instrStart, int word) {
    pc = instrStart;
    halted = true;
    sleeping = false;
    haltReason =
        'Illegal instruction H\'${word.toRadixString(16).toUpperCase().padLeft(4, '0')}';
  }

  // ---------------------------------------------------------------------
  // Branch condition evaluation (condition field 0-15).
  // ---------------------------------------------------------------------

  bool _cond(int cc) {
    final c = (ccr & H8Flag.c) != 0;
    final z = (ccr & H8Flag.z) != 0;
    final n = (ccr & H8Flag.n) != 0;
    final v = (ccr & H8Flag.v) != 0;
    switch (cc) {
      case 0x0: return true; // BRA
      case 0x1: return false; // BRN
      case 0x2: return !(c || z); // BHI
      case 0x3: return c || z; // BLS
      case 0x4: return !c; // BCC / BHS
      case 0x5: return c; // BCS / BLO
      case 0x6: return !z; // BNE
      case 0x7: return z; // BEQ
      case 0x8: return !v; // BVC
      case 0x9: return v; // BVS
      case 0xA: return !n; // BPL
      case 0xB: return n; // BMI
      case 0xC: return n == v; // BGE
      case 0xD: return n != v; // BLT
      case 0xE: return !z && (n == v); // BGT
      default: return z || (n != v); // BLE
    }
  }

  // ---------------------------------------------------------------------
  // Shift/rotate core: value in, value out, flags set. [kind] selects the
  // operation; all set N and Z, clear V (except SHAL which sets V on a
  // sign change), and load C with the bit shifted out.
  // ---------------------------------------------------------------------

  static const int _shll = 0, _shal = 1, _shlr = 2, _shar = 3;
  static const int _rotxl = 4, _rotl = 5, _rotxr = 6, _rotr = 7;

  int _shift(int kind, int v, int bits) {
    final mask = bits == 8
        ? 0xFF
        : bits == 16
            ? 0xFFFF
            : 0xFFFFFFFF;
    final sign = bits == 8
        ? 0x80
        : bits == 16
            ? 0x8000
            : 0x80000000;
    final oldC = (ccr & H8Flag.c) != 0;
    var r = 0;
    var newC = false;
    var newV = false;
    switch (kind) {
      case _shll:
        newC = (v & sign) != 0;
        r = (v << 1) & mask;
        break;
      case _shal:
        newC = (v & sign) != 0;
        r = (v << 1) & mask;
        newV = ((v ^ r) & sign) != 0; // sign changed -> overflow
        break;
      case _shlr:
        newC = (v & 1) != 0;
        r = v >> 1;
        break;
      case _shar:
        newC = (v & 1) != 0;
        r = (v >> 1) | (v & sign);
        break;
      case _rotxl:
        newC = (v & sign) != 0;
        r = ((v << 1) | (oldC ? 1 : 0)) & mask;
        break;
      case _rotl:
        newC = (v & sign) != 0;
        r = ((v << 1) | (newC ? 1 : 0)) & mask;
        break;
      case _rotxr:
        newC = (v & 1) != 0;
        r = (v >> 1) | (oldC ? sign : 0);
        break;
      case _rotr:
        newC = (v & 1) != 0;
        r = (v >> 1) | (newC ? sign : 0);
        break;
    }
    var f = ccr & ~(H8Flag.n | H8Flag.z | H8Flag.v | H8Flag.c);
    if ((r & sign) != 0) f |= H8Flag.n;
    if (r == 0) f |= H8Flag.z;
    if (newV) f |= H8Flag.v;
    if (newC) f |= H8Flag.c;
    ccr = f;
    return r;
  }

  // ---------------------------------------------------------------------
  // Execute one instruction. Returns the number of states consumed
  // (0 when halted).
  // ---------------------------------------------------------------------

  /// States charged per [step] while the CPU is in sleep mode. The processor
  /// is stopped but the clock and the on-chip peripherals are not, so time
  /// has to keep passing — otherwise a timer could never raise the interrupt
  /// that wakes it.
  static const int sleepStates = 2;

  int step() {
    final j = undo;
    if (j == null) return _step();
    // Taken before the instruction, so the state kept belongs to the
    // position it is filed under.
    if (j.wantsCheckpoint) j.addCheckpoint(saveState());
    _undoBefore.setAll(0, er);
    j.beginStep(
        pc: pc, ccr: ccr, cycles: cycles, halted: halted, sleeping: sleeping);
    final wasCycles = cycles, wasPc = pc;
    final states = _step();
    // Nothing ran -- a condition held, or the CPU is halted for good. A
    // record for it would be a step back that goes nowhere while using one
    // up, which reads as the history being broken.
    if (cycles == wasCycles && pc == wasPc) {
      j.abandonStep();
    } else {
      j.endStep(_undoBefore, er);
    }
    return states;
  }

  int _step() {
    // The condition is asked before each instruction rather than after, so
    // "stopped" means "about to execute this", which is what a debugger's
    // stop should mean. The state it sees is the one the instruction before
    // it left behind.
    final stop = stopWhen;
    if (stop != null && !conditionHit) {
      try {
        if (stop.test(_conditionView)) {
          conditionHit = true;
          return 0;
        }
      } catch (_) {
        // A condition that cannot be evaluated -- an address that reads
        // oddly, say -- stops rather than throwing out of the run loop.
        conditionHit = true;
        return 0;
      }
    }
    if (halted && sleeping) cycles += sleepStates;
    // Peripherals run between instructions: they advance with the clock and
    // may raise an interrupt, which is also what wakes a sleeping CPU. Each
    // is skipped while idle, so an unused peripheral costs almost nothing
    // per instruction.
    for (final s in sci) {
      if (s.active) s.tick(cycles);
      final v = s.pendingVector();
      if (v != null && interrupt(v)) return 16;
    }
    if (itu.anyRunning) itu.tick(cycles);
    if (adc.running) adc.tick(cycles);
    // The DMAC moves data before the CPU sees the peripheral interrupt: an
    // enabled channel takes the request instead of the processor.
    final dendVector = dmac.service();
    if (dendVector != null && interrupt(dendVector)) return 16;
    final ituVector = itu.pendingVector();
    if (ituVector != null && interrupt(ituVector)) return 16;
    final adiVector = adc.pendingVector();
    if (adiVector != null && interrupt(adiVector)) return 16;
    if (halted) return sleeping ? sleepStates : 0;
    if (profiling) instrExecCount.bump(pc);
    final instrStart = pc;
    _instrPc = pc;
    final w0 = _fetchW();
    final b0 = w0 >> 8;
    final b1 = w0 & 0xFF;
    int states;

    switch (b0) {
      case 0x00: // NOP
        if (b1 != 0x00) {
          _illegal(instrStart, w0);
          return 0;
        }
        states = 2;
        break;

      case 0x01: // prefix group
        states = _exec01(instrStart, w0);
        // SLEEP halts but still consumes its states; only an illegal
        // encoding aborts the step entirely.
        if (halted && !sleeping) return 0;
        break;

      case 0x02: // STC.B CCR, Rd
        if ((b1 & 0xF0) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        wr8(b1 & 0xF, ccr);
        states = 2;
        break;

      case 0x03: // LDC.B Rs, CCR
        if ((b1 & 0xF0) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        ccr = rd8(b1 & 0xF);
        states = 2;
        break;

      case 0x04: // ORC #xx:8, CCR
        ccr |= b1;
        states = 2;
        break;

      case 0x05: // XORC #xx:8, CCR
        ccr ^= b1;
        states = 2;
        break;

      case 0x06: // ANDC #xx:8, CCR
        ccr &= b1;
        states = 2;
        break;

      case 0x07: // LDC #xx:8, CCR
        ccr = b1;
        states = 2;
        break;

      case 0x08: // ADD.B Rs, Rd
        wr8(b1 & 0xF,
            _arith(rd8(b1 & 0xF), rd8(b1 >> 4), 0, 8, sub: false));
        states = 2;
        break;

      case 0x09: // ADD.W Rs, Rd
        wr16(b1 & 0xF,
            _arith(rd16(b1 & 0xF), rd16(b1 >> 4), 0, 16, sub: false));
        states = 2;
        break;

      case 0x0A: // INC.B Rd | ADD.L ERs, ERd
        if ((b1 & 0x80) != 0) {
          if ((b1 & 0x08) != 0) {
            _illegal(instrStart, w0);
            return 0;
          }
          wr32(b1 & 7,
              _arith(rd32(b1 & 7), rd32((b1 >> 4) & 7), 0, 32, sub: false));
        } else if ((b1 & 0xF0) == 0x00) {
          wr8(b1 & 0xF, _incDec(rd8(b1 & 0xF), 1, 8));
        } else {
          _illegal(instrStart, w0);
          return 0;
        }
        states = 2;
        break;

      case 0x0B: // ADDS / INC.W / INC.L
        states = _addsIncGroup(instrStart, w0, negate: false);
        if (halted) return 0;
        break;

      case 0x0C: // MOV.B Rs, Rd
        final v = rd8(b1 >> 4);
        wr8(b1 & 0xF, v);
        _setNZV0(v, 8);
        states = 2;
        break;

      case 0x0D: // MOV.W Rs, Rd
        final v = rd16(b1 >> 4);
        wr16(b1 & 0xF, v);
        _setNZV0(v, 16);
        states = 2;
        break;

      case 0x0E: // ADDX.B Rs, Rd
        wr8(b1 & 0xF, _arithX(rd8(b1 & 0xF), rd8(b1 >> 4), 8, sub: false));
        states = 2;
        break;

      case 0x0F: // DAA Rd | MOV.L ERs, ERd
        if ((b1 & 0x80) != 0) {
          if ((b1 & 0x08) != 0) {
            _illegal(instrStart, w0);
            return 0;
          }
          final v = rd32((b1 >> 4) & 7);
          wr32(b1 & 7, v);
          _setNZV0(v, 32);
        } else if ((b1 & 0xF0) == 0x00) {
          _daa(b1 & 0xF);
        } else {
          _illegal(instrStart, w0);
          return 0;
        }
        states = 2;
        break;

      case 0x10: // SHLL / SHAL
      case 0x11: // SHLR / SHAR
      case 0x12: // ROTXL / ROTL
      case 0x13: // ROTXR / ROTR
        states = _shiftGroup(instrStart, w0);
        if (halted) return 0;
        break;

      case 0x14: // OR.B Rs, Rd
        final r = rd8(b1 & 0xF) | rd8(b1 >> 4);
        wr8(b1 & 0xF, r);
        _setNZV0(r, 8);
        states = 2;
        break;

      case 0x15: // XOR.B Rs, Rd
        final r = rd8(b1 & 0xF) ^ rd8(b1 >> 4);
        wr8(b1 & 0xF, r);
        _setNZV0(r, 8);
        states = 2;
        break;

      case 0x16: // AND.B Rs, Rd
        final r = rd8(b1 & 0xF) & rd8(b1 >> 4);
        wr8(b1 & 0xF, r);
        _setNZV0(r, 8);
        states = 2;
        break;

      case 0x17: // NOT / EXTU / NEG / EXTS
        states = _notExtNegGroup(instrStart, w0);
        if (halted) return 0;
        break;

      case 0x18: // SUB.B Rs, Rd
        wr8(b1 & 0xF, _arith(rd8(b1 & 0xF), rd8(b1 >> 4), 0, 8, sub: true));
        states = 2;
        break;

      case 0x19: // SUB.W Rs, Rd
        wr16(
            b1 & 0xF, _arith(rd16(b1 & 0xF), rd16(b1 >> 4), 0, 16, sub: true));
        states = 2;
        break;

      case 0x1A: // DEC.B Rd | SUB.L ERs, ERd
        if ((b1 & 0x80) != 0) {
          if ((b1 & 0x08) != 0) {
            _illegal(instrStart, w0);
            return 0;
          }
          wr32(b1 & 7,
              _arith(rd32(b1 & 7), rd32((b1 >> 4) & 7), 0, 32, sub: true));
        } else if ((b1 & 0xF0) == 0x00) {
          wr8(b1 & 0xF, _incDec(rd8(b1 & 0xF), -1, 8));
        } else {
          _illegal(instrStart, w0);
          return 0;
        }
        states = 2;
        break;

      case 0x1B: // SUBS / DEC.W / DEC.L
        states = _addsIncGroup(instrStart, w0, negate: true);
        if (halted) return 0;
        break;

      case 0x1C: // CMP.B Rs, Rd
        _arith(rd8(b1 & 0xF), rd8(b1 >> 4), 0, 8, sub: true);
        states = 2;
        break;

      case 0x1D: // CMP.W Rs, Rd
        _arith(rd16(b1 & 0xF), rd16(b1 >> 4), 0, 16, sub: true);
        states = 2;
        break;

      case 0x1E: // SUBX.B Rs, Rd
        wr8(b1 & 0xF, _arithX(rd8(b1 & 0xF), rd8(b1 >> 4), 8, sub: true));
        states = 2;
        break;

      case 0x1F: // DAS Rd | CMP.L ERs, ERd
        if ((b1 & 0x80) != 0) {
          if ((b1 & 0x08) != 0) {
            _illegal(instrStart, w0);
            return 0;
          }
          _arith(rd32(b1 & 7), rd32((b1 >> 4) & 7), 0, 32, sub: true);
        } else if ((b1 & 0xF0) == 0x00) {
          _das(b1 & 0xF);
        } else {
          _illegal(instrStart, w0);
          return 0;
        }
        states = 2;
        break;

      // MOV.B @aa:8, Rd — the 8-bit absolute area is H'FFFF00-H'FFFFFF.
      case 0x20:
      case 0x21:
      case 0x22:
      case 0x23:
      case 0x24:
      case 0x25:
      case 0x26:
      case 0x27:
      case 0x28:
      case 0x29:
      case 0x2A:
      case 0x2B:
      case 0x2C:
      case 0x2D:
      case 0x2E:
      case 0x2F:
        final v = readB(0xFFFF00 | b1);
        wr8(b0 & 0xF, v);
        _setNZV0(v, 8);
        states = 4;
        break;

      // MOV.B Rs, @aa:8
      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33:
      case 0x34:
      case 0x35:
      case 0x36:
      case 0x37:
      case 0x38:
      case 0x39:
      case 0x3A:
      case 0x3B:
      case 0x3C:
      case 0x3D:
      case 0x3E:
      case 0x3F:
        final v = rd8(b0 & 0xF);
        writeB(0xFFFF00 | b1, v);
        _setNZV0(v, 8);
        states = 4;
        break;

      // Bcc d:8
      case 0x40:
      case 0x41:
      case 0x42:
      case 0x43:
      case 0x44:
      case 0x45:
      case 0x46:
      case 0x47:
      case 0x48:
      case 0x49:
      case 0x4A:
      case 0x4B:
      case 0x4C:
      case 0x4D:
      case 0x4E:
      case 0x4F:
        if (_cond(b0 & 0xF)) {
          final disp = b1 < 0x80 ? b1 : b1 - 0x100;
          pc = (pc + disp) & 0xFFFFFF;
        }
        states = 4;
        break;

      case 0x50: // MULXU.B Rs, Rd
        final rd = b1 & 0xF;
        wr16(rd, (rd16(rd) & 0xFF) * rd8(b1 >> 4));
        states = 14;
        break;

      case 0x51: // DIVXU.B Rs, Rd
        states = _divxu(b1, byte: true);
        break;

      case 0x52: // MULXU.W Rs, ERd
        if ((b1 & 0x08) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        final erd = b1 & 7;
        wr32(erd, (rd32(erd) & 0xFFFF) * rd16(b1 >> 4));
        states = 22;
        break;

      case 0x53: // DIVXU.W Rs, ERd
        if ((b1 & 0x08) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        states = _divxu(b1, byte: false);
        break;

      case 0x54: // RTS
        if (b1 != 0x70) {
          _illegal(instrStart, w0);
          return 0;
        }
        final sp = rd32(7);
        pc = readL(sp) & 0xFFFFFF;
        wr32(7, sp + 4);
        states = 10;
        break;

      case 0x55: // BSR d:8
        final disp = b1 < 0x80 ? b1 : b1 - 0x100;
        _pushPC();
        pc = (pc + disp) & 0xFFFFFF;
        states = 8;
        break;

      case 0x56: // RTE
        if (b1 != 0x70) {
          _illegal(instrStart, w0);
          return 0;
        }
        final sp = rd32(7);
        final frame = readL(sp);
        wr32(7, sp + 4);
        ccr = (frame >> 24) & 0xFF;
        pc = frame & 0xFFFFFF;
        states = 10;
        break;

      case 0x57: // TRAPA #x:2
        if ((b1 & 0xCF) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        _exception(8 + ((b1 >> 4) & 3));
        states = 0; // _exception already added the states
        break;

      case 0x58: // Bcc d:16
        if ((b1 & 0x0F) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        final d = _fetchW();
        if (_cond(b1 >> 4)) {
          final disp = d < 0x8000 ? d : d - 0x10000;
          pc = (pc + disp) & 0xFFFFFF;
        }
        states = 6;
        break;

      case 0x59: // JMP @ERn
        if ((b1 & 0x8F) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        pc = rd32((b1 >> 4) & 7) & 0xFFFFFF;
        states = 4;
        break;

      case 0x5A: // JMP @aa:24
        pc = ((b1 << 16) | _fetchW()) & 0xFFFFFF;
        states = 6;
        break;

      case 0x5B: // JMP @@aa:8
        pc = readL(b1) & 0xFFFFFF;
        states = 10;
        break;

      case 0x5C: // BSR d:16
        if (b1 != 0x00) {
          _illegal(instrStart, w0);
          return 0;
        }
        final d = _fetchW();
        final disp = d < 0x8000 ? d : d - 0x10000;
        _pushPC();
        pc = (pc + disp) & 0xFFFFFF;
        states = 10;
        break;

      case 0x5D: // JSR @ERn
        if ((b1 & 0x8F) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        _pushPC();
        pc = rd32((b1 >> 4) & 7) & 0xFFFFFF;
        states = 8;
        break;

      case 0x5E: // JSR @aa:24
        final target = ((b1 << 16) | _fetchW()) & 0xFFFFFF;
        _pushPC();
        pc = target;
        states = 10;
        break;

      case 0x5F: // JSR @@aa:8
        final target = readL(b1) & 0xFFFFFF;
        _pushPC();
        pc = target;
        states = 12;
        break;

      case 0x60: // BSET Rn, Rd
      case 0x61: // BNOT Rn, Rd
      case 0x62: // BCLR Rn, Rd
      case 0x63: // BTST Rn, Rd
        final bit = rd8(b1 >> 4) & 7;
        final rd = b1 & 0xF;
        wr8(rd, _bitOp(b0 & 3, rd8(rd), bit));
        states = 2;
        break;

      case 0x64: // OR.W Rs, Rd
        final r = rd16(b1 & 0xF) | rd16(b1 >> 4);
        wr16(b1 & 0xF, r);
        _setNZV0(r, 16);
        states = 2;
        break;

      case 0x65: // XOR.W Rs, Rd
        final r = rd16(b1 & 0xF) ^ rd16(b1 >> 4);
        wr16(b1 & 0xF, r);
        _setNZV0(r, 16);
        states = 2;
        break;

      case 0x66: // AND.W Rs, Rd
        final r = rd16(b1 & 0xF) & rd16(b1 >> 4);
        wr16(b1 & 0xF, r);
        _setNZV0(r, 16);
        states = 2;
        break;

      case 0x67: // BST / BIST #xx:3, Rd
        final bit = (b1 >> 4) & 7;
        final rd = b1 & 0xF;
        final cSet = (ccr & H8Flag.c) != 0;
        final store = (b1 & 0x80) != 0 ? !cSet : cSet; // BIST stores ~C
        var v = rd8(rd);
        v = store ? (v | (1 << bit)) : (v & ~(1 << bit));
        wr8(rd, v);
        states = 2;
        break;

      case 0x68: // MOV.B @ERs, Rd | MOV.B Rs, @ERd
        final ern = (b1 >> 4) & 7;
        final r = b1 & 0xF;
        if ((b1 & 0x80) != 0) {
          final v = rd8(r);
          writeB(rd32(ern), v);
          _setNZV0(v, 8);
        } else {
          final v = readB(rd32(ern));
          wr8(r, v);
          _setNZV0(v, 8);
        }
        states = 4;
        break;

      case 0x69: // MOV.W @ERs, Rd | MOV.W Rs, @ERd
        final ern = (b1 >> 4) & 7;
        final r = b1 & 0xF;
        if ((b1 & 0x80) != 0) {
          final v = rd16(r);
          writeW(rd32(ern), v);
          _setNZV0(v, 16);
        } else {
          final v = readW(rd32(ern));
          wr16(r, v);
          _setNZV0(v, 16);
        }
        states = 4;
        break;

      case 0x6A: // MOV.B absolute (aa:16 / aa:24)
        states = _movAbs(instrStart, w0, size: 8);
        if (halted) return 0;
        break;

      case 0x6B: // MOV.W absolute (aa:16 / aa:24)
        states = _movAbs(instrStart, w0, size: 16);
        if (halted) return 0;
        break;

      case 0x6C: // MOV.B @ERs+, Rd | MOV.B Rs, @-ERd
        final ern = (b1 >> 4) & 7;
        final r = b1 & 0xF;
        if ((b1 & 0x80) != 0) {
          final addr = (rd32(ern) - 1) & 0xFFFFFFFF;
          wr32(ern, addr);
          final v = rd8(r);
          writeB(addr, v);
          _setNZV0(v, 8);
        } else {
          final addr = rd32(ern);
          wr32(ern, addr + 1);
          final v = readB(addr);
          wr8(r, v);
          _setNZV0(v, 8);
        }
        states = 6;
        break;

      case 0x6D: // MOV.W @ERs+, Rd | MOV.W Rs, @-ERd (POP.W / PUSH.W)
        final ern = (b1 >> 4) & 7;
        final r = b1 & 0xF;
        if ((b1 & 0x80) != 0) {
          final addr = (rd32(ern) - 2) & 0xFFFFFFFF;
          wr32(ern, addr);
          final v = rd16(r);
          writeW(addr, v);
          _setNZV0(v, 16);
        } else {
          final addr = rd32(ern);
          wr32(ern, addr + 2);
          final v = readW(addr);
          wr16(r, v);
          _setNZV0(v, 16);
        }
        states = 6;
        break;

      case 0x6E: // MOV.B @(d:16, ERs), Rd | MOV.B Rs, @(d:16, ERd)
        final ern = (b1 >> 4) & 7;
        final r = b1 & 0xF;
        final d = _fetchW();
        final disp = d < 0x8000 ? d : d - 0x10000;
        final addr = (rd32(ern) + disp) & 0xFFFFFFFF;
        if ((b1 & 0x80) != 0) {
          final v = rd8(r);
          writeB(addr, v);
          _setNZV0(v, 8);
        } else {
          final v = readB(addr);
          wr8(r, v);
          _setNZV0(v, 8);
        }
        states = 6;
        break;

      case 0x6F: // MOV.W @(d:16, ERs), Rd | MOV.W Rs, @(d:16, ERd)
        final ern = (b1 >> 4) & 7;
        final r = b1 & 0xF;
        final d = _fetchW();
        final disp = d < 0x8000 ? d : d - 0x10000;
        final addr = (rd32(ern) + disp) & 0xFFFFFFFF;
        if ((b1 & 0x80) != 0) {
          final v = rd16(r);
          writeW(addr, v);
          _setNZV0(v, 16);
        } else {
          final v = readW(addr);
          wr16(r, v);
          _setNZV0(v, 16);
        }
        states = 6;
        break;

      case 0x70: // BSET #xx:3, Rd
      case 0x71: // BNOT #xx:3, Rd
      case 0x72: // BCLR #xx:3, Rd
      case 0x73: // BTST #xx:3, Rd
        if ((b1 & 0x80) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        final rd = b1 & 0xF;
        wr8(rd, _bitOp(b0 & 3, rd8(rd), (b1 >> 4) & 7));
        states = 2;
        break;

      case 0x74: // BOR / BIOR #xx:3, Rd
      case 0x75: // BXOR / BIXOR #xx:3, Rd
      case 0x76: // BAND / BIAND #xx:3, Rd
      case 0x77: // BLD / BILD #xx:3, Rd
        _bitLogic(b0 & 3, rd8(b1 & 0xF), (b1 >> 4) & 7, (b1 & 0x80) != 0);
        states = 2;
        break;

      case 0x78: // MOV.B/W @(d:24, ERs) forms
        states = _movDisp24(instrStart, w0);
        if (halted) return 0;
        break;

      case 0x79: // word-immediate group
        states = _immGroup(instrStart, w0, size: 16);
        if (halted) return 0;
        break;

      case 0x7A: // longword-immediate group
        states = _immGroup(instrStart, w0, size: 32);
        if (halted) return 0;
        break;

      case 0x7B: // EEPMOV.B / EEPMOV.W
        states = _eepmov(instrStart, w0);
        if (halted) return 0;
        break;

      case 0x7C: // bit ops, read, @ERd
      case 0x7D: // bit ops, write, @ERd
        if ((b1 & 0x8F) != 0) {
          _illegal(instrStart, w0);
          return 0;
        }
        states =
            _bitMem(instrStart, rd32((b1 >> 4) & 7), write: b0 == 0x7D);
        if (halted) return 0;
        break;

      case 0x7E: // bit ops, read, @aa:8
      case 0x7F: // bit ops, write, @aa:8
        states =
            _bitMem(instrStart, 0xFFFF00 | b1, write: b0 == 0x7F);
        if (halted) return 0;
        break;

      default: // 0x80-0xFF: byte-immediate ops, register field in b0
        final rd = b0 & 0xF;
        switch (b0 & 0xF0) {
          case 0x80: // ADD.B #xx:8, Rd
            wr8(rd, _arith(rd8(rd), b1, 0, 8, sub: false));
            break;
          case 0x90: // ADDX.B #xx:8, Rd
            wr8(rd, _arithX(rd8(rd), b1, 8, sub: false));
            break;
          case 0xA0: // CMP.B #xx:8, Rd
            _arith(rd8(rd), b1, 0, 8, sub: true);
            break;
          case 0xB0: // SUBX.B #xx:8, Rd
            wr8(rd, _arithX(rd8(rd), b1, 8, sub: true));
            break;
          case 0xC0: // OR.B #xx:8, Rd
            final r = rd8(rd) | b1;
            wr8(rd, r);
            _setNZV0(r, 8);
            break;
          case 0xD0: // XOR.B #xx:8, Rd
            final r = rd8(rd) ^ b1;
            wr8(rd, r);
            _setNZV0(r, 8);
            break;
          case 0xE0: // AND.B #xx:8, Rd
            final r = rd8(rd) & b1;
            wr8(rd, r);
            _setNZV0(r, 8);
            break;
          default: // 0xF0: MOV.B #xx:8, Rd
            wr8(rd, b1);
            _setNZV0(b1, 8);
            break;
        }
        states = 2;
        break;
    }

    cycles += states;
    return states;
  }

  /// Pushes the 24-bit PC as a longword (top byte zero) for JSR/BSR.
  void _pushPC() {
    final sp = (rd32(7) - 4) & 0xFFFFFFFF;
    wr32(7, sp);
    writeL(sp, pc & 0xFFFFFF);
  }

  // ---- 0x01-prefixed instructions ---------------------------------------

  int _exec01(int instrStart, int w0) {
    final b1 = w0 & 0xFF;
    switch (b1) {
      case 0x00: // MOV.L register-memory forms
        return _movLong(instrStart);
      case 0x40: // LDC.W / STC.W memory forms
        return _ldcStcW(instrStart);
      case 0x80: // SLEEP
        halted = true;
        sleeping = true;
        haltReason = 'SLEEP';
        return 2;
      case 0xC0: // MULXS
        final w1 = _fetchW();
        final b2 = w1 >> 8;
        final b3 = w1 & 0xFF;
        if (b2 == 0x50) {
          // MULXS.B Rs, Rd — sets N and Z only (V, C unchanged).
          final rd = b3 & 0xF;
          final a = _sext8(rd16(rd) & 0xFF);
          final b = _sext8(rd8(b3 >> 4));
          final r = (a * b) & 0xFFFF;
          wr16(rd, r);
          _setNZ(r, 16);
          return 16;
        }
        if (b2 == 0x52 && (b3 & 0x08) == 0) {
          // MULXS.W Rs, ERd
          final erd = b3 & 7;
          final a = _sext16(rd32(erd) & 0xFFFF);
          final b = _sext16(rd16(b3 >> 4));
          final r = (a * b) & 0xFFFFFFFF;
          wr32(erd, r);
          _setNZ(r, 32);
          return 24;
        }
        _illegal(instrStart, w1);
        return 0;
      case 0xD0: // DIVXS
        final w1 = _fetchW();
        final b2 = w1 >> 8;
        final b3 = w1 & 0xFF;
        if (b2 == 0x51) return _divxs(b3, byte: true);
        if (b2 == 0x53 && (b3 & 0x08) == 0) return _divxs(b3, byte: false);
        _illegal(instrStart, w1);
        return 0;
      case 0xF0: // OR.L / XOR.L / AND.L ERs, ERd
        final w1 = _fetchW();
        final b2 = w1 >> 8;
        final b3 = w1 & 0xFF;
        if ((b3 & 0x88) != 0) {
          _illegal(instrStart, w1);
          return 0;
        }
        final erd = b3 & 7;
        final s = rd32((b3 >> 4) & 7);
        int r;
        switch (b2) {
          case 0x64:
            r = rd32(erd) | s;
            break;
          case 0x65:
            r = rd32(erd) ^ s;
            break;
          case 0x66:
            r = rd32(erd) & s;
            break;
          default:
            _illegal(instrStart, w1);
            return 0;
        }
        wr32(erd, r);
        _setNZV0(r, 32);
        return 4;
      default:
        _illegal(instrStart, w0);
        return 0;
    }
  }

  int _sext8(int v) => v < 0x80 ? v : v - 0x100;
  int _sext16(int v) => v < 0x8000 ? v : v - 0x10000;

  /// MOV.L register-memory forms (prefix 01 00).
  int _movLong(int instrStart) {
    final w1 = _fetchW();
    final b2 = w1 >> 8;
    final b3 = w1 & 0xFF;
    final store = (b3 & 0x80) != 0;
    final ern = (b3 >> 4) & 7;
    final erd = b3 & 7;
    if ((b3 & 0x08) != 0) {
      _illegal(instrStart, w1);
      return 0;
    }
    switch (b2) {
      case 0x69: // MOV.L @ERs, ERd | MOV.L ERs, @ERd
        if (store) {
          final v = rd32(erd);
          writeL(rd32(ern), v);
          _setNZV0(v, 32);
        } else {
          final v = readL(rd32(ern));
          wr32(erd, v);
          _setNZV0(v, 32);
        }
        return 8;
      case 0x6B: // MOV.L @aa:16/@aa:24
        final mode = (b3 >> 4) & 0x7;
        if (mode == 0) {
          final a = _fetchW();
          final addr = _sext16(a) & 0xFFFFFF;
          if (store) {
            final v = rd32(erd);
            writeL(addr, v);
            _setNZV0(v, 32);
          } else {
            final v = readL(addr);
            wr32(erd, v);
            _setNZV0(v, 32);
          }
          return 10;
        }
        if (mode == 2) {
          final hi = _fetchW();
          final lo = _fetchW();
          final addr = (((hi & 0xFF) << 16) | lo) & 0xFFFFFF;
          if (store) {
            final v = rd32(erd);
            writeL(addr, v);
            _setNZV0(v, 32);
          } else {
            final v = readL(addr);
            wr32(erd, v);
            _setNZV0(v, 32);
          }
          return 12;
        }
        _illegal(instrStart, w1);
        return 0;
      case 0x6D: // MOV.L @ERs+, ERd (POP.L) | MOV.L ERs, @-ERd (PUSH.L)
        if (store) {
          final addr = (rd32(ern) - 4) & 0xFFFFFFFF;
          wr32(ern, addr);
          final v = rd32(erd);
          writeL(addr, v);
          _setNZV0(v, 32);
        } else {
          final addr = rd32(ern);
          wr32(ern, addr + 4);
          final v = readL(addr);
          wr32(erd, v);
          _setNZV0(v, 32);
        }
        return 10;
      case 0x6F: // MOV.L @(d:16, ERs), ERd | store
        final d = _fetchW();
        final addr = (rd32(ern) + _sext16(d)) & 0xFFFFFFFF;
        if (store) {
          final v = rd32(erd);
          writeL(addr, v);
          _setNZV0(v, 32);
        } else {
          final v = readL(addr);
          wr32(erd, v);
          _setNZV0(v, 32);
        }
        return 10;
      case 0x78: // MOV.L @(d:24, ERs), ERd  and the store direction
        // Unlike the byte and word forms, the longword d:24 encoding also
        // sets bit 7 of this byte for a store (H'E0 rather than H'60). The
        // direction that matters is the one in the second sub-opcode below,
        // so accept either here.
        if ((b3 & 0x0F) != 0) {
          _illegal(instrStart, w1);
          return 0;
        }
        final w2 = _fetchW();
        if ((w2 >> 8) != 0x6B) {
          _illegal(instrStart, w2);
          return 0;
        }
        final sub = w2 & 0xFF;
        final store2 = (sub & 0x80) != 0;
        if ((sub & 0x78) != 0x20) {
          _illegal(instrStart, w2);
          return 0;
        }
        final erd2 = sub & 7;
        final dHi = _fetchW();
        final dLo = _fetchW();
        var disp = ((dHi & 0xFF) << 16) | dLo;
        if (disp >= 0x800000) disp -= 0x1000000;
        final addr = (rd32(ern) + disp) & 0xFFFFFFFF;
        if (store2) {
          final v = rd32(erd2);
          writeL(addr, v);
          _setNZV0(v, 32);
        } else {
          final v = readL(addr);
          wr32(erd2, v);
          _setNZV0(v, 32);
        }
        return 14;
      default:
        _illegal(instrStart, w1);
        return 0;
    }
  }

  /// LDC.W / STC.W memory forms (prefix 01 40). The CCR travels in the
  /// upper byte of the word.
  int _ldcStcW(int instrStart) {
    final w1 = _fetchW();
    final b2 = w1 >> 8;
    final b3 = w1 & 0xFF;
    final store = (b3 & 0x80) != 0; // STC
    final ern = (b3 >> 4) & 7;
    switch (b2) {
      case 0x69: // @ERn
        if ((b3 & 0x0F) != 0) {
          _illegal(instrStart, w1);
          return 0;
        }
        if (store) {
          writeW(rd32(ern), (ccr << 8) | ccr);
        } else {
          ccr = (readW(rd32(ern)) >> 8) & 0xFF;
        }
        return 6;
      case 0x6B: // @aa:16 / @aa:24
        final mode = (b3 >> 4) & 7;
        if ((b3 & 0x0F) != 0) {
          _illegal(instrStart, w1);
          return 0;
        }
        int addr;
        int st;
        if (mode == 0) {
          addr = _sext16(_fetchW()) & 0xFFFFFF;
          st = 8;
        } else if (mode == 2) {
          final hi = _fetchW();
          final lo = _fetchW();
          addr = (((hi & 0xFF) << 16) | lo) & 0xFFFFFF;
          st = 10;
        } else {
          _illegal(instrStart, w1);
          return 0;
        }
        if (store) {
          writeW(addr, (ccr << 8) | ccr);
        } else {
          ccr = (readW(addr) >> 8) & 0xFF;
        }
        return st;
      case 0x6D: // LDC.W @ERs+ | STC.W @-ERd
        if ((b3 & 0x0F) != 0) {
          _illegal(instrStart, w1);
          return 0;
        }
        if (store) {
          final addr = (rd32(ern) - 2) & 0xFFFFFFFF;
          wr32(ern, addr);
          writeW(addr, (ccr << 8) | ccr);
        } else {
          final addr = rd32(ern);
          wr32(ern, addr + 2);
          ccr = (readW(addr) >> 8) & 0xFF;
        }
        return 8;
      case 0x6F: // @(d:16, ERn)
        if ((b3 & 0x0F) != 0) {
          _illegal(instrStart, w1);
          return 0;
        }
        final d = _fetchW();
        final addr = (rd32(ern) + _sext16(d)) & 0xFFFFFFFF;
        if (store) {
          writeW(addr, (ccr << 8) | ccr);
        } else {
          ccr = (readW(addr) >> 8) & 0xFF;
        }
        return 8;
      case 0x78: // @(d:24, ERn)
        if ((b3 & 0x8F) != 0) {
          _illegal(instrStart, w1);
          return 0;
        }
        final w2 = _fetchW();
        final sub = w2 & 0xFF;
        if ((w2 >> 8) != 0x6B || (sub & 0x7F) != 0x20) {
          _illegal(instrStart, w2);
          return 0;
        }
        final store2 = (sub & 0x80) != 0;
        final dHi = _fetchW();
        final dLo = _fetchW();
        var disp = ((dHi & 0xFF) << 16) | dLo;
        if (disp >= 0x800000) disp -= 0x1000000;
        final addr = (rd32(ern) + disp) & 0xFFFFFFFF;
        if (store2) {
          writeW(addr, (ccr << 8) | ccr);
        } else {
          ccr = (readW(addr) >> 8) & 0xFF;
        }
        return 12;
      default:
        _illegal(instrStart, w1);
        return 0;
    }
  }

  // ---- Instruction group helpers ----------------------------------------

  /// 0x0B (ADDS/INC.W/INC.L) and 0x1B (SUBS/DEC.W/DEC.L).
  int _addsIncGroup(int instrStart, int w0, {required bool negate}) {
    final b1 = w0 & 0xFF;
    final hi = b1 >> 4;
    final lo = b1 & 0xF;
    final sgn = negate ? -1 : 1;
    switch (hi) {
      case 0x0: // ADDS/SUBS #1, ERd
      case 0x8: // ADDS/SUBS #2, ERd
      case 0x9: // ADDS/SUBS #4, ERd
        if (lo >= 8) {
          _illegal(instrStart, w0);
          return 0;
        }
        final amount = hi == 0 ? 1 : (hi == 8 ? 2 : 4);
        wr32(lo, rd32(lo) + sgn * amount); // no flags
        return 2;
      case 0x5: // INC/DEC.W #1, Rd
        wr16(lo, _incDec(rd16(lo), sgn * 1, 16));
        return 2;
      case 0xD: // INC/DEC.W #2, Rd
        wr16(lo, _incDec(rd16(lo), sgn * 2, 16));
        return 2;
      case 0x7: // INC/DEC.L #1, ERd
        if (lo >= 8) {
          _illegal(instrStart, w0);
          return 0;
        }
        wr32(lo, _incDec(rd32(lo), sgn * 1, 32));
        return 2;
      case 0xF: // INC/DEC.L #2, ERd
        if (lo >= 8) {
          _illegal(instrStart, w0);
          return 0;
        }
        wr32(lo, _incDec(rd32(lo), sgn * 2, 32));
        return 2;
      default:
        _illegal(instrStart, w0);
        return 0;
    }
  }

  /// 0x10-0x13: shifts and rotates.
  int _shiftGroup(int instrStart, int w0) {
    final b0 = w0 >> 8;
    final b1 = w0 & 0xFF;
    final hi = b1 >> 4;
    final lo = b1 & 0xF;
    // Which operation: 0x10 SHLL/SHAL, 0x11 SHLR/SHAR, 0x12 ROTXL/ROTL,
    // 0x13 ROTXR/ROTR. High-nibble bit 3 selects the second of the pair.
    final alt = (hi & 0x8) != 0;
    final kind = switch (b0) {
      0x10 => alt ? _shal : _shll,
      0x11 => alt ? _shar : _shlr,
      0x12 => alt ? _rotl : _rotxl,
      _ => alt ? _rotr : _rotxr,
    };
    switch (hi & 0x7) {
      case 0x0: // byte
        wr8(lo, _shift(kind, rd8(lo), 8));
        return 2;
      case 0x1: // word
        wr16(lo, _shift(kind, rd16(lo), 16));
        return 2;
      case 0x3: // long
        if (lo >= 8) {
          _illegal(instrStart, w0);
          return 0;
        }
        wr32(lo, _shift(kind, rd32(lo), 32));
        return 2;
      default:
        _illegal(instrStart, w0);
        return 0;
    }
  }

  /// 0x17: NOT / EXTU / NEG / EXTS.
  int _notExtNegGroup(int instrStart, int w0) {
    final b1 = w0 & 0xFF;
    final hi = b1 >> 4;
    final lo = b1 & 0xF;
    switch (hi) {
      case 0x0: // NOT.B
        final r = (~rd8(lo)) & 0xFF;
        wr8(lo, r);
        _setNZV0(r, 8);
        return 2;
      case 0x1: // NOT.W
        final r = (~rd16(lo)) & 0xFFFF;
        wr16(lo, r);
        _setNZV0(r, 16);
        return 2;
      case 0x3: // NOT.L
        if (lo >= 8) break;
        final r = (~rd32(lo)) & 0xFFFFFFFF;
        wr32(lo, r);
        _setNZV0(r, 32);
        return 2;
      case 0x5: // EXTU.W
        final r = rd16(lo) & 0xFF;
        wr16(lo, r);
        _setNZV0(r, 16);
        return 2;
      case 0x7: // EXTU.L
        if (lo >= 8) break;
        final r = rd32(lo) & 0xFFFF;
        wr32(lo, r);
        _setNZV0(r, 32);
        return 2;
      case 0x8: // NEG.B
        wr8(lo, _arith(0, rd8(lo), 0, 8, sub: true));
        return 2;
      case 0x9: // NEG.W
        wr16(lo, _arith(0, rd16(lo), 0, 16, sub: true));
        return 2;
      case 0xB: // NEG.L
        if (lo >= 8) break;
        wr32(lo, _arith(0, rd32(lo), 0, 32, sub: true));
        return 2;
      case 0xD: // EXTS.W
        final v = rd16(lo) & 0xFF;
        final r = v < 0x80 ? v : v | 0xFF00;
        wr16(lo, r);
        _setNZV0(r, 16);
        return 2;
      case 0xF: // EXTS.L
        if (lo >= 8) break;
        final v = rd32(lo) & 0xFFFF;
        final r = v < 0x8000 ? v : v | 0xFFFF0000;
        wr32(lo, r);
        _setNZV0(r, 32);
        return 2;
    }
    _illegal(instrStart, w0);
    return 0;
  }

  /// 0x6A / 0x6B: MOV byte/word with absolute 16- or 24-bit address.
  int _movAbs(int instrStart, int w0, {required int size}) {
    final b1 = w0 & 0xFF;
    final mode = b1 >> 4;
    final r = b1 & 0xF;
    int addr;
    int states;
    switch (mode & 0x7) {
      case 0x0: // aa:16 (sign-extended into the 24-bit space)
        addr = _sext16(_fetchW()) & 0xFFFFFF;
        states = 6;
        break;
      case 0x2: // aa:24 (stored as a padding byte + 3 address bytes)
        final hi = _fetchW();
        final lo = _fetchW();
        addr = (((hi & 0xFF) << 16) | lo) & 0xFFFFFF;
        states = 8;
        break;
      default:
        _illegal(instrStart, w0);
        return 0;
    }
    final store = (mode & 0x8) != 0;
    if (size == 8) {
      if (store) {
        final v = rd8(r);
        writeB(addr, v);
        _setNZV0(v, 8);
      } else {
        final v = readB(addr);
        wr8(r, v);
        _setNZV0(v, 8);
      }
    } else {
      if (store) {
        final v = rd16(r);
        writeW(addr, v);
        _setNZV0(v, 16);
      } else {
        final v = readW(addr);
        wr16(r, v);
        _setNZV0(v, 16);
      }
    }
    return states;
  }

  /// 0x78: MOV.B / MOV.W with 24-bit displacement:
  ///   78 s0 6A 2r 00 dd dd dd   MOV.B @(d:24,ERs), Rd
  ///   78 d0 6A Ar 00 dd dd dd   MOV.B Rs, @(d:24,ERd)  (6B for word)
  int _movDisp24(int instrStart, int w0) {
    final b1 = w0 & 0xFF;
    if ((b1 & 0x8F) != 0) {
      _illegal(instrStart, w0);
      return 0;
    }
    final ern = (b1 >> 4) & 7;
    final w1 = _fetchW();
    final b2 = w1 >> 8;
    final b3 = w1 & 0xFF;
    if (b2 != 0x6A && b2 != 0x6B) {
      _illegal(instrStart, w1);
      return 0;
    }
    if ((b3 & 0x70) != 0x20) {
      _illegal(instrStart, w1);
      return 0;
    }
    final store = (b3 & 0x80) != 0;
    final r = b3 & 0xF;
    final dHi = _fetchW();
    final dLo = _fetchW();
    var disp = ((dHi & 0xFF) << 16) | dLo;
    if (disp >= 0x800000) disp -= 0x1000000;
    final addr = (rd32(ern) + disp) & 0xFFFFFFFF;
    if (b2 == 0x6A) {
      if (store) {
        final v = rd8(r);
        writeB(addr, v);
        _setNZV0(v, 8);
      } else {
        final v = readB(addr);
        wr8(r, v);
        _setNZV0(v, 8);
      }
    } else {
      if (store) {
        final v = rd16(r);
        writeW(addr, v);
        _setNZV0(v, 16);
      } else {
        final v = readW(addr);
        wr16(r, v);
        _setNZV0(v, 16);
      }
    }
    return 10;
  }

  /// 0x79 / 0x7A: MOV/ADD/CMP/SUB/OR/XOR/AND with 16- or 32-bit immediate.
  int _immGroup(int instrStart, int w0, {required int size}) {
    final b1 = w0 & 0xFF;
    final op = b1 >> 4;
    final r = b1 & 0xF;
    final int imm;
    final int states;
    if (size == 16) {
      imm = _fetchW();
      states = 4;
    } else {
      if (r >= 8) {
        _illegal(instrStart, w0);
        return 0;
      }
      imm = (_fetchW() << 16) | _fetchW();
      states = 6;
    }
    int rdv() => size == 16 ? rd16(r) : rd32(r);
    void wrv(int v) => size == 16 ? wr16(r, v) : wr32(r, v);
    switch (op) {
      case 0x0: // MOV
        wrv(imm);
        _setNZV0(imm, size);
        return states;
      case 0x1: // ADD
        wrv(_arith(rdv(), imm, 0, size, sub: false));
        return states;
      case 0x2: // CMP
        _arith(rdv(), imm, 0, size, sub: true);
        return states;
      case 0x3: // SUB
        wrv(_arith(rdv(), imm, 0, size, sub: true));
        return states;
      case 0x4: // OR
        final v = rdv() | imm;
        wrv(v);
        _setNZV0(v, size);
        return states;
      case 0x5: // XOR
        final v = rdv() ^ imm;
        wrv(v);
        _setNZV0(v, size);
        return states;
      case 0x6: // AND
        final v = rdv() & imm;
        wrv(v);
        _setNZV0(v, size);
        return states;
      default:
        _illegal(instrStart, w0);
        return 0;
    }
  }

  /// 0x7B: EEPMOV.B (7B 5C 59 8F) / EEPMOV.W (7B D4 59 8F). Copies
  /// R4L/R4 bytes from @ER5 to @ER6, executed atomically here.
  int _eepmov(int instrStart, int w0) {
    final b1 = w0 & 0xFF;
    final w1 = _fetchW();
    if (w1 != 0x598F || (b1 != 0x5C && b1 != 0xD4)) {
      _illegal(instrStart, w0);
      return 0;
    }
    final word = b1 == 0xD4;
    var count = word ? rd16(4) : rd8(12); // R4 or R4L
    final n = count;
    var src = rd32(5);
    var dst = rd32(6);
    while (count > 0) {
      writeB(dst, readB(src));
      src = (src + 1) & 0xFFFFFFFF;
      dst = (dst + 1) & 0xFFFFFFFF;
      count--;
    }
    wr32(5, src);
    wr32(6, dst);
    if (word) {
      wr16(4, 0);
    } else {
      wr8(12, 0);
    }
    return 8 + 4 * n;
  }

  /// Register-destination BSET/BNOT/BCLR/BTST core: returns the updated
  /// byte (unchanged for BTST). [op] 0=BSET 1=BNOT 2=BCLR 3=BTST.
  int _bitOp(int op, int v, int bit) {
    final m = 1 << bit;
    switch (op) {
      case 0:
        return v | m;
      case 1:
        return v ^ m;
      case 2:
        return v & ~m;
      default:
        setFlag(H8Flag.z, (v & m) == 0);
        return v;
    }
  }

  /// BOR/BXOR/BAND/BLD (and their inverted BI- forms) against C.
  /// [op] 0=BOR 1=BXOR 2=BAND 3=BLD.
  void _bitLogic(int op, int v, int bit, bool inverted) {
    var b = (v >> bit) & 1;
    if (inverted) b ^= 1;
    final c = (ccr & H8Flag.c) != 0 ? 1 : 0;
    final int r;
    switch (op) {
      case 0:
        r = c | b;
        break;
      case 1:
        r = c ^ b;
        break;
      case 2:
        r = c & b;
        break;
      default:
        r = b;
        break;
    }
    setFlag(H8Flag.c, r != 0);
  }

  /// Bit-manipulation on memory: the second word of 7C/7D/7E/7F forms.
  /// [write] selects the read-modify-write group (7D/7F).
  int _bitMem(int instrStart, int addr, {required bool write}) {
    addr &= 0xFFFFFF;
    final w1 = _fetchW();
    final b2 = w1 >> 8;
    final b3 = w1 & 0xFF;
    if ((b3 & 0x0F) != 0) {
      _illegal(instrStart, w1);
      return 0;
    }
    final hi = (b3 >> 4) & 0x7;
    final inverted = (b3 & 0x80) != 0;
    if (write) {
      switch (b2) {
        case 0x60: // BSET Rn, @ERd
        case 0x61: // BNOT Rn, @ERd
        case 0x62: // BCLR Rn, @ERd
          final bit = rd8(b3 >> 4) & 7;
          writeB(addr, _bitOp(b2 & 3, readB(addr), bit));
          return 8;
        case 0x67: // BST / BIST #xx:3, @ERd
          final cSet = (ccr & H8Flag.c) != 0;
          final store = inverted ? !cSet : cSet;
          var v = readB(addr);
          v = store ? (v | (1 << hi)) : (v & ~(1 << hi));
          writeB(addr, v);
          return 8;
        case 0x70: // BSET #xx:3, @ERd
        case 0x71: // BNOT #xx:3, @ERd
        case 0x72: // BCLR #xx:3, @ERd
          if (inverted) break;
          writeB(addr, _bitOp(b2 & 3, readB(addr), hi));
          return 8;
      }
    } else {
      switch (b2) {
        case 0x63: // BTST Rn, @ERd
          final bit = rd8(b3 >> 4) & 7;
          setFlag(H8Flag.z, (readB(addr) & (1 << bit)) == 0);
          return 6;
        case 0x73: // BTST #xx:3, @ERd
          if (inverted) break;
          setFlag(H8Flag.z, (readB(addr) & (1 << hi)) == 0);
          return 6;
        case 0x74: // BOR / BIOR
        case 0x75: // BXOR / BIXOR
        case 0x76: // BAND / BIAND
        case 0x77: // BLD / BILD
          _bitLogic(b2 & 3, readB(addr), hi, inverted);
          return 6;
      }
    }
    _illegal(instrStart, w1);
    return 0;
  }

  /// DIVXU: unsigned divide. Byte: Rd16 / Rs8 -> RdL quotient, RdH
  /// remainder. Word: ERd32 / Rs16 -> Rd quotient, Ed remainder.
  /// N = divisor MSB, Z = divisor is zero (table A-1 notes 6, 7). A zero
  /// divisor leaves the destination unchanged (result is undefined on
  /// real hardware).
  int _divxu(int b1, {required bool byte}) {
    if (byte) {
      final rd = b1 & 0xF;
      final divisor = rd8(b1 >> 4);
      setFlag(H8Flag.n, (divisor & 0x80) != 0);
      setFlag(H8Flag.z, divisor == 0);
      if (divisor != 0) {
        final dividend = rd16(rd);
        final q = (dividend ~/ divisor) & 0xFF;
        final r = (dividend % divisor) & 0xFF;
        wr16(rd, (r << 8) | q);
      }
      return 14;
    } else {
      final erd = b1 & 7;
      final divisor = rd16(b1 >> 4);
      setFlag(H8Flag.n, (divisor & 0x8000) != 0);
      setFlag(H8Flag.z, divisor == 0);
      if (divisor != 0) {
        final dividend = rd32(erd);
        final q = (dividend ~/ divisor) & 0xFFFF;
        final r = (dividend % divisor) & 0xFFFF;
        wr32(erd, (r << 16) | q);
      }
      return 22;
    }
  }

  /// DIVXS: signed divide. N = quotient would be negative (dividend and
  /// divisor signs differ), Z = divisor is zero (table A-1 notes 7, 8).
  /// Quotient truncates toward zero; the remainder takes the dividend's
  /// sign (standard H8 semantics).
  int _divxs(int b1, {required bool byte}) {
    if (byte) {
      final rd = b1 & 0xF;
      final divisor = _sext8(rd8(b1 >> 4));
      final dividend = _sext16(rd16(rd));
      setFlag(H8Flag.n, (dividend < 0) != (divisor < 0));
      setFlag(H8Flag.z, divisor == 0);
      if (divisor != 0) {
        final q = (dividend ~/ divisor) & 0xFF;
        final r = (dividend.remainder(divisor)) & 0xFF;
        wr16(rd, (r << 8) | q);
      }
      return 16;
    } else {
      final erd = b1 & 7;
      final divisor = _sext16(rd16(b1 >> 4));
      final v32 = rd32(erd);
      final dividend = v32 < 0x80000000 ? v32 : v32 - 0x100000000;
      setFlag(H8Flag.n, (dividend < 0) != (divisor < 0));
      setFlag(H8Flag.z, divisor == 0);
      if (divisor != 0) {
        final q = (dividend ~/ divisor) & 0xFFFF;
        final r = (dividend.remainder(divisor)) & 0xFFFF;
        wr32(erd, (r << 16) | q);
      }
      return 24;
    }
  }

  /// DAA: decimal adjust after BCD addition. C is set when the adjustment
  /// produces a carry, otherwise retains its previous value (note 4);
  /// H and V are left unchanged (undefined on hardware).
  void _daa(int rd) {
    var v = rd8(rd);
    final lower = v & 0xF;
    final upper = v >> 4;
    final hSet = (ccr & H8Flag.h) != 0;
    final cSet = (ccr & H8Flag.c) != 0;
    var add = 0;
    var carry = cSet;
    if (hSet || lower > 9) add |= 0x06;
    if (cSet || upper > 9 || (upper == 9 && lower > 9)) {
      add |= 0x60;
      carry = true;
    }
    v = (v + add) & 0xFF;
    wr8(rd, v);
    var f = ccr & ~(H8Flag.n | H8Flag.z | H8Flag.c);
    if ((v & 0x80) != 0) f |= H8Flag.n;
    if (v == 0) f |= H8Flag.z;
    if (carry) f |= H8Flag.c;
    ccr = f;
  }

  /// Disassembles the instruction at [addr] (side-effect free).
  H8Disasm disassemble(int addr) => disassembleH8(mem.peek, addr);

  /// DAS: decimal adjust after BCD subtraction. C retains its value.
  void _das(int rd) {
    var v = rd8(rd);
    var sub = 0;
    if ((ccr & H8Flag.h) != 0) sub |= 0x06;
    if ((ccr & H8Flag.c) != 0) sub |= 0x60;
    v = (v - sub) & 0xFF;
    wr8(rd, v);
    var f = ccr & ~(H8Flag.n | H8Flag.z);
    if ((v & 0x80) != 0) f |= H8Flag.n;
    if (v == 0) f |= H8Flag.z;
    ccr = f;
  }
}


/// Lets a condition read the machine without knowing what a CPU is.
class _CpuView implements MachineView {
  _CpuView(this.cpu);
  final H8Cpu cpu;

  @override
  int readByte(int addr) => cpu.peekBus(addr & 0xFFFFFF);

  @override
  int? register(String name) => switch (name) {
        'pc' => cpu.pc,
        'ccr' => cpu.ccr,
        'cycles' => cpu.cycles,
        'sp' => cpu.er[7],
        'i' => (cpu.ccr >> 7) & 1,
        'ui' => (cpu.ccr >> 6) & 1,
        'h' => (cpu.ccr >> 5) & 1,
        'u' => (cpu.ccr >> 4) & 1,
        'n' => (cpu.ccr >> 3) & 1,
        'z' => (cpu.ccr >> 2) & 1,
        'v' => (cpu.ccr >> 1) & 1,
        'c' => cpu.ccr & 1,
        _ => _numbered(name),
      };

  int? _numbered(String name) {
    if (name.length == 3 && name.startsWith('er')) {
      final n = int.tryParse(name[2]);
      return n == null || n > 7 ? null : cpu.er[n];
    }
    if (name.length == 2) {
      final n = int.tryParse(name[1]);
      if (n == null || n > 7) return null;
      if (name[0] == 'r') return cpu.er[n] & 0xFFFF;
      if (name[0] == 'e') return (cpu.er[n] >> 16) & 0xFFFF;
    }
    return null;
  }
}
