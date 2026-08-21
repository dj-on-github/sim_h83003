// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Turns one function of H8/300H machine code into C-like pseudocode.
//
// This is a reading aid, not a translation. It recovers the control-flow
// graph, renders each instruction as an expression, and works out what each
// conditional branch is really testing by looking back at the instruction
// that set the flags. What it cannot do is infer types, reconstruct stack
// frames, or resolve computed addresses — so anything it is unsure of is
// left in the output as a comment rather than guessed at.
//
// Usage:
//   dart run tool/decompile.dart <image.bin> <address|symbol>
//                                [--sym FILE.sym] [--max N]

import 'dart:io';
import 'dart:typed_data';

import 'package:sim_h83003/h8disasm.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');

/// Registers alias each other: R6L is the low byte of R6, which is the low
/// word of ER6. The output uses the name the instruction used, and the
/// preamble says so, rather than pretending they are separate variables.
String regName(String r) => r.toLowerCase();

class Sym {
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

  String? name(int a) => byAddr[a];
}

/// What the last flag-setting instruction compared, so a branch can be
/// written as a condition rather than as a flag test.
class Flags {
  Flags.compare(this.lhs, this.rhs) : bit = null, value = null;
  Flags.bitTest(this.bit) : lhs = null, rhs = null, value = null;
  Flags.result(this.value) : lhs = null, rhs = null, bit = null;

  final String? lhs;
  final String? rhs;
  final String? bit; // expression that is 1 when the tested bit is set
  final String? value; // expression whose value set N and Z
}

class Decompiler {
  Decompiler(this.image, this.sym);

  final Uint8List image;
  final Sym sym;

  int peek(int a) => (a >= 0 && a < image.length) ? image[a] : 0;
  int word(int a) => (peek(a) << 8) | peek(a + 1);
  int long(int a) => (word(a) << 16) | word(a + 2);

  // ---- operand rendering -------------------------------------------------

  /// A memory reference, named when a symbol covers it.
  String mem(int size, int addr) {
    final s = sym.name(addr);
    final ref = s ?? '0x${hex6(addr)}';
    if (s != null) return s;
    return 'MEM$size($ref)';
  }

  static final _imm = RegExp(r"^#H'([0-9A-F]+)$");
  static final _immDec = RegExp(r'^#(\d+)$');
  static final _absAddr = RegExp(r"^@H'([0-9A-F]+):(8|16|24)$");
  static final _regInd = RegExp(r'^@(ER\d)$');
  static final _postInc = RegExp(r'^@(ER\d)\+$');
  static final _preDec = RegExp(r'^@-(ER\d)$');
  static final _disp = RegExp(r"^@\(H'([0-9A-F]+):(8|16|24),(ER\d)\)$");
  static final _reg = RegExp(r'^(ER\d|E\d|R\d[HL]?)$');

  /// Renders one operand as an expression. [size] is 8, 16 or 32.
  /// Returns null when the form is not one this handles.
  String? operand(String op, int size) {
    var m = _imm.firstMatch(op);
    if (m != null) return '0x${m.group(1)}';
    m = _immDec.firstMatch(op);
    if (m != null) return m.group(1)!;
    m = _absAddr.firstMatch(op);
    if (m != null) {
      var a = int.parse(m.group(1)!, radix: 16);
      // An 8- or 16-bit absolute address is sign-extended into the on-chip
      // register area, which is how the firmware reaches SCR0 and friends.
      if (m.group(2) == '8') a = 0xFFFF00 | (a & 0xFF);
      if (m.group(2) == '16' && a >= 0x8000) a = 0xFF0000 | a;
      return mem(size, a);
    }
    m = _regInd.firstMatch(op);
    if (m != null) return 'MEM$size(${regName(m.group(1)!)})';
    m = _postInc.firstMatch(op);
    if (m != null) return 'MEM$size(${regName(m.group(1)!)}++)';
    m = _preDec.firstMatch(op);
    if (m != null) return 'MEM$size(--${regName(m.group(1)!)})';
    m = _disp.firstMatch(op);
    if (m != null) {
      final base = int.parse(m.group(1)!, radix: 16);
      final reg = regName(m.group(3)!);
      // A 24-bit displacement is a table address indexed by the register; an
      // 8- or 16-bit one is an offset from a pointer, and usually a stack
      // frame. Only the former is worth resolving against the symbol table —
      // naming a frame offset after whatever happens to live at H'0002 is
      // worse than useless.
      if (m.group(2) == '24') {
        final s = sym.name(base);
        return 'MEM$size(${s ?? '0x${hex6(base)}'} + $reg)';
      }
      final d = base.toRadixString(16).toUpperCase();
      return reg == 'er7'
          ? 'STACK$size(0x$d)'
          : 'MEM$size($reg + 0x$d)';
    }
    m = _reg.firstMatch(op);
    if (m != null) return regName(op);
    return null;
  }

  int sizeOf(String mnemonic) {
    if (mnemonic.endsWith('.B')) return 8;
    if (mnemonic.endsWith('.W')) return 16;
    if (mnemonic.endsWith('.L')) return 32;
    return 8;
  }

  /// Splits "MOV.B  #H'01,R6L" into its mnemonic and operand list.
  (String, List<String>) parts(String text) {
    final t = text.trim();
    final sp = t.indexOf(' ');
    if (sp < 0) return (t, const []);
    final mnem = t.substring(0, sp);
    final rest = t.substring(sp + 1).trim();
    // Operands are comma separated, but a displacement form contains a
    // comma inside its parentheses.
    final ops = <String>[];
    var depth = 0, start = 0;
    for (var i = 0; i < rest.length; i++) {
      final c = rest[i];
      if (c == '(') depth++;
      if (c == ')') depth--;
      if (c == ',' && depth == 0) {
        ops.add(rest.substring(start, i).trim());
        start = i + 1;
      }
    }
    ops.add(rest.substring(start).trim());
    return (mnem, ops);
  }

  Flags? flags;

  /// One instruction as a statement, or null if it is a branch (handled by
  /// the block emitter) or could not be translated.
  String? statement(String text) {
    final (mnem, ops) = parts(text);
    final base = mnem.split('.').first;
    final size = sizeOf(mnem);
    String? o(int i) => i < ops.length ? operand(ops[i], size) : null;

    switch (base) {
      case 'NOP':
        return null;
      case 'RTS':
      case 'RTE':
        return 'return;';
      case 'SLEEP':
        return 'sleep();';
      case 'PUSH':
        // Worth showing: this firmware passes arguments on the stack, so
        // suppressing pushes makes the loads before a call look like dead
        // stores.
        final p = o(0);
        return p == null ? null : 'push($p);';
      case 'POP':
        final q = o(0);
        return q == null ? null : '$q = pop();';
      case 'MOV':
        final src = o(0), dst = o(1);
        if (src == null || dst == null) return null;
        flags = Flags.result(dst);
        return '$dst = $src;';
      case 'MOVFPE':
      case 'MOVTPE':
        return null;
      case 'ADD':
      case 'ADDS':
        final a = o(0), b = o(1);
        if (a == null || b == null) return null;
        flags = Flags.result(b);
        return '$b += $a;';
      case 'SUB':
      case 'SUBS':
        final a = o(0), b = o(1);
        if (a == null || b == null) return null;
        flags = Flags.result(b);
        // SUB.B R6L,R6L is the idiom for zeroing a register.
        return a == b ? '$b = 0;' : '$b -= $a;';
      case 'INC':
        final a = ops.length > 1 ? o(1) : o(0);
        if (a == null) return null;
        flags = Flags.result(a);
        return ops.length > 1 ? '$a += ${o(0)};' : '$a++;';
      case 'DEC':
        final a = ops.length > 1 ? o(1) : o(0);
        if (a == null) return null;
        flags = Flags.result(a);
        return ops.length > 1 ? '$a -= ${o(0)};' : '$a--;';
      case 'CMP':
        final a = o(0), b = o(1);
        if (a == null || b == null) return null;
        flags = Flags.compare(b, a); // CMP src,dst compares dst against src
        return null; // the comparison shows up in the branch
      case 'AND':
        final a = o(0), b = o(1);
        if (a == null || b == null) return null;
        flags = Flags.result(b);
        return '$b &= $a;';
      case 'OR':
        final a = o(0), b = o(1);
        if (a == null || b == null) return null;
        flags = Flags.result(b);
        return '$b |= $a;';
      case 'XOR':
        final a = o(0), b = o(1);
        if (a == null || b == null) return null;
        flags = Flags.result(b);
        return '$b ^= $a;';
      case 'NOT':
        final a = o(0);
        if (a == null) return null;
        flags = Flags.result(a);
        return '$a = ~$a;';
      case 'NEG':
        final a = o(0);
        if (a == null) return null;
        flags = Flags.result(a);
        return '$a = -$a;';
      case 'SHLL':
      case 'SHAL':
        final a = o(0);
        if (a == null) return null;
        flags = Flags.result(a);
        return '$a <<= 1;';
      case 'SHLR':
        final a = o(0);
        if (a == null) return null;
        flags = Flags.result(a);
        return '$a >>= 1;';
      case 'SHAR':
        final a = o(0);
        if (a == null) return null;
        flags = Flags.result(a);
        return '$a >>= 1;  /* arithmetic */';
      case 'EXTS':
        final a = o(0);
        return a == null ? null : '$a = sign_extend($a);';
      case 'EXTU':
        final a = o(0);
        return a == null ? null : '$a = zero_extend($a);';
      case 'MULXU':
      case 'MULXS':
        final a = o(0), b = o(1);
        return (a == null || b == null) ? null : '$b *= $a;';
      case 'DIVXU':
      case 'DIVXS':
        final a = o(0), b = o(1);
        return (a == null || b == null)
            ? null
            : '$b = divmod($b, $a);  /* quotient low, remainder high */';
      case 'BSET':
      case 'BCLR':
      case 'BNOT':
        final bit = ops.isEmpty ? null : ops[0].replaceFirst('#', '');
        final target = o(1);
        if (bit == null || target == null) return null;
        final op = base == 'BSET'
            ? '|= (1 << $bit)'
            : (base == 'BCLR' ? '&= ~(1 << $bit)' : '^= (1 << $bit)');
        return '$target $op;';
      case 'BTST':
        final bit = ops.isEmpty ? null : ops[0].replaceFirst('#', '');
        final target = o(1);
        if (bit == null || target == null) return null;
        flags = Flags.bitTest('($target & (1 << $bit))');
        return null;
      case 'JSR':
      case 'BSR':
        final t = callTarget(ops.isEmpty ? '' : ops[0]);
        return t == null ? null : '$t();';
      case 'ANDC':
      case 'ORC':
      case 'XORC':
      case 'LDC':
      case 'STC':
        return null; // condition-code housekeeping
    }
    return null;
  }

  /// The name of a call target written as "@H'20084A:24" or "H'210DC6:16".
  String? callTarget(String op) {
    final m = RegExp(r"H'([0-9A-F]+)").firstMatch(op);
    if (m == null) return null;
    final a = int.parse(m.group(1)!, radix: 16);
    return sym.name(a) ?? 'sub_${hex6(a)}';
  }

  /// The condition a Bcc tests, expressed against the last flag setter.
  String condition(String cc) {
    final f = flags;
    if (f == null) return '/* condition unknown */ $cc';
    if (f.bit != null) {
      switch (cc) {
        case 'BEQ':
          return '!${f.bit}';
        case 'BNE':
          return '${f.bit}';
      }
    }
    if (f.lhs != null) {
      final a = f.lhs!, b = f.rhs!;
      switch (cc) {
        case 'BEQ':
          return '$a == $b';
        case 'BNE':
          return '$a != $b';
        case 'BCS':
        case 'BLO':
          return '$a < $b';
        case 'BCC':
        case 'BHS':
          return '$a >= $b';
        case 'BHI':
          return '$a > $b';
        case 'BLS':
          return '$a <= $b';
        case 'BGE':
          return '(signed)$a >= (signed)$b';
        case 'BLT':
          return '(signed)$a < (signed)$b';
        case 'BGT':
          return '(signed)$a > (signed)$b';
        case 'BLE':
          return '(signed)$a <= (signed)$b';
        case 'BMI':
          return '(signed)($a - $b) < 0';
        case 'BPL':
          return '(signed)($a - $b) >= 0';
      }
    }
    if (f.value != null) {
      final v = f.value!;
      switch (cc) {
        case 'BEQ':
          return '$v == 0';
        case 'BNE':
          return '$v != 0';
        case 'BMI':
          return '(signed)$v < 0';
        case 'BPL':
          return '(signed)$v >= 0';
      }
    }
    return '/* $cc after an untracked flag setter */';
  }
}

/// A conditional branch mnemonic and its target, or null.
(String, int)? branchOf(String text) {
  final m = RegExp(r"^(B[A-Z]{2})\s+H'([0-9A-F]+)").firstMatch(text.trim());
  if (m == null) return null;
  final cc = m.group(1)!;
  if (cc == 'BSR') return null;
  return (cc, int.parse(m.group(2)!, radix: 16));
}

void main(List<String> args) {
  if (args.length < 2) {
    stderr.writeln('usage: dart run tool/decompile.dart <image.bin> '
        '<address|symbol> [--sym FILE.sym] [--max N]');
    exit(2);
  }
  String? opt(String n) {
    final i = args.indexOf(n);
    return (i >= 0 && i + 1 < args.length) ? args[i + 1] : null;
  }

  final image = File(args.first).readAsBytesSync();
  final sym = Sym();
  final symFile = opt('--sym');
  if (symFile != null) sym.load(symFile);

  final target = args[1];
  final entry = sym.byName[target] ?? int.tryParse(target, radix: 16);
  if (entry == null) {
    stderr.writeln('"$target" is neither a symbol in the table nor hex');
    exit(2);
  }
  final maxSpan = int.parse(opt('--max') ?? '2048');

  final d = Decompiler(image, sym);

  // ---- walk the function -------------------------------------------------
  final body = <int>{};
  final targets = <int>{entry};
  final work = <int>[entry];
  while (work.isNotEmpty) {
    var pc = work.removeLast();
    while (true) {
      if (pc < entry || pc >= entry + maxSpan || body.contains(pc)) break;
      body.add(pc);
      final ins = disassembleH8(d.peek, pc);
      final text = ins.text.trim();
      final next = pc + ins.length;
      final br = branchOf(text);
      if (br != null) {
        targets.add(br.$2);
        if (br.$2 >= entry && br.$2 < entry + maxSpan) work.add(br.$2);
        if (br.$1 == 'BRA') break;
        pc = next;
        continue;
      }
      if (text == 'RTS' || text == 'RTE' || text.startsWith('JMP')) break;
      pc = next;
    }
  }

  final ordered = body.toList()..sort();

  // ---- basic blocks ------------------------------------------------------
  // A block starts at the entry, at any branch target, and after anything
  // that ends a straight run. Structuring needs whole blocks to move around,
  // which the flat listing did not.
  final leaders = <int>{entry};
  for (final pc in ordered) {
    final ins = disassembleH8(d.peek, pc);
    final text = ins.text.trim();
    final next = pc + ins.length;
    final br = branchOf(text);
    if (br != null) {
      if (body.contains(br.$2)) leaders.add(br.$2);
      if (body.contains(next)) leaders.add(next);
    } else if (text == 'RTS' || text == 'RTE' || text.startsWith('JMP')) {
      if (body.contains(next)) leaders.add(next);
    }
  }

  final blocks = <Block>[];
  for (final pc in ordered) {
    if (blocks.isEmpty || leaders.contains(pc)) blocks.add(Block(pc));
    blocks.last.pcs.add(pc);
  }
  for (final b in blocks) {
    final last = b.pcs.last;
    final text = disassembleH8(d.peek, last).text.trim();
    final br = branchOf(text);
    if (br != null) {
      if (br.$1 == 'BRA') {
        b.gotoTarget = br.$2;
      } else {
        b.condCc = br.$1;
        b.condTarget = br.$2;
      }
      b.terminator = last;
    } else if (text == 'RTS' || text == 'RTE') {
      b.returns = true;
    }
  }
  final indexOf = <int, int>{
    for (var i = 0; i < blocks.length; i++) blocks[i].start: i,
  };
  for (var i = 0; i < blocks.length; i++) {
    final sw = recoverSwitch(d, blocks, i);
    if (sw != null) {
      blocks[i].dispatch = sw;
      // The switch is the jump, so the JMP itself is not also a statement.
      blocks[i].terminator = blocks[i].pcs.last;
      blocks[i - 1].suppressCond = true; // it is the default case
    }
  }

  // ---- structuring -------------------------------------------------------
  // Lines are collected rather than printed, so labels nothing jumps to can
  // be dropped at the end: structuring removes most of the gotos, and the
  // labels they used would otherwise be left behind.
  final lines = <String>[];
  String labelFor(int a) => 'L_${hex6(a)}';

  void statements(Block b, String indent) {
    for (final pc in b.pcs) {
      final text = disassembleH8(d.peek, pc).text.trim();
      if (pc == b.terminator) continue; // the branch is the structure
      if (b.returns && pc == b.pcs.last) continue; // emitted by the caller
      final st = d.statement(text);
      if (st != null) {
        lines.add('$indent$st');
      } else if (text != 'NOP' &&
          !text.startsWith('CMP') &&
          !text.startsWith('BTST')) {
        lines.add('$indent/* ${hex6(pc)}: $text */');
      }
    }
  }

  /// Emits blocks [from, to). [loopHead] is the block a trailing backward
  /// goto belongs to, and [fallOut] the block a trailing forward goto leaves
  /// for; a goto to either is structure rather than control flow, and is
  /// dropped.
  void region(int from, int to, String indent,
      {int? loopHead, int? fallOut, int? dropCondAt,
       int? skipDoWhileAt}) {
    var i = from;
    while (i < to && i < blocks.length) {
      final b = blocks[i];

      // do-while: a later block branches back here on a condition, so the
      // test is at the bottom and the body always runs once.
      // skipDoWhileAt stops this recursing for ever: the body is
      // emitted by calling region() again from this very block, and
      // without the guard that call rediscovers the same loop.
      if (i != dropCondAt && i != skipDoWhileAt &&
          b.dispatch == null) {
        int? tail;
        for (var j = i; j < to; j++) {
          final cj = blocks[j].condCc;
          if (cj != null &&
              blocks[j].condTarget != null &&
              indexOf[blocks[j].condTarget!] == i) {
            tail = j;
          }
        }
        if (tail != null) {
          if (i != from) lines.add('  ${labelFor(b.start)}:');
          lines.add('${indent}do {');
          region(i, tail + 1, '$indent    ',
              dropCondAt: tail, skipDoWhileAt: i);
          lines.add('$indent} '
              'while (${d.condition(blocks[tail].condCc!)});');
          i = tail + 1;
          continue;
        }
      }

      // Labels sit outdented, so they read as landmarks rather than as
      // statements at whatever depth the structuring happens to be at.
      if (i != from) lines.add('  ${labelFor(b.start)}:');
      statements(b, indent);

      if (b.returns) {
        lines.add('${indent}return;');
        i++;
        continue;
      }

      // A recovered jump table reads as a switch rather than an indirect
      // jump into nowhere.
      final sw = b.dispatch;
      if (sw != null) {
        lines.add('${indent}switch (${sw.index}) {');
        // Cases that share a target are grouped, which is what makes the
        // AN0/AN4 style of table readable.
        final byTarget = <int, List<int>>{};
        for (var k = 0; k < sw.targets.length; k++) {
          byTarget.putIfAbsent(sw.targets[k], () => []).add(k);
        }
        final order = byTarget.keys.toList()
          ..sort((x, y) => byTarget[x]!.first.compareTo(byTarget[y]!.first));
        for (final t in order) {
          for (final v in byTarget[t]!) {
            lines.add('$indent'
                'case 0x${v.toRadixString(16).toUpperCase()}:');
          }
          lines.add('$indent    goto ${sym.name(t) ?? labelFor(t)};');
        }
        final def = sw.defaultTarget;
        if (def != null) {
          lines.add('${indent}default:');
          final di = indexOf[def];
          final defName = di != null
              ? labelFor(def)
              : (sym.name(def) ?? "H'${hex6(def)}");
          lines.add('$indent    goto $defName;');
        }
        lines.add('$indent}');
        i++;
        continue;
      }

      final cc = b.condCc;
      if (cc != null && b.suppressCond) {
        i++; // the comparison and branch belong to the switch below
        continue;
      }
      if (cc != null && i == dropCondAt) {
        i++; // the caller is emitting this as a do-while condition
        continue;
      }
      if (cc != null) {
        final t = indexOf[b.condTarget!];
        if (t != null && t > i && t <= to) {
          final thenFrom = i + 1, thenTo = t;
          final last = thenTo > thenFrom ? blocks[thenTo - 1] : null;
          final lastGoto =
              last?.gotoTarget == null ? null : indexOf[last!.gotoTarget!];

          // while: the body jumps back to the test.
          if (lastGoto == i) {
            lines.add('$indent' 'while (${d.condition(invertCc(cc))}) {');
            region(thenFrom, thenTo, '$indent    ', loopHead: i);
            lines.add('$indent}');
            i = t;
            continue;
          }
          // if/else: the then-part jumps forward over the else-part.
          if (lastGoto != null && lastGoto > t && lastGoto <= to) {
            lines.add('$indent' 'if (${d.condition(invertCc(cc))}) {');
            region(thenFrom, thenTo, '$indent    ', fallOut: lastGoto);
            lines.add('$indent} else {');
            region(t, lastGoto, '$indent    ', fallOut: lastGoto);
            lines.add('$indent}');
            i = lastGoto;
            continue;
          }
          // plain if.
          lines.add('$indent' 'if (${d.condition(invertCc(cc))}) {');
          region(thenFrom, thenTo, '$indent    ', fallOut: t);
          lines.add('$indent}');
          i = t;
          continue;
        }
        // A backward or out-of-region branch stays a goto.
        final label = t != null
            ? labelFor(b.condTarget!)
            : (sym.name(b.condTarget!) ?? "H'${hex6(b.condTarget!)}");
        lines.add('${indent}if (${d.condition(cc)}) goto $label;');
        i++;
        continue;
      }

      final g = b.gotoTarget;
      if (g != null) {
        final gi = indexOf[g];
        // A jump to the very next block, or out of a structure the caller is
        // closing, is not control flow worth writing down.
        final isStructure =
            gi != null && (gi == loopHead || gi == fallOut || gi == i + 1);
        if (!isStructure) {
          final label =
              gi != null ? labelFor(g) : (sym.name(g) ?? "H'${hex6(g)}");
          lines.add('${indent}goto $label;');
        }
        i++;
        continue;
      }
      i++;
    }
  }

  region(0, blocks.length, '    ');

  // ---- output ------------------------------------------------------------
  final name = sym.name(entry) ?? 'sub_${hex6(entry)}';
  print('/* ${args.first.split(Platform.pathSeparator).last} : '
      "H'${hex6(entry)}");
  print(' *');
  print(' * Pseudocode reconstructed from ${ordered.length} instructions in '
      '${blocks.length} basic blocks.');
  print(' * Registers keep their machine names and alias each other the way');
  print(' * the hardware does: r6l is the low byte of r6, which is the low');
  print(' * half of er6. Nothing here is compilable C, and no types have');
  print(' * been inferred.');
  print(' *');
  print(' *   MEM8/16/32(a)    a byte, word or longword at address a');
  print(' *   STACK8/16/32(d)  a local, at displacement d from the stack');
  print(' *   named symbols    come from the loaded symbol table');
  print(' */');
  print('void $name(void)');
  print('{');
  // Drop labels nothing jumps to any more.
  final referenced = <String>{};
  final gotoRe = RegExp(r'goto (L_[0-9A-F]+);');
  for (final l in lines) {
    final m = gotoRe.firstMatch(l);
    if (m != null) referenced.add(m.group(1)!);
  }
  final labelRe = RegExp(r'^\s*(L_[0-9A-F]+):$');
  for (final l in lines) {
    final m = labelRe.firstMatch(l);
    if (m != null && !referenced.contains(m.group(1)!)) continue;
    print(l);
  }
  print('}');
}

/// One straight run of instructions and how it ends.
class Block {
  Block(this.start);
  final int start;
  final List<int> pcs = [];

  /// Address of the branch that ends the block, if it ends in one.
  int? terminator;
  String? condCc;
  int? condTarget;
  int? gotoTarget;
  bool returns = false;

  /// Set on the block holding a jump table's bounds check: its comparison
  /// and branch become the switch's default case, so it must not also be
  /// emitted as an if of its own.
  bool suppressCond = false;

  /// Recovered dispatch, when the block ends in JMP through a table.
  SwitchInfo? dispatch;
}

/// A jump table recovered from the bounds check, the indexed load and the
/// indirect jump that always appear together.
class SwitchInfo {
  SwitchInfo(this.index, this.table, this.targets, this.defaultTarget);

  /// The expression being switched on.
  final String index;
  final int table;

  /// Case value to target address, in index order.
  final List<int> targets;
  final int? defaultTarget;
}

/// Recovers the dispatch a block performs, if it ends in JMP through a
/// table. The shape is always the same: a bounds check in the preceding
/// block, then the index scaled and used to load a pointer, then the jump.
/// Without the bounds check there is no way to know how many entries the
/// table has, so nothing is recovered rather than guessed.
SwitchInfo? recoverSwitch(Decompiler d, List<Block> blocks, int i) {
  final b = blocks[i];
  final lastText = disassembleH8(d.peek, b.pcs.last).text.trim();
  if (!RegExp(r'^JMP\s+@ER\d$').hasMatch(lastText)) return null;

  int? table;
  final loadRe = RegExp(r"^MOV\.L\s+@\(H'([0-9A-F]+):24,ER\d\),ER\d$");
  for (final pc in b.pcs) {
    final m = loadRe.firstMatch(disassembleH8(d.peek, pc).text.trim());
    if (m != null) table = int.parse(m.group(1)!, radix: 16);
  }
  if (table == null || i == 0) return null;

  final prev = blocks[i - 1];
  if (prev.condCc == null) return null;
  final cmpRe = RegExp(r"^CMP\.B\s+#H'([0-9A-F]+),(R\d[HL])$");
  int? highest;
  String? indexReg;
  for (final pc in prev.pcs) {
    final m = cmpRe.firstMatch(disassembleH8(d.peek, pc).text.trim());
    if (m != null) {
      highest = int.parse(m.group(1)!, radix: 16);
      indexReg = m.group(2)!.toLowerCase();
    }
  }
  if (highest == null || indexReg == null) return null;
  // The check branches away when the index is above the last valid one, so
  // the table has one more entry than the value compared against.
  final count = highest + 1;
  if (count < 2 || count > 256) return null;

  final targets = <int>[];
  for (var k = 0; k < count; k++) {
    final t = d.long(table + k * 4) & 0xFFFFFF;
    if (t == 0) return null;
    targets.add(t);
  }
  return SwitchInfo(indexReg, table, targets, prev.condTarget);
}

/// The opposite branch condition. The H8's Bcc encodings are laid out in
/// true/false pairs, so the mnemonics pair up the same way — which is what
/// lets "branch away if X" become "if (not X) { fall through }".
String invertCc(String cc) {
  const pairs = {
    'BEQ': 'BNE', 'BNE': 'BEQ',
    'BCS': 'BCC', 'BCC': 'BCS',
    'BLO': 'BHS', 'BHS': 'BLO',
    'BHI': 'BLS', 'BLS': 'BHI',
    'BGE': 'BLT', 'BLT': 'BGE',
    'BGT': 'BLE', 'BLE': 'BGT',
    'BMI': 'BPL', 'BPL': 'BMI',
    'BVS': 'BVC', 'BVC': 'BVS',
  };
  return pairs[cc] ?? cc;
}
