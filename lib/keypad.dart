/// The artista 180's front panel: eighteen keys on a scanned matrix, the
/// reverse key on a port pin of its own, and the two quadrature knobs.
///
/// The firmware does not read a key register. `key_banks_read` drives one of
/// three strobes on port C low at a time, reads the latch at H'060000, and
/// keeps the complement -- so a key is a crossing between a strobe and one of
/// the eight return lines, and the latch is active low. Reverse is not on the
/// matrix at all: it is bit 1 of port 8's data register, read straight into
/// MACHINE_FLAGS.
///
/// The knobs are quadrature pairs on port C, bits 2-3 and 4-5. The firmware
/// samples them in the millisecond interrupt -- knob A in slice 2, knob B in
/// slice 3, so each is looked at every third millisecond -- and moves its
/// count by one for each single-step transition it sees. A transition of two
/// steps matches none of its cases and is dropped, so the phase here is
/// advanced at most once per [knobStepCycles] and only when the CPU actually
/// reads port C. That is what keeps a fast drag from being silently lost.
library;

/// Where a key hangs.
///
/// Matrix keys give [bank] (which strobe) and [bit] (which return line).
/// Reverse gives [pinDr]/[pinBit] instead, and is active high where the
/// matrix is active low.
enum KeyWiring { matrix, pin }

/// One key on the panel: what it is wired to, what it looks like, and where
/// it sits. Positions are in the coordinate space of the panel drawing,
/// which the view scales to whatever room it has.
class PanelKey {
  const PanelKey({
    required this.code,
    required this.x,
    required this.y,
    this.bank = -1,
    this.bit = -1,
    this.pinDr = -1,
    this.pinBit = -1,
    this.label = '',
    this.glyph = PanelGlyph.none,
    this.radius = 45,
    this.labelBelow = false,
  });

  /// What `key_scan` writes to H'11B10E when this key is the first one down.
  final int code;

  /// Matrix position: which strobe, and which of the eight return lines.
  final int bank;
  final int bit;

  /// For a key on a port pin instead: the data register and the bit in it.
  final int pinDr;
  final int pinBit;

  final String label;
  final PanelGlyph glyph;
  final double x;
  final double y;
  final double radius;

  /// Most labels sit to the right of the key; two sit under it.
  final bool labelBelow;

  KeyWiring get wiring => bank >= 0 ? KeyWiring.matrix : KeyWiring.pin;
}

/// The markings on the keys that are drawn rather than written.
enum PanelGlyph {
  none,
  straightStitch, // H'71, a dashed line
  buttonhole, // H'70, a solid bar
  fancyStitch, // H'72, a squiggle
  frame, // H'7D, an embroidery frame
  letterA, // H'78
  module, // H'74, a machine with two arrows at it
  leftArrow, // H'6E
  rightArrow, // H'6F
}

/// One of the two knobs.
class PanelKnob {
  PanelKnob({
    required this.name,
    required this.shift,
    required this.x,
    required this.y,
    this.radius = 70,
  });

  final String name;

  /// The low bit of its pair in port C: 2 for knob A, 4 for knob B.
  final int shift;

  final double x;
  final double y;
  final double radius;

  /// Where in the quadrature cycle the contacts are, 0-3. The two-bit value
  /// the firmware reads is [_gray] of this, so that stepping the phase up is
  /// 00-01-11-10 and stepping it down is the same run backwards.
  int phase = 0;

  /// Detents asked for by the view and not yet handed to the firmware.
  /// Signed: positive is clockwise. The view can add these as fast as a drag
  /// produces them; they are paid out one at a time.
  int pending = 0;

  /// How far the pointer has been turned, for drawing. Not wrapped, so the
  /// knob keeps turning the same way rather than snapping back.
  double angle = 0;

  static const List<int> _gray = [0x0, 0x1, 0x3, 0x2];

  /// The two bits as the firmware reads them.
  int get levels => _gray[phase & 3];

  void step(int direction) {
    phase = (phase + direction) & 3;
    angle += direction * 0.28;
  }
}

/// The panel, and the wires between it and the CPU.
class Keypad {
  Keypad();

  /// The latch the key returns are read through. Area 0 carries a device
  /// every H'20000: the boot flash at 0, the LCD registers, the frame
  /// buffer, this, and the switch latch at H'080000.
  static const int latchBase = 0x060000;
  static const int latchSize = 0x020000;

  static const int pcDdr = 0xFFFFD5;
  static const int pcDr = 0xFFFFD7;
  static const int p8Dr = 0xFFFFCF;

  /// Which port C bit strobes each bank, in bank order.
  static const List<int> strobeBits = [0, 1, 6];

  /// Simulated cycles between one quadrature step and the next. The firmware
  /// looks at a knob every third millisecond, which at 11.0592MHz is about
  /// 33,000 cycles; this leaves room to spare, and still turns fast enough
  /// that a drag feels continuous.
  int knobStepCycles = 90000;

  /// Reads CPU memory. Supplied when the keypad is attached, so the strobe
  /// state can be worked out from the port registers.
  late int Function(int addr) peek;

  /// Holds and releases a port pin, the same way a switch or the EEPROM
  /// does.
  late void Function(int drAddr, int bit, bool high) hold;
  late void Function(int drAddr, int bit) release;

  final Set<int> down = {};

  /// Keys the user has latched down, which stay down until clicked again.
  final Set<int> latched = {};

  int _lastKnobCycles = 0;

  bool owns(int addr) => addr >= latchBase && addr < latchBase + latchSize;

  bool isDown(int code) => down.contains(code);

  /// Presses or releases a key, and for the ones wired to a port pin drives
  /// that pin to match.
  void setDown(PanelKey key, bool pressed) {
    if (pressed) {
      down.add(key.code);
    } else {
      down.remove(key.code);
      latched.remove(key.code);
    }
    if (key.wiring == KeyWiring.pin) {
      // Reverse reads as a 1 in MACHINE_FLAGS when it is down, so unlike the
      // matrix it is driven high rather than pulled low.
      if (pressed) {
        hold(key.pinDr, key.pinBit, true);
      } else {
        hold(key.pinDr, key.pinBit, false);
      }
    }
  }

  /// Lets go of everything, including whatever was latched.
  void releaseAll() {
    for (final k in panelKeys) {
      if (k.wiring == KeyWiring.pin) release(k.pinDr, k.pinBit);
    }
    down.clear();
    latched.clear();
  }

  /// True when the CPU is driving [bit] of port C low: the data direction
  /// register makes it an output and the data register holds a 0.
  bool _strobeLow(int bit) {
    final ddr = peek(pcDdr);
    final dr = peek(pcDr);
    return ((ddr >> bit) & 1) == 1 && ((dr >> bit) & 1) == 0;
  }

  /// What the latch reads. Idle is all ones; a key that is down shorts its
  /// return line to its strobe, so it only shows while that strobe is low.
  int latchValue() {
    var v = 0xFF;
    for (var bank = 0; bank < strobeBits.length; bank++) {
      if (!_strobeLow(strobeBits[bank])) continue;
      for (final k in panelKeys) {
        if (k.wiring == KeyWiring.matrix &&
            k.bank == bank &&
            down.contains(k.code)) {
          v &= ~(1 << k.bit);
        }
      }
    }
    return v & 0xFF;
  }

  int read(int addr) => latchValue();

  /// Pays out one queued detent, if one is waiting and enough simulated time
  /// has gone by for the firmware to have seen the phase the knob is in.
  ///
  /// Called from the port C read, so a knob only moves on when the CPU has
  /// looked at it. Both halves matter: the cycle gate stops a fast drag
  /// outrunning the millisecond interrupt, and hanging it off the read stops
  /// the knob turning while the CPU is stopped at a breakpoint.
  void pollKnobs(int cycles) {
    // A reset puts the cycle count back to zero, and a knob turned before it
    // would have left a mark somewhere far ahead. Without this the gate below
    // would refuse every step until the count climbed back to where it was --
    // after a long run, for ever -- and the knobs would look broken while the
    // keys carried on working.
    if (cycles < _lastKnobCycles) _lastKnobCycles = cycles;
    if (cycles - _lastKnobCycles < knobStepCycles) return;
    var moved = false;
    for (final k in knobs) {
      if (k.pending == 0) continue;
      final dir = k.pending > 0 ? 1 : -1;
      k.pending -= dir;
      k.step(dir);
      _driveKnob(k);
      moved = true;
    }
    if (moved) _lastKnobCycles = cycles;
  }

  void _driveKnob(PanelKnob k) {
    final v = k.levels;
    hold(pcDr, k.shift, (v & 1) != 0);
    hold(pcDr, k.shift + 1, (v & 2) != 0);
  }

  /// Puts both knobs' contacts on the pins, so the firmware sees a settled
  /// pair from the start rather than whatever the data register held.
  void driveKnobs() {
    for (final k in knobs) {
      _driveKnob(k);
    }
  }

  /// Asks a knob to turn. Clockwise is positive, and counts up.
  void turn(PanelKnob k, int detents) {
    k.pending += detents;
  }

  final List<PanelKnob> knobs = [
    PanelKnob(name: 'stitch length', shift: 2, x: 1330, y: 545),
    PanelKnob(name: 'stitch width', shift: 4, x: 1330, y: 868),
  ];

  /// The panel, laid out as it is on the machine. Codes, banks and bits are
  /// read straight off `key_scan`; the positions are from the panel drawing.
  static const List<PanelKey> panelKeys = [
    // Left block, two columns.
    PanelKey(
        code: 0x71,
        bank: 0,
        bit: 0,
        x: 110,
        y: 180,
        glyph: PanelGlyph.straightStitch),
    PanelKey(
        code: 0x70,
        bank: 1,
        bit: 0,
        x: 335,
        y: 180,
        glyph: PanelGlyph.buttonhole),
    PanelKey(
        code: 0x72,
        bank: 2,
        bit: 0,
        x: 110,
        y: 362,
        glyph: PanelGlyph.fancyStitch),
    PanelKey(
        code: 0x7D, bank: 0, bit: 1, x: 335, y: 362, glyph: PanelGlyph.frame),
    PanelKey(
        code: 0x78, bank: 1, bit: 1, x: 110, y: 558, glyph: PanelGlyph.letterA),
    PanelKey(
        code: 0x74, bank: 2, bit: 1, x: 335, y: 558, glyph: PanelGlyph.module),
    PanelKey(code: 0x77, bank: 0, bit: 2, x: 110, y: 797, label: 'clr'),
    PanelKey(code: 0x6D, bank: 1, bit: 2, x: 335, y: 797, label: 'mem'),
    PanelKey(code: 0x76, bank: 2, bit: 2, x: 110, y: 1038, label: 'up/down'),
    PanelKey(code: 0x75, bank: 0, bit: 3, x: 110, y: 1230, label: '?'),
    PanelKey(code: 0x73, bank: 1, bit: 3, x: 335, y: 1230, label: 'help'),

    // The group of three, in their own moulding.
    PanelKey(code: 0x7A, bank: 0, bit: 6, x: 770, y: 207, label: 'F'),
    PanelKey(code: 0x7C, bank: 0, bit: 7, x: 1005, y: 207, label: 'C='),
    PanelKey(
        code: 0x7B,
        pinDr: p8Dr,
        pinBit: 1,
        x: 855,
        y: 357,
        radius: 58,
        label: 'reverse'),

    // The middle column.
    PanelKey(
        code: 0x6E,
        bank: 2,
        bit: 3,
        x: 760,
        y: 818,
        glyph: PanelGlyph.leftArrow),
    PanelKey(
        code: 0x6F,
        bank: 2,
        bit: 5,
        x: 972,
        y: 818,
        glyph: PanelGlyph.rightArrow),
    PanelKey(
        code: 0x81,
        bank: 1,
        bit: 4,
        x: 760,
        y: 1013,
        label: 'smart',
        labelBelow: true),
    PanelKey(code: 0x79, bank: 2, bit: 4, x: 972, y: 1013, label: 'setup'),
    PanelKey(code: 0x7E, bank: 1, bit: 5, x: 972, y: 1208, label: 'eco'),
  ];

  /// The drawing's own width and height, which the view scales from.
  static const double artWidth = 1500;
  static const double artHeight = 1330;
}
