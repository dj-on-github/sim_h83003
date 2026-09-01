// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Two images run side by side, stopped where they part company.
//
// "The new build behaves differently from the old one" is the question this
// project keeps asking, and answering it by hand meant writing the harness
// again each time. compare_routines answers it for one routine at a time;
// this is the whole-machine complement.
//
//   dart run tool/lockstep.dart <a.bin> <b.bin> [options]
//
//   --compare pc|state|writes   what has to match (default state)
//   --settle N                  run N instructions before comparing anything
//   --ignore FROM:TO            no divergence while either PC is in here
//                               (repeatable)
//   --watch FROM:TO             compare the writes landing here (repeatable;
//                               required by --compare writes)
//   --max N                     give up after N instructions (default 40M)
//   --depth N                   instructions of run-up to print (default 16)
//   --symbols FILE              a symbol file, to name what it prints
//   --hold CODE                 hold a panel key down on both sides
//   --hold-b CODE               hold it on the second side only
//   --no-flash                  leave the flash model off
//
// An image is loaded at 0 unless it is given an address: dump.bin@200000.
// Intel HEX and S-records carry their own addresses and are detected.
//
// The three comparisons, and when each is the one you want:
//
//   state   the registers and flags as well as the PC, so a difference is
//           caught at the instruction that caused it rather than at the
//           branch that revealed it. The one to reach for when the two
//           images are meant to be the same.
//   pc      the code only. Right when the two images genuinely differ
//           somewhere you do not care about, with --settle or --ignore to
//           step over it.
//   writes  not the code at all -- only what each side writes to --watch,
//           in order. Two implementations of the same routine make the same
//           writes however they get there, which is the comparison that
//           survives a rewrite.
//
// Comparing one image against itself with --hold-b is how to ask what a
// button actually changes: the answer is wherever the two runs first
// disagree.

import 'dart:io';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/keypad.dart';
import 'package:sim_h83003/lockstep.dart';

const _usage = '''
usage: dart run tool/lockstep.dart <a.bin> <b.bin> [options]

  --compare pc|state|writes   what has to match (default state)
  --settle N                  instructions to run before comparing
  --ignore FROM:TO            no divergence while a PC is in here
  --watch FROM:TO             compare the writes landing here
  --max N                     give up after N instructions (default 40000000)
  --depth N                   instructions of run-up to print (default 16)
  --symbols FILE              a symbol file, to name what it prints
  --hold CODE                 hold a panel key on both sides
  --hold-b CODE               hold it on the second side only
  --no-flash                  leave the flash model off

An image may carry an address: dump.bin@200000. Numbers are hex unless the
option is a count (--settle, --max, --depth), which are decimal.
''';

int _hex(String s) {
  final v = int.tryParse(s.replaceAll("H'", '').replaceAll('0x', ''),
      radix: 16);
  if (v == null) _die('not a hex number: $s');
  return v;
}

(int, int) _range(String s) {
  final parts = s.split(':');
  if (parts.length != 2) _die('a range is FROM:TO, not "$s"');
  final a = _hex(parts[0]), b = _hex(parts[1]);
  return a <= b ? (a, b) : (b, a);
}

Never _die(String why) {
  stderr.writeln('lockstep: $why');
  exit(2);
}

/// Builds one side. [spec] is a path, optionally with @address.
H8Cpu _machine(String spec, {required bool flash, required Keypad pad}) {
  final at = spec.lastIndexOf('@');
  final path = at < 0 ? spec : spec.substring(0, at);
  final base = at < 0 ? 0 : _hex(spec.substring(at + 1));

  final file = File(path);
  if (!file.existsSync()) _die('no such file: $path');
  final data = file.readAsBytesSync();

  final cpu = H8Cpu();
  final result = detectProgramFormat(data) == ProgramFormat.raw
      ? loadRawBinary(data, base, cpu.mem.poke)
      : parseHexFile(String.fromCharCodes(data), cpu.mem.poke);
  if (result.isEmpty) _die('could not load $path as HEX, S-records or binary');

  cpu.attachKeypad(pad);
  if (flash) {
    for (final r in artista180Flash) {
      cpu.attachFlash(JedecFlash.forRegion(r));
    }
  }
  cpu.reset();
  return cpu;
}

Map<int, String> _symbols(String path) {
  final out = <int, String>{};
  for (final line in File(path).readAsLinesSync()) {
    final parts = line.trim().split(RegExp(r'\s+'));
    if (parts.length == 3) {
      final addr = int.tryParse(parts[0], radix: 16);
      if (addr != null) out[addr] = parts[2];
    }
  }
  return out;
}

void _hold(Keypad pad, int code) {
  final key = Keypad.panelKeys.where((k) => k.code == code).firstOrNull;
  if (key == null) _die("no panel key with code H'${code.toRadixString(16)}");
  pad.setDown(key, true);
}

void main(List<String> args) {
  if (args.length < 2 || args.contains('--help') || args.contains('-h')) {
    print(_usage);
    exit(args.length < 2 ? 2 : 0);
  }

  var compare = LockstepCompare.state;
  var settle = 0, max = 40000000, depth = 16;
  var flash = true;
  final ignore = <(int, int)>[];
  final watch = <(int, int)>[];
  final holdBoth = <int>[];
  final holdB = <int>[];
  String? symbolFile;

  final positional = <String>[];
  for (var i = 0; i < args.length; i++) {
    String next(String what) {
      if (i + 1 >= args.length) _die('$what needs a value');
      return args[++i];
    }

    switch (args[i]) {
      case '--compare':
        final v = next('--compare');
        compare = switch (v) {
          'pc' => LockstepCompare.pc,
          'state' => LockstepCompare.state,
          'writes' => LockstepCompare.writes,
          _ => _die('--compare is pc, state or writes, not "$v"'),
        };
      case '--settle':
        settle = int.parse(next('--settle'));
      case '--max':
        max = int.parse(next('--max'));
      case '--depth':
        depth = int.parse(next('--depth'));
      case '--ignore':
        ignore.add(_range(next('--ignore')));
      case '--watch':
        watch.add(_range(next('--watch')));
      case '--symbols':
        symbolFile = next('--symbols');
      case '--hold':
        holdBoth.add(_hex(next('--hold')));
      case '--hold-b':
        holdB.add(_hex(next('--hold-b')));
      case '--no-flash':
        flash = false;
      default:
        if (args[i].startsWith('-')) _die('unknown option ${args[i]}');
        positional.add(args[i]);
    }
  }

  if (positional.length != 2) _die('two images are needed');
  if (compare == LockstepCompare.writes && watch.isEmpty) {
    _die('--compare writes needs at least one --watch FROM:TO');
  }

  final padA = Keypad(), padB = Keypad();
  final a = _machine(positional[0], flash: flash, pad: padA);
  final b = _machine(positional[1], flash: flash, pad: padB);
  for (final c in holdBoth) {
    _hold(padA, c);
    _hold(padB, c);
  }
  for (final c in holdB) {
    _hold(padB, c);
  }

  print('a: ${positional[0]}');
  print('b: ${positional[1]}');
  print('comparing ${compare.name}, up to $max instructions'
      '${settle == 0 ? '' : ', from instruction $settle'}');
  if (watch.isNotEmpty) {
    print('watching ${watch.map((r) => '${_h6(r.$1)}:${_h6(r.$2)}').join(' ')}');
  }
  print('');

  final started = DateTime.now();
  final result = Lockstep(
    LockstepSide('a', a),
    LockstepSide('b', b),
    config: LockstepConfig(
      compare: compare,
      maxSteps: max,
      settleSteps: settle,
      ignorePc: ignore,
      writeRanges: watch,
      historyDepth: depth,
    ),
  ).run();

  final byWrites = compare == LockstepCompare.writes;
  print(describeDivergence(result,
      symbols: symbolFile == null ? const {} : _symbols(symbolFile),
      writes: byWrites));
  final secs = DateTime.now().difference(started).inMilliseconds / 1000;
  final ran = byWrites
      ? '${result.a.stepsRun} and ${result.b.stepsRun} instructions'
      : '${result.a.stepsRun} instructions each side';
  print('$ran in ${secs.toStringAsFixed(1)}s');
  exit(result.inStep ? 0 : 1);
}

String _h6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
