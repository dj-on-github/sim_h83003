/// The simulator's startup settings, as they live in ~/.h8simrc.
///
/// Setting a session up by hand is a dozen small decisions -- which views to
/// open, where the memory window sits, which port to bridge, what to trace,
/// which flash image, which keys to hold down -- and every one of them has to
/// be made again the next time. This is that set of decisions, written down.
///
/// The file is JSON, and every field is optional: a missing one keeps the
/// built-in default, so a file naming only the two things you care about is a
/// perfectly good file. Unknown fields are ignored rather than rejected, so a
/// file written by a later version still loads.
///
/// Nothing here touches the running simulator. Applying a config and
/// capturing one are the app's job, in main.dart, because they reach into
/// widget state; this is the data and the file, which is the part worth
/// testing on its own.
library;

import 'dart:convert';

/// Numbers that name addresses are written as hex strings, because that is
/// how they are written everywhere else in the app and in the machine's own
/// documentation. "11B10E", "0x11B10E" and "H'11B10E" are all accepted.
int? parseAddress(Object? v) {
  if (v == null) return null;
  if (v is int) return v;
  var s = v.toString().trim();
  if (s.isEmpty) return null;
  if (s.startsWith("H'") || s.startsWith("h'")) {
    s = s.substring(2);
  } else if (s.startsWith('0x') || s.startsWith('0X')) {
    s = s.substring(2);
  }
  return int.tryParse(s, radix: 16);
}

String formatAddress(int v, [int digits = 6]) =>
    v.toRadixString(16).toUpperCase().padLeft(digits, '0');

/// Which panes are open. In the wide layout these are the checkboxes; in the
/// narrow one the first that is set decides the tab.
class ViewConfig {
  const ViewConfig({
    this.memory = true,
    this.disassembly = true,
    this.screen = false,
    this.sci = false,
    this.itu = false,
    this.dma = false,
    this.io = false,
    this.trace = false,
    this.profile = false,
    this.flash = false,
    this.eeprom = false,
    this.buttons = false,
    this.watch = false,
  });

  final bool memory, disassembly, screen, sci, itu, dma;
  final bool io, trace, profile, flash, eeprom, buttons, watch;

  static const _keys = [
    'memory', 'disassembly', 'screen', 'sci', 'itu', 'dma',
    'io', 'trace', 'profile', 'flash', 'eeprom', 'buttons', 'watch',
  ];

  factory ViewConfig.fromJson(Map<String, dynamic> j) {
    bool at(String k, bool fallback) =>
        j[k] is bool ? j[k] as bool : fallback;
    return ViewConfig(
      memory: at('memory', true),
      disassembly: at('disassembly', true),
      screen: at('screen', false),
      sci: at('sci', false),
      itu: at('itu', false),
      dma: at('dma', false),
      io: at('io', false),
      trace: at('trace', false),
      profile: at('profile', false),
      flash: at('flash', false),
      eeprom: at('eeprom', false),
      buttons: at('buttons', false),
      watch: at('watch', false),
    );
  }

  List<bool> get asList => [
        memory, disassembly, screen, sci, itu, dma,
        io, trace, profile, flash, eeprom, buttons, watch,
      ];

  Map<String, dynamic> toJson() {
    final v = asList;
    return {for (var i = 0; i < _keys.length; i++) _keys[i]: v[i]};
  }
}

/// Where the memory window sits, and whether it chases the PC.
class MemoryConfig {
  const MemoryConfig({this.followPc = true, this.address = 0x000100});

  final bool followPc;

  /// Where to park the window. Only used when [followPc] is off -- with it on
  /// the PC decides, and this is just where it starts.
  final int address;

  factory MemoryConfig.fromJson(Map<String, dynamic> j) => MemoryConfig(
        followPc: j['followPc'] is bool ? j['followPc'] as bool : true,
        address: parseAddress(j['address']) ?? 0x000100,
      );

  Map<String, dynamic> toJson() =>
      {'followPc': followPc, 'address': formatAddress(address)};
}

/// The SCI tab's host link.
class SciConfig {
  const SciConfig({
    this.channel = 1,
    this.transport = 'serial',
    this.port,
    this.tcpPort = 5555,
    this.phiHz = 11059200,
    this.bridge = false,
  });

  /// 0 or 1. SCI1 is the machine's PC port.
  final int channel;

  /// 'serial' or 'tcp'.
  final String transport;

  /// The host serial port, when the transport is 'serial'. Null leaves the
  /// menu on nothing chosen.
  final String? port;

  /// The port to listen on, when the transport is 'tcp'.
  final int tcpPort;

  final int phiHz;

  /// Whether to switch the bridge on at startup.
  final bool bridge;

  bool get useTcp => transport.toLowerCase() == 'tcp';

  factory SciConfig.fromJson(Map<String, dynamic> j) => SciConfig(
        channel: j['channel'] is int ? j['channel'] as int : 1,
        transport: j['transport']?.toString() ?? 'serial',
        port: j['port']?.toString(),
        tcpPort: j['tcpPort'] is int ? j['tcpPort'] as int : 5555,
        phiHz: j['phiHz'] is int ? j['phiHz'] as int : 11059200,
        bridge: j['bridge'] is bool ? j['bridge'] as bool : false,
      );

  Map<String, dynamic> toJson() => {
        'channel': channel,
        'transport': transport,
        if (port != null) 'port': port,
        'tcpPort': tcpPort,
        'phiHz': phiHz,
        'bridge': bridge,
      };
}

/// One pin held from outside, as the IO tab's overrides do it.
class PinConfig {
  const PinConfig({required this.port, required this.bit, required this.level});

  /// The port letter or digit as the manual names it: '4'..'9', 'A'..'C'.
  final String port;
  final int bit;

  /// 'high', 'low', or 'float' to leave it alone.
  final String level;

  bool get isHigh => level.toLowerCase() == 'high';
  bool get isFloat => level.toLowerCase() == 'float';

  factory PinConfig.fromJson(Map<String, dynamic> j) => PinConfig(
        port: j['port']?.toString() ?? '',
        bit: j['bit'] is int ? j['bit'] as int : 0,
        level: j['level']?.toString() ?? 'float',
      );

  Map<String, dynamic> toJson() => {'port': port, 'bit': bit, 'level': level};
}

/// One watched location in the Trace tab.
class TraceConfig {
  const TraceConfig({
    required this.target,
    this.bits = 8,
    this.bigEndian = true,
  });

  /// A symbol name, or an address in hex.
  final String target;
  final int bits;
  final bool bigEndian;

  factory TraceConfig.fromJson(Map<String, dynamic> j) => TraceConfig(
        target: j['target']?.toString() ?? '',
        bits: j['bits'] is int ? j['bits'] as int : 8,
        bigEndian: j['bigEndian'] is bool ? j['bigEndian'] as bool : true,
      );

  Map<String, dynamic> toJson() =>
      {'target': target, 'bits': bits, 'bigEndian': bigEndian};
}

/// The flash model and the image behind it.
class FlashConfig {
  const FlashConfig({this.file, this.load = false, this.enable = false});

  final String? file;

  /// Read the file at startup. Switching the model on reads it anyway, so
  /// this is really "load it even with the model off".
  final bool load;

  /// Switch the model on, which reads the file and attaches the devices.
  final bool enable;

  factory FlashConfig.fromJson(Map<String, dynamic> j) => FlashConfig(
        file: j['file']?.toString(),
        load: j['load'] is bool ? j['load'] as bool : false,
        enable: j['enable'] is bool ? j['enable'] as bool : false,
      );

  Map<String, dynamic> toJson() => {
        if (file != null) 'file': file,
        'load': load,
        'enable': enable,
      };
}

/// The serial EEPROM on port 4.
class EepromConfig {
  const EepromConfig({
    this.file = 'eeprom.json',
    this.enable = false,
    this.verifyFriendly = false,
  });

  final String file;
  final bool enable;

  /// Leave the address counter on the byte just written, so the firmware's
  /// write-and-verify agrees. A real part leaves it one further on.
  final bool verifyFriendly;

  factory EepromConfig.fromJson(Map<String, dynamic> j) => EepromConfig(
        file: j['file']?.toString() ?? 'eeprom.json',
        enable: j['enable'] is bool ? j['enable'] as bool : false,
        verifyFriendly:
            j['verifyFriendly'] is bool ? j['verifyFriendly'] as bool : false,
      );

  Map<String, dynamic> toJson() =>
      {'file': file, 'enable': enable, 'verifyFriendly': verifyFriendly};
}

/// A snapshot to restore at startup.
///
/// It beats everything else in the file that touches the machine: a snapshot
/// already has the memory image, the flash contents and the EEPROM in it, so
/// loading one and then loading an image over the top would undo it.
class SnapshotConfig {
  const SnapshotConfig({this.file, this.load = false});

  final String? file;
  final bool load;

  factory SnapshotConfig.fromJson(Map<String, dynamic> j) => SnapshotConfig(
        file: j['file']?.toString(),
        load: j['load'] is bool ? j['load'] as bool : false,
      );

  Map<String, dynamic> toJson() =>
      {if (file != null) 'file': file, 'load': load};
}

/// The stop condition and the addresses whose writes are recorded.
///
/// Both are things you set before a run rather than during one, which is
/// what this file is for. The condition is kept as text, not as a parsed
/// tree: a file with a typo in it should open with the typo showing in the
/// field, ready to be fixed, rather than being dropped on the way in.
class WatchConfig {
  const WatchConfig({this.condition, this.armed = false, this.writes = const []});

  /// The condition, in the language of lib/condition.dart.
  final String? condition;

  /// Whether it is armed at startup. A condition can be kept in the file
  /// without stopping every run that starts.
  final bool armed;

  /// Ranges whose writes are recorded, each a (first, last) pair. A single
  /// address is written as one string; a range as "first-last".
  final List<(int, int)> writes;

  factory WatchConfig.fromJson(Map<String, dynamic> j) {
    final ranges = <(int, int)>[];
    if (j['writes'] is List) {
      for (final e in j['writes'] as List) {
        final r = parseRange(e);
        if (r != null) ranges.add(r);
      }
    }
    return WatchConfig(
      condition: j['condition']?.toString(),
      armed: j['armed'] is bool ? j['armed'] as bool : false,
      writes: ranges,
    );
  }

  Map<String, dynamic> toJson() => {
        if (condition != null && condition!.isNotEmpty) 'condition': condition,
        'armed': armed,
        'writes': [for (final r in writes) formatRange(r)],
      };
}

/// Reads "11B10E" or "11B10E-11B10F" into a pair. Null if neither.
(int, int)? parseRange(Object? v) {
  if (v is! String) {
    final one = parseAddress(v);
    return one == null ? null : (one, one);
  }
  final text = v.trim();
  // Split on the last '-', so a range written with H' prefixes still parts
  // in the right place.
  final cut = text.lastIndexOf('-');
  if (cut > 0) {
    final a = parseAddress(text.substring(0, cut));
    final b = parseAddress(text.substring(cut + 1));
    if (a != null && b != null) return a <= b ? (a, b) : (b, a);
  }
  final one = parseAddress(text);
  return one == null ? null : (one, one);
}

/// The way back, matching how the app writes addresses everywhere else.
String formatRange((int, int) r) => r.$1 == r.$2
    ? formatAddress(r.$1)
    : '${formatAddress(r.$1)}-${formatAddress(r.$2)}';

/// Keeping enough of each instruction to step back over it.
///
/// Off by default: recording costs speed, and a session that only ever runs
/// forwards should not pay for it.
class HistoryConfig {
  const HistoryConfig({this.enabled = false, this.steps = 200000});

  final bool enabled;

  /// How many instructions to keep.
  final int steps;

  factory HistoryConfig.fromJson(Map<String, dynamic> j) => HistoryConfig(
        enabled: j['enabled'] is bool ? j['enabled'] as bool : false,
        steps: j['steps'] is int && (j['steps'] as int) > 0
            ? j['steps'] as int
            : 200000,
      );

  Map<String, dynamic> toJson() => {'enabled': enabled, 'steps': steps};
}

/// A memory image to load over the demo program.
class ImageConfig {
  const ImageConfig({this.file, this.load = false});

  final String? file;
  final bool load;

  factory ImageConfig.fromJson(Map<String, dynamic> j) => ImageConfig(
        file: j['file']?.toString(),
        load: j['load'] is bool ? j['load'] as bool : false,
      );

  Map<String, dynamic> toJson() =>
      {if (file != null) 'file': file, 'load': load};
}

/// How the app looks and how fast it runs. The same three the gear button
/// already offers; they are here so one file holds the lot.
class AppearanceConfig {
  const AppearanceConfig({
    this.darkMode = true,
    this.fontFamily = 'monospace',
    this.cpuHz = 0,
  });

  final bool darkMode;
  final String fontFamily;

  /// 0 is "as fast as it will go".
  final int cpuHz;

  factory AppearanceConfig.fromJson(Map<String, dynamic> j) =>
      AppearanceConfig(
        darkMode: j['darkMode'] is bool ? j['darkMode'] as bool : true,
        fontFamily: j['fontFamily']?.toString() ?? 'monospace',
        cpuHz: j['cpuHz'] is int ? j['cpuHz'] as int : 0,
      );

  Map<String, dynamic> toJson() =>
      {'darkMode': darkMode, 'fontFamily': fontFamily, 'cpuHz': cpuHz};
}

/// Everything the file can say.
class SimConfig {
  const SimConfig({
    this.views = const ViewConfig(),
    this.memory = const MemoryConfig(),
    this.sci = const SciConfig(),
    this.pins = const [],
    this.traces = const [],
    this.profiling = false,
    this.flash = const FlashConfig(),
    this.eeprom = const EepromConfig(),
    this.heldKeys = const [],
    this.image = const ImageConfig(),
    this.snapshot = const SnapshotConfig(),
    this.watch = const WatchConfig(),
    this.history = const HistoryConfig(),
    this.dataBreakpoints = const [],
    this.instructionBreakpoints = const [],
    this.appearance = const AppearanceConfig(),
  });

  final ViewConfig views;
  final MemoryConfig memory;
  final SciConfig sci;
  final List<PinConfig> pins;
  final List<TraceConfig> traces;
  final bool profiling;
  final FlashConfig flash;
  final EepromConfig eeprom;

  /// Panel keys to hold down from the start, by code -- H'77 is clr, which
  /// is how the machine is put into service mode.
  final List<int> heldKeys;

  final ImageConfig image;
  final SnapshotConfig snapshot;
  final WatchConfig watch;
  final HistoryConfig history;
  final List<int> dataBreakpoints;
  final List<int> instructionBreakpoints;
  final AppearanceConfig appearance;

  static List<int> _addresses(Object? v) {
    if (v is! List) return const [];
    final out = <int>[];
    for (final e in v) {
      final a = parseAddress(e);
      if (a != null) out.add(a);
    }
    return out;
  }

  static Map<String, dynamic> _obj(Object? v) =>
      v is Map ? Map<String, dynamic>.from(v) : const <String, dynamic>{};

  factory SimConfig.fromJson(Map<String, dynamic> j) => SimConfig(
        views: ViewConfig.fromJson(_obj(j['views'])),
        memory: MemoryConfig.fromJson(_obj(j['memory'])),
        sci: SciConfig.fromJson(_obj(j['sci'])),
        pins: [
          for (final p in (j['pins'] is List ? j['pins'] as List : const []))
            PinConfig.fromJson(_obj(p)),
        ],
        traces: [
          for (final t
              in (j['traces'] is List ? j['traces'] as List : const []))
            TraceConfig.fromJson(_obj(t)),
        ],
        profiling: j['profiling'] is bool ? j['profiling'] as bool : false,
        flash: FlashConfig.fromJson(_obj(j['flash'])),
        eeprom: EepromConfig.fromJson(_obj(j['eeprom'])),
        heldKeys: _addresses(j['heldKeys']),
        image: ImageConfig.fromJson(_obj(j['image'])),
        snapshot: SnapshotConfig.fromJson(_obj(j['snapshot'])),
        watch: WatchConfig.fromJson(_obj(j['watch'])),
        history: HistoryConfig.fromJson(_obj(j['history'])),
        dataBreakpoints: _addresses(j['dataBreakpoints']),
        instructionBreakpoints: _addresses(j['instructionBreakpoints']),
        appearance: AppearanceConfig.fromJson(_obj(j['appearance'])),
      );

  /// Reads a file's contents. Anything that is not an object, or is not JSON
  /// at all, is reported rather than swallowed: a config with a typo in it
  /// should say so, not quietly do nothing.
  static SimConfig parse(String text) {
    final decoded = jsonDecode(text);
    if (decoded is! Map) {
      throw const FormatException('the file must hold a JSON object');
    }
    return SimConfig.fromJson(Map<String, dynamic>.from(decoded));
  }

  Map<String, dynamic> toJson() => {
        'views': views.toJson(),
        'memory': memory.toJson(),
        'sci': sci.toJson(),
        'pins': [for (final p in pins) p.toJson()],
        'traces': [for (final t in traces) t.toJson()],
        'profiling': profiling,
        'flash': flash.toJson(),
        'eeprom': eeprom.toJson(),
        'heldKeys': [for (final k in heldKeys) formatAddress(k, 2)],
        'image': image.toJson(),
        'snapshot': snapshot.toJson(),
        'watch': watch.toJson(),
        'history': history.toJson(),
        'dataBreakpoints': [for (final a in dataBreakpoints) formatAddress(a)],
        'instructionBreakpoints': [
          for (final a in instructionBreakpoints) formatAddress(a)
        ],
        'appearance': appearance.toJson(),
      };

  /// Indented, so the file is meant to be edited by hand as well as written
  /// by the app.
  String toText() => '${const JsonEncoder.withIndent('  ').convert(toJson())}\n';
}

/// What the file is called. Kept here so the name is stated once.
const String kConfigFileName = '.h8simrc';
