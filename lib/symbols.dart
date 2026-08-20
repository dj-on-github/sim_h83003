// Reading a symbol table, in any of the forms this project accepts.
//
// Lives apart from the UI because the command-line tools need it too, and
// they cannot import anything that pulls in Flutter.

import 'dart:convert';

import 'sparse_memory.dart';

/// One `name = H'hhhhhh` assignment, as written in a `.sym` file or in the
/// symbol listing an assembler emits.
final RegExp _symbolLine =
    RegExp(r"^\s*([A-Za-z_.$][\w.$]*)\s*=\s*(?:H'|\$|0x|0X)?([0-9A-Fa-f]+)");

/// Coerces a symbol-table value to a 24-bit address. Accepts an int, or a
/// string in H', $, 0x or bare-hex form, or decimal.
int? symbolAddress(dynamic v) {
  if (v is int) return v & SparseMemory.addrMask;
  if (v is String) {
    var s = v.trim();
    if (s.startsWith("H'")) s = s.substring(2);
    if (s.startsWith('\$')) s = s.substring(1);
    if (s.startsWith('0x') || s.startsWith('0X')) s = s.substring(2);
    final hex = int.tryParse(s, radix: 16);
    if (hex != null) return hex & SparseMemory.addrMask;
    final dec = int.tryParse(s);
    if (dec != null) return dec & SparseMemory.addrMask;
  }
  return null;
}

/// Reads a symbol table in any of the forms the simulator accepts: a JSON
/// `{ "label": address }` object, a JSON list of listing lines, or a
/// plain-text `.sym` file of `NAME = H'ADDRESS` lines with `;`, `#` or `//`
/// comments. Returns address-to-label, empty if nothing parsed.
Map<int, String> parseSymbolTable(String text) {
  final map = <int, String>{};
  void addLine(String line) {
    final m = _symbolLine.firstMatch(line);
    if (m != null) {
      map[int.parse(m.group(2)!, radix: 16) & SparseMemory.addrMask] =
          m.group(1)!;
    }
  }

  try {
    final decoded = json.decode(text);
    if (decoded is Map) {
      decoded.forEach((k, v) {
        final addr = symbolAddress(v);
        if (addr != null) map[addr] = k.toString();
      });
    } else if (decoded is List) {
      for (final item in decoded) {
        addLine(item.toString());
      }
    }
    return map;
  } on FormatException {
    // Not JSON: a plain-text symbol file, one assignment per line.
  } catch (_) {
    return {};
  }
  map.clear();
  for (var line in text.split('\n')) {
    for (final marker in const [';', '#', '//']) {
      final i = line.indexOf(marker);
      if (i >= 0) line = line.substring(0, i);
    }
    if (line.trim().isEmpty) continue;
    addLine(line);
  }
  return map;
}
