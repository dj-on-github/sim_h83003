// The 16-bit absolute addressing mode.
//
// The H8/300H sign-extends @aa:16, so a field of H'FD1C reaches H'FFFD1C —
// which is how a compiler saves two bytes over the 24-bit form when the
// target is in the on-chip register area. The listing must show the address
// the instruction actually reaches, and the CPU must read it, or generated
// code looks wrong when it is right.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/h8disasm.dart';

/// Disassembles bytes placed at address 0.
String textOf(List<int> bytes) {
  int peek(int a) => (a >= 0 && a < bytes.length) ? bytes[a] : 0;
  return disassembleH8(peek, 0).text;
}

void main() {
  group('how it is shown', () {
    test('a field above H\'8000 is shown as the address it reaches', () {
      // MOV.B @H'FD1C:16,R2L
      expect(textOf([0x6A, 0x0A, 0xFD, 0x1C]), "MOV.B @H'FFFD1C:16,R2L");
    });

    test('a field below H\'8000 is left where it is', () {
      // MOV.B @H'1234:16,R2L — no sign extension, so no rewriting.
      expect(textOf([0x6A, 0x0A, 0x12, 0x34]), "MOV.B @H'001234:16,R2L");
    });

    test('the boundary is H\'8000 exactly', () {
      expect(textOf([0x6A, 0x0A, 0x7F, 0xFF]), contains("H'007FFF"));
      expect(textOf([0x6A, 0x0A, 0x80, 0x00]), contains("H'FF8000"));
    });

    test('stores read the same way as loads', () {
      // MOV.B R2L,@H'FD1C:16
      expect(textOf([0x6A, 0x8A, 0xFD, 0x1C]), "MOV.B R2L,@H'FFFD1C:16");
    });

    test('it agrees with the 8-bit absolute form, which always did this', () {
      // MOV.B @H'FFFFBD:8,R0L — the :8 form has always shown its full address.
      expect(textOf([0x28, 0xBD]), "MOV.B @H'FFFFBD:8,R0L");
    });
  });

  group('what the CPU does', () {
    test('a load reads the sign-extended address, not the raw field', () {
      final cpu = H8Cpu();
      for (final e in [0x6A, 0x0A, 0xFD, 0x1C].asMap().entries) {
        cpu.mem.poke(0x1000 + e.key, e.value);
      }
      cpu.mem.poke(2, 0x10);
      cpu.mem.poke(3, 0x00);
      cpu.reset();
      cpu.mem.poke(0xFFFD1C, 0xA5); // where it should look
      cpu.mem.poke(0x00FD1C, 0x3C); // where it would look if unextended
      cpu.step();
      expect(cpu.er[2] & 0xFF, 0xA5);
    });

    test('a store writes the sign-extended address', () {
      final cpu = H8Cpu();
      // MOV.B R2L,@H'FD1C:16
      for (final e in [0x6A, 0x8A, 0xFD, 0x1C].asMap().entries) {
        cpu.mem.poke(0x1000 + e.key, e.value);
      }
      cpu.mem.poke(2, 0x10);
      cpu.mem.poke(3, 0x00);
      cpu.reset();
      cpu.er[2] = 0x5A;
      cpu.step();
      expect(cpu.mem.peek(0xFFFD1C), 0x5A);
      expect(cpu.mem.peek(0x00FD1C), 0x00);
    });

    test('an address below the boundary is not extended', () {
      final cpu = H8Cpu();
      for (final e in [0x6A, 0x0A, 0x12, 0x34].asMap().entries) {
        cpu.mem.poke(0x1000 + e.key, e.value);
      }
      cpu.mem.poke(2, 0x10);
      cpu.mem.poke(3, 0x00);
      cpu.reset();
      cpu.mem.poke(0x001234, 0x77);
      cpu.step();
      expect(cpu.er[2] & 0xFF, 0x77);
    });
  });
}
