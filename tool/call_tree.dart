// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Writes the call hierarchy as an indented tree, in the format of
// call_hierarchy.txt.
//
// Each function's calls are listed in the order the call instructions
// appear, so a callee reached from two places shows up twice. The first
// occurrence of a symbol anywhere in the tree carries its subtree; later
// ones say "// defined_elsewhere", which is also what stops recursion from
// running away. A function that calls nothing says "// terminal".
//
// Usage:
//   dart run tool/call_tree.dart <image.bin> [--sym FILE.sym]
//                                [--root SYMBOL] [--all-roots]
//                                [--out FILE] [--depth N]

import 'dart:io';
import 'dart:typed_data';

import 'package:sim_h83003/h8disasm.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');

class Image {
  Image(this.bytes);
  final Uint8List bytes;
  int peek(int a) => (a >= 0 && a < bytes.length) ? bytes[a] : 0;
  bool has(int a) => a >= 0 && a < bytes.length;
  int word(int a) => (peek(a) << 8) | peek(a + 1);
  int long(int a) => (word(a) << 16) | word(a + 2);
}

/// Symbol table, as written by analyze_dump.dart --sym.
class Symbols {
  final Map<int, String> byAddr = {};
  final Map<String, int> byName = {};

  void load(String path) {
    final re = RegExp(r"^\s*([A-Za-z_.$][\w.$]*)\s*=\s*H'([0-9A-Fa-f]+)");
    for (var line in File(path).readAsLinesSync()) {
      final i = line.indexOf(';');
      if (i >= 0) line = line.substring(0, i);
      final m = re.firstMatch(line);
      if (m == null) continue;
      final a = int.parse(m.group(2)!, radix: 16);
      byAddr.putIfAbsent(a, () => m.group(1)!);
      byName[m.group(1)!] = a;
    }
  }

  String name(int a) => byAddr[a] ?? 'sub_${hex6(a)}';
}

/// The call target of an instruction, if it is a call.
int? callTarget(Image img, int pc) {
  final b0 = img.peek(pc), b1 = img.peek(pc + 1);
  if (b0 == 0x55) {
    return (pc + 2 + (b1 < 0x80 ? b1 : b1 - 0x100)) & 0xFFFFFF; // BSR d:8
  }
  if (b0 == 0x5C && b1 == 0x00) {
    final w = img.word(pc + 2);
    return (pc + 4 + (w < 0x8000 ? w : w - 0x10000)) & 0xFFFFFF; // BSR d:16
  }
  if (b0 == 0x5E) return ((b1 << 16) | img.word(pc + 2)) & 0xFFFFFF; // JSR
  if (b0 == 0x5F) return img.long(b1) & 0xFFFFFF; // JSR @@aa:8
  return null;
}

/// Every call made by the function at [entry], in call-site order.
///
/// The body is followed the way execution would run it: fall through, take
/// both sides of a conditional branch, stop at a return or an unconditional
/// jump out. [limit] bounds it so a function that runs off the end of what
/// is really code cannot swallow the rest of the image.
List<int> callsFrom(Image img, int entry, int limit) {
  final seen = <int>{};
  final work = <int>[entry];
  final sites = <int, int>{}; // call-site pc -> callee
  while (work.isNotEmpty) {
    var pc = work.removeLast();
    while (true) {
      if (pc < entry || pc >= limit || seen.contains(pc) || !img.has(pc)) {
        break;
      }
      seen.add(pc);
      final ins = disassembleH8(img.peek, pc);
      final b0 = img.peek(pc), b1 = img.peek(pc + 1);
      final next = pc + ins.length;

      final callee = callTarget(img, pc);
      if (callee != null) {
        sites[pc] = callee;
        pc = next;
        continue;
      }
      // Conditional branches: follow the target as well as the fallthrough.
      if (b0 >= 0x40 && b0 <= 0x4F) {
        final t = (pc + 2 + (b1 < 0x80 ? b1 : b1 - 0x100)) & 0xFFFFFF;
        work.add(t);
        if (b0 == 0x40) break; // BRA
        pc = next;
        continue;
      }
      if (b0 == 0x58) {
        final w = img.word(pc + 2);
        final t = (pc + 4 + (w < 0x8000 ? w : w - 0x10000)) & 0xFFFFFF;
        work.add(t);
        if ((b1 >> 4) == 0) break; // BRA d:16
        pc = next;
        continue;
      }
      // Returns and jumps end this path.
      if (b0 == 0x54 || b0 == 0x56) break; // RTS / RTE
      if (b0 == 0x59 || b0 == 0x5A || b0 == 0x5B) break; // JMP
      if (b0 == 0x01 && b1 == 0x00 && img.peek(pc + 2) == 0x00) break;
      pc = next;
    }
  }
  final ordered = sites.keys.toList()..sort();
  return [for (final pc in ordered) sites[pc]!];
}

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/call_tree.dart <image.bin> '
        '[--sym FILE.sym] [--root SYMBOL] [--all-roots] [--out FILE]');
    exit(2);
  }
  String? opt(String n) {
    final i = args.indexOf(n);
    return (i >= 0 && i + 1 < args.length) ? args[i + 1] : null;
  }

  final img = Image(File(args.first).readAsBytesSync());
  final syms = Symbols();
  final symFile = opt('--sym');
  if (symFile != null) syms.load(symFile);
  final maxDepth = int.parse(opt('--depth') ?? '64');

  // Where each function ends: the next function entry. Without that bound a
  // walk can spill past a return into the next routine.
  final knownEntries = <int>{};
  syms.byAddr.forEach((a, n) {
    if (n.startsWith('sub_') ||
        n.startsWith('isr_') ||
        n.startsWith('tramp_')) {
      knownEntries.add(a);
    }
  });
  // Curated names sit on code too; anything below the on-chip registers and
  // above the vector table is a candidate.
  syms.byAddr.forEach((a, n) {
    if (a >= 0x000100 && a < 0x400000) knownEntries.add(a);
  });
  final entries = knownEntries.toList()..sort();

  int limitFor(int entry) {
    var lo = 0, hi = entries.length;
    while (lo < hi) {
      final mid = (lo + hi) >> 1;
      if (entries[mid] <= entry) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    final next = lo < entries.length ? entries[lo] : entry + 0x1000;
    return next > entry ? next : entry + 0x1000;
  }

  // The boot ROM's vector trampolines do not call their handler: they fetch
  // its address out of the application table and jump indirectly, which no
  // static walk can follow. Reconnecting them is what joins the application
  // to the tree instead of leaving it as a heap of unreachable roots.
  final trampToHandler = <int, int>{};
  final appTableArg = opt('--apptable');
  if (appTableArg != null) {
    final table = int.parse(appTableArg, radix: 16);
    for (var v = 0; v < 64; v++) {
      final tramp = img.long(v * 4) & 0xFFFFFF;
      final handler = img.long(table + v * 4) & 0xFFFFFF;
      if (tramp == 0 || handler == 0 || !img.has(handler)) continue;
      if (handler == tramp) continue;
      trampToHandler[tramp] = handler;
    }
  }

  final callCache = <int, List<int>>{};
  List<int> calls(int entry) => callCache.putIfAbsent(entry, () {
        final direct = callsFrom(img, entry, limitFor(entry));
        final handler = trampToHandler[entry];
        if (handler != null && !direct.contains(handler)) {
          return [...direct, handler];
        }
        return direct;
      });

  final expanded = <int>{};
  final out = StringBuffer();
  var lines = 0;

  void emit(int addr, int depth) {
    final pad = ' ' * (4 * depth);
    final name = syms.name(addr);
    out.writeln('$pad$name');
    lines++;
    final childPad = ' ' * (4 * (depth + 1));

    if (expanded.contains(addr)) {
      out.writeln('$childPad// defined_elsewhere');
      lines++;
      return;
    }
    expanded.add(addr);

    if (depth >= maxDepth) {
      out.writeln('$childPad// depth limit reached');
      lines++;
      return;
    }
    final cs = calls(addr);
    if (cs.isEmpty) {
      out.writeln('$childPad// terminal');
      lines++;
      return;
    }
    for (final c in cs) {
      emit(c, depth + 1);
    }
  }

  if (!args.contains('--no-header')) {
    out.writeln('// Call hierarchy of '
        '${args.first.split(Platform.pathSeparator).last}');
    out.writeln('// Generated by tool/call_tree.dart.');
    out.writeln('//');
    out.writeln('// Indentation by 4 spaces means the symbol is called from '
        'the one above it.');
    out.writeln('// Calls appear in the order the call instructions do, so a '
        'callee reached');
    out.writeln('// twice is listed twice. The first appearance of a symbol '
        'carries its');
    out.writeln('// subtree; later ones say defined_elsewhere.');
    out.writeln('//');
    out.writeln('// A vector trampoline is joined to its handler through the '
        'application');
    out.writeln('// table, which the hardware reaches by an indirect jump '
        'rather than a call.');
    out.writeln('// Interrupt handlers with no such link, and routines '
        'reached only through a');
    out.writeln('// jump table, appear as roots of their own at the end.');
    out.writeln();
  }

  final rootName = opt('--root') ?? 'boot_reset';
  final root = syms.byName[rootName] ?? (img.long(0) & 0xFFFFFF);
  emit(root, 0);

  // Interrupt handlers are entered by the hardware, not called, so nothing
  // under boot_reset reaches them. Without these roots most of the firmware
  // is missing from the tree.
  if (args.contains('--all-roots')) {
    final extra = <int>[];
    syms.byAddr.forEach((a, n) {
      if ((n.startsWith('isr_') || n.startsWith('tramp_')) &&
          !expanded.contains(a)) {
        extra.add(a);
      }
    });
    extra.sort();
    for (final a in extra) {
      // One of the earlier roots may have reached this one meanwhile; a
      // bare "defined_elsewhere" at the top level is only noise.
      if (expanded.contains(a)) continue;
      out.writeln();
      emit(a, 0);
    }
    // Anything still unvisited: reached only through a jump table.
    final orphans = entries
        .where((a) => !expanded.contains(a) && syms.name(a).startsWith('sub_'))
        .toList();
    var wroteOrphanHeading = false;
    for (final a in orphans) {
      if (expanded.contains(a)) continue;
      if (!wroteOrphanHeading) {
        out.writeln();
        out.writeln('// reached only through a jump table, so no caller '
            'appears above them');
        wroteOrphanHeading = true;
      }
      out.writeln();
      emit(a, 0);
    }
  }

  final outFile = opt('--out');
  if (outFile != null) {
    File(outFile).writeAsStringSync(out.toString());
    print('wrote $lines lines to $outFile');
    print('${expanded.length} distinct symbols expanded');
  } else {
    stdout.write(out.toString());
  }
}
