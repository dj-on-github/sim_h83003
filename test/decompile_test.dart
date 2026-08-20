// The pseudocode emitter's operand rendering and condition recovery.
//
// The parts worth pinning down are the ones that were wrong first time: an
// 8-bit absolute address is sign-extended into the on-chip register area, a
// small displacement is a stack offset rather than an address to look up in
// the symbol table, and a branch means nothing without the instruction that
// set the flags.

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import '../tool/decompile.dart';

/// Rendering and condition recovery need no image, only the symbol table.
Decompiler build({Map<int, String> symbols = const {}}) {
  final s = Sym();
  symbols.forEach((a, n) {
    s.byAddr[a] = n;
    s.byName[n] = a;
  });
  return Decompiler(Uint8List(0), s);
}

void main() {
  group('operands', () {
    test('immediates in both hex and decimal', () {
      final d = build();
      expect(d.operand("#H'4C", 8), '0x4C');
      expect(d.operand('#2', 32), '2');
    });

    test('an 8-bit absolute reaches the on-chip registers', () {
      final d = build(symbols: {0xFFFFCF: 'P8DR'});
      expect(d.operand("@H'FFCF:8", 8), 'P8DR');
      expect(d.operand("@H'FFC0:8", 8), 'MEM8(0xFFFFC0)');
    });

    test('a 16-bit absolute sign-extends only above H\'8000', () {
      final d = build(symbols: {0xFFFED9: 'touch_x_raw'});
      expect(d.operand("@H'FED9:16", 8), 'touch_x_raw',
          reason: "H'FED9 is above H'8000, so it reaches H'FFFED9");
      expect(d.operand("@H'0100:16", 8), 'MEM8(0x000100)',
          reason: 'below the halfway point it stays where it is');
      expect(d.operand("@H'FFFED9:24", 8), 'touch_x_raw');
    });

    test('registers, including the upper-word forms', () {
      final d = build();
      expect(d.operand('ER6', 32), 'er6');
      expect(d.operand('R6L', 8), 'r6l');
      expect(d.operand('E2', 16), 'e2');
    });

    test('register indirect, post-increment and pre-decrement', () {
      final d = build();
      expect(d.operand('@ER3', 8), 'MEM8(er3)');
      expect(d.operand('@ER3+', 8), 'MEM8(er3++)');
      expect(d.operand('@-ER7', 32), 'MEM32(--er7)');
    });

    test('a 24-bit displacement is a table, and gets its symbol', () {
      final d = build(symbols: {0x11A251: 'adc_results'});
      expect(d.operand("@(H'11A251:24,ER5)", 8), 'MEM8(adc_results + er5)');
    });

    test('a small displacement off the stack is a local, not an address', () {
      // H'0002 happens to be inside the vector table; naming a frame slot
      // after that would be actively misleading.
      final d = build(symbols: {0x0002: 'VEC_vec0_high'});
      expect(d.operand("@(H'0002:16,ER7)", 16), 'STACK16(0x2)');
      expect(d.operand("@(H'0004:16,ER5)", 16), 'MEM16(er5 + 0x4)');
    });
  });

  group('statements', () {
    test('a self-subtract is the idiom for zero', () {
      final d = build();
      expect(d.statement('SUB.B R6L,R6L'), 'r6l = 0;');
      expect(d.statement('SUB.W R5,R6'), 'r6 -= r5;');
    });

    test('bit operations name the bit', () {
      final d = build(symbols: {0x114DD7: 'latch'});
      expect(d.statement("BSET #3,@H'114DD7:24"), 'latch |= (1 << 3);');
      expect(d.statement("BCLR #3,@H'114DD7:24"), 'latch &= ~(1 << 3);');
    });

    test('calls resolve through the symbol table', () {
      final d = build(symbols: {0x20084A: 'adc_get_result'});
      expect(d.statement("JSR @H'20084A:24"), 'adc_get_result();');
      expect(d.statement("BSR H'210DC6:16"), 'sub_210DC6();');
    });

    test('pushes are shown, because they are how arguments are passed', () {
      final d = build();
      expect(d.statement('PUSH.W R6'), 'push(r6);');
      expect(d.statement('POP.L ER5'), 'er5 = pop();');
    });

    test('a comparison produces no statement of its own', () {
      final d = build();
      expect(d.statement("CMP.B #H'4C,R6L"), isNull);
    });
  });

  group('conditions', () {
    test('a branch after a compare reads as that comparison', () {
      final d = build();
      d.statement("CMP.B #H'4C,R6L");
      expect(d.condition('BCS'), 'r6l < 0x4C');
      expect(d.condition('BCC'), 'r6l >= 0x4C');
      expect(d.condition('BEQ'), 'r6l == 0x4C');
      expect(d.condition('BNE'), 'r6l != 0x4C');
    });

    test('a branch after a bit test reads as that bit', () {
      final d = build(symbols: {0xFFFEC1: 'input_flags'});
      d.statement("BTST #5,@H'FFFEC1:24");
      expect(d.condition('BNE'), '(input_flags & (1 << 5))');
      expect(d.condition('BEQ'), '!(input_flags & (1 << 5))');
    });

    test('a branch after a move tests the value moved', () {
      final d = build(symbols: {0x11A814: 'count'});
      d.statement("MOV.B @H'11A814:24,R6L");
      expect(d.condition('BEQ'), 'r6l == 0');
      expect(d.condition('BNE'), 'r6l != 0');
    });

    test('an untracked flag setter is admitted, not guessed', () {
      final d = build();
      expect(d.condition('BVS'), contains('BVS'));
    });
  });

  group('inverting a branch condition', () {
    // Structuring turns "branch away if X" into "if (not X) { fall through }",
    // so the negated form has to come from the opposite mnemonic rather than
    // from wrapping the text in a "!".
    test('the pairs are mutual', () {
      for (final cc in [
        'BEQ', 'BNE', 'BCS', 'BCC', 'BLO', 'BHS',
        'BHI', 'BLS', 'BGE', 'BLT', 'BGT', 'BLE',
        'BMI', 'BPL', 'BVS', 'BVC',
      ]) {
        expect(invertCc(invertCc(cc)), cc, reason: '$cc round-trips');
        expect(invertCc(cc), isNot(cc), reason: '$cc must change');
      }
    });

    test('an inverted compare reads as the opposite test', () {
      final d = build();
      d.statement("CMP.B #H'4C,R6L");
      expect(d.condition(invertCc('BCS')), 'r6l >= 0x4C');
      expect(d.condition(invertCc('BEQ')), 'r6l != 0x4C');
    });

    test('an inverted bit test negates the mask', () {
      final d = build(symbols: {0xFFFD1C: 'CHAN_SELECTION'});
      d.statement("BTST #1,@H'FFFD1C:24");
      expect(d.condition('BNE'), '(CHAN_SELECTION & (1 << 1))');
      expect(d.condition(invertCc('BNE')), '!(CHAN_SELECTION & (1 << 1))');
    });

    test('an unknown mnemonic is left alone rather than mangled', () {
      expect(invertCc('BSR'), 'BSR');
    });
  });

  group('recovering a jump table as a switch', () {
    // The shape the firmware always uses: a bounds check in one block, then
    // the index scaled and used to load a pointer, then an indirect jump.
    // Built by hand here so the test does not need the dump.
    ({Decompiler d, List<Block> blocks}) dispatchAt(
      int highest, {
      bool withBoundsCheck = true,
      List<int> targets = const [0x200954, 0x200960, 0x20096C, 0x200978],
    }) {
      final img = Uint8List(0x400);
      // Block A: CMP.B #highest,R6L ; BHI +2
      img[0x100] = 0xAE;
      img[0x101] = highest;
      img[0x102] = 0x42;
      img[0x103] = 0x40;
      // Block B: MOV.L @(H'000200:24,ER6),ER6 ; JMP @ER6
      final load = [0x01, 0x00, 0x78, 0x60, 0x6B, 0x26, 0x00, 0x00, 0x02, 0x00];
      for (var i = 0; i < load.length; i++) {
        img[0x104 + i] = load[i];
      }
      img[0x10E] = 0x59;
      img[0x10F] = 0x60;
      // The table itself.
      for (var k = 0; k < targets.length; k++) {
        final t = targets[k];
        img[0x200 + k * 4] = 0x00;
        img[0x201 + k * 4] = (t >> 16) & 0xFF;
        img[0x202 + k * 4] = (t >> 8) & 0xFF;
        img[0x203 + k * 4] = t & 0xFF;
      }

      final s = Sym();
      final d = Decompiler(img, s);
      final a = Block(0x100)..pcs.addAll([0x100, 0x102]);
      if (withBoundsCheck) {
        a.condCc = 'BHI';
        a.condTarget = 0x144;
      }
      final b = Block(0x104)..pcs.addAll([0x104, 0x10E]);
      return (d: d, blocks: [a, b]);
    }

    test('the table, its length and the index all come back', () {
      final c = dispatchAt(3);
      final sw = recoverSwitch(c.d, c.blocks, 1);
      expect(sw, isNotNull);
      expect(sw!.table, 0x000200);
      expect(sw.index, 'r6l');
      expect(sw.targets, [0x200954, 0x200960, 0x20096C, 0x200978]);
      expect(sw.defaultTarget, 0x144);
    });

    test('the entry count is one more than the value compared against', () {
      // CMP #3 / BHI means 0..3 are valid, so four entries.
      final c = dispatchAt(3);
      expect(recoverSwitch(c.d, c.blocks, 1)!.targets.length, 4);
    });

    test('without a bounds check nothing is recovered', () {
      // There would be no way to know where the table ends, and reading past
      // it would invent cases out of whatever followed.
      final c = dispatchAt(3, withBoundsCheck: false);
      expect(recoverSwitch(c.d, c.blocks, 1), isNull);
    });

    test('a table entry of zero is treated as the end of what is known', () {
      final c = dispatchAt(3, targets: [0x200954, 0x200960, 0, 0x200978]);
      expect(recoverSwitch(c.d, c.blocks, 1), isNull);
    });

    test('a block that does not end in an indirect jump is not a switch', () {
      final c = dispatchAt(3);
      expect(recoverSwitch(c.d, c.blocks, 0), isNull);
    });
  });

  group('branch decoding', () {
    test('conditional and unconditional branches are recognised', () {
      expect(branchOf("BEQ H'209280"), ('BEQ', 0x209280));
      expect(branchOf("BRA H'2091B2"), ('BRA', 0x2091B2));
    });

    test('a subroutine call is not a branch', () {
      expect(branchOf("BSR H'210DC6:16"), isNull);
    });

    test('a non-branch is not a branch', () {
      expect(branchOf('MOV.B R6L,R5L'), isNull);
      expect(branchOf('RTS'), isNull);
    });
  });
}
