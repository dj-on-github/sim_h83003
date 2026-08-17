// DMA controller (DMAC) for the H8/3003.
//
// Four channels, each split into an A and a B half. The halves work
// independently in *short address mode* — one memory address register, one
// fixed I/O address, a transfer count and an activation source each — or
// together in *full address mode*, where MARA is the source, MARB the
// destination and the channel moves a block of memory.
//
// Transfers are carried out when the programmed activation source fires:
// an ITU compare match, an SCI transmit-data-empty or receive-data-full, or
// an auto-request in full address mode. That is what makes the difference
// between a firmware DMA setup sitting inert and the data actually moving.
//
// Reference: H8/3003 hardware manual section 8 (page 173). Registers occupy
// H'FFFF20-H'FFFF5F.
//
// Simplification: a transfer happens between instructions rather than by
// stealing a precise bus cycle, and burst mode completes in one go. The
// register state the firmware observes is the same either way.

/// Data transfer control register bits, short address mode.
class DmacControl {
  static const int dte = 0x80; // transfer enable
  static const int dtsz = 0x40; // 0 = byte, 1 = word
  static const int dtid = 0x20; // 0 = increment MAR, 1 = decrement
  static const int rpe = 0x10; // repeat enable
  static const int dtie = 0x08; // DEND interrupt enable
  static const int dts = 0x07; // activation source

  /// Full address mode: DTS2A and DTS1A both set.
  static const int fullAddress = 0x06;
}

/// Data transfer control register B bits, full address mode.
class DmacControlB {
  static const int dtme = 0x80; // transfer master enable
  static const int daid = 0x20; // destination increment/decrement
  static const int daide = 0x10; // destination update enable
  static const int tms = 0x08; // block area is source (1) or destination (0)
  static const int dts = 0x07;
}

/// Full address mode source-address control, in DTCRA.
class DmacControlA {
  static const int said = 0x20;
  static const int saide = 0x10;

  /// DTS0A selects normal (0) or block transfer (1) mode.
  static const int blockMode = 0x01;
}

/// Activation sources, DTS2-0.
class DmacSource {
  static const int ituMatch0 = 0;
  static const int ituMatch1 = 1;
  static const int ituMatch2 = 2;
  static const int ituMatch3 = 3;
  static const int sciTransmit = 4;
  static const int sciReceive = 5;
}

/// One half of a channel: the register set the manual calls A or B.
class DmacHalf {
  DmacHalf(this.label);

  /// "0A", "0B", and so on.
  final String label;

  /// 24-bit memory address register.
  int mar = 0;

  /// 16-bit transfer counter.
  int etcr = 0;

  /// Low byte of the fixed I/O address, i.e. H'FFFF00 | ioar.
  int ioar = 0;

  /// Data transfer control register.
  int dtcr = 0;

  /// Values reloaded in repeat mode.
  int marReload = 0;
  int etcrReload = 0;

  bool get enabled => (dtcr & DmacControl.dte) != 0;
  bool get wordSize => (dtcr & DmacControl.dtsz) != 0;
  int get step => (wordSize ? 2 : 1) * ((dtcr & DmacControl.dtid) != 0 ? -1 : 1);
  int get source => dtcr & DmacControl.dts;

  /// Repeat mode reloads MAR and ETCR each time the count runs out; idle
  /// mode is selected by RPE and DTIE together (manual section 8.2.4).
  bool get repeatMode =>
      (dtcr & DmacControl.rpe) != 0 && (dtcr & DmacControl.dtie) == 0;

  /// The fixed device address this half talks to.
  int get ioAddress => 0xFFFF00 | ioar;

  /// Transfers already performed since the count was loaded.
  int transfers = 0;

  void reset() {
    mar = 0;
    etcr = 0;
    ioar = 0;
    dtcr = 0;
    marReload = 0;
    etcrReload = 0;
    transfers = 0;
  }

  String get modeName {
    if ((dtcr & DmacControl.rpe) == 0) return 'I/O';
    return (dtcr & DmacControl.dtie) != 0 ? 'idle' : 'repeat';
  }

  String get sourceName {
    switch (source) {
      case DmacSource.ituMatch0:
        return 'ITU0 match A';
      case DmacSource.ituMatch1:
        return 'ITU1 match A';
      case DmacSource.ituMatch2:
        return 'ITU2 match A';
      case DmacSource.ituMatch3:
        return 'ITU3 match A';
      case DmacSource.sciTransmit:
        return 'SCI transmit';
      case DmacSource.sciReceive:
        return 'SCI receive';
      default:
        return 'full address';
    }
  }
}

/// One DMAC channel: two halves that either run independently or combine.
class DmacChannel {
  DmacChannel(this.index)
      : a = DmacHalf('${index}A'),
        b = DmacHalf('${index}B');

  final int index;
  final DmacHalf a;
  final DmacHalf b;

  /// Base address of this channel's sixteen registers.
  int get base => 0xFFFF20 + index * 0x10;

  /// DEND vector of the A half; the B half is the next one along.
  int get vectorBase => 44 + index * 2;

  /// The A and B halves combine when DTS2A and DTS1A are both set.
  bool get fullAddress =>
      (a.dtcr & DmacControl.fullAddress) == DmacControl.fullAddress;

  /// In full address mode both DTE and DTME must be set.
  bool get fullEnabled =>
      fullAddress && a.enabled && (b.dtcr & DmacControlB.dtme) != 0;

  bool get blockMode => (a.dtcr & DmacControlA.blockMode) != 0;

  String get modeName {
    if (!fullAddress) return 'short address';
    return blockMode ? 'full address, block' : 'full address, normal';
  }

  void reset() {
    a.reset();
    b.reset();
  }
}

/// The DMA controller.
class Dmac {
  Dmac() {
    for (var i = 0; i < 4; i++) {
      channels.add(DmacChannel(i));
    }
  }

  static const int base = 0xFFFF20;
  static const int end = 0xFFFF5F;

  final List<DmacChannel> channels = [];

  /// Bus access, supplied by the CPU so transfers go through the same path
  /// as any other access — including the on-chip peripherals.
  late int Function(int addr) readByte;
  late void Function(int addr, int value) writeByte;

  /// True when the given ITU channel is signalling compare match A.
  late bool Function(int channel) ituMatchA;

  /// Clears that flag, as a DMA transfer does on real hardware.
  late void Function(int channel) clearItuMatchA;

  /// The SCI status/data hooks, keyed by the fixed I/O address the channel
  /// was pointed at. Returning null means "no SCI owns that address".
  late bool Function(int ioAddress)? sciReady;
  late void Function(int ioAddress)? sciAcknowledge;

  /// Mirrors register values into memory for the views.
  void Function(int addr, int value)? mirror;

  /// Total transfers performed, for display.
  int transferCount = 0;

  bool owns(int addr) => addr >= base && addr <= end;

  /// True when any channel is armed. Checked before doing any per-instruction
  /// work, so an idle controller is nearly free.
  bool get anyEnabled {
    for (final c in channels) {
      if (c.fullAddress) {
        if (c.fullEnabled) return true;
      } else if (c.a.enabled || c.b.enabled) {
        return true;
      }
    }
    return false;
  }

  void reset() {
    for (final c in channels) {
      c.reset();
    }
    transferCount = 0;
    syncToMemory();
  }

  /// Locates the half and register offset an address refers to.
  (DmacHalf, int)? _locate(int addr) {
    final lo = addr & 0xFF;
    for (final c in channels) {
      final cb = c.base & 0xFF;
      if (lo >= cb && lo < cb + 8) return (c.a, lo - cb);
      if (lo >= cb + 8 && lo < cb + 16) return (c.b, lo - cb - 8);
    }
    return null;
  }

  int read(int addr) {
    final found = _locate(addr);
    if (found == null) return 0xFF;
    final (h, off) = found;
    switch (off) {
      case 0:
        return 0xFF; // MARxxR: reserved, reads as all ones
      case 1:
        return (h.mar >> 16) & 0xFF;
      case 2:
        return (h.mar >> 8) & 0xFF;
      case 3:
        return h.mar & 0xFF;
      case 4:
        return (h.etcr >> 8) & 0xFF;
      case 5:
        return h.etcr & 0xFF;
      case 6:
        return h.ioar;
      default:
        return h.dtcr;
    }
  }

  void write(int addr, int value) {
    value &= 0xFF;
    final found = _locate(addr);
    if (found == null) return;
    final (h, off) = found;
    switch (off) {
      case 0:
        break; // reserved
      case 1:
        h.mar = ((value << 16) | (h.mar & 0x00FFFF)) & 0xFFFFFF;
        h.marReload = h.mar;
        break;
      case 2:
        h.mar = ((h.mar & 0xFF00FF) | (value << 8)) & 0xFFFFFF;
        h.marReload = h.mar;
        break;
      case 3:
        h.mar = ((h.mar & 0xFFFF00) | value) & 0xFFFFFF;
        h.marReload = h.mar;
        break;
      case 4:
        h.etcr = ((value << 8) | (h.etcr & 0xFF)) & 0xFFFF;
        h.etcrReload = h.etcr;
        break;
      case 5:
        h.etcr = ((h.etcr & 0xFF00) | value) & 0xFFFF;
        h.etcrReload = h.etcr;
        break;
      case 6:
        h.ioar = value;
        break;
      default:
        final wasEnabled = h.enabled;
        h.dtcr = value;
        if (!wasEnabled && h.enabled) h.transfers = 0;
        break;
    }
    syncToMemory();
  }

  /// Performs any transfers whose activation source is asserting. Returns a
  /// DEND vector to raise, or null.
  int? service() {
    if (!anyEnabled) return null;
    for (final c in channels) {
      final v = c.fullAddress ? _serviceFull(c) : _serviceShort(c);
      if (v != null) return v;
    }
    return null;
  }

  /// Short address mode: each half moves data between its memory address
  /// and its fixed I/O address whenever its source fires.
  int? _serviceShort(DmacChannel c) {
    for (final h in [c.a, c.b]) {
      if (!h.enabled) continue;
      if (!_triggered(h)) continue;

      final receiving = h.source == DmacSource.sciReceive;
      if (receiving) {
        _move(h.ioAddress, h.mar, h.wordSize, fixedSource: true);
      } else {
        _move(h.mar, h.ioAddress, h.wordSize, fixedDestination: true);
      }
      _acknowledge(h);
      h.mar = (h.mar + h.step) & 0xFFFFFF;
      h.transfers++;
      transferCount++;

      h.etcr = (h.etcr - 1) & 0xFFFF;
      if (h.etcr == 0) {
        if (h.repeatMode) {
          h.mar = h.marReload;
          h.etcr = h.etcrReload;
        } else {
          h.dtcr &= ~DmacControl.dte; // done
          syncToMemory();
          if ((h.dtcr & DmacControl.dtie) != 0) {
            return c.vectorBase + (identical(h, c.a) ? 0 : 1);
          }
        }
      }
      syncToMemory();
      return null; // one transfer per service call
    }
    return null;
  }

  /// Full address mode: memory to memory, MARA to MARB.
  int? _serviceFull(DmacChannel c) {
    if (!c.fullEnabled) return null;
    final auto = (c.b.dtcr & DmacControlB.dts) < 4 && !c.blockMode;
    if (!auto && !_triggeredFull(c)) return null;

    // Auto-request burst transfers everything at once; everything else moves
    // one unit per service call.
    final burst = (c.b.dtcr & DmacControlB.dts) == 0 && !c.blockMode;
    var moved = 0;
    do {
      _move(c.a.mar, c.b.mar, c.a.wordSize);
      final unit = c.a.wordSize ? 2 : 1;
      if ((c.a.dtcr & DmacControlA.saide) != 0) {
        c.a.mar = (c.a.mar +
                unit * ((c.a.dtcr & DmacControlA.said) != 0 ? -1 : 1)) &
            0xFFFFFF;
      }
      if ((c.b.dtcr & DmacControlB.daide) != 0) {
        c.b.mar = (c.b.mar +
                unit * ((c.b.dtcr & DmacControlB.daid) != 0 ? -1 : 1)) &
            0xFFFFFF;
      }
      c.a.transfers++;
      transferCount++;
      moved++;
      c.a.etcr = (c.a.etcr - 1) & 0xFFFF;
      if (c.a.etcr == 0) {
        c.a.dtcr &= ~DmacControl.dte;
        syncToMemory();
        if ((c.a.dtcr & DmacControl.dtie) != 0) return c.vectorBase;
        return null;
      }
      // Guard against a runaway burst if the count is enormous.
    } while (burst && moved < 0x10000);
    _acknowledgeFull(c);
    syncToMemory();
    return null;
  }

  /// Is this half's activation source asserting?
  bool _triggered(DmacHalf h) {
    switch (h.source) {
      case DmacSource.ituMatch0:
      case DmacSource.ituMatch1:
      case DmacSource.ituMatch2:
      case DmacSource.ituMatch3:
        return ituMatchA(h.source);
      case DmacSource.sciTransmit:
      case DmacSource.sciReceive:
        return sciReady?.call(h.ioAddress) ?? false;
      default:
        return false;
    }
  }

  bool _triggeredFull(DmacChannel c) {
    final dts = c.b.dtcr & DmacControlB.dts;
    if (dts < 4) return ituMatchA(dts & 3);
    return sciReady?.call(c.b.mar) ?? false;
  }

  void _acknowledge(DmacHalf h) {
    switch (h.source) {
      case DmacSource.ituMatch0:
      case DmacSource.ituMatch1:
      case DmacSource.ituMatch2:
      case DmacSource.ituMatch3:
        clearItuMatchA(h.source);
        break;
      case DmacSource.sciTransmit:
      case DmacSource.sciReceive:
        sciAcknowledge?.call(h.ioAddress);
        break;
    }
  }

  void _acknowledgeFull(DmacChannel c) {
    final dts = c.b.dtcr & DmacControlB.dts;
    if (dts < 4) clearItuMatchA(dts & 3);
  }

  void _move(int from, int to, bool word,
      {bool fixedSource = false, bool fixedDestination = false}) {
    if (word) {
      final hi = readByte(from);
      final lo = readByte(fixedSource ? from : from + 1);
      writeByte(to, hi);
      writeByte(fixedDestination ? to : to + 1, lo);
    } else {
      writeByte(to, readByte(from));
    }
  }

  void syncToMemory() {
    final m = mirror;
    if (m == null) return;
    for (var a = base; a <= end; a++) {
      m(a, read(a));
    }
  }
}
