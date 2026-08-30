/// What the artista 180 wires its port pins to, where that is known.
///
/// The H8/3003 has no opinion about any of this: a port bit is a port bit,
/// and what it does depends on what the board hangs off it. These are the
/// ones worked out from the firmware or from the machine itself, and they
/// are here so the IO tab can say "sewing light" rather than "port 4 bit 2".
///
/// Only add one when it is actually known. A pin with no entry is shown as
/// its number, which is honest; a wrong name is worse than no name, because
/// it will be believed.
library;

/// One pin whose purpose is known.
class PinRole {
  const PinRole({
    required this.port,
    required this.bit,
    required this.name,
    required this.detail,
    this.activeHigh = true,
  });

  /// Port letter or digit as the manual names it.
  final String port;
  final int bit;

  /// Short enough to sit under a bit box.
  final String name;

  /// What it does, and how it is driven. Shown in the tooltip.
  final String detail;

  /// True when a 1 is the active state. Recorded because it is the thing
  /// most easily got backwards, and the IO tab can then say which way round
  /// the pin currently is.
  final bool activeHigh;

  String get where => 'port $port bit $bit';
}

/// The pins whose purpose is known, in port order.
const List<PinRole> artista180PinRoles = [
  // ---- port 4 ------------------------------------------------------------
  PinRole(
    port: '4',
    bit: 2,
    name: 'sewing light',
    detail: 'The lamp over the needle. 1 turns it on, 0 off.',
  ),
  PinRole(
    port: '4',
    bit: 5,
    name: 'pedal',
    detail: "Read into bit 4 of PEDAL_FLAGS at H'FFFEC4 on every pass of the "
        'motor service.',
  ),
  PinRole(
    port: '4',
    bit: 6,
    name: 'EEPROM SCL',
    detail: 'The clock of the bit-banged I2C link to the settings EEPROM.',
  ),
  PinRole(
    port: '4',
    bit: 7,
    name: 'EEPROM SDA',
    detail: 'The data line of the same link, driven both ways.',
  ),

  // ---- port 8 ------------------------------------------------------------
  PinRole(
    port: '8',
    bit: 1,
    name: 'reverse key',
    detail: "The reverse button, which is not on the key matrix. Read into "
        "bit 1 of MACHINE_FLAGS at H'FFFEC1; a 1 is the key down.",
  ),

  // ---- port B ------------------------------------------------------------
  PinRole(
    port: 'B',
    bit: 1,
    name: 'presser switch',
    detail: "Cleared into bit 3 of H'FFFEF8 by the motor service, alongside "
        'the switch latch bits.',
    activeHigh: false,
  ),

  // ---- port C ------------------------------------------------------------
  PinRole(
    port: 'C',
    bit: 0,
    name: 'key strobe 0',
    detail: 'Driven low on its own to read the first bank of the key matrix, '
        "which comes back on the latch at H'060000.",
    activeHigh: false,
  ),
  PinRole(
    port: 'C',
    bit: 1,
    name: 'key strobe 1',
    detail: 'The second bank of the key matrix.',
    activeHigh: false,
  ),
  PinRole(
    port: 'C',
    bit: 2,
    name: 'stitch length A',
    detail: 'One half of the stitch length knob, a quadrature pair with '
        'bit 3.',
  ),
  PinRole(
    port: 'C',
    bit: 3,
    name: 'stitch length B',
    detail: 'The other half of the same pair.',
  ),
  PinRole(
    port: 'C',
    bit: 4,
    name: 'stitch width A',
    detail: 'One half of the stitch width knob, a quadrature pair with '
        'bit 5.',
  ),
  PinRole(
    port: 'C',
    bit: 5,
    name: 'stitch width B',
    detail: 'The other half of the same pair.',
  ),
  PinRole(
    port: 'C',
    bit: 6,
    name: 'key strobe 2',
    detail: 'The third bank of the key matrix.',
    activeHigh: false,
  ),
];

/// What [port] bit [bit] is for, or null if nobody has worked it out.
PinRole? pinRole(String port, int bit) {
  for (final r in artista180PinRoles) {
    if (r.bit == bit && r.port.toUpperCase() == port.toUpperCase()) return r;
  }
  return null;
}
