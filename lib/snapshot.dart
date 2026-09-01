/// A whole machine, written down and put back.
///
/// Getting the artista 180 to a drawn screen costs twenty-five million
/// instructions, and every investigation pays that again from the top. A
/// snapshot is that cost paid once: the registers, the peripherals, every
/// allocated byte of memory, the pins anything is holding, the panel, and
/// the EEPROM -- so the machine comes back exactly where it was left, with
/// the settings it had configured for itself still in it.
///
/// The EEPROM matters more than its size suggests. It is where the machine
/// keeps its presser-foot calibration and the rest of what it works out
/// about itself, and a snapshot without it would come back to a machine that
/// had forgotten how it was set up.
///
/// The file is gzipped JSON. JSON because a snapshot that cannot be looked
/// at is hard to trust; gzipped because the flash images in it are
/// megabytes and compress to very little.
library;

import 'dart:convert';
import 'dart:typed_data';

import 'flash.dart';
import 'h8300h.dart';
import 'i2c_eeprom.dart';
import 'keypad.dart';
import 'snapshot_gzip.dart';

/// Bumped when the shape changes in a way an older reader could not cope
/// with. A snapshot from a newer version is refused rather than misread.
const int kSnapshotVersion = 1;
const String kSnapshotFormat = 'h8sim-snapshot';

String _hex(int v, [int w = 6]) =>
    v.toRadixString(16).toUpperCase().padLeft(w, '0');
int _unhex(Object? v) =>
    v is int ? v : int.parse(v.toString(), radix: 16);

/// Everything worth keeping about a running machine.
class Snapshot {
  Snapshot({
    required this.cpuState,
    required this.memory,
    required this.pins,
    required this.flashWrites,
    required this.keypad,
    required this.eeprom,
    required this.externalInputs,
    this.note = '',
  });

  /// The registers, flags, cycle count and the four peripheral models, as
  /// H8Cpu.saveState packs them.
  final List<int> cpuState;

  /// The allocated banks, by base address. Sparse: a machine that has only
  /// touched its flash and its work RAM does not carry sixteen megabytes.
  final Map<int, Uint8List> memory;

  /// What the outside world is holding, by data register address:
  /// (driven mask, level mask).
  final Map<int, (int, int)> pins;

  /// The Flash tab's written-banks log, by device base address.
  final Map<int, List<Map<String, int>>> flashWrites;

  /// The panel: which keys are down, which are latched, where the knobs are.
  final Map<String, dynamic> keypad;

  /// The EEPROM's array, or null when none is attached.
  final Uint8List? eeprom;

  /// The switch and key latches, by base address.
  final Map<int, int> externalInputs;

  /// Free text, so a file can say what it is.
  final String note;

  // ---- taking one -------------------------------------------------------

  /// Reads the machine as it stands.
  ///
  /// [eepromModel] and [panel] are passed rather than found on the CPU
  /// because the CPU holds them only while they are attached, and a snapshot
  /// should carry them either way.
  static Snapshot capture(
    H8Cpu cpu, {
    I2cEeprom? eepromModel,
    Keypad? panel,
    String note = '',
  }) {
    final mem = <int, Uint8List>{};
    for (final (start, end) in cpu.mem.regions()) {
      final bytes = Uint8List(end - start);
      for (var i = 0; i < bytes.length; i++) {
        bytes[i] = cpu.mem.peek(start + i);
      }
      mem[start] = bytes;
    }

    final pins = <int, (int, int)>{};
    for (final entry in cpu.pinDriven.entries) {
      if (entry.value == 0) continue;
      pins[entry.key] = (entry.value, cpu.pinLevel[entry.key] ?? 0);
    }

    final writes = <int, List<Map<String, int>>>{};
    for (final f in cpu.flash) {
      if (f.written.isEmpty) continue;
      writes[f.base] = [
        for (final b in f.written.values)
          {
            'bank': b.bank,
            'pages': b.pages,
            'bytes': b.bytes,
            'erases': b.erases,
            'lowest': b.lowest,
            'highest': b.highest,
          },
      ];
    }

    return Snapshot(
      cpuState: cpu.saveState(),
      memory: mem,
      pins: pins,
      flashWrites: writes,
      keypad: panel == null
          ? const {}
          : {
              'down': panel.down.toList()..sort(),
              'latched': panel.latched.toList()..sort(),
              'knobs': [
                for (final k in panel.knobs)
                  {'phase': k.phase, 'pending': k.pending, 'angle': k.angle},
              ],
            },
      eeprom: eepromModel == null
          ? null
          : Uint8List.fromList(eepromModel.data),
      externalInputs: {
        for (final e in cpu.externalInputs)
          if (e.value != null) e.base: e.value!,
      },
      note: note,
    );
  }

  // ---- putting one back -------------------------------------------------

  /// Puts the machine back. Anything the snapshot does not mention is left
  /// as it is, which is what lets a snapshot taken without an EEPROM be
  /// restored onto a machine that has one.
  void apply(
    H8Cpu cpu, {
    I2cEeprom? eepromModel,
    Keypad? panel,
  }) {
    cpu.mem.clear();
    for (final entry in memory.entries) {
      final base = entry.key;
      final bytes = entry.value;
      cpu.mem.allocate(base);
      for (var i = 0; i < bytes.length; i++) {
        cpu.mem.poke(base + i, bytes[i]);
      }
    }

    // The registers and peripherals go back after memory: the DMAC syncs
    // itself to memory as it is restored.
    cpu.restoreState(cpuState);

    cpu.releaseAllPins();
    for (final entry in pins.entries) {
      final (driven, level) = entry.value;
      for (var bit = 0; bit < 8; bit++) {
        if ((driven >> bit) & 1 == 0) continue;
        cpu.setPin(entry.key, bit, (level >> bit) & 1 == 1);
      }
    }

    for (final f in cpu.flash) {
      f.clearWriteLog();
      // The command phase is deliberately not carried: a device caught
      // mid-unlock is a transient, and starting from read mode is both
      // safer and what a power cycle would do.
      f.reset();
      for (final b in flashWrites[f.base] ?? const <Map<String, int>>[]) {
        final rec = f.written.putIfAbsent(
            b['bank']!, () => FlashBankWrites(b['bank']!));
        rec.pages = b['pages']!;
        rec.bytes = b['bytes']!;
        rec.erases = b['erases']!;
        rec.lowest = b['lowest']!;
        rec.highest = b['highest']!;
      }
    }

    if (panel != null && keypad.isNotEmpty) {
      panel.down
        ..clear()
        ..addAll((keypad['down'] as List).cast<int>());
      panel.latched
        ..clear()
        ..addAll((keypad['latched'] as List).cast<int>());
      final knobs = (keypad['knobs'] as List?) ?? const [];
      for (var i = 0; i < knobs.length && i < panel.knobs.length; i++) {
        final k = Map<String, dynamic>.from(knobs[i] as Map);
        panel.knobs[i].phase = k['phase'] as int;
        panel.knobs[i].pending = k['pending'] as int;
        panel.knobs[i].angle = (k['angle'] as num).toDouble();
      }
      // The reverse key and the knob contacts are pins, and the pins were
      // restored above -- but a panel that was never attached would not have
      // driven them, so put them back from the panel's own state.
      panel.driveKnobs();
    }

    if (eepromModel != null && eeprom != null) {
      final n = eeprom!.length < eepromModel.data.length
          ? eeprom!.length
          : eepromModel.data.length;
      eepromModel.data.setRange(0, n, eeprom!);
      // Whatever transfer was in flight is not carried: the bus goes idle,
      // which is what the machine sees after a power cycle. Only worth doing
      // to a part that is actually on the pins -- reset() lets go of SDA,
      // and a detached model has nothing to let go of.
      if (cpu.eeprom == eepromModel) eepromModel.reset();
    }

    for (final e in cpu.externalInputs) {
      final v = externalInputs[e.base];
      if (v != null) e.value = v;
    }
  }

  // ---- the file ---------------------------------------------------------

  Map<String, dynamic> toJson() => {
        'format': kSnapshotFormat,
        'version': kSnapshotVersion,
        if (note.isNotEmpty) 'note': note,
        'cpu': cpuState,
        'memory': [
          for (final e in memory.entries)
            {'base': _hex(e.key), 'bytes': base64Encode(e.value)},
        ],
        'pins': [
          for (final e in pins.entries)
            {'dr': _hex(e.key), 'driven': e.value.$1, 'level': e.value.$2},
        ],
        'flashWrites': [
          for (final e in flashWrites.entries)
            {'base': _hex(e.key), 'banks': e.value},
        ],
        if (keypad.isNotEmpty) 'keypad': keypad,
        if (eeprom != null) 'eeprom': base64Encode(eeprom!),
        'externalInputs': [
          for (final e in externalInputs.entries)
            {'base': _hex(e.key), 'value': e.value},
        ],
      };

  static Snapshot fromJson(Map<String, dynamic> j) {
    if (j['format'] != kSnapshotFormat) {
      throw const FormatException('not a simulator snapshot');
    }
    final version = j['version'];
    if (version is! int || version > kSnapshotVersion) {
      throw FormatException(
          'this snapshot is version $version and this build reads up to '
          '$kSnapshotVersion');
    }

    final mem = <int, Uint8List>{};
    for (final e in (j['memory'] as List? ?? const [])) {
      final m = Map<String, dynamic>.from(e as Map);
      mem[_unhex(m['base'])] = base64Decode(m['bytes'].toString());
    }

    final pins = <int, (int, int)>{};
    for (final e in (j['pins'] as List? ?? const [])) {
      final m = Map<String, dynamic>.from(e as Map);
      pins[_unhex(m['dr'])] = (m['driven'] as int, m['level'] as int);
    }

    final writes = <int, List<Map<String, int>>>{};
    for (final e in (j['flashWrites'] as List? ?? const [])) {
      final m = Map<String, dynamic>.from(e as Map);
      writes[_unhex(m['base'])] = [
        for (final b in (m['banks'] as List))
          Map<String, int>.from(b as Map),
      ];
    }

    final inputs = <int, int>{};
    for (final e in (j['externalInputs'] as List? ?? const [])) {
      final m = Map<String, dynamic>.from(e as Map);
      inputs[_unhex(m['base'])] = m['value'] as int;
    }

    final ee = j['eeprom'];

    return Snapshot(
      cpuState: (j['cpu'] as List).cast<int>(),
      memory: mem,
      pins: pins,
      flashWrites: writes,
      keypad: j['keypad'] == null
          ? const {}
          : Map<String, dynamic>.from(j['keypad'] as Map),
      eeprom: ee == null ? null : base64Decode(ee.toString()),
      externalInputs: inputs,
      note: j['note']?.toString() ?? '',
    );
  }

  /// The bytes that go in the file: JSON, gzipped.
  List<int> encode() => gzipEncode(utf8.encode(jsonEncode(toJson())));

  static Snapshot decode(List<int> bytes) {
    // A file that is not gzipped is read as plain JSON, so one that has been
    // unpacked by hand to look at still loads.
    List<int> raw;
    try {
      raw = gzipDecode(bytes);
    } catch (_) {
      raw = bytes;
    }
    final decoded = jsonDecode(utf8.decode(raw));
    if (decoded is! Map) {
      throw const FormatException('a snapshot must be a JSON object');
    }
    return Snapshot.fromJson(Map<String, dynamic>.from(decoded));
  }

  /// How many bytes of memory it carries, for the status line.
  int get memoryBytes =>
      memory.values.fold(0, (total, b) => total + b.length);
}

