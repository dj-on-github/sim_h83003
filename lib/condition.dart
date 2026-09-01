/// Conditions: "stop when this is true".
///
/// A third of tool/ is the same program written again with a different test
/// compiled into it -- run until something, then report. This is that test,
/// written as text instead, so the next one does not need a new file.
///
///   [11B10E].w == 77          the panel is asking for clr
///   pc >= 800 && pc < 2400    execution has entered the boot ROM's code
///   [FFFFC7] & 4              the sewing light is on
///   er0 != 0 && [200004].l == 3F0000
///
/// Numbers are hex, because that is how every address in this machine is
/// written everywhere else. A leading # makes one decimal: `cycles > #1000000`.
///
/// Nothing here knows about the CPU. It reads through [MachineView], which
/// is what lets the whole language be tested without one.
library;

/// What a condition can see.
abstract class MachineView {
  /// One byte of the address space, as the CPU would read it.
  int readByte(int addr);

  /// A named register, or null if there is no such name. Recognised names
  /// are pc, ccr, cycles, er0-er7, r0-r7, e0-e7, and the flag letters
  /// i, ui, h, u, n, z, v, c.
  int? register(String name);
}

/// A condition that could not be read, with where it went wrong.
class ConditionError extends FormatException {
  const ConditionError(super.message, [super.source, super.offset]);
}

/// A parsed condition, ready to be asked.
class Condition {
  const Condition._(this._root, this.source);

  final _Node _root;

  /// What the user typed, kept for showing back to them.
  final String source;

  /// Reads [text]. Throws [ConditionError] if it cannot.
  factory Condition.parse(String text) {
    final tokens = _lex(text);
    final p = _Parser(tokens, text);
    final node = p.parseExpression();
    p.expectEnd();
    return Condition._(node, text.trim());
  }

  /// True when the condition holds. A condition that is a bare value is true
  /// when that value is non-zero, so `[FFFFC7] & 4` reads as it looks.
  bool test(MachineView m) => _root.eval(m) != 0;

  /// The value the condition works out to, for showing alongside the answer.
  int value(MachineView m) => _root.eval(m);

  @override
  String toString() => source;
}

// ---- the tokens ----------------------------------------------------------

enum _Kind { number, name, punct, end }

class _Token {
  const _Token(this.kind, this.text, this.offset, [this.value = 0]);
  final _Kind kind;
  final String text;
  final int offset;
  final int value;
}

const _punctuation = [
  '&&', '||', '==', '!=', '<=', '>=', '<<', '>>',
  '<', '>', '+', '-', '&', '|', '^', '(', ')', '[', ']', '!', '.',
];

List<_Token> _lex(String s) {
  final out = <_Token>[];
  var i = 0;
  while (i < s.length) {
    final c = s[i];
    if (c.trim().isEmpty) {
      i++;
      continue;
    }

    // #123 is decimal; everything else numeric is hex.
    if (c == '#') {
      final start = i++;
      final digits = StringBuffer();
      while (i < s.length && _isDigit(s[i])) {
        digits.write(s[i++]);
      }
      if (digits.isEmpty) {
        throw ConditionError('a # needs decimal digits after it', s, start);
      }
      out.add(_Token(_Kind.number, '#$digits', start, int.parse('$digits')));
      continue;
    }

    // One run of letters, digits and underscores, then decide what it was.
    // Splitting on "is this a hex digit" would cut "cycles" after its c.
    if (_isNameChar(c)) {
      final start = i;
      final word = StringBuffer();
      while (i < s.length && _isNameChar(s[i])) {
        word.write(s[i++]);
      }
      final text = word.toString();
      final lower = text.toLowerCase();

      if (_registerNames.contains(lower)) {
        out.add(_Token(_Kind.name, lower, start));
        continue;
      }

      var digits = text;
      if (lower.startsWith('0x')) digits = text.substring(2);
      final v = digits.isEmpty ? null : int.tryParse(digits, radix: 16);
      if (v != null) {
        out.add(_Token(_Kind.number, text, start, v));
        continue;
      }

      // Not a number and not a register: a name, so the error names it.
      out.add(_Token(_Kind.name, lower, start));
      continue;
    }

    final two = i + 1 < s.length ? s.substring(i, i + 2) : '';
    if (_punctuation.contains(two)) {
      out.add(_Token(_Kind.punct, two, i));
      i += 2;
      continue;
    }
    if (_punctuation.contains(c)) {
      out.add(_Token(_Kind.punct, c, i));
      i++;
      continue;
    }
    throw ConditionError('"$c" does not belong here', s, i);
  }
  out.add(_Token(_Kind.end, '', s.length));
  return out;
}

bool _isDigit(String c) => c.codeUnitAt(0) >= 0x30 && c.codeUnitAt(0) <= 0x39;

bool _isNameStart(String c) {
  final u = c.toLowerCase();
  return (u.compareTo('a') >= 0 && u.compareTo('z') <= 0) || c == '_';
}

bool _isNameChar(String c) => _isNameStart(c) || _isDigit(c);

const _registerNames = {
  'pc', 'ccr', 'cycles',
  'er0', 'er1', 'er2', 'er3', 'er4', 'er5', 'er6', 'er7',
  'r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7',
  'e0', 'e1', 'e2', 'e3', 'e4', 'e5', 'e6', 'e7',
  'sp', 'i', 'ui', 'h', 'u', 'n', 'z', 'v', 'c',
};

// ---- the tree ------------------------------------------------------------

abstract class _Node {
  int eval(MachineView m);
}

class _Literal implements _Node {
  const _Literal(this.v);
  final int v;
  @override
  int eval(MachineView m) => v;
}

class _Register implements _Node {
  _Register(this.name, this.offset, this.source);
  final String name;
  final int offset;
  final String source;
  @override
  int eval(MachineView m) {
    final v = m.register(name);
    if (v == null) {
      throw ConditionError('there is no register called "$name"', source,
          offset);
    }
    return v;
  }
}

class _Memory implements _Node {
  const _Memory(this.addr, this.width);
  final _Node addr;

  /// 1, 2 or 4 bytes. Big-endian, as the H8 is.
  final int width;

  @override
  int eval(MachineView m) {
    final a = addr.eval(m) & 0xFFFFFF;
    var v = 0;
    for (var i = 0; i < width; i++) {
      v = (v << 8) | (m.readByte(a + i) & 0xFF);
    }
    return v;
  }
}

class _Unary implements _Node {
  const _Unary(this.op, this.a);
  final String op;
  final _Node a;
  @override
  int eval(MachineView m) {
    final v = a.eval(m);
    return switch (op) {
      '!' => v == 0 ? 1 : 0,
      '-' => -v,
      _ => v,
    };
  }
}

class _Binary implements _Node {
  const _Binary(this.op, this.a, this.b);
  final String op;
  final _Node a, b;

  @override
  int eval(MachineView m) {
    // && and || short-circuit, so a condition can guard its own reads.
    if (op == '&&') return a.eval(m) != 0 && b.eval(m) != 0 ? 1 : 0;
    if (op == '||') return a.eval(m) != 0 || b.eval(m) != 0 ? 1 : 0;
    final x = a.eval(m), y = b.eval(m);
    return switch (op) {
      '==' => x == y ? 1 : 0,
      '!=' => x != y ? 1 : 0,
      '<' => x < y ? 1 : 0,
      '<=' => x <= y ? 1 : 0,
      '>' => x > y ? 1 : 0,
      '>=' => x >= y ? 1 : 0,
      '+' => x + y,
      '-' => x - y,
      '&' => x & y,
      '|' => x | y,
      '^' => x ^ y,
      '<<' => x << y,
      '>>' => x >> y,
      _ => 0,
    };
  }
}

// ---- the parser ----------------------------------------------------------

class _Parser {
  _Parser(this.t, this.source);
  final List<_Token> t;
  final String source;
  int i = 0;

  _Token get here => t[i];
  bool at(String p) => here.kind == _Kind.punct && here.text == p;

  void expectEnd() {
    if (here.kind != _Kind.end) {
      throw ConditionError('"${here.text}" is left over at the end', source,
          here.offset);
    }
  }

  _Node parseExpression() => _parseOr();

  _Node _parseOr() {
    var left = _parseAnd();
    while (at('||')) {
      i++;
      left = _Binary('||', left, _parseAnd());
    }
    return left;
  }

  _Node _parseAnd() {
    var left = _parseComparison();
    while (at('&&')) {
      i++;
      left = _Binary('&&', left, _parseComparison());
    }
    return left;
  }

  _Node _parseComparison() {
    final left = _parseSum();
    for (final op in ['==', '!=', '<=', '>=', '<', '>']) {
      if (at(op)) {
        i++;
        return _Binary(op, left, _parseSum());
      }
    }
    return left;
  }

  _Node _parseSum() {
    var left = _parseTerm();
    while (true) {
      final op = ['+', '-', '&', '|', '^', '<<', '>>']
          .where(at)
          .firstOrNull;
      if (op == null) return left;
      i++;
      left = _Binary(op, left, _parseTerm());
    }
  }

  _Node _parseTerm() {
    if (at('!') || at('-')) {
      final op = here.text;
      i++;
      return _Unary(op, _parseTerm());
    }
    if (at('(')) {
      i++;
      final inner = parseExpression();
      if (!at(')')) {
        throw ConditionError('a ( is not closed', source, here.offset);
      }
      i++;
      return inner;
    }
    if (at('[')) {
      final open = here.offset;
      i++;
      final addr = parseExpression();
      if (!at(']')) {
        throw ConditionError('a [ is not closed', source, open);
      }
      i++;
      var width = 1;
      if (at('.')) {
        i++;
        if (here.kind != _Kind.name) {
          throw ConditionError('a width has to be .b, .w or .l', source,
              here.offset);
        }
        width = switch (here.text) {
          'b' => 1,
          'w' => 2,
          'l' => 4,
          _ => throw ConditionError(
              '"${here.text}" is not a width; use .b, .w or .l', source,
              here.offset),
        };
        i++;
      }
      return _Memory(addr, width);
    }
    if (here.kind == _Kind.number) {
      final v = here.value;
      i++;
      return _Literal(v);
    }
    if (here.kind == _Kind.name) {
      final name = here.text;
      final off = here.offset;
      i++;
      return _Register(name, off, source);
    }
    throw ConditionError(
        here.kind == _Kind.end
            ? 'the condition stops short'
            : '"${here.text}" does not belong here',
        source,
        here.offset);
  }
}
