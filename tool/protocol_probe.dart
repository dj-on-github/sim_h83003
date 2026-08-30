// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Drives the boot ROM's serial command protocol and records what comes back.
//
// The protocol only runs when a host is talking, so none of it is exercised
// by booting the machine. This feeds bytes into the simulated SCI, lets the
// firmware answer, and writes the exchange out as a fixture. Replaying that
// fixture against a rebuilt ROM and comparing is what turns "the handlers
// compile" into "the handlers behave".
//
// Usage:
//   dart run tool/protocol_probe.dart <image.bin> --capture FILE.json
//   dart run tool/protocol_probe.dart <image.bin> --compare FILE.json
//     [--warmup N] [--settle N] [--send "48 65"] [--flash machine|BASE:SIZE]

import 'dart:convert';
import 'dart:io';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci.dart';

String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// The protocol state byte the dispatcher switches on.
const int stateByte = 0xFFFD1E;
const int chanSelection = 0xFFFD1C;

/// Commands the boot ROM recognises, from the table at H'0014E5.
const List<int> commands = [
  0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x50, 0x52,
  0x53, 0x54, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x72, 0x77,
];

class Machine {
  /// [download] invalidates the application's entry pointer so the boot ROM
  /// keeps control and runs its own service loop.
  ///
  /// This is the only state the command protocol can be reached in. While
  /// the application is running it owns SCI1 and clears RDRF itself, so a
  /// byte delivered to the channel is gone before the boot ROM's state-0
  /// handler looks for it -- the handler runs some 14,700 times and never
  /// sees a thing.
  Machine(String path, {bool download = true, String? flashSpec}) {
    loadRawBinary(File(path).readAsBytesSync(), 0, cpu.mem.poke);
    if (download) cpu.mem.poke(0x200000, 0x01);
    if (flashSpec != null) {
      // "machine" attaches the artista 180's own two devices; otherwise
      // BASE:SIZE, both hex. Without either, the address space is plain
      // memory and every programming command is refused at the identify.
      if (flashSpec == 'machine') {
        for (final r in artista180Flash) {
          cpu.attachFlash(JedecFlash.forRegion(r));
        }
      } else {
        final parts = flashSpec.split(':');
        cpu.attachFlash(JedecFlash(
          base: int.parse(parts[0], radix: 16),
          size: int.parse(parts.length > 1 ? parts[1] : '80000', radix: 16),
        ));
      }
    }
    cpu.reset();
  }

  final H8Cpu cpu = H8Cpu();

  /// Bit 1 of CHAN_SELECTION picks the channel every serial routine uses.
  SciChannel get channel =>
      (cpu.peekBus(chanSelection) & 0x02) != 0 ? cpu.sci0 : cpu.sci1;

  void run(int steps) {
    for (var i = 0; i < steps && !(cpu.halted && !cpu.sleeping); i++) {
      cpu.step();
    }
  }

  /// Sends [bytes] and returns whatever the firmware transmits in reply.
  List<int> exchange(List<int> bytes, int settle) {
    final ch = channel;
    ch.txLog.clear();
    ch.receive(bytes);
    run(settle);
    return List<int>.from(ch.txLog);
  }

  /// Zeroing the state byte is NOT enough to isolate one command from the
  /// next: H'FFFD14, H'FFFD1F and H'FFFD20 carry over, and work started by an
  /// earlier command can emit bytes during a later one's window. Measured
  /// that way, the sweep credited a reply of "GO" to 'I' when 'I' transmits
  /// nothing at all. Every exchange therefore gets a machine booted from
  /// reset.
}

String show(List<int> bytes) {
  if (bytes.isEmpty) return '(nothing)';
  final hex = bytes.map(hex2).join(' ');
  final text = String.fromCharCodes(
      bytes.map((b) => b >= 32 && b < 127 ? b : 0x2E));
  return '$hex   "$text"';
}

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/protocol_probe.dart <image.bin> '
        '[--capture F | --compare F] [--warmup N] [--settle N] '
        '[--send "hex"]');
    exit(2);
  }
  String? opt(String n) {
    final i = args.indexOf(n);
    return (i >= 0 && i + 1 < args.length) ? args[i + 1] : null;
  }

  // Long enough that the application is running and calling the service
  // routine round its main loop; the original first does so at about 3.3M.
  // Reaching the boot ROM's own loop takes far less than reaching the
  // application's main loop did.
  final flashSpec = opt('--flash');
  final warmup = int.parse(opt('--warmup') ?? '1500000');
  final settle = int.parse(opt('--settle') ?? '200000');

  /// A machine booted from reset, ready to be given one command.
  Machine fresh() {
    final m = Machine(args.first,
        download: !args.contains('--running'), flashSpec: flashSpec);
    m.run(warmup);
    return m;
  }

  stderr.writeln('booting $warmup instructions per exchange...');
  final probe = fresh();
  final ch = (probe.cpu.peekBus(chanSelection) & 0x02) != 0 ? 'SCI0' : 'SCI1';
  stderr.writeln("protocol channel: $ch  "
      "(CHAN_SELECTION = H'${hex2(probe.cpu.peekBus(chanSelection))})");

  // --send: one ad-hoc exchange.
  final send = opt('--send');
  if (send != null) {
    // Either hex bytes ("72 30 30") or, in double quotes, literal text
    // ("\"r000400\"") with \\r and \\n understood.
    final List<int> bytes;
    if (send.startsWith('"') && send.endsWith('"') && send.length >= 2) {
      bytes = send
          .substring(1, send.length - 1)
          .replaceAll(r'\r', '\r')
          .replaceAll(r'\n', '\n')
          .codeUnits;
    } else {
      bytes = send
          .split(RegExp(r'[\s,]+'))
          .where((s) => s.isNotEmpty)
          .map((s) => int.parse(s, radix: 16))
          .toList();
    }
    final m = fresh();
    final reply = m.exchange(bytes, settle);
    print('sent  ${show(bytes)}');
    print('got   ${show(reply)}');
    print("state H'${hex2(m.cpu.peekBus(stateByte))}");
    return;
  }

  // Sweep every command the table knows about.
  final results = <Map<String, dynamic>>[];
  print('  cmd        reply                              state after');
  for (final c in commands) {
    final m = fresh();
    final reply = m.exchange([c], settle);
    final state = m.cpu.peekBus(stateByte);
    results.add({
      'command': c,
      'reply': reply,
      'state': state,
    });
    final label = "'${String.fromCharCode(c)}' H'${hex2(c)}";
    print('  ${label.padRight(10)} ${show(reply).padRight(34)} '
        "H'${hex2(state)}");
  }

  final capture = opt('--capture');
  if (capture != null) {
    File(capture).writeAsStringSync(
        const JsonEncoder.withIndent('  ').convert({
      'image': args.first.split(Platform.pathSeparator).last,
      'warmup': warmup,
      'settle': settle,
      'exchanges': results,
    }));
    print('\nwrote ${results.length} exchanges to $capture');
    return;
  }

  final compare = opt('--compare');
  if (compare != null) {
    final want = jsonDecode(File(compare).readAsStringSync())
        as Map<String, dynamic>;
    final ref = (want['exchanges'] as List).cast<Map<String, dynamic>>();
    var same = 0;
    final diffs = <String>[];
    for (var i = 0; i < ref.length && i < results.length; i++) {
      final a = ref[i], b = results[i];
      final okReply = '${a['reply']}' == '${b['reply']}';
      final okState = a['state'] == b['state'];
      if (okReply && okState) {
        same++;
      } else {
        final c = String.fromCharCode(a['command'] as int);
        diffs.add("  '$c'  expected ${show((a['reply'] as List).cast<int>())}"
            " state H'${hex2(a['state'] as int)}\n"
            "       got      ${show((b['reply'] as List).cast<int>())}"
            " state H'${hex2(b['state'] as int)}");
      }
    }
    print('\n$same of ${ref.length} exchanges match');
    for (final d in diffs) {
      print(d);
    }
    exit(diffs.isEmpty ? 0 : 1);
  }
}
