// Serial Communication Interface (SCI) for the H8/3003.
//
// Models one of the chip's two SCI channels closely enough that firmware
// talking to it makes progress: the transmit-data-empty and transmit-end
// flags follow a real character time, so a polling loop like
//
//     BTST #7,@SSR0:8      ; wait for TDRE
//     BEQ   back
//     MOV.B R6H,@TDR0:8    ; write the byte
//     BCLR  #7,@SSR0:8     ; clear TDRE, starting the transmission
//
// completes instead of spinning forever.
//
// Reference: H8/3003 hardware manual section 13 (page 423). Registers are at
// H'FFFFB0-H'FFFFB5 for channel 0 and H'FFFFB8-H'FFFFBD for channel 1.

/// Serial status register (SSR) bit masks.
class SciStatus {
  static const int tdre = 0x80; // transmit data register empty
  static const int rdrf = 0x40; // receive data register full
  static const int orer = 0x20; // overrun error
  static const int fer = 0x10; // framing error
  static const int per = 0x08; // parity error
  static const int tend = 0x04; // transmit end
  static const int mpb = 0x02; // multiprocessor bit
  static const int mpbt = 0x01; // multiprocessor bit transfer

  /// Flags the CPU may clear by writing 0 (it can never write a 1).
  static const int clearable = tdre | rdrf | orer | fer | per;
}

/// Serial control register (SCR) bit masks.
class SciControl {
  static const int tie = 0x80; // transmit-data-empty interrupt enable
  static const int rie = 0x40; // receive-data-full interrupt enable
  static const int te = 0x20; // transmit enable
  static const int re = 0x10; // receive enable
  static const int mpie = 0x08; // multiprocessor interrupt enable
  static const int teie = 0x04; // transmit-end interrupt enable
  static const int cke1 = 0x02;
  static const int cke0 = 0x01;
}

/// Serial mode register (SMR) bit masks.
class SciMode {
  static const int ca = 0x80; // 0 = asynchronous, 1 = synchronous
  static const int chr = 0x40; // 0 = 8-bit data, 1 = 7-bit
  static const int pe = 0x20; // parity enable
  static const int oe = 0x10; // 0 = even parity, 1 = odd
  static const int stop = 0x08; // 0 = 1 stop bit, 1 = 2 stop bits
  static const int mp = 0x04; // multiprocessor mode
  static const int cks = 0x03; // clock select
}

/// One SCI channel.
class SciChannel {
  SciChannel({
    required this.name,
    required this.base,
    required this.vectorBase,
  });

  /// Display name, e.g. "SCI0".
  final String name;

  /// Address of SMR; the other registers follow it.
  final int base;

  /// Exception vector of this channel's ERI; RXI, TXI and TEI follow it.
  final int vectorBase;

  int get smrAddr => base;
  int get brrAddr => base + 1;
  int get scrAddr => base + 2;
  int get tdrAddr => base + 3;
  int get ssrAddr => base + 4;
  int get rdrAddr => base + 5;

  // Registers, with their reset values (SSR resets to H'84: TDRE and TEND).
  int smr = 0x00;
  int brr = 0xFF;
  int scr = 0x00;
  int tdr = 0xFF;
  int ssr = SciStatus.tdre | SciStatus.tend;
  int rdr = 0x00;

  /// Mirrors register values into simulated memory so the memory view and
  /// the disassembler see the same values the CPU reads.
  void Function(int addr, int value)? mirror;

  /// The transmit shift register: busy until [_tsrDoneAt] states.
  bool _tsrBusy = false;
  int _tsrDoneAt = 0;

  /// Bytes the firmware has transmitted, oldest first.
  final List<int> txLog = [];

  /// Called with each byte as it leaves the transmitter, when set. This is
  /// how the channel is bridged to a real serial port; the model itself has
  /// no idea whether anything is listening.
  void Function(int byte)? onTransmit;

  /// How many transmitted bytes to keep for display.
  static const int txLogLimit = 8192;

  /// Total transmitted, including any dropped from the head of [txLog].
  int txCount = 0;

  /// Bytes waiting to be delivered to the receiver.
  final List<int> rxQueue = [];
  int _rxReadyAt = 0;

  /// Bits in one character: start + data + optional parity + stop bit(s).
  int get bitsPerChar =>
      1 +
      ((smr & SciMode.chr) != 0 ? 7 : 8) +
      ((smr & SciMode.pe) != 0 ? 1 : 0) +
      ((smr & SciMode.stop) != 0 ? 2 : 1);

  /// System-clock states per bit, from BRR and the SMR clock select:
  /// 64 x 2^(2n-1) x (N+1), which is 32 << 2n times (N+1).
  int get statesPerBit => (32 << (2 * (smr & SciMode.cks))) * (brr + 1);

  /// States to shift out one whole character.
  int get statesPerChar => bitsPerChar * statesPerBit;

  /// Baud rate at a given system clock, for display.
  double baudAt(double phi) => phi / statesPerBit;

  bool get transmitting => _tsrBusy;

  /// True when this address belongs to the channel.
  /// The whole of this channel's state, packed, so a caller can put it back
  /// where it was without rebuilding the machine. The transmit and receive
  /// logs are left alone: they are for display, not for behaviour.
  List<int> saveState() => [
        smr, brr, scr, tdr, ssr, rdr,
        _tsrBusy ? 1 : 0, _tsrDoneAt, txCount, _rxReadyAt,
        rxQueue.length, ...rxQueue,
      ];

  void restoreState(List<int> v) {
    smr = v[0]; brr = v[1]; scr = v[2]; tdr = v[3]; ssr = v[4]; rdr = v[5];
    _tsrBusy = v[6] != 0; _tsrDoneAt = v[7]; txCount = v[8]; _rxReadyAt = v[9];
    rxQueue..clear()..addAll(v.sublist(11, 11 + v[10]));
    syncToMemory();
  }

  bool owns(int addr) => addr >= base && addr <= base + 5;

  void reset() {
    smr = 0x00;
    brr = 0xFF;
    scr = 0x00;
    tdr = 0xFF;
    ssr = SciStatus.tdre | SciStatus.tend;
    rdr = 0x00;
    _tsrBusy = false;
    _tsrDoneAt = 0;
    _rxReadyAt = 0;
    rxQueue.clear();
    syncToMemory();
  }

  /// Writes the current register values into simulated memory.
  void syncToMemory() {
    final m = mirror;
    if (m == null) return;
    m(smrAddr, smr);
    m(brrAddr, brr);
    m(scrAddr, scr);
    m(tdrAddr, tdr);
    m(ssrAddr, ssr);
    m(rdrAddr, rdr);
  }

  /// CPU read of one of this channel's registers.
  int read(int addr) {
    switch (addr - base) {
      case 0:
        return smr;
      case 1:
        return brr;
      case 2:
        return scr;
      case 3:
        return tdr;
      case 4:
        return ssr;
      default:
        return rdr;
    }
  }

  /// CPU write. SSR is special: the status flags can only be cleared, never
  /// set, and TEND and MPB are read-only (manual section 13.2.7).
  void write(int addr, int value) {
    value &= 0xFF;
    switch (addr - base) {
      case 0:
        smr = value;
        break;
      case 1:
        brr = value;
        break;
      case 2:
        final wasTe = (scr & SciControl.te) != 0;
        scr = value;
        // Clearing TE parks the transmitter with TDRE and TEND set.
        if (wasTe && (scr & SciControl.te) == 0) {
          ssr |= SciStatus.tdre | SciStatus.tend;
          _tsrBusy = false;
        }
        break;
      case 3:
        tdr = value;
        break;
      case 4:
        // Only zeros clear; ones are ignored. TEND/MPB are read-only.
        ssr &= ~(SciStatus.clearable & ~value);
        ssr = (ssr & ~SciStatus.mpbt) | (value & SciStatus.mpbt);
        break;
      default:
        break; // RDR is read-only
    }
    syncToMemory();
  }

  /// Advances the transmitter and receiver to [cycles].
  void tick(int cycles) {
    var changed = false;

    // A character has finished shifting out.
    if (_tsrBusy && cycles >= _tsrDoneAt) {
      _tsrBusy = false;
      // TEND is set when the last bit goes out and no new data is waiting.
      if ((ssr & SciStatus.tdre) != 0) ssr |= SciStatus.tend;
      changed = true;
    }

    // TDR holds data (TDRE clear) and the shift register is free: load it,
    // which frees TDR again. This is the point at which the byte goes on
    // the wire.
    if (!_tsrBusy &&
        (scr & SciControl.te) != 0 &&
        (ssr & SciStatus.tdre) == 0) {
      txLog.add(tdr & 0xFF);
      txCount++;
      if (txLog.length > txLogLimit) txLog.removeAt(0);
      onTransmit?.call(tdr & 0xFF);
      ssr |= SciStatus.tdre; // TDR free for the next byte
      ssr &= ~SciStatus.tend; // transmission in progress
      _tsrBusy = true;
      _tsrDoneAt = cycles + statesPerChar;
      changed = true;
    }

    // Receiver: deliver a queued byte no faster than the line rate.
    if ((scr & SciControl.re) != 0 &&
        (ssr & SciStatus.rdrf) == 0 &&
        rxQueue.isNotEmpty &&
        cycles >= _rxReadyAt) {
      rdr = rxQueue.removeAt(0);
      ssr |= SciStatus.rdrf;
      _rxReadyAt = cycles + statesPerChar;
      changed = true;
    }

    if (changed) syncToMemory();
  }

  /// True when the channel has anything to do, so an idle channel costs
  /// nothing per instruction.
  bool get active =>
      (scr & (SciControl.te | SciControl.re)) != 0 ||
      _tsrBusy ||
      rxQueue.isNotEmpty;

  /// Queues bytes to be received on this channel.
  void receive(Iterable<int> bytes) {
    for (final b in bytes) {
      rxQueue.add(b & 0xFF);
    }
  }

  /// The exception vector this channel is currently requesting, or null.
  /// Order follows the manual's priority: error, receive, transmit, end.
  int? pendingVector() {
    final err = SciStatus.orer | SciStatus.fer | SciStatus.per;
    if ((scr & SciControl.rie) != 0 && (ssr & err) != 0) return vectorBase;
    if ((scr & SciControl.rie) != 0 && (ssr & SciStatus.rdrf) != 0) {
      return vectorBase + 1;
    }
    if ((scr & SciControl.tie) != 0 && (ssr & SciStatus.tdre) != 0) {
      return vectorBase + 2;
    }
    if ((scr & SciControl.teie) != 0 && (ssr & SciStatus.tend) != 0) {
      return vectorBase + 3;
    }
    return null;
  }

  /// One-line summary of the framing, for the UI.
  String get framing {
    if ((smr & SciMode.ca) != 0) return 'synchronous';
    final data = (smr & SciMode.chr) != 0 ? 7 : 8;
    final parity = (smr & SciMode.pe) == 0
        ? 'N'
        : ((smr & SciMode.oe) != 0 ? 'O' : 'E');
    final stop = (smr & SciMode.stop) != 0 ? 2 : 1;
    return '$data$parity$stop';
  }
}
