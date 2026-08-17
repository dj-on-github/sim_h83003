// 16-Bit Integrated Timer Unit (ITU) for the H8/3003.
//
// Five 16-bit counter channels, each with two general registers that can act
// as compare-match targets, a prescaler off the system clock, and three
// interrupt sources (compare match A and B, and overflow). Channels 3 and 4
// additionally have buffer registers.
//
// The counters run off the simulated cycle count, so a firmware timing loop
// that waits on a compare-match flag completes, and the interrupt handlers a
// program installs actually fire.
//
// Reference: H8/3003 hardware manual section 10 (page 281). Registers occupy
// H'FFFF60-H'FFFF9F.

/// Timer control register (TCR) fields.
class ItuControl {
  /// Counter clear source, bits 6-5.
  static const int cclrMask = 0x60;
  static const int cclrNone = 0x00;
  static const int cclrGra = 0x20;
  static const int cclrGrb = 0x40;
  static const int cclrSync = 0x60;

  /// Clock select, bits 2-0. Values 0-3 are internal, 4-7 external.
  static const int tpscMask = 0x07;
}

/// Timer status register (TSR) flags. Bits 7-3 read as 1.
class ItuStatus {
  static const int ovf = 0x04;
  static const int imfb = 0x02;
  static const int imfa = 0x01;
  static const int flags = ovf | imfb | imfa;
  static const int reserved = 0xF8;
}

/// Timer interrupt enable register (TIER) flags. Bits 7-3 read as 1.
class ItuInterrupt {
  static const int ovie = 0x04;
  static const int imieb = 0x02;
  static const int imiea = 0x01;
  static const int reserved = 0xF8;
}

/// One ITU channel.
class ItuChannel {
  ItuChannel({required this.index, required this.hasBuffers});

  /// Channel number, 0 to 4.
  final int index;

  /// Channels 3 and 4 have the BRA/BRB buffer registers.
  final bool hasBuffers;

  String get name => 'ITU$index';

  /// First exception vector of this channel: IMIA, then IMIB, then OVI.
  /// Channel 0 starts at 24 and each channel is four vectors further on.
  int get vectorBase => 24 + index * 4;

  int tcr = 0x80;
  int tior = 0x88;
  int tier = ItuInterrupt.reserved;
  int tsr = ItuStatus.reserved;
  int tcnt = 0;
  int gra = 0xFFFF;
  int grb = 0xFFFF;
  int bra = 0xFFFF;
  int brb = 0xFFFF;

  /// Clock cycles not yet consumed by the prescaler.
  int _prescale = 0;

  /// True while TSTR has this channel's start bit set.
  bool running = false;

  /// Prescaler divisor, or null when an external clock is selected — those
  /// pins are not modelled, so the counter simply does not advance.
  int? get divisor {
    final tpsc = tcr & ItuControl.tpscMask;
    return tpsc < 4 ? (1 << tpsc) : null;
  }

  /// Human-readable clock source.
  String get clockSource {
    const names = ['φ', 'φ/2', 'φ/4', 'φ/8', 'TCLKA', 'TCLKB', 'TCLKC', 'TCLKD'];
    return names[tcr & ItuControl.tpscMask];
  }

  /// What clears the counter.
  String get clearSource {
    switch (tcr & ItuControl.cclrMask) {
      case ItuControl.cclrGra:
        return 'GRA match';
      case ItuControl.cclrGrb:
        return 'GRB match';
      case ItuControl.cclrSync:
        return 'synchronous';
      default:
        return 'free-running';
    }
  }

  void reset() {
    tcr = 0x80;
    tior = 0x88;
    tier = ItuInterrupt.reserved;
    tsr = ItuStatus.reserved;
    tcnt = 0;
    gra = 0xFFFF;
    grb = 0xFFFF;
    bra = 0xFFFF;
    brb = 0xFFFF;
    _prescale = 0;
    running = false;
  }

  /// Advances the counter by [cycles] system-clock states.
  void advance(int cycles) {
    final div = divisor;
    if (!running || div == null || cycles <= 0) return;
    _prescale += cycles;
    var steps = _prescale ~/ div;
    _prescale %= div;
    if (steps <= 0) return;

    // Walk event to event rather than one count at a time, so a long
    // interval costs no more than the number of matches inside it.
    while (steps > 0) {
      var chunk = 0x10000 - tcnt; // distance to overflow
      for (final target in [gra, grb]) {
        if (target > tcnt) {
          final d = target - tcnt;
          if (d < chunk) chunk = d;
        }
      }
      if (chunk > steps) chunk = steps;
      tcnt += chunk;
      steps -= chunk;

      var cleared = false;
      if (tcnt == gra) {
        tsr |= ItuStatus.imfa;
        if ((tcr & ItuControl.cclrMask) == ItuControl.cclrGra) {
          tcnt = 0;
          cleared = true;
        }
      }
      if (tcnt == grb) {
        tsr |= ItuStatus.imfb;
        if ((tcr & ItuControl.cclrMask) == ItuControl.cclrGrb) {
          tcnt = 0;
          cleared = true;
        }
      }
      if (!cleared && tcnt >= 0x10000) {
        tcnt = 0;
        tsr |= ItuStatus.ovf;
      }
    }
  }

  /// The exception vector this channel is requesting, or null.
  int? pendingVector() {
    if ((tier & ItuInterrupt.imiea) != 0 && (tsr & ItuStatus.imfa) != 0) {
      return vectorBase;
    }
    if ((tier & ItuInterrupt.imieb) != 0 && (tsr & ItuStatus.imfb) != 0) {
      return vectorBase + 1;
    }
    if ((tier & ItuInterrupt.ovie) != 0 && (tsr & ItuStatus.ovf) != 0) {
      return vectorBase + 2;
    }
    return null;
  }
}

/// The whole timer unit: five channels plus the shared registers.
class Itu {
  Itu() {
    for (var i = 0; i < 5; i++) {
      channels.add(ItuChannel(index: i, hasBuffers: i >= 3));
    }
  }

  static const int base = 0xFFFF60;
  static const int end = 0xFFFF9F;

  final List<ItuChannel> channels = [];

  /// Shared registers.
  int tstr = 0xE0; // reserved bits read as 1
  int tsnc = 0xE0;
  int tmdr = 0x80;
  int tfcr = 0xC0;
  int toer = 0xFF;
  int tocr = 0xFF;

  /// Mirrors register values into simulated memory for the views.
  void Function(int addr, int value)? mirror;

  int _lastCycles = 0;

  bool owns(int addr) => addr >= base && addr <= end;

  void reset() {
    tstr = 0xE0;
    tsnc = 0xE0;
    tmdr = 0x80;
    tfcr = 0xC0;
    toer = 0xFF;
    tocr = 0xFF;
    for (final c in channels) {
      c.reset();
    }
    _lastCycles = 0;
    syncToMemory();
  }

  /// Byte offsets of a channel's registers from [base]. Channels 0-2 occupy
  /// 10 bytes each from H'64; channel 3 has 14 from H'82; TOER/TOCR sit at
  /// H'90-H'91; channel 4 has 14 from H'92.
  int _channelBase(int ch) {
    switch (ch) {
      case 0:
        return 0x64;
      case 1:
        return 0x6E;
      case 2:
        return 0x78;
      case 3:
        return 0x82;
      default:
        return 0x92;
    }
  }

  /// The channel owning [addr], and the register offset within it, or null
  /// when the address is one of the shared registers.
  (ItuChannel, int)? _locate(int addr) {
    final lo = addr & 0xFF;
    for (final c in channels) {
      final cb = _channelBase(c.index);
      final len = c.hasBuffers ? 14 : 10;
      if (lo >= cb && lo < cb + len) return (c, lo - cb);
    }
    return null;
  }

  int read(int addr) {
    final lo = addr & 0xFF;
    switch (lo) {
      case 0x60:
        return tstr;
      case 0x61:
        return tsnc;
      case 0x62:
        return tmdr;
      case 0x63:
        return tfcr;
      case 0x90:
        return toer;
      case 0x91:
        return tocr;
    }
    final found = _locate(addr);
    if (found == null) return 0xFF;
    final (c, off) = found;
    switch (off) {
      case 0:
        return c.tcr;
      case 1:
        return c.tior;
      case 2:
        return c.tier;
      case 3:
        return c.tsr;
      case 4:
        return (c.tcnt >> 8) & 0xFF;
      case 5:
        return c.tcnt & 0xFF;
      case 6:
        return (c.gra >> 8) & 0xFF;
      case 7:
        return c.gra & 0xFF;
      case 8:
        return (c.grb >> 8) & 0xFF;
      case 9:
        return c.grb & 0xFF;
      case 10:
        return (c.bra >> 8) & 0xFF;
      case 11:
        return c.bra & 0xFF;
      case 12:
        return (c.brb >> 8) & 0xFF;
      default:
        return c.brb & 0xFF;
    }
  }

  void write(int addr, int value) {
    value &= 0xFF;
    final lo = addr & 0xFF;
    switch (lo) {
      case 0x60:
        tstr = value | 0xE0;
        for (final c in channels) {
          c.running = (value & (1 << c.index)) != 0;
        }
        syncToMemory();
        return;
      case 0x61:
        tsnc = value | 0xE0;
        syncToMemory();
        return;
      case 0x62:
        tmdr = value | 0x80;
        syncToMemory();
        return;
      case 0x63:
        tfcr = value | 0xC0;
        syncToMemory();
        return;
      case 0x90:
        toer = value;
        syncToMemory();
        return;
      case 0x91:
        tocr = value;
        syncToMemory();
        return;
    }
    final found = _locate(addr);
    if (found == null) return;
    final (c, off) = found;
    switch (off) {
      case 0:
        c.tcr = value | 0x80;
        break;
      case 1:
        c.tior = value;
        break;
      case 2:
        c.tier = value | ItuInterrupt.reserved;
        break;
      case 3:
        // Status flags can only be cleared, never set.
        c.tsr = (c.tsr & ~(ItuStatus.flags & ~value)) | ItuStatus.reserved;
        break;
      case 4:
        c.tcnt = ((value << 8) | (c.tcnt & 0xFF)) & 0xFFFF;
        break;
      case 5:
        c.tcnt = ((c.tcnt & 0xFF00) | value) & 0xFFFF;
        break;
      case 6:
        c.gra = ((value << 8) | (c.gra & 0xFF)) & 0xFFFF;
        break;
      case 7:
        c.gra = ((c.gra & 0xFF00) | value) & 0xFFFF;
        break;
      case 8:
        c.grb = ((value << 8) | (c.grb & 0xFF)) & 0xFFFF;
        break;
      case 9:
        c.grb = ((c.grb & 0xFF00) | value) & 0xFFFF;
        break;
      case 10:
        c.bra = ((value << 8) | (c.bra & 0xFF)) & 0xFFFF;
        break;
      case 11:
        c.bra = ((c.bra & 0xFF00) | value) & 0xFFFF;
        break;
      case 12:
        c.brb = ((value << 8) | (c.brb & 0xFF)) & 0xFFFF;
        break;
      default:
        c.brb = ((c.brb & 0xFF00) | value) & 0xFFFF;
        break;
    }
    syncToMemory();
  }

  /// Advances every running counter to [cycles].
  void tick(int cycles) {
    final delta = cycles - _lastCycles;
    if (delta <= 0) {
      _lastCycles = cycles;
      return;
    }
    _lastCycles = cycles;
    for (final c in channels) {
      if (c.running) c.advance(delta);
    }
    // Deliberately no syncToMemory() here: the counters change on every
    // instruction, and mirroring 64 registers each time dominated execution
    // time. Views read live values through the bus instead.
  }

  /// True when at least one counter is running, so the caller can skip the
  /// tick entirely when the unit is idle.
  bool get anyRunning {
    for (final c in channels) {
      if (c.running) return true;
    }
    return false;
  }

  /// The highest-priority vector any channel is requesting, or null.
  int? pendingVector() {
    for (final c in channels) {
      final v = c.pendingVector();
      if (v != null) return v;
    }
    return null;
  }

  void syncToMemory() {
    final m = mirror;
    if (m == null) return;
    for (var a = base; a <= end; a++) {
      m(a, read(a));
    }
  }
}
