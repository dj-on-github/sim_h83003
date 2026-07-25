// Unit tests for the H8/300H CPU core, the disassembler, and the hex
// file round-trip. Instruction encodings are hand-assembled from the
// H8/3003 hardware manual (appendix A).

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

/// Loads [bytes] at [addr], points the PC there, and returns the CPU.
H8Cpu cpuWith(List<int> bytes, {int addr = 0x1000}) {
  final cpu = H8Cpu();
  for (var i = 0; i < bytes.length; i++) {
    cpu.mem.poke(addr + i, bytes[i]);
  }
  cpu.pc = addr;
  cpu.er[7] = 0x00FFFF00; // usable stack
  return cpu;
}

void main() {
  group('reset and vectors', () {
    test('reset loads PC from the reset vector and sets I', () {
      final cpu = H8Cpu();
      cpu.mem.poke(0, 0x00);
      cpu.mem.poke(1, 0x01);
      cpu.mem.poke(2, 0x23);
      cpu.mem.poke(3, 0x44);
      cpu.ccr = 0;
      cpu.reset();
      expect(cpu.pc, 0x012344);
      expect(cpu.getFlag(H8Flag.i), isTrue);
      expect(cpu.halted, isFalse);
    });
  });

  group('MOV', () {
    test('MOV.B #xx:8, Rd sets N/Z and clears V', () {
      final cpu = cpuWith([0xF8, 0x80]); // MOV.B #H'80,R0L
      cpu.ccr |= H8Flag.v;
      cpu.step();
      expect(cpu.rd8(8), 0x80);
      expect(cpu.getFlag(H8Flag.n), isTrue);
      expect(cpu.getFlag(H8Flag.z), isFalse);
      expect(cpu.getFlag(H8Flag.v), isFalse);
      expect(cpu.cycles, 2);
    });

    test('MOV.B Rs, Rd between high and low bytes', () {
      final cpu = cpuWith([0x0C, 0x08]); // MOV.B R0H,R0L
      cpu.er[0] = 0x0000AB00; // R0H = AB
      cpu.step();
      expect(cpu.er[0] & 0xFF, 0xAB);
    });

    test('MOV.W #xx:16 and MOV.L #xx:32', () {
      final cpu = cpuWith([
        0x79, 0x02, 0x12, 0x34, // MOV.W #H'1234,R2
        0x7A, 0x03, 0xDE, 0xAD, 0xBE, 0xEF, // MOV.L #H'DEADBEEF,ER3
      ]);
      cpu.step();
      expect(cpu.rd16(2), 0x1234);
      cpu.step();
      expect(cpu.er[3], 0xDEADBEEF);
      expect(cpu.getFlag(H8Flag.n), isTrue);
      expect(cpu.cycles, 4 + 6);
    });

    test('MOV.B @ERs, Rd and MOV.B Rs, @ERd', () {
      final cpu = cpuWith([
        0x68, 0x21, // MOV.B @ER2,R1H
        0x68, 0xB9, // MOV.B R1L,@ER3
      ]);
      cpu.er[2] = 0x002000;
      cpu.er[3] = 0x003000;
      cpu.er[1] = 0x000077; // R1L = 0x77
      cpu.mem.poke(0x002000, 0x5A);
      cpu.step();
      expect(cpu.rd8(1), 0x5A); // R1H
      cpu.step();
      expect(cpu.mem.peek(0x003000), 0x77);
      expect(cpu.cycles, 8);
    });

    test('MOV.B @aa:8 targets the H\'FFFFxx area', () {
      final cpu = cpuWith([
        0x3A, 0x80, // MOV.B R2L,@H'FFFF80:8
        0x24, 0x80, // MOV.B @H'FFFF80:8,R4H
      ]);
      cpu.wr8(10, 0x42); // R2L
      cpu.step();
      expect(cpu.mem.peek(0xFFFF80), 0x42);
      cpu.step();
      expect(cpu.rd8(4), 0x42);
    });

    test('MOV.W @aa:16 sign-extends into the top of the space', () {
      // @aa:16 with a negative value maps to H'FFxxxx.
      final cpu = cpuWith([0x6B, 0x81, 0xFD, 0x10]); // MOV.W R1,@H'FFFD10:16
      cpu.wr16(1, 0xBEEF);
      cpu.step();
      expect(cpu.mem.peek(0xFFFD10), 0xBE);
      expect(cpu.mem.peek(0xFFFD11), 0xEF);
    });

    test('MOV.B @aa:24 (6-byte form)', () {
      final cpu = cpuWith([0x6A, 0x28, 0x00, 0x12, 0x34, 0x56]);
      cpu.mem.poke(0x123456, 0x99);
      cpu.step(); // MOV.B @H'123456:24,R0L
      expect(cpu.rd8(8), 0x99);
      expect(cpu.cycles, 8);
    });

    test('MOV.B @ERs+ and @-ERd update the address register', () {
      final cpu = cpuWith([
        0x6C, 0x50, // MOV.B @ER5+,R0H
        0x6C, 0xE8, // MOV.B R0L,@-ER6
      ]);
      cpu.er[5] = 0x004000;
      cpu.er[6] = 0x005000;
      cpu.mem.poke(0x004000, 0x11);
      cpu.er[0] = 0x22; // R0L
      cpu.step();
      expect(cpu.rd8(0), 0x11);
      expect(cpu.er[5], 0x004001);
      cpu.step();
      expect(cpu.er[6], 0x004FFF);
      expect(cpu.mem.peek(0x004FFF), 0x22);
    });

    test('MOV.W @(d:16, ERs) with a negative displacement', () {
      final cpu = cpuWith([0x6F, 0x30, 0xFF, 0xFE]); // MOV.W @(-2,ER3),R0
      cpu.er[3] = 0x002002;
      cpu.mem.poke(0x002000, 0xCA);
      cpu.mem.poke(0x002001, 0xFE);
      cpu.step();
      expect(cpu.rd16(0), 0xCAFE);
      expect(cpu.cycles, 6);
    });

    test('MOV.B @(d:24, ERs) 8-byte form', () {
      final cpu =
          cpuWith([0x78, 0x10, 0x6A, 0x28, 0x00, 0x00, 0x00, 0x10]);
      cpu.er[1] = 0x008000;
      cpu.mem.poke(0x008010, 0x3C);
      cpu.step(); // MOV.B @(H'000010:24,ER1),R0L
      expect(cpu.rd8(8), 0x3C);
      expect(cpu.cycles, 10);
    });

    test('MOV.L @ERs, ERd / MOV.L ERs, @ERd (01 00 69)', () {
      final cpu = cpuWith([
        0x01, 0x00, 0x69, 0x21, // MOV.L @ER2,ER1
        0x01, 0x00, 0x69, 0xB1, // MOV.L ER1,@ER3
      ]);
      cpu.er[2] = 0x006000;
      cpu.er[3] = 0x007000;
      cpu.mem.poke(0x006000, 0x01);
      cpu.mem.poke(0x006001, 0x02);
      cpu.mem.poke(0x006002, 0x03);
      cpu.mem.poke(0x006003, 0x04);
      cpu.step();
      expect(cpu.er[1], 0x01020304);
      cpu.step();
      expect(cpu.mem.peek(0x007003), 0x04);
      expect(cpu.cycles, 16);
    });

    test('PUSH.L / POP.L via 01 00 6D', () {
      final cpu = cpuWith([
        0x01, 0x00, 0x6D, 0xF2, // PUSH.L ER2 (MOV.L ER2,@-SP)
        0x01, 0x00, 0x6D, 0x74, // POP.L ER4 (MOV.L @SP+,ER4)
      ]);
      cpu.er[2] = 0xCAFEBABE;
      final sp0 = cpu.er[7];
      cpu.step();
      expect(cpu.er[7], sp0 - 4);
      cpu.step();
      expect(cpu.er[4], 0xCAFEBABE);
      expect(cpu.er[7], sp0);
    });
  });

  group('arithmetic', () {
    test('ADD.B sets H, V, C correctly', () {
      final cpu = cpuWith([0x08, 0x98]); // ADD.B R1L,R0L
      cpu.wr8(8, 0x7F);
      cpu.wr8(9, 0x01);
      cpu.step();
      expect(cpu.rd8(8), 0x80);
      expect(cpu.getFlag(H8Flag.v), isTrue); // 7F+01 overflows
      expect(cpu.getFlag(H8Flag.h), isTrue); // carry out of bit 3
      expect(cpu.getFlag(H8Flag.c), isFalse);
      expect(cpu.getFlag(H8Flag.n), isTrue);
    });

    test('ADD.W and ADD.L register forms', () {
      final cpu = cpuWith([
        0x09, 0x01, // ADD.W R0,R1
        0x0A, 0xB2, // ADD.L ER3,ER2
      ]);
      cpu.wr16(0, 0x8000);
      cpu.wr16(1, 0x8000);
      cpu.step();
      expect(cpu.rd16(1), 0x0000);
      expect(cpu.getFlag(H8Flag.c), isTrue);
      expect(cpu.getFlag(H8Flag.z), isTrue);
      expect(cpu.getFlag(H8Flag.v), isTrue);
      cpu.er[2] = 1;
      cpu.er[3] = 0xFFFFFFFF;
      cpu.step();
      expect(cpu.er[2], 0);
      expect(cpu.getFlag(H8Flag.c), isTrue);
      expect(cpu.getFlag(H8Flag.z), isTrue);
    });

    test('ADDX chains Z across words', () {
      // Simulate a 16-bit add of 0x00FF + 0x0001 via two ADDX.B.
      final cpu = cpuWith([
        0x0E, 0x98, // ADDX R1L,R0L (low bytes)
        0x0E, 0xB2, // ADDX R3L,R2H... placeholder replaced below
      ]);
      cpu.setFlag(H8Flag.c, false);
      cpu.setFlag(H8Flag.z, true);
      cpu.wr8(8, 0xFF); // R0L
      cpu.wr8(9, 0x01); // R1L
      cpu.step();
      expect(cpu.rd8(8), 0x00);
      expect(cpu.getFlag(H8Flag.c), isTrue);
      expect(cpu.getFlag(H8Flag.z), isTrue); // zero result keeps old Z
    });

    test('SUBX borrows and Z-chains', () {
      final cpu = cpuWith([0x1E, 0x98]); // SUBX R1L,R0L
      cpu.setFlag(H8Flag.c, true); // borrow in
      cpu.setFlag(H8Flag.z, false); // previous result nonzero
      cpu.wr8(8, 0x01);
      cpu.wr8(9, 0x00);
      cpu.step();
      expect(cpu.rd8(8), 0x00);
      expect(cpu.getFlag(H8Flag.z), isFalse); // stays clear (chained)
    });

    test('SUB.B / CMP.B borrow flags', () {
      final cpu = cpuWith([
        0x18, 0x98, // SUB.B R1L,R0L
        0xA8, 0x01, // CMP.B #1,R0L
      ]);
      cpu.wr8(8, 0x10);
      cpu.wr8(9, 0x20);
      cpu.step();
      expect(cpu.rd8(8), 0xF0);
      expect(cpu.getFlag(H8Flag.c), isTrue); // borrow
      expect(cpu.getFlag(H8Flag.n), isTrue);
      cpu.step(); // CMP F0 - 1: no borrow
      expect(cpu.rd8(8), 0xF0); // unchanged
      expect(cpu.getFlag(H8Flag.c), isFalse);
    });

    test('INC/DEC set V at the signed limits but preserve C', () {
      final cpu = cpuWith([
        0x0A, 0x08, // INC.B R0L
        0x1A, 0x09, // DEC.B R1L
      ]);
      cpu.setFlag(H8Flag.c, true);
      cpu.wr8(8, 0x7F);
      cpu.wr8(9, 0x80);
      cpu.step();
      expect(cpu.rd8(8), 0x80);
      expect(cpu.getFlag(H8Flag.v), isTrue);
      expect(cpu.getFlag(H8Flag.c), isTrue); // preserved
      cpu.step();
      expect(cpu.rd8(9), 0x7F);
      expect(cpu.getFlag(H8Flag.v), isTrue);
    });

    test('ADDS/SUBS change no flags', () {
      final cpu = cpuWith([
        0x0B, 0x92, // ADDS #4,ER2
        0x1B, 0x82, // SUBS #2,ER2
      ]);
      final flags = cpu.ccr;
      cpu.er[2] = 10;
      cpu.step();
      cpu.step();
      expect(cpu.er[2], 12);
      expect(cpu.ccr, flags);
    });

    test('NEG, EXTU, EXTS', () {
      final cpu = cpuWith([
        0x17, 0x88, // NEG.B R0L
        0x17, 0x51, // EXTU.W R1
        0x17, 0xD2, // EXTS.W R2
        0x17, 0xF3, // EXTS.L ER3
      ]);
      cpu.wr8(8, 0x01);
      cpu.wr16(1, 0xFFAB);
      cpu.wr16(2, 0x0080);
      cpu.er[3] = 0x00008000;
      cpu.step();
      expect(cpu.rd8(8), 0xFF);
      expect(cpu.getFlag(H8Flag.c), isTrue);
      cpu.step();
      expect(cpu.rd16(1), 0x00AB);
      cpu.step();
      expect(cpu.rd16(2), 0xFF80);
      cpu.step();
      expect(cpu.er[3], 0xFFFF8000);
      expect(cpu.getFlag(H8Flag.n), isTrue);
    });

    test('MULXU.B / DIVXU.B', () {
      final cpu = cpuWith([
        0x50, 0x90, // MULXU.B R1L,R0
        0x51, 0xA2, // DIVXU.B R2L,R2... see below
      ]);
      cpu.wr16(0, 0x0012); // low byte 0x12 = 18
      cpu.wr8(9, 10); // R1L
      cpu.step();
      expect(cpu.rd16(0), 180);
      expect(cpu.cycles, 14);
      // DIVXU.B R2L,R2: 500 / 7 = 71 r 3 — set up R2 = 500, R2L is the
      // divisor inside R2 itself; use distinct registers instead:
    });

    test('DIVXU.B computes quotient and remainder', () {
      final cpu = cpuWith([0x51, 0x93]); // DIVXU.B R1L,R3
      cpu.wr16(3, 500);
      cpu.wr8(9, 7); // R1L divisor
      cpu.step();
      expect(cpu.rd16(3) & 0xFF, 71); // quotient in R3L
      expect((cpu.rd16(3) >> 8) & 0xFF, 3); // remainder in R3H
      expect(cpu.getFlag(H8Flag.z), isFalse);
      expect(cpu.getFlag(H8Flag.n), isFalse);
    });

    test('DIVXU by zero sets Z and leaves the destination alone', () {
      final cpu = cpuWith([0x51, 0x93]);
      cpu.wr16(3, 500);
      cpu.wr8(9, 0);
      cpu.step();
      expect(cpu.rd16(3), 500);
      expect(cpu.getFlag(H8Flag.z), isTrue);
    });

    test('MULXS.B multiplies signed', () {
      final cpu = cpuWith([0x01, 0xC0, 0x50, 0x90]); // MULXS.B R1L,R0
      cpu.wr16(0, 0x00FE); // -2 in the low byte
      cpu.wr8(9, 3);
      cpu.step();
      expect(cpu.rd16(0), 0xFFFA); // -6
      expect(cpu.getFlag(H8Flag.n), isTrue);
      expect(cpu.cycles, 16);
    });

    test('DIVXS.W divides signed', () {
      final cpu = cpuWith([0x01, 0xD0, 0x53, 0x12]); // DIVXS.W R1,ER2
      cpu.er[2] = (-100).toUnsigned(32);
      cpu.wr16(1, 7);
      cpu.step();
      expect(cpu.rd16(2), (-14).toUnsigned(16)); // quotient
      expect((cpu.er[2] >> 16) & 0xFFFF, (-2).toUnsigned(16)); // remainder
      expect(cpu.getFlag(H8Flag.n), isTrue);
    });

    test('DAA adjusts a BCD sum', () {
      // 0x19 + 0x28 = 0x41 binary; BCD answer is 47.
      final cpu = cpuWith([0x08, 0x98, 0x0F, 0x08]); // ADD.B R1L,R0L ; DAA R0L
      cpu.wr8(8, 0x19);
      cpu.wr8(9, 0x28);
      cpu.step();
      expect(cpu.rd8(8), 0x41);
      cpu.step();
      expect(cpu.rd8(8), 0x47);
      expect(cpu.getFlag(H8Flag.c), isFalse);
    });

    test('DAS adjusts a BCD difference', () {
      // 0x41 - 0x19 = 0x28 binary with half-borrow; BCD answer is 22.
      final cpu = cpuWith([0x18, 0x98, 0x1F, 0x08]); // SUB.B R1L,R0L ; DAS R0L
      cpu.wr8(8, 0x41);
      cpu.wr8(9, 0x19);
      cpu.step();
      expect(cpu.rd8(8), 0x28);
      expect(cpu.getFlag(H8Flag.h), isTrue);
      cpu.step();
      expect(cpu.rd8(8), 0x22);
    });
  });

  group('logic and shifts', () {
    test('AND/OR/XOR byte immediates', () {
      final cpu = cpuWith([
        0xE8, 0x0F, // AND.B #H'0F,R0L
        0xC8, 0x30, // OR.B #H'30,R0L
        0xD8, 0xFF, // XOR.B #H'FF,R0L
      ]);
      cpu.wr8(8, 0xA5);
      cpu.step();
      expect(cpu.rd8(8), 0x05);
      cpu.step();
      expect(cpu.rd8(8), 0x35);
      cpu.step();
      expect(cpu.rd8(8), 0xCA);
      expect(cpu.getFlag(H8Flag.n), isTrue);
    });

    test('AND.L / OR.L / XOR.L via 01 F0', () {
      final cpu = cpuWith([
        0x01, 0xF0, 0x66, 0x01, // AND.L ER0,ER1
        0x01, 0xF0, 0x64, 0x23, // OR.L ER2,ER3
        0x01, 0xF0, 0x65, 0x45, // XOR.L ER4,ER5
      ]);
      cpu.er[0] = 0xFF00FF00;
      cpu.er[1] = 0x12345678;
      cpu.er[2] = 0x000000FF;
      cpu.er[3] = 0xFF000000;
      cpu.er[4] = 0xFFFFFFFF;
      cpu.er[5] = 0xAAAAAAAA;
      cpu.step();
      expect(cpu.er[1], 0x12005600);
      cpu.step();
      expect(cpu.er[3], 0xFF0000FF);
      cpu.step();
      expect(cpu.er[5], 0x55555555);
      expect(cpu.cycles, 12);
    });

    test('NOT.W', () {
      final cpu = cpuWith([0x17, 0x11]); // NOT.W R1
      cpu.wr16(1, 0x00FF);
      cpu.step();
      expect(cpu.rd16(1), 0xFF00);
      expect(cpu.getFlag(H8Flag.n), isTrue);
    });

    test('shifts and rotates', () {
      final cpu = cpuWith([
        0x10, 0x08, // SHLL.B R0L
        0x11, 0x88, // SHAR.B R0L
        0x12, 0x88, // ROTL.B R0L
        0x13, 0x08, // ROTXR.B R0L
      ]);
      cpu.wr8(8, 0x81);
      cpu.step(); // SHLL: 81 -> 02, C=1
      expect(cpu.rd8(8), 0x02);
      expect(cpu.getFlag(H8Flag.c), isTrue);
      cpu.step(); // SHAR: 02 -> 01, C=0
      expect(cpu.rd8(8), 0x01);
      expect(cpu.getFlag(H8Flag.c), isFalse);
      cpu.step(); // ROTL: 01 -> 02, C=0
      expect(cpu.rd8(8), 0x02);
      cpu.step(); // ROTXR with C=0: 02 -> 01
      expect(cpu.rd8(8), 0x01);
    });

    test('SHAL sets V when the sign changes', () {
      final cpu = cpuWith([0x10, 0x88]); // SHAL.B R0L
      cpu.wr8(8, 0x40);
      cpu.step();
      expect(cpu.rd8(8), 0x80);
      expect(cpu.getFlag(H8Flag.v), isTrue);
    });

    test('SHLL.L on a 32-bit register', () {
      final cpu = cpuWith([0x10, 0x33]); // SHLL.L ER3
      cpu.er[3] = 0x80000001;
      cpu.step();
      expect(cpu.er[3], 0x00000002);
      expect(cpu.getFlag(H8Flag.c), isTrue);
    });
  });

  group('bit manipulation', () {
    test('BSET/BCLR/BNOT/BTST on registers', () {
      final cpu = cpuWith([
        0x70, 0x38, // BSET #3,R0L
        0x73, 0x38, // BTST #3,R0L
        0x72, 0x38, // BCLR #3,R0L
        0x73, 0x38, // BTST #3,R0L
        0x71, 0x08, // BNOT #0,R0L
      ]);
      cpu.wr8(8, 0x00);
      cpu.step();
      expect(cpu.rd8(8), 0x08);
      cpu.step();
      expect(cpu.getFlag(H8Flag.z), isFalse);
      cpu.step();
      expect(cpu.rd8(8), 0x00);
      cpu.step();
      expect(cpu.getFlag(H8Flag.z), isTrue);
      cpu.step();
      expect(cpu.rd8(8), 0x01);
    });

    test('BLD/BST move bits through C', () {
      final cpu = cpuWith([
        0x77, 0x78, // BLD #7,R0L
        0x67, 0x09, // BST #0,R1L
      ]);
      cpu.wr8(8, 0x80);
      cpu.wr8(9, 0x00);
      cpu.step();
      expect(cpu.getFlag(H8Flag.c), isTrue);
      cpu.step();
      expect(cpu.rd8(9), 0x01);
    });

    test('BAND combines a bit with C', () {
      final cpu = cpuWith([0x76, 0x08]); // BAND #0,R0L
      cpu.wr8(8, 0x01);
      cpu.setFlag(H8Flag.c, true);
      cpu.step();
      expect(cpu.getFlag(H8Flag.c), isTrue);
    });

    test('BSET #, @ERd and BTST #, @ERd (7D/7C forms)', () {
      final cpu = cpuWith([
        0x7D, 0x10, 0x70, 0x50, // BSET #5,@ER1
        0x7C, 0x10, 0x73, 0x50, // BTST #5,@ER1
      ]);
      cpu.er[1] = 0x002000;
      cpu.step();
      expect(cpu.mem.peek(0x002000), 0x20);
      expect(cpu.cycles, 8);
      cpu.step();
      expect(cpu.getFlag(H8Flag.z), isFalse);
      expect(cpu.cycles, 14);
    });

    test('BCLR Rn, @aa:8 (7F form)', () {
      final cpu = cpuWith([0x7F, 0x40, 0x62, 0x10]); // BCLR R1H,@H'FFFF40:8
      cpu.mem.poke(0xFFFF40, 0xFF);
      cpu.wr8(1, 2); // R1H = bit 2
      cpu.step();
      expect(cpu.mem.peek(0xFFFF40), 0xFB);
    });
  });

  group('branches and subroutines', () {
    test('conditional branches take and fall through', () {
      final cpu = cpuWith([
        0xA8, 0x00, // CMP.B #0,R0L  (sets Z)
        0x47, 0x02, // BEQ +2
        0x00, 0x00, // NOP (skipped)
        0xF8, 0x01, // MOV.B #1,R0L
      ]);
      cpu.wr8(8, 0);
      cpu.step();
      cpu.step(); // BEQ taken
      expect(cpu.pc, 0x1006);
      cpu.step();
      expect(cpu.rd8(8), 1);
    });

    test('Bcc d:16 with a negative displacement', () {
      final cpu = cpuWith([0x58, 0x00, 0xFF, 0xFC]); // BRA -4 (to itself)
      cpu.step();
      expect(cpu.pc, 0x1000);
      expect(cpu.cycles, 6);
    });

    test('BSR/RTS round-trip preserves the return address', () {
      final cpu = cpuWith([
        0x55, 0x02, // BSR +2 -> 0x1004
        0x00, 0x00, // NOP (return lands here)
        0x54, 0x70, // 0x1004: RTS
      ]);
      final sp0 = cpu.er[7];
      cpu.step(); // BSR
      expect(cpu.pc, 0x1004);
      expect(cpu.er[7], sp0 - 4);
      cpu.step(); // RTS
      expect(cpu.pc, 0x1002);
      expect(cpu.er[7], sp0);
      expect(cpu.cycles, 8 + 10);
    });

    test('JMP @ERn, JMP @aa:24 and JSR @aa:24', () {
      final cpu = cpuWith([0x59, 0x30]); // JMP @ER3
      cpu.er[3] = 0x00345678;
      cpu.step();
      expect(cpu.pc, 0x345678);

      final cpu2 = cpuWith([0x5A, 0x01, 0x23, 0x44]); // JMP @H'012344:24
      cpu2.step();
      expect(cpu2.pc, 0x012344);

      final cpu3 = cpuWith([0x5E, 0x00, 0x20, 0x00]); // JSR @H'002000:24
      final sp0 = cpu3.er[7];
      cpu3.step();
      expect(cpu3.pc, 0x002000);
      expect(cpu3.er[7], sp0 - 4);
      // Return address 0x1004 as a longword.
      expect(cpu3.mem.peek(sp0 - 1), 0x04);
      expect(cpu3.mem.peek(sp0 - 2), 0x10);
      expect(cpu3.mem.peek(sp0 - 3), 0x00);
      expect(cpu3.mem.peek(sp0 - 4), 0x00);
    });

    test('JMP @@aa:8 reads the memory-indirect vector', () {
      final cpu = cpuWith([0x5B, 0x40]); // JMP @@H'40:8
      cpu.mem.poke(0x40, 0x00);
      cpu.mem.poke(0x41, 0x01);
      cpu.mem.poke(0x42, 0x23);
      cpu.mem.poke(0x43, 0x44);
      cpu.step();
      expect(cpu.pc, 0x012344);
    });
  });

  group('system control and exceptions', () {
    test('LDC/STC/ANDC/ORC/XORC on CCR', () {
      final cpu = cpuWith([
        0x07, 0xFF, // LDC #H'FF,CCR
        0x06, 0x7F, // ANDC #H'7F,CCR (clear I)
        0x04, 0x01, // ORC #H'01,CCR (set C)
        0x02, 0x08, // STC.B CCR,R0L
      ]);
      cpu.step();
      expect(cpu.ccr, 0xFF);
      cpu.step();
      expect(cpu.getFlag(H8Flag.i), isFalse);
      cpu.step();
      expect(cpu.getFlag(H8Flag.c), isTrue);
      cpu.step();
      expect(cpu.rd8(8), cpu.ccr);
    });

    test('TRAPA pushes CCR:PC and vectors; RTE returns', () {
      final cpu = cpuWith([0x57, 0x10, 0x00, 0x00]); // TRAPA #1
      // Vector 9 (TRAPA #1) at H'0024 -> H'002000.
      cpu.mem.poke(0x24, 0x00);
      cpu.mem.poke(0x25, 0x00);
      cpu.mem.poke(0x26, 0x20);
      cpu.mem.poke(0x27, 0x00);
      cpu.mem.poke(0x002000, 0x56); // RTE
      cpu.mem.poke(0x002001, 0x70);
      cpu.ccr = H8Flag.c; // I clear, C set
      final sp0 = cpu.er[7];
      cpu.step(); // TRAPA
      expect(cpu.pc, 0x002000);
      expect(cpu.getFlag(H8Flag.i), isTrue);
      expect(cpu.er[7], sp0 - 4);
      expect(cpu.mem.peek(sp0 - 4), H8Flag.c); // saved CCR
      expect(cpu.mem.peek(sp0 - 2), 0x10); // saved PC mid byte
      expect(cpu.mem.peek(sp0 - 1), 0x02); // saved PC low byte
      cpu.step(); // RTE
      expect(cpu.pc, 0x1002);
      expect(cpu.ccr, H8Flag.c); // restored
      expect(cpu.er[7], sp0);
    });

    test('NMI vectors through 7 and IRQ honours the I mask', () {
      final cpu = cpuWith([0x00, 0x00]);
      cpu.mem.poke(0x1C, 0x00);
      cpu.mem.poke(0x1D, 0x00);
      cpu.mem.poke(0x1E, 0x30);
      cpu.mem.poke(0x1F, 0x00);
      cpu.mem.poke(0x30, 0x00); // IRQ0 vector (12) at H'30
      cpu.mem.poke(0x31, 0x00);
      cpu.mem.poke(0x32, 0x40);
      cpu.mem.poke(0x33, 0x00);
      cpu.ccr = H8Flag.i;
      expect(cpu.irq(0), isFalse); // masked
      cpu.nmi(); // NMI ignores the mask
      expect(cpu.pc, 0x003000);
      cpu.ccr = 0; // unmask
      cpu.pc = 0x1000;
      expect(cpu.irq(0), isTrue);
      expect(cpu.pc, 0x004000);
      expect(cpu.getFlag(H8Flag.i), isTrue);
    });

    test('SLEEP halts; NMI wakes', () {
      final cpu = cpuWith([0x01, 0x80, 0x00, 0x00]); // SLEEP; NOP
      cpu.mem.poke(0x1C, 0x00);
      cpu.mem.poke(0x1D, 0x00);
      cpu.mem.poke(0x1E, 0x20);
      cpu.mem.poke(0x1F, 0x00);
      cpu.step();
      expect(cpu.halted, isTrue);
      expect(cpu.sleeping, isTrue);
      expect(cpu.cycles, 2); // SLEEP consumes its states
      expect(cpu.step(), 0); // stays halted
      cpu.nmi();
      expect(cpu.halted, isFalse);
      expect(cpu.pc, 0x002000);
    });

    test('an undefined opcode halts with the PC rewound', () {
      final cpu = cpuWith([0x02, 0xFF]); // STC with a bad register field
      cpu.step();
      expect(cpu.halted, isTrue);
      expect(cpu.sleeping, isFalse);
      expect(cpu.pc, 0x1000);
    });
  });

  group('EEPMOV', () {
    test('EEPMOV.B copies R4L bytes from @ER5 to @ER6', () {
      final cpu = cpuWith([0x7B, 0x5C, 0x59, 0x8F]);
      cpu.wr8(12, 4); // R4L = 4
      cpu.er[5] = 0x002000;
      cpu.er[6] = 0x003000;
      for (var i = 0; i < 4; i++) {
        cpu.mem.poke(0x002000 + i, 0x10 + i);
      }
      cpu.step();
      for (var i = 0; i < 4; i++) {
        expect(cpu.mem.peek(0x003000 + i), 0x10 + i);
      }
      expect(cpu.er[5], 0x002004);
      expect(cpu.er[6], 0x003004);
      expect(cpu.rd8(12), 0);
      expect(cpu.cycles, 8 + 16);
    });
  });

  group('demo program', () {
    test('sums 1..10 into R0L and stores at the on-chip RAM', () {
      final cpu = H8Cpu();
      cpu.mem.poke(0, 0x00);
      cpu.mem.poke(1, 0x00);
      cpu.mem.poke(2, 0x01);
      cpu.mem.poke(3, 0x00);
      const program = <int>[
        0x7A, 0x07, 0x00, 0xFF, 0xFF, 0x00, // MOV.L #H'00FFFF00,ER7
        0xF8, 0x00, // MOV.B #0,R0L
        0xF9, 0x01, // MOV.B #1,R1L
        0x08, 0x98, // ADD.B R1L,R0L
        0x0A, 0x09, // INC.B R1L
        0xA9, 0x0B, // CMP.B #11,R1L
        0x46, 0xF8, // BNE loop
        0x6A, 0xA8, 0x00, 0xFF, 0xFD, 0x10, // MOV.B R0L,@H'FFFD10:24
        0xF1, 0xFF, // MOV.B #H'FF,R1H
        0x31, 0xC5, // MOV.B R1H,@H'FFFFC5:8 (P4DDR: all outputs)
        0x38, 0xC7, // MOV.B R0L,@H'FFFFC7:8 (P4DR: the sum)
        0x01, 0x80, // SLEEP
        0x40, 0xFC, // BRA done
      ];
      for (var i = 0; i < program.length; i++) {
        cpu.mem.poke(0x100 + i, program[i]);
      }
      cpu.reset();
      expect(cpu.pc, 0x000100);
      var guard = 0;
      while (!cpu.halted && guard++ < 1000) {
        cpu.step();
      }
      expect(cpu.halted, isTrue);
      expect(cpu.sleeping, isTrue);
      expect(cpu.rd8(8), 55);
      expect(cpu.mem.peek(0xFFFD10), 55);
      expect(cpu.mem.peek(0xFFFFC5), 0xFF); // P4DDR: all outputs
      expect(cpu.mem.peek(0xFFFFC7), 55); // P4DR: the sum
    });
  });

  group('I/O ports', () {
    test('reset initializes the port DDR/DR registers (mode 3/4 values)', () {
      final cpu = H8Cpu();
      cpu.reset();
      int at(int a) => cpu.mem.peek(a);
      expect(at(0xFFFFC5), 0x00); // P4DDR
      expect(at(0xFFFFC8), 0xFF); // P5DDR (address output, fixed 1)
      expect(at(0xFFFFC9), 0x80); // P6DDR
      expect(at(0xFFFFCB), 0x80); // P6DR
      expect(at(0xFFFFCD), 0xF0); // P8DDR (P84/CS0 output)
      expect(at(0xFFFFCF), 0xE0); // P8DR
      expect(at(0xFFFFD0), 0xC0); // P9DDR
      expect(at(0xFFFFD5), 0x00); // PCDDR
    });

    test('port descriptors cover the documented pins', () {
      final p7 = H8Cpu.ports.firstWhere((p) => p.name == '7');
      expect(p7.inputOnly, isTrue);
      final p5 = H8Cpu.ports.firstWhere((p) => p.name == '5');
      expect(p5.pinMask, 0xF0); // P57-P54
      final p8 = H8Cpu.ports.firstWhere((p) => p.name == '8');
      expect(p8.pinMask, 0x1F); // P84-P80
    });
  });

  group('disassembler', () {
    void check(List<int> bytes, String text, {int? length}) {
      final cpu = cpuWith(bytes);
      final d = cpu.disassemble(0x1000);
      expect(d.text, text);
      expect(d.length, length ?? bytes.length);
    }

    test('spot checks across the instruction set', () {
      check([0x00, 0x00], 'NOP');
      check([0x0C, 0x89], 'MOV.B R0L,R1L');
      check([0x79, 0x02, 0x12, 0x34], "MOV.W #H'1234,R2");
      check([0x7A, 0x03, 0xDE, 0xAD, 0xBE, 0xEF], "MOV.L #H'DEADBEEF,ER3");
      check([0x68, 0x21], 'MOV.B @ER2,R1H');
      check([0x68, 0xB9], 'MOV.B R1L,@ER3');
      check([0x6C, 0x50], 'MOV.B @ER5+,R0H');
      check([0x6D, 0xF1], 'PUSH.W R1');
      check([0x01, 0x00, 0x6D, 0xF2], 'PUSH.L ER2');
      check([0x01, 0x00, 0x6D, 0x74], 'POP.L ER4');
      check([0x6A, 0x28, 0x00, 0x12, 0x34, 0x56], "MOV.B @H'123456:24,R0L");
      check([0x6E, 0x39, 0x00, 0x10], "MOV.B @(H'0010:16,ER3),R1L");
      check(
        [0x78, 0x10, 0x6A, 0x28, 0x00, 0x00, 0x00, 0x10],
        "MOV.B @(H'000010:24,ER1),R0L",
      );
      check([0x08, 0x98], 'ADD.B R1L,R0L');
      check([0x0A, 0xB2], 'ADD.L ER3,ER2');
      check([0x0B, 0x92], 'ADDS #4,ER2');
      check([0x17, 0xF3], 'EXTS.L ER3');
      check([0x10, 0x33], 'SHLL.L ER3');
      check([0x12, 0x88], 'ROTL.B R0L');
      check([0x50, 0x90], 'MULXU.B R1L,R0');
      check([0x01, 0xC0, 0x50, 0x90], 'MULXS.B R1L,R0');
      check([0x01, 0xD0, 0x53, 0x12], 'DIVXS.W R1,ER2');
      check([0x01, 0xF0, 0x66, 0x01], 'AND.L ER0,ER1');
      check([0x40, 0xFE], "BRA H'001000");
      check([0x46, 0xF8], "BNE H'000FFA");
      check([0x58, 0x70, 0x01, 0x00], "BEQ H'001104:16");
      check([0x54, 0x70], 'RTS');
      check([0x56, 0x70], 'RTE');
      check([0x57, 0x20], 'TRAPA #2');
      check([0x59, 0x30], 'JMP @ER3');
      check([0x5A, 0x01, 0x23, 0x44], "JMP @H'012344:24");
      check([0x5E, 0x00, 0x20, 0x00], "JSR @H'002000:24");
      check([0x01, 0x80], 'SLEEP');
      check([0x07, 0xFF], "LDC #H'FF,CCR");
      check([0x70, 0x38], 'BSET #3,R0L');
      check([0x7D, 0x10, 0x70, 0x50], 'BSET #5,@ER1');
      check([0x7C, 0x10, 0x73, 0x50], 'BTST #5,@ER1');
      check([0x7F, 0x40, 0x62, 0x10], "BCLR R1H,@H'FFFF40:8");
      check([0x7B, 0x5C, 0x59, 0x8F], 'EEPMOV.B');
      check([0xF8, 0x37], "MOV.B #H'37,R0L");
      check([0x20, 0x80], "MOV.B @H'FFFF80:8,R0H");
    });

    test('unknown opcodes decode as .WORD with length 2', () {
      check([0x02, 0xFF], ".WORD H'02FF");
    });
  });

  group('hex files', () {
    test('Intel HEX round-trip through the sparse memory', () {
      final cpu = H8Cpu();
      cpu.mem.poke(0x000100, 0x12);
      cpu.mem.poke(0x000101, 0x34);
      cpu.mem.poke(0xFFFD10, 0xAB);
      final text = memoryToIntelHex(cpu.mem);

      final cpu2 = H8Cpu();
      final result = parseHexFile(text, cpu2.mem.poke);
      expect(result.errors, isEmpty);
      expect(cpu2.mem.peek(0x000100), 0x12);
      expect(cpu2.mem.peek(0x000101), 0x34);
      expect(cpu2.mem.peek(0xFFFD10), 0xAB);
      expect(result.sawEof, isTrue);
    });

    test('S-records load with 24-bit addresses and a start address', () {
      // S2 record: len=5 (3 addr + 1 data + checksum), addr=012345, data=5A.
      const rec = 'S2050123455A37\r\nS80401234493\r\n';
      final cpu = H8Cpu();
      final result = parseHexFile(rec, cpu.mem.poke);
      expect(result.errors, isEmpty);
      expect(cpu.mem.peek(0x012345), 0x5A);
      expect(result.startAddress, 0x012344);
    });
  });
}
