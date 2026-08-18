// The symbol-table loader accepts three shapes: the JSON object the analyser
// writes with --symbols, a JSON list of assembler listing lines, and the
// plain-text .sym file written with --sym.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/main.dart';

void main() {
  test('JSON object of label to address', () {
    final map = parseSymbolTable('{"reset": "0x000400", "scr0": 16777138, '
        '"brr0": "H\'FFFFB1"}');
    expect(map[0x000400], 'reset');
    expect(map[0xFFFFB2], 'scr0');
    expect(map[0xFFFFB1], 'brr0');
  });

  test('JSON list of listing lines', () {
    final map = parseSymbolTable('["main = H\'200200", "loop = \$20071E"]');
    expect(map[0x200200], 'main');
    expect(map[0x20071E], 'loop');
  });

  group('plain text', () {
    test('assignments, comments and blank lines', () {
      final map = parseSymbolTable('''
; a comment line
   ; indented too

SCR0        = H'FFFFB2
touch_x_raw = H'FFFEF0   ; from AN4
# hash comment
sub_200200  = 0x200200
// slash comment
not a symbol line at all
''');
      expect(map.length, 3);
      expect(map[0xFFFFB2], 'SCR0');
      expect(map[0xFFFEF0], 'touch_x_raw');
      expect(map[0x200200], 'sub_200200');
    });

    test('a trailing comment cannot smuggle in a second symbol', () {
      final map = parseSymbolTable("a = H'1000 ; b = H'2000");
      expect(map, {0x1000: 'a'});
    });

    test('nothing parsable yields an empty table', () {
      expect(parseSymbolTable('just some prose\nand more of it'), isEmpty);
      expect(parseSymbolTable(''), isEmpty);
    });
  });

  test('the generated artista 180 symbol file loads', () {
    final f = File('bernina_artista180/Bernina180_20260816.sym');
    if (!f.existsSync()) {
      markTestSkipped('dump-derived symbol file not present');
      return;
    }
    final map = parseSymbolTable(f.readAsStringSync());
    expect(map.length, greaterThan(1000));
    // On-chip registers, a vector slot, the reset handler, and locations
    // found by analysing this image.
    expect(map[0xFFFFB2], 'SCR0');
    expect(map[0xFFFFE8], 'ADCSR');
    expect(map[0x000000], 'VEC_RESET');
    expect(map[0x000400], 'boot_reset');
    expect(map[0x040000], 'lcd_frame_buffer');
    expect(map[0xFFFEF0], 'an4_sample');
    expect(map[0xFFFEF1], 'an6_sample');
    // Every name is unique: two symbols must not collapse onto one label.
    expect(map.values.toSet().length, map.length);
  });
}
