// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Disassembles a program file, or any part of one.
//
// Takes a flat binary dump, an Intel HEX file or a Motorola S-record, works
// out which from the contents, and prints a listing over whatever address
// range is asked for. With a symbol table it labels the addresses it knows
// and substitutes names into operands, so the listing reads in the project's
// own vocabulary rather than in bare hex.
//
// Usage:
//   dart run tool/disasm.dart <file> [options]
//     --sym FILE        symbol table (.sym, or the JSON form)
//     --base HEX        where a flat binary loads (default 0)
//     --start HEX       first address to disassemble
//     --end HEX         last address, inclusive
//     --count N         number of instructions instead of --end
//     --no-bytes        leave out the raw byte column
//     --no-labels       do not break the listing for symbol labels
//     --out FILE        write to a file instead of stdout

import 'dart:io';

import 'package:sim_h83003/h8disasm.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sparse_memory.dart';
import 'package:sim_h83003/symbols.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

/// Accepts H'xxxx, 0xxxxx, $xxxx or bare hex.
int? parseAddr(String? s) {
  if (s == null) return null;
  var t = s.trim();
  if (t.startsWith("H'") || t.startsWith("h'")) t = t.substring(2);
  if (t.startsWith('\$')) t = t.substring(1);
  if (t.startsWith('0x') || t.startsWith('0X')) t = t.substring(2);
  return int.tryParse(t, radix: 16);
}

void main(List<String> args) {
  if (args.isEmpty || args.contains('--help') || args.contains('-h')) {
    stderr.writeln('usage: dart run tool/disasm.dart <file> [--sym FILE] '
        '[--base HEX] [--start HEX] [--end HEX | --count N]\n'
        '                                 [--no-bytes] [--no-labels] '
        '[--out FILE]');
    exit(args.isEmpty ? 2 : 0);
  }
  String? opt(String n) {
    final i = args.indexOf(n);
    return (i >= 0 && i + 1 < args.length) ? args[i + 1] : null;
  }

  final path = args.first;
  final file = File(path);
  if (!file.existsSync()) {
    stderr.writeln('no such file: $path');
    exit(2);
  }
  final bytes = file.readAsBytesSync();

  // ---- load it, whatever it is -------------------------------------------
  final mem = SparseMemory();
  final format = detectProgramFormat(bytes);
  final base = parseAddr(opt('--base')) ?? 0;
  final HexResult result;
  switch (format) {
    case ProgramFormat.raw:
      result = loadRawBinary(bytes, base, mem.poke);
    case ProgramFormat.intelHex:
    case ProgramFormat.srecord:
      result = parseHexFile(String.fromCharCodes(bytes), mem.poke);
  }
  if (result.isEmpty) {
    stderr.writeln('nothing loaded from $path'
        '${result.errors.isEmpty ? '' : ': ${result.errors.first}'}');
    exit(1);
  }

  // ---- symbols ------------------------------------------------------------
  var symbols = const <int, String>{};
  final symPath = opt('--sym');
  if (symPath != null) {
    final f = File(symPath);
    if (!f.existsSync()) {
      stderr.writeln('no such symbol file: $symPath');
      exit(2);
    }
    symbols = parseSymbolTable(f.readAsStringSync());
    if (symbols.isEmpty) {
      stderr.writeln('warning: no symbols found in $symPath');
    }
  }

  // ---- where to start and stop -------------------------------------------
  // With nothing asked for, the reset vector is the one address in an H8
  // image that is always meaningful.
  int? resetVector;
  if (mem.isAllocated(0)) {
    final v = (mem.peek(0) << 24) |
        (mem.peek(1) << 16) |
        (mem.peek(2) << 8) |
        mem.peek(3);
    final a = v & SparseMemory.addrMask;
    if (a != 0 && mem.isAllocated(a)) resetVector = a;
  }
  final start = parseAddr(opt('--start')) ??
      resetVector ??
      result.minAddress ??
      base;

  final end = parseAddr(opt('--end'));
  final countArg = opt('--count');
  final count = countArg == null ? null : int.tryParse(countArg);
  if (end != null && countArg != null) {
    stderr.writeln('--end and --count are alternatives; pick one');
    exit(2);
  }
  // Neither given: enough to see what is there without flooding the terminal.
  final limitInstructions = count ?? (end == null ? 64 : 1 << 30);
  // Sparse memory allocates a whole 64K bank the first time any byte in it is
  // written, so "is this allocated" says nothing about where the file's data
  // actually stopped. Without this bound a short HEX file runs off the end of
  // its own contents into a bank of zeros.
  final hardEnd = end ?? result.maxAddress;

  final showBytes = !args.contains('--no-bytes');
  final showLabels = !args.contains('--no-labels');

  // ---- listing ------------------------------------------------------------
  final out = StringBuffer();
  final formatName = switch (format) {
    ProgramFormat.raw => 'flat binary',
    ProgramFormat.intelHex => 'Intel HEX',
    ProgramFormat.srecord => 'S-record',
  };
  final span = result.minAddress == null
      ? ''
      : "  H'${hex6(result.minAddress!)}-H'${hex6(result.maxAddress!)}";
  final name = path.split(Platform.pathSeparator).last;
  out.writeln('; $name — $formatName, ${result.bytesLoaded} bytes loaded$span');
  if (symbols.isNotEmpty) out.writeln('; ${symbols.length} symbols');
  if (result.errors.isNotEmpty) {
    out.writeln('; ${result.errors.length} warning(s) while loading; '
        'first: ${result.errors.first}');
  }
  out.writeln(';');

  /// Replaces an address in an operand with its symbol. Immediates are left
  /// alone — `#H'4C` is a number, not a place.
  final addrInOperand =
      RegExp(r"(?<!#)H'([0-9A-Fa-f]{6}|[0-9A-Fa-f]{4})(?![0-9A-Fa-f])");
  String withSymbols(String text) {
    if (symbols.isEmpty) return text;
    return text.replaceAllMapped(addrInOperand, (m) {
      final raw = m.group(1)!;
      var a = int.parse(raw, radix: 16);
      // A 16-bit absolute above H'8000 is sign-extended into the on-chip
      // register area, which is how the firmware reaches SCR0 and friends.
      if (raw.length == 4 && a >= 0x8000) a |= 0xFF0000;
      return symbols[a] ?? m.group(0)!;
    });
  }

  var pc = start & SparseMemory.addrMask;
  var emitted = 0;
  var skipped = 0;
  while (emitted < limitInstructions) {
    if (hardEnd != null && pc > hardEnd) break;
    if (pc > SparseMemory.addrMask) break;

    // Unallocated stretches are reported once rather than disassembled as a
    // long run of zeros that was never there.
    if (!mem.isAllocated(pc)) {
      skipped++;
      pc += 1;
      continue;
    }
    if (skipped > 0) {
      out.writeln('; ... $skipped byte(s) not present in the image');
      skipped = 0;
    }

    if (showLabels && symbols[pc] != null) {
      out.writeln();
      out.writeln('${symbols[pc]}:');
    }
    final d = disassembleH8(mem.peek, pc);
    final raw = showBytes
        ? [for (var k = 0; k < d.length; k++) hex2(mem.peek(pc + k))]
            .join(' ')
            .padRight(26)
        : '';
    out.writeln("  H'${hex6(pc)}  $raw${withSymbols(d.text)}");
    pc += d.length;
    emitted++;
  }
  if (skipped > 0) {
    out.writeln('; ... $skipped byte(s) not present in the image');
  }

  final outPath = opt('--out');
  if (outPath != null) {
    File(outPath).writeAsStringSync(out.toString());
    print('wrote $emitted instructions to $outPath');
  } else {
    stdout.write(out.toString());
  }
}
