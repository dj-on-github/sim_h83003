// The serial EEPROM on the artista 180's port 4.
//
// The machine keeps a handful of settings in a 24Cxx-style part hung off two
// port pins, and the firmware bit-bangs I2C at it: SDA on P4 bit 7, SCL on
// bit 6. There is no I2C controller involved -- every edge is a write to the
// port's data register or to its direction register, so the whole protocol
// is visible from the bus side and can be answered from here.
//
// What the machine does with it, as far as the reconstruction has got:
//
//   H'A9, H'AA   the two end stops of the presser-foot lift. Written at
//                start-up from the configuration block in flash, written
//                again by the foot calibration in service mode, and written
//                by the handwheel trim whenever it moves.
//
// Nothing reads them back. The only read the firmware performs is the one
// inside its own write-and-verify, and there is a wrinkle in that which this
// model reproduces -- see [verifyFriendly].
//
// How the pin model works: SDA is open-drain with a pull-up, so the device
// can only ever pull the line down. The simulator's port layer substitutes
// an externally driven level only for pins the DDR has as inputs, which is
// exactly the real behaviour -- while the master drives SDA the device's
// pull is invisible to it, and the moment the master lets go the device's
// level is what it reads. So this class holds SDA at all times: low while it
// is pulling, high otherwise, standing in for the pull-up.

import 'dart:convert';
import 'dart:typed_data';

/// What the device is doing between one clock edge and the next.
enum I2cPhase {
  /// No start condition seen; not listening.
  idle,

  /// Taking in the control byte that follows a start.
  control,

  /// Taking in the word address of a write.
  wordAddress,

  /// Taking in data bytes, buffered until the stop commits them.
  writing,

  /// Handing bytes back.
  reading,

  /// Addressed to somebody else. Waiting for the next start or stop.
  ignoring,
}

/// One completed exchange, kept so the tab can show what went past.
class I2cTransfer {
  I2cTransfer(this.control, this.wordAddress, this.bytes, this.wrote);

  /// The control byte as it appeared on the wire.
  final int control;

  /// The word address a write named, or the address a read started at.
  final int? wordAddress;

  /// The data bytes, in the order they went by.
  final List<int> bytes;

  /// True for a write, false for a read.
  final bool wrote;

  @override
  String toString() {
    final hex = bytes.map(_h2).join(' ');
    final at = wordAddress == null ? '' : ' @${_h2(wordAddress!)}';
    return '${wrote ? "W" : "R"} ${_h2(control)}$at'
        '${hex.isEmpty ? "" : "  $hex"}';
  }
}

String _h2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// A 24Cxx-style serial EEPROM listening on two port pins.
class I2cEeprom {
  I2cEeprom({
    this.size = 256,
    this.pageSize = 8,
    this.deviceAddress = 0x50,
    this.drAddr = 0xFFFFC7,
    this.ddrAddr = 0xFFFFC5,
    this.sdaBit = 7,
    this.sclBit = 6,
  }) : data = Uint8List(size) {
    data.fillRange(0, size, 0xFF);
  }

  /// How many bytes the part holds. H'AA is the highest address the firmware
  /// names, so it is at least a 24C02.
  final int size;

  /// Page-write size. Addresses wrap within a page rather than carrying, as
  /// they do on the real part; the firmware only ever writes one byte at a
  /// time, so this is here for correctness rather than because it is used.
  final int pageSize;

  /// The control byte with its read/write bit masked off.
  ///
  /// The firmware sends H'50 to write and H'51 to read. Taken as an I2C
  /// control byte that is device H'28 with the read/write bit in the usual
  /// place, not the H'A0/H'A1 a 24Cxx answers to -- the firmware appears to
  /// send the seven-bit address unshifted. What the part actually is does
  /// not matter here: this model answers what the machine sends.
  final int deviceAddress;

  final int drAddr;
  final int ddrAddr;
  final int sdaBit;
  final int sclBit;

  /// The array. Byte values, H'FF where the part has never been written.
  final Uint8List data;

  /// Reads a port register as the CPU last wrote it. Wired up by [H8Cpu].
  late int Function(int addr) peek;

  /// Holds a pin from outside the chip. Wired up by [H8Cpu].
  late void Function(int drAddr, int bit, bool high) hold;

  /// Lets a pin go entirely, so it reads back the data register again.
  /// Wired up by [H8Cpu], and used only when the model is detached.
  late void Function(int drAddr, int bit) float;

  /// Called after a write transfer has been committed, so the contents can
  /// be put somewhere they will survive the session.
  void Function()? onCommit;

  /// After a byte write, where the internal address counter is left.
  ///
  /// A real 24Cxx leaves it one past the byte just written, which is what
  /// [false] does. The firmware's `eeprom_write_verify` writes a byte and
  /// then does a current-address read expecting to see it again, so against
  /// a real part that check reads the *next* byte and nearly always says no
  /// -- which is consistent with the original ignoring the answer at all
  /// twelve of its call sites.
  ///
  /// Setting this true leaves the counter on the byte just written, so the
  /// verify agrees. It is not what the datasheet says, and it is off by
  /// default; it is here because "the verify never passes" looks like a
  /// broken model until you know why.
  bool verifyFriendly = false;

  // ---- bus state --------------------------------------------------------

  bool _scl = true;
  bool _sda = true;
  bool _pulling = false;

  I2cPhase _phase = I2cPhase.idle;

  /// Data bits transferred in the byte in progress, 0..8.
  int _bit = 0;

  /// The byte being shifted in or out.
  int _shift = 0;

  /// True while the ninth (acknowledge) clock is in progress.
  int _acking = 0;

  /// Where a write is putting bytes, before the stop commits them.
  int _addr = 0;

  /// The internal address counter a current-address read starts from.
  int _pointer = 0;

  /// The page buffer: address to value, filled by a write and written into
  /// [data] when the stop arrives.
  final Map<int, int> _pending = {};

  /// True while the control byte said read.
  bool _reading = false;

  /// Set on the ninth clock of a byte handed back: the master wants no more.
  bool _masterNack = false;

  // ---- what the tab shows ----------------------------------------------

  /// The last few completed transfers, newest last.
  final List<I2cTransfer> log = [];

  /// How many transfers to keep.
  int logLimit = 24;

  /// Bytes committed to the array since the model was attached.
  int writeCount = 0;

  /// Bytes handed back since the model was attached.
  int readCount = 0;

  I2cPhase get phase => _phase;
  int get pointer => _pointer;
  bool get sclHigh => _scl;
  bool get sdaHigh => _sda;

  int _control = 0;
  int? _logWordAddress;
  final List<int> _logBytes = [];

  /// Puts the bus back to idle without touching the array. Called when the
  /// CPU is reset, since a device part-way through a transfer would other-
  /// wise still be holding SDA down.
  void reset() {
    _phase = I2cPhase.idle;
    _bit = 0;
    _shift = 0;
    _acking = 0;
    _pending.clear();
    _release();
    _scl = true;
    _sda = true;
  }

  /// Attaches: releases the pin and takes the current levels as the start.
  void attach() {
    reset();
    _sample();
  }

  /// Lets go of SDA altogether, for when the model is switched off. The
  /// pull-up goes with it: on the real board the pull-up is the one inside
  /// the CPU that `i2c_init` switches on through P4PCR, so with nothing on
  /// the pins there is nothing here to stand in for.
  void detach() {
    _pulling = false;
    float(drAddr, sdaBit);
  }

  // ---- the bus ----------------------------------------------------------

  /// Called after every write to the port's data or direction register.
  void portWritten() => _sample();

  void _sample() {
    final dr = peek(drAddr);
    final ddr = peek(ddrAddr);

    // A pin the CPU has as an input floats to the pull-up.
    final scl = ((ddr >> sclBit) & 1) == 0 || ((dr >> sclBit) & 1) == 1;
    final master = ((ddr >> sdaBit) & 1) == 0 || ((dr >> sdaBit) & 1) == 1;
    final sda = master && !_pulling;

    if (_scl && scl && sda != _sda) {
      // A change on SDA while the clock is high is a start or a stop; it is
      // never data.
      if (!sda) {
        _start();
      } else {
        _stop();
      }
    } else if (!_scl && scl) {
      _rising(sda);
    } else if (_scl && !scl) {
      _falling();
    }

    _scl = scl;
    // The handlers above may have changed what the device is holding, so the
    // level is worked out again rather than carried over.
    _sda = master && !_pulling;
  }

  void _pull() {
    _pulling = true;
    hold(drAddr, sdaBit, false);
  }

  void _release() {
    _pulling = false;
    hold(drAddr, sdaBit, true);
  }

  void _start() {
    _flushLog();
    _phase = I2cPhase.control;
    _bit = 0;
    _shift = 0;
    _acking = 0;
    _masterNack = false;
    _release();
  }

  void _stop() {
    _commit();
    _flushLog();
    _phase = I2cPhase.idle;
    _bit = 0;
    _acking = 0;
    _release();
  }

  void _rising(bool sda) {
    if (_acking != 0) {
      // The ninth clock. While handing bytes back it is the master's answer:
      // a not-acknowledge ends the read, but the byte it is answering has
      // still gone out and the address counter has still moved on, so that
      // is dealt with when the clock falls rather than here.
      if (_phase == I2cPhase.reading) _masterNack = sda;
      return;
    }
    if (_phase == I2cPhase.idle || _phase == I2cPhase.ignoring) return;
    if (_bit < 8) {
      if (_phase != I2cPhase.reading) {
        _shift = ((_shift << 1) | (sda ? 1 : 0)) & 0xFF;
      }
      _bit++;
    }
  }

  void _falling() {
    if (_acking != 0) {
      _acking = 0;
      _release();
      _afterAck();
      return;
    }
    if (_phase == I2cPhase.idle || _phase == I2cPhase.ignoring) return;
    if (_bit >= 8) {
      _acking = 1;
      _byteComplete();
      return;
    }
    if (_phase == I2cPhase.reading) _driveBit();
  }

  /// The eighth data bit has gone by: deal with the byte and set up the
  /// acknowledge slot the ninth clock will show.
  void _byteComplete() {
    switch (_phase) {
      case I2cPhase.control:
        _control = _shift;
        if ((_shift & 0xFE) != (deviceAddress & 0xFE)) {
          _phase = I2cPhase.ignoring; // not us: no acknowledge
          return;
        }
        _reading = (_shift & 0x01) != 0;
        _pull();
        break;

      case I2cPhase.wordAddress:
        _addr = _shift % size;
        _pointer = _addr;
        _logWordAddress = _addr;
        _pull();
        break;

      case I2cPhase.writing:
        _pending[_addr] = _shift;
        _logBytes.add(_shift);
        // Within a page the address wraps rather than carrying.
        final page = _addr - (_addr % pageSize);
        _addr = page + ((_addr + 1 - page) % pageSize);
        _pull();
        break;

      case I2cPhase.reading:
        // The master drives the ninth clock, so let go of the line.
        _release();
        break;

      case I2cPhase.idle:
      case I2cPhase.ignoring:
        break;
    }
  }

  /// The acknowledge clock has finished.
  void _afterAck() {
    _bit = 0;
    switch (_phase) {
      case I2cPhase.control:
        if (_reading) {
          _phase = I2cPhase.reading;
          _shift = data[_pointer % size];
          _logWordAddress = _pointer % size;
          _driveBit();
        } else {
          _phase = I2cPhase.wordAddress;
        }
        break;

      case I2cPhase.wordAddress:
        _phase = I2cPhase.writing;
        break;

      case I2cPhase.writing:
        break;

      case I2cPhase.reading:
        _logBytes.add(_shift);
        readCount++;
        _pointer = (_pointer + 1) % size;
        if (_masterNack) {
          // Nothing more wanted. The line stays released and the master
          // follows with a stop or another start.
          _phase = I2cPhase.ignoring;
          break;
        }
        _shift = data[_pointer];
        _driveBit();
        break;

      case I2cPhase.idle:
      case I2cPhase.ignoring:
        break;
    }
    if (_phase != I2cPhase.reading) _shift = 0;
  }

  /// Puts the next bit of [_shift] on the line, most significant first.
  void _driveBit() {
    final high = ((_shift >> (7 - _bit)) & 1) == 1;
    if (high) {
      _release();
    } else {
      _pull();
    }
  }

  /// A stop after a write: the page buffer goes into the array.
  void _commit() {
    if (_pending.isEmpty) return;
    var last = 0;
    _pending.forEach((a, v) {
      data[a % size] = v;
      last = a % size;
      writeCount++;
    });
    _pointer = verifyFriendly ? last : (last + 1) % size;
    _pending.clear();
    onCommit?.call();
  }

  /// Closes off the transfer in progress for the activity log.
  void _flushLog() {
    if (_control != 0 || _logBytes.isNotEmpty) {
      log.add(I2cTransfer(_control, _logWordAddress, List.of(_logBytes),
          (_control & 0x01) == 0));
      while (log.length > logLimit) {
        log.removeAt(0);
      }
    }
    _control = 0;
    _logWordAddress = null;
    _logBytes.clear();
  }

  // ---- the file it lives in --------------------------------------------

  /// The array as a JSON document: readable, and meant to be hand-editable.
  ///
  /// `rows` is the array itself, sixteen bytes to a line with the address it
  /// starts at, so a diff between two sessions points at what moved.
  String toJson({String? note}) {
    final rows = <String>[];
    for (var a = 0; a < size; a += 16) {
      final b = <String>[];
      for (var i = 0; i < 16 && a + i < size; i++) {
        b.add(_h2(data[a + i]));
      }
      rows.add('${_h2(a)}: ${b.join(' ')}');
    }
    final doc = <String, dynamic>{
      'device': 'artista 180 settings EEPROM, on port 4',
      'note': note ??
          'Sixteen bytes to a row, "address: bytes", all hexadecimal. '
              'H\'FF is an unwritten byte.',
      'size': size,
      'fields': {
        for (final f in knownFields)
          _h2(f.address): '${f.name} = ${f.describe(data[f.address])}',
      },
      'rows': rows,
    };
    return const JsonEncoder.withIndent('  ').convert(doc);
  }

  /// Takes the array back out of a JSON document. Returns null on success,
  /// or a sentence saying what was wrong with the file.
  ///
  /// Both shapes are accepted: the `rows` this class writes, and a flat
  /// `bytes` map of address to value, which is easier to write by hand when
  /// only one or two bytes matter.
  String? fromJson(String text) {
    dynamic doc;
    try {
      doc = json.decode(text);
    } catch (e) {
      return 'Not JSON: $e';
    }
    if (doc is! Map) return 'Expected a JSON object at the top level.';

    final fresh = Uint8List(size)..fillRange(0, size, 0xFF);

    final rows = doc['rows'];
    if (rows is List) {
      for (final row in rows) {
        if (row is! String) continue;
        final parts = row.split(':');
        if (parts.length != 2) return 'Row "$row" is not "address: bytes".';
        final at = int.tryParse(parts[0].trim(), radix: 16);
        if (at == null) return 'Row "$row" has no address.';
        var i = at;
        for (final b in parts[1].trim().split(RegExp(r'\s+'))) {
          if (b.isEmpty) continue;
          final v = int.tryParse(b, radix: 16);
          if (v == null) return 'Row "$row" has "$b" in it, which is not hex.';
          if (i < size) fresh[i++] = v & 0xFF;
        }
      }
    }

    final bytes = doc['bytes'];
    if (bytes is Map) {
      for (final e in bytes.entries) {
        final at = int.tryParse(e.key.toString(), radix: 16);
        final v = e.value is int
            ? e.value as int
            : int.tryParse(e.value.toString(), radix: 16);
        if (at == null || v == null) {
          return 'Byte "${e.key}": "${e.value}" is not a hex address and value.';
        }
        if (at < size) fresh[at] = v & 0xFF;
      }
    }

    if (rows is! List && bytes is! Map) {
      return 'No "rows" and no "bytes": nothing to load.';
    }

    data.setAll(0, fresh);
    return null;
  }

  /// What is known about the addresses the firmware uses.
  static const List<EepromField> knownFields = [
    EepromField(
      0xA9,
      'Foot lift, first end stop',
      'H\'FFFEF3. Written from the configuration block at H\'57FF90 on every '
          'start-up, by the foot calibration in service mode, and by the '
          'handwheel trim when it moves. The trim adds H\'28 before storing '
          'unless bit 1 of H\'FFFEE3 says otherwise.',
    ),
    EepromField(
      0xAA,
      'Foot lift, second end stop',
      'H\'FFFEF4, and the configuration block at H\'57FF91. Written the same '
          'three ways as H\'A9.',
    ),
  ];
}

/// One address the firmware is known to use, and what it means.
class EepromField {
  const EepromField(this.address, this.name, this.detail);

  final int address;
  final String name;
  final String detail;

  /// The value as something to read: raw, and with the trim offset taken
  /// off, since that is the form the machine works in.
  String describe(int v) {
    if (v == 0xFF) return "H'FF (never written)";
    final trimmed = v >= 0x28 ? v - 0x28 : null;
    return "H'${_h2(v)} ($v)"
        "${trimmed == null ? "" : ", less the H'28 trim: $trimmed"}";
  }
}
