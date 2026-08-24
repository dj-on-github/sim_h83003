// A/D converter for the H8/3003.
//
// Eight multiplexed analog inputs AN0-AN7, converted to 10 bits and held in
// four 16-bit result registers, left-justified: ADDRA holds AN0 or AN4,
// ADDRB holds AN1 or AN5, ADDRC AN2 or AN6, ADDRD AN3 or AN7. Firmware that
// only wants eight bits reads the high byte, which is what the artista 180
// does.
//
// Conversion takes 266 system states (134 with CKS set), after which ADF is
// set and, if ADIE is on, the ADI interrupt is requested. Single mode
// converts the selected channel once; scan mode converts a group repeatedly
// until ADST is cleared.
//
// Reference: H8/3003 hardware manual section 14 (page 483). Registers occupy
// H'FFFFE0-H'FFFFE9.

/// A/D control/status register (ADCSR) bits.
class AdcStatus {
  static const int adf = 0x80; // conversion complete
  static const int adie = 0x40; // interrupt enable
  static const int adst = 0x20; // start / running
  static const int scan = 0x10; // 0 = single mode, 1 = scan mode
  static const int cks = 0x08; // conversion time: 0 = 266 states, 1 = 134
  static const int channel = 0x07; // CH2-0
}

/// The converter.
class AdConverter {
  static const int base = 0xFFFFE0;
  static const int end = 0xFFFFE9;

  /// Exception vector for the ADI interrupt.
  static const int vector = 60;

  /// The voltage on each pin, as a 10-bit conversion result (0-1023).
  /// Nothing connected reads as 0.
  final List<int> inputs = List<int>.filled(8, 0);

  /// The four result registers, left-justified 10-bit values.
  final List<int> results = List<int>.filled(4, 0);

  int adcsr = 0x00;
  int adcr = 0x7F; // bit 7 TRGE; the rest are reserved and read as 1

  /// Mirrors register values into memory for the views.
  void Function(int addr, int value)? mirror;

  /// State counter at which the conversion in progress completes.
  int _doneAt = 0;
  bool _converting = false;

  /// Conversions performed, for display.
  int conversions = 0;

  /// The converter's state, packed. The input levels are not included:
  /// they are what is on the pins, not what the model has done.
  List<int> saveState() =>
      [adcsr, adcr, _doneAt, _converting ? 1 : 0, conversions, ...results];

  void restoreState(List<int> v) {
    adcsr = v[0]; adcr = v[1]; _doneAt = v[2];
    _converting = v[3] != 0; conversions = v[4];
    for (var i = 0; i < results.length; i++) {
      results[i] = v[5 + i];
    }
    syncToMemory();
  }

  bool owns(int addr) => addr >= base && addr <= end;

  int get selectedChannel => adcsr & AdcStatus.channel;
  bool get scanMode => (adcsr & AdcStatus.scan) != 0;
  bool get running => (adcsr & AdcStatus.adst) != 0;

  /// States one conversion takes.
  int get conversionStates => (adcsr & AdcStatus.cks) != 0 ? 134 : 266;

  /// Which result register a channel lands in: AN0/AN4 -> A, AN1/AN5 -> B,
  /// AN2/AN6 -> C, AN3/AN7 -> D.
  static int registerFor(int channel) => channel & 3;

  void reset() {
    adcsr = 0x00;
    adcr = 0x7F;
    for (var i = 0; i < 4; i++) {
      results[i] = 0;
    }
    _converting = false;
    _doneAt = 0;
    syncToMemory();
  }

  int read(int addr) {
    final off = addr - base;
    if (off >= 0 && off < 8) {
      final r = results[off >> 1];
      return (off & 1) == 0 ? (r >> 8) & 0xFF : r & 0xFF;
    }
    if (off == 8) return adcsr;
    return adcr;
  }

  void write(int addr, int value) {
    value &= 0xFF;
    final off = addr - base;
    if (off < 8) return; // result registers are read-only
    if (off == 9) {
      adcr = value | 0x7F;
      syncToMemory();
      return;
    }
    // ADCSR. ADF can only be cleared, never set, by software.
    final keepAdf = adcsr & value & AdcStatus.adf;
    final wasRunning = running;
    adcsr = (value & ~AdcStatus.adf) | keepAdf;
    if (!wasRunning && running) {
      _startConversion(_lastCycles);
    } else if (wasRunning && !running) {
      _converting = false;
    }
    syncToMemory();
  }

  int _lastCycles = 0;

  void _startConversion(int cycles) {
    _converting = true;
    _doneAt = cycles + conversionStates;
  }

  /// Advances the converter to [cycles].
  void tick(int cycles) {
    _lastCycles = cycles;
    if (!_converting || cycles < _doneAt) return;

    if (scanMode) {
      // Scan mode converts AN0 up to the selected channel, then repeats
      // until the firmware clears ADST.
      for (var ch = 0; ch <= selectedChannel; ch++) {
        results[registerFor(ch)] = (inputs[ch] & 0x3FF) << 6;
        conversions++;
      }
      adcsr |= AdcStatus.adf;
      _startConversion(cycles); // keep going
    } else {
      final ch = selectedChannel;
      results[registerFor(ch)] = (inputs[ch] & 0x3FF) << 6;
      conversions++;
      adcsr |= AdcStatus.adf;
      adcsr &= ~AdcStatus.adst; // single mode stops after one conversion
      _converting = false;
    }
    syncToMemory();
  }

  /// The ADI vector when a completed conversion has its interrupt enabled.
  int? pendingVector() {
    if ((adcsr & AdcStatus.adie) != 0 && (adcsr & AdcStatus.adf) != 0) {
      return vector;
    }
    return null;
  }

  /// Sets the voltage on a pin as a 10-bit value.
  void setInput(int channel, int value) {
    inputs[channel & 7] = value.clamp(0, 1023);
  }

  /// Sets the voltage on a pin as the 8-bit value firmware sees when it
  /// reads only the high byte of the result.
  void setInput8(int channel, int value) =>
      setInput(channel, (value.clamp(0, 255)) << 2);

  /// The value firmware would read from the high byte of [channel]'s result.
  int result8(int channel) => (results[registerFor(channel)] >> 8) & 0xFF;

  void syncToMemory() {
    final m = mirror;
    if (m == null) return;
    for (var a = base; a <= end; a++) {
      m(a, read(a));
    }
  }
}
