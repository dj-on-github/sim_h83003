// Disassembler for the H8/300H instruction set (advanced mode), used by
// the simulator's disassembly view and profiler. Mirrors the decoder in
// h8300h.dart; the two must agree on instruction lengths so that the
// linear sweep and the executed instruction stream line up.
//
// Output follows the Renesas assembler conventions: sizes as .B/.W/.L
// suffixes, immediates as #H'xx, absolute addresses as @H'xxxx:16, branch
// targets resolved to absolute addresses.

/// One decoded instruction: its text and byte length.
typedef H8Disasm = ({String text, int length});

String _h2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');
/// Effective address of a 16-bit absolute operand.
///
/// The H8/300H sign-extends @aa:16, so a field of H'FD1C addresses H'FFFD1C.
/// Printing the raw field is what makes a perfectly correct `MOV.B @aa:16`
/// look like a 16-bit access; the :8 form has always been shown as its full
/// address, and this makes the two agree.
int _abs16(int w) => w < 0x8000 ? w : 0xFF0000 | w;

String _h4(int v) => v.toRadixString(16).toUpperCase().padLeft(4, '0');
String _h6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String _h8(int v) => v.toRadixString(16).toUpperCase().padLeft(8, '0');

/// 8-bit register name for a 4-bit field (0-7 RnH, 8-15 RnL).
String _r8(int n) => n < 8 ? 'R${n}H' : 'R${n - 8}L';

/// 16-bit register name for a 4-bit field (0-7 Rn, 8-15 En).
String _r16(int n) => n < 8 ? 'R$n' : 'E${n - 8}';

/// 32-bit register name for a 3-bit field.
String _r32(int n) => 'ER${n & 7}';

H8Disasm _ill(int word) => (text: ".WORD H'${_h4(word)}", length: 2);

const List<String> _bccNames = [
  'BRA', 'BRN', 'BHI', 'BLS', 'BCC', 'BCS', 'BNE', 'BEQ',
  'BVC', 'BVS', 'BPL', 'BMI', 'BGE', 'BLT', 'BGT', 'BLE',
];

/// Disassembles the instruction at [addr], reading bytes through [peek]
/// (which must be side-effect free).
H8Disasm disassembleH8(int Function(int addr) peek, int addr) {
  int byteAt(int i) => peek(addr + i);
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);

  final b0 = byteAt(0);
  final b1 = byteAt(1);
  final w0 = (b0 << 8) | b1;

  switch (b0) {
    case 0x00:
      return b1 == 0 ? (text: 'NOP', length: 2) : _ill(w0);

    case 0x01:
      return _disasm01(peek, addr);

    case 0x02:
      return (b1 & 0xF0) == 0
          ? (text: 'STC.B CCR,${_r8(b1 & 0xF)}', length: 2)
          : _ill(w0);

    case 0x03:
      return (b1 & 0xF0) == 0
          ? (text: 'LDC.B ${_r8(b1 & 0xF)},CCR', length: 2)
          : _ill(w0);

    case 0x04:
      return (text: "ORC #H'${_h2(b1)},CCR", length: 2);
    case 0x05:
      return (text: "XORC #H'${_h2(b1)},CCR", length: 2);
    case 0x06:
      return (text: "ANDC #H'${_h2(b1)},CCR", length: 2);
    case 0x07:
      return (text: "LDC #H'${_h2(b1)},CCR", length: 2);

    case 0x08:
      return (text: 'ADD.B ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x09:
      return (text: 'ADD.W ${_r16(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);

    case 0x0A:
      if ((b1 & 0x80) != 0) {
        if ((b1 & 0x08) != 0) return _ill(w0);
        return (
          text: 'ADD.L ${_r32((b1 >> 4) & 7)},${_r32(b1 & 7)}',
          length: 2
        );
      }
      if ((b1 & 0xF0) == 0) {
        return (text: 'INC.B ${_r8(b1 & 0xF)}', length: 2);
      }
      return _ill(w0);

    case 0x0B:
      return _disasmAddsInc(w0, sub: false);

    case 0x0C:
      return (text: 'MOV.B ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x0D:
      return (text: 'MOV.W ${_r16(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);
    case 0x0E:
      return (text: 'ADDX ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);

    case 0x0F:
      if ((b1 & 0x80) != 0) {
        if ((b1 & 0x08) != 0) return _ill(w0);
        return (
          text: 'MOV.L ${_r32((b1 >> 4) & 7)},${_r32(b1 & 7)}',
          length: 2
        );
      }
      if ((b1 & 0xF0) == 0) return (text: 'DAA ${_r8(b1 & 0xF)}', length: 2);
      return _ill(w0);

    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
      return _disasmShift(w0);

    case 0x14:
      return (text: 'OR.B ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x15:
      return (text: 'XOR.B ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x16:
      return (text: 'AND.B ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);

    case 0x17:
      return _disasmNotExtNeg(w0);

    case 0x18:
      return (text: 'SUB.B ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x19:
      return (text: 'SUB.W ${_r16(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);

    case 0x1A:
      if ((b1 & 0x80) != 0) {
        if ((b1 & 0x08) != 0) return _ill(w0);
        return (
          text: 'SUB.L ${_r32((b1 >> 4) & 7)},${_r32(b1 & 7)}',
          length: 2
        );
      }
      if ((b1 & 0xF0) == 0) {
        return (text: 'DEC.B ${_r8(b1 & 0xF)}', length: 2);
      }
      return _ill(w0);

    case 0x1B:
      return _disasmAddsInc(w0, sub: true);

    case 0x1C:
      return (text: 'CMP.B ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x1D:
      return (text: 'CMP.W ${_r16(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);
    case 0x1E:
      return (text: 'SUBX ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);

    case 0x1F:
      if ((b1 & 0x80) != 0) {
        if ((b1 & 0x08) != 0) return _ill(w0);
        return (
          text: 'CMP.L ${_r32((b1 >> 4) & 7)},${_r32(b1 & 7)}',
          length: 2
        );
      }
      if ((b1 & 0xF0) == 0) return (text: 'DAS ${_r8(b1 & 0xF)}', length: 2);
      return _ill(w0);
  }

  // MOV.B @aa:8 (H'FFFFxx)
  if (b0 >= 0x20 && b0 <= 0x2F) {
    return (
      text: "MOV.B @H'${_h6(0xFFFF00 | b1)}:8,${_r8(b0 & 0xF)}",
      length: 2
    );
  }
  if (b0 >= 0x30 && b0 <= 0x3F) {
    return (
      text: "MOV.B ${_r8(b0 & 0xF)},@H'${_h6(0xFFFF00 | b1)}:8",
      length: 2
    );
  }

  // Bcc d:8
  if (b0 >= 0x40 && b0 <= 0x4F) {
    final disp = b1 < 0x80 ? b1 : b1 - 0x100;
    final target = (addr + 2 + disp) & 0xFFFFFF;
    return (text: "${_bccNames[b0 & 0xF]} H'${_h6(target)}", length: 2);
  }

  switch (b0) {
    case 0x50:
      return (text: 'MULXU.B ${_r8(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);
    case 0x51:
      return (text: 'DIVXU.B ${_r8(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);
    case 0x52:
      return (b1 & 0x08) == 0
          ? (text: 'MULXU.W ${_r16(b1 >> 4)},${_r32(b1 & 7)}', length: 2)
          : _ill(w0);
    case 0x53:
      return (b1 & 0x08) == 0
          ? (text: 'DIVXU.W ${_r16(b1 >> 4)},${_r32(b1 & 7)}', length: 2)
          : _ill(w0);

    case 0x54:
      return b1 == 0x70 ? (text: 'RTS', length: 2) : _ill(w0);
    case 0x55:
      final disp = b1 < 0x80 ? b1 : b1 - 0x100;
      return (
        text: "BSR H'${_h6((addr + 2 + disp) & 0xFFFFFF)}",
        length: 2
      );
    case 0x56:
      return b1 == 0x70 ? (text: 'RTE', length: 2) : _ill(w0);
    case 0x57:
      return (b1 & 0xCF) == 0
          ? (text: 'TRAPA #${(b1 >> 4) & 3}', length: 2)
          : _ill(w0);

    case 0x58: // Bcc d:16
      if ((b1 & 0x0F) != 0) return _ill(w0);
      final d = wordAt(2);
      final disp = d < 0x8000 ? d : d - 0x10000;
      final target = (addr + 4 + disp) & 0xFFFFFF;
      return (text: "${_bccNames[b1 >> 4]} H'${_h6(target)}:16", length: 4);

    case 0x59:
      return (b1 & 0x8F) == 0
          ? (text: 'JMP @${_r32((b1 >> 4) & 7)}', length: 2)
          : _ill(w0);
    case 0x5A:
      return (
        text: "JMP @H'${_h6(((b1 << 16) | wordAt(2)) & 0xFFFFFF)}:24",
        length: 4
      );
    case 0x5B:
      return (text: "JMP @@H'${_h2(b1)}:8", length: 2);
    case 0x5C:
      if (b1 != 0x00) return _ill(w0);
      final d = wordAt(2);
      final disp = d < 0x8000 ? d : d - 0x10000;
      return (
        text: "BSR H'${_h6((addr + 4 + disp) & 0xFFFFFF)}:16",
        length: 4
      );
    case 0x5D:
      return (b1 & 0x8F) == 0
          ? (text: 'JSR @${_r32((b1 >> 4) & 7)}', length: 2)
          : _ill(w0);
    case 0x5E:
      return (
        text: "JSR @H'${_h6(((b1 << 16) | wordAt(2)) & 0xFFFFFF)}:24",
        length: 4
      );
    case 0x5F:
      return (text: "JSR @@H'${_h2(b1)}:8", length: 2);

    case 0x60:
      return (text: 'BSET ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x61:
      return (text: 'BNOT ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x62:
      return (text: 'BCLR ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);
    case 0x63:
      return (text: 'BTST ${_r8(b1 >> 4)},${_r8(b1 & 0xF)}', length: 2);

    case 0x64:
      return (text: 'OR.W ${_r16(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);
    case 0x65:
      return (text: 'XOR.W ${_r16(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);
    case 0x66:
      return (text: 'AND.W ${_r16(b1 >> 4)},${_r16(b1 & 0xF)}', length: 2);

    case 0x67:
      final name = (b1 & 0x80) != 0 ? 'BIST' : 'BST';
      return (text: '$name #${(b1 >> 4) & 7},${_r8(b1 & 0xF)}', length: 2);

    case 0x68:
      final ern = _r32((b1 >> 4) & 7);
      final r = _r8(b1 & 0xF);
      return (b1 & 0x80) != 0
          ? (text: 'MOV.B $r,@$ern', length: 2)
          : (text: 'MOV.B @$ern,$r', length: 2);
    case 0x69:
      final ern = _r32((b1 >> 4) & 7);
      final r = _r16(b1 & 0xF);
      return (b1 & 0x80) != 0
          ? (text: 'MOV.W $r,@$ern', length: 2)
          : (text: 'MOV.W @$ern,$r', length: 2);

    case 0x6A:
      return _disasmMovAbs(peek, addr, w0, byte: true);
    case 0x6B:
      return _disasmMovAbs(peek, addr, w0, byte: false);

    case 0x6C:
      final ern = _r32((b1 >> 4) & 7);
      final r = _r8(b1 & 0xF);
      return (b1 & 0x80) != 0
          ? (text: 'MOV.B $r,@-$ern', length: 2)
          : (text: 'MOV.B @$ern+,$r', length: 2);
    case 0x6D:
      final ern = (b1 >> 4) & 7;
      final r = _r16(b1 & 0xF);
      if ((b1 & 0x80) != 0) {
        return ern == 7
            ? (text: 'PUSH.W $r', length: 2)
            : (text: 'MOV.W $r,@-${_r32(ern)}', length: 2);
      }
      return ern == 7
          ? (text: 'POP.W $r', length: 2)
          : (text: 'MOV.W @${_r32(ern)}+,$r', length: 2);

    case 0x6E:
      final ern = _r32((b1 >> 4) & 7);
      final r = _r8(b1 & 0xF);
      final d = wordAt(2);
      final at = "@(H'${_h4(d)}:16,$ern)";
      return (b1 & 0x80) != 0
          ? (text: 'MOV.B $r,$at', length: 4)
          : (text: 'MOV.B $at,$r', length: 4);
    case 0x6F:
      final ern = _r32((b1 >> 4) & 7);
      final r = _r16(b1 & 0xF);
      final d = wordAt(2);
      final at = "@(H'${_h4(d)}:16,$ern)";
      return (b1 & 0x80) != 0
          ? (text: 'MOV.W $r,$at', length: 4)
          : (text: 'MOV.W $at,$r', length: 4);

    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
      if ((b1 & 0x80) != 0) return _ill(w0);
      final names = ['BSET', 'BNOT', 'BCLR', 'BTST'];
      return (
        text: '${names[b0 & 3]} #${(b1 >> 4) & 7},${_r8(b1 & 0xF)}',
        length: 2
      );

    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
      final inv = (b1 & 0x80) != 0;
      final names = inv
          ? ['BIOR', 'BIXOR', 'BIAND', 'BILD']
          : ['BOR', 'BXOR', 'BAND', 'BLD'];
      return (
        text: '${names[b0 & 3]} #${(b1 >> 4) & 7},${_r8(b1 & 0xF)}',
        length: 2
      );

    case 0x78:
      return _disasmMovDisp24(peek, addr, w0);

    case 0x79:
      return _disasmImmGroup(peek, addr, w0, size: 16);
    case 0x7A:
      return _disasmImmGroup(peek, addr, w0, size: 32);

    case 0x7B:
      if (wordAt(2) == 0x598F && b1 == 0x5C) {
        return (text: 'EEPMOV.B', length: 4);
      }
      if (wordAt(2) == 0x598F && b1 == 0xD4) {
        return (text: 'EEPMOV.W', length: 4);
      }
      return _ill(w0);

    case 0x7C:
    case 0x7D:
      if ((b1 & 0x8F) != 0) return _ill(w0);
      return _disasmBitMem(peek, addr, '@${_r32((b1 >> 4) & 7)}',
          write: b0 == 0x7D);
    case 0x7E:
    case 0x7F:
      return _disasmBitMem(peek, addr, "@H'${_h6(0xFFFF00 | b1)}:8",
          write: b0 == 0x7F);
  }

  // 0x80-0xFF: byte-immediate group.
  final rd = _r8(b0 & 0xF);
  final imm = "#H'${_h2(b1)}";
  switch (b0 & 0xF0) {
    case 0x80:
      return (text: 'ADD.B $imm,$rd', length: 2);
    case 0x90:
      return (text: 'ADDX $imm,$rd', length: 2);
    case 0xA0:
      return (text: 'CMP.B $imm,$rd', length: 2);
    case 0xB0:
      return (text: 'SUBX $imm,$rd', length: 2);
    case 0xC0:
      return (text: 'OR.B $imm,$rd', length: 2);
    case 0xD0:
      return (text: 'XOR.B $imm,$rd', length: 2);
    case 0xE0:
      return (text: 'AND.B $imm,$rd', length: 2);
    default:
      return (text: 'MOV.B $imm,$rd', length: 2);
  }
}

H8Disasm _disasmAddsInc(int w0, {required bool sub}) {
  final b1 = w0 & 0xFF;
  final hi = b1 >> 4;
  final lo = b1 & 0xF;
  final incName = sub ? 'DEC' : 'INC';
  final addsName = sub ? 'SUBS' : 'ADDS';
  switch (hi) {
    case 0x0:
      return lo < 8
          ? (text: '$addsName #1,${_r32(lo)}', length: 2)
          : _ill(w0);
    case 0x8:
      return lo < 8
          ? (text: '$addsName #2,${_r32(lo)}', length: 2)
          : _ill(w0);
    case 0x9:
      return lo < 8
          ? (text: '$addsName #4,${_r32(lo)}', length: 2)
          : _ill(w0);
    case 0x5:
      return (text: '$incName.W #1,${_r16(lo)}', length: 2);
    case 0xD:
      return (text: '$incName.W #2,${_r16(lo)}', length: 2);
    case 0x7:
      return lo < 8
          ? (text: '$incName.L #1,${_r32(lo)}', length: 2)
          : _ill(w0);
    case 0xF:
      return lo < 8
          ? (text: '$incName.L #2,${_r32(lo)}', length: 2)
          : _ill(w0);
    default:
      return _ill(w0);
  }
}

H8Disasm _disasmShift(int w0) {
  final b0 = w0 >> 8;
  final b1 = w0 & 0xFF;
  final hi = b1 >> 4;
  final lo = b1 & 0xF;
  final alt = (hi & 0x8) != 0;
  final name = switch (b0) {
    0x10 => alt ? 'SHAL' : 'SHLL',
    0x11 => alt ? 'SHAR' : 'SHLR',
    0x12 => alt ? 'ROTL' : 'ROTXL',
    _ => alt ? 'ROTR' : 'ROTXR',
  };
  switch (hi & 0x7) {
    case 0x0:
      return (text: '$name.B ${_r8(lo)}', length: 2);
    case 0x1:
      return (text: '$name.W ${_r16(lo)}', length: 2);
    case 0x3:
      return lo < 8 ? (text: '$name.L ${_r32(lo)}', length: 2) : _ill(w0);
    default:
      return _ill(w0);
  }
}

H8Disasm _disasmNotExtNeg(int w0) {
  final b1 = w0 & 0xFF;
  final hi = b1 >> 4;
  final lo = b1 & 0xF;
  switch (hi) {
    case 0x0:
      return (text: 'NOT.B ${_r8(lo)}', length: 2);
    case 0x1:
      return (text: 'NOT.W ${_r16(lo)}', length: 2);
    case 0x3:
      return lo < 8 ? (text: 'NOT.L ${_r32(lo)}', length: 2) : _ill(w0);
    case 0x5:
      return (text: 'EXTU.W ${_r16(lo)}', length: 2);
    case 0x7:
      return lo < 8 ? (text: 'EXTU.L ${_r32(lo)}', length: 2) : _ill(w0);
    case 0x8:
      return (text: 'NEG.B ${_r8(lo)}', length: 2);
    case 0x9:
      return (text: 'NEG.W ${_r16(lo)}', length: 2);
    case 0xB:
      return lo < 8 ? (text: 'NEG.L ${_r32(lo)}', length: 2) : _ill(w0);
    case 0xD:
      return (text: 'EXTS.W ${_r16(lo)}', length: 2);
    case 0xF:
      return lo < 8 ? (text: 'EXTS.L ${_r32(lo)}', length: 2) : _ill(w0);
    default:
      return _ill(w0);
  }
}

H8Disasm _disasmMovAbs(int Function(int) peek, int addr, int w0,
    {required bool byte}) {
  final b1 = w0 & 0xFF;
  final mode = b1 >> 4;
  final r = byte ? _r8(b1 & 0xF) : _r16(b1 & 0xF);
  final sz = byte ? 'B' : 'W';
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);
  switch (mode) {
    case 0x0:
      return (
        text: "MOV.$sz @H'${_h6(_abs16(wordAt(2)))}:16,$r",
        length: 4
      );
    case 0x8:
      return (
        text: "MOV.$sz $r,@H'${_h6(_abs16(wordAt(2)))}:16",
        length: 4
      );
    case 0x2:
      final a = ((wordAt(2) & 0xFF) << 16) | wordAt(4);
      return (text: "MOV.$sz @H'${_h6(a)}:24,$r", length: 6);
    case 0xA:
      final a = ((wordAt(2) & 0xFF) << 16) | wordAt(4);
      return (text: "MOV.$sz $r,@H'${_h6(a)}:24", length: 6);
    default:
      return _ill(w0);
  }
}

H8Disasm _disasmMovDisp24(int Function(int) peek, int addr, int w0) {
  final b1 = w0 & 0xFF;
  if ((b1 & 0x8F) != 0) return _ill(w0);
  final ern = _r32((b1 >> 4) & 7);
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);
  final w1 = wordAt(2);
  final b2 = w1 >> 8;
  final b3 = w1 & 0xFF;
  if ((b2 != 0x6A && b2 != 0x6B) || (b3 & 0x70) != 0x20) return _ill(w0);
  final byte = b2 == 0x6A;
  final store = (b3 & 0x80) != 0;
  final r = byte ? _r8(b3 & 0xF) : _r16(b3 & 0xF);
  final sz = byte ? 'B' : 'W';
  final disp = ((wordAt(4) & 0xFF) << 16) | wordAt(6);
  final at = "@(H'${_h6(disp)}:24,$ern)";
  return store
      ? (text: 'MOV.$sz $r,$at', length: 8)
      : (text: 'MOV.$sz $at,$r', length: 8);
}

H8Disasm _disasmImmGroup(int Function(int) peek, int addr, int w0,
    {required int size}) {
  final b1 = w0 & 0xFF;
  final op = b1 >> 4;
  const names = ['MOV', 'ADD', 'CMP', 'SUB', 'OR', 'XOR', 'AND'];
  if (op >= names.length) return _ill(w0);
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);
  if (size == 16) {
    final r = _r16(b1 & 0xF);
    return (
      text: "${names[op]}.W #H'${_h4(wordAt(2))},$r",
      length: 4
    );
  }
  if ((b1 & 0x08) != 0) return _ill(w0);
  final r = _r32(b1 & 7);
  final imm = (wordAt(2) << 16) | wordAt(4);
  return (text: "${names[op]}.L #H'${_h8(imm)},$r", length: 6);
}

H8Disasm _disasmBitMem(int Function(int) peek, int addr, String dest,
    {required bool write}) {
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);
  final w1 = wordAt(2);
  final b2 = w1 >> 8;
  final b3 = w1 & 0xFF;
  if ((b3 & 0x0F) != 0) return _ill((peek(addr) << 8) | peek(addr + 1));
  final bit = (b3 >> 4) & 7;
  final inv = (b3 & 0x80) != 0;
  if (write) {
    switch (b2) {
      case 0x60:
        return (text: 'BSET ${_r8(b3 >> 4)},$dest', length: 4);
      case 0x61:
        return (text: 'BNOT ${_r8(b3 >> 4)},$dest', length: 4);
      case 0x62:
        return (text: 'BCLR ${_r8(b3 >> 4)},$dest', length: 4);
      case 0x67:
        return (text: '${inv ? 'BIST' : 'BST'} #$bit,$dest', length: 4);
      case 0x70:
        if (!inv) return (text: 'BSET #$bit,$dest', length: 4);
      case 0x71:
        if (!inv) return (text: 'BNOT #$bit,$dest', length: 4);
      case 0x72:
        if (!inv) return (text: 'BCLR #$bit,$dest', length: 4);
    }
  } else {
    switch (b2) {
      case 0x63:
        return (text: 'BTST ${_r8(b3 >> 4)},$dest', length: 4);
      case 0x73:
        if (!inv) return (text: 'BTST #$bit,$dest', length: 4);
      case 0x74:
        return (text: '${inv ? 'BIOR' : 'BOR'} #$bit,$dest', length: 4);
      case 0x75:
        return (text: '${inv ? 'BIXOR' : 'BXOR'} #$bit,$dest', length: 4);
      case 0x76:
        return (text: '${inv ? 'BIAND' : 'BAND'} #$bit,$dest', length: 4);
      case 0x77:
        return (text: '${inv ? 'BILD' : 'BLD'} #$bit,$dest', length: 4);
    }
  }
  return _ill((peek(addr) << 8) | peek(addr + 1));
}

/// 0x01-prefixed instructions.
H8Disasm _disasm01(int Function(int) peek, int addr) {
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);
  final w0 = wordAt(0);
  final b1 = w0 & 0xFF;
  switch (b1) {
    case 0x00: // MOV.L register-memory
      return _disasmMovLong(peek, addr);
    case 0x40: // LDC.W / STC.W memory
      return _disasmLdcStcW(peek, addr);
    case 0x80:
      return (text: 'SLEEP', length: 2);
    case 0xC0:
      final w1 = wordAt(2);
      final b2 = w1 >> 8;
      final b3 = w1 & 0xFF;
      if (b2 == 0x50) {
        return (
          text: 'MULXS.B ${_r8(b3 >> 4)},${_r16(b3 & 0xF)}',
          length: 4
        );
      }
      if (b2 == 0x52 && (b3 & 0x08) == 0) {
        return (
          text: 'MULXS.W ${_r16(b3 >> 4)},${_r32(b3 & 7)}',
          length: 4
        );
      }
      return _ill(w0);
    case 0xD0:
      final w1 = wordAt(2);
      final b2 = w1 >> 8;
      final b3 = w1 & 0xFF;
      if (b2 == 0x51) {
        return (
          text: 'DIVXS.B ${_r8(b3 >> 4)},${_r16(b3 & 0xF)}',
          length: 4
        );
      }
      if (b2 == 0x53 && (b3 & 0x08) == 0) {
        return (
          text: 'DIVXS.W ${_r16(b3 >> 4)},${_r32(b3 & 7)}',
          length: 4
        );
      }
      return _ill(w0);
    case 0xF0:
      final w1 = wordAt(2);
      final b2 = w1 >> 8;
      final b3 = w1 & 0xFF;
      if ((b3 & 0x88) != 0) return _ill(w0);
      final s = _r32((b3 >> 4) & 7);
      final d = _r32(b3 & 7);
      switch (b2) {
        case 0x64:
          return (text: 'OR.L $s,$d', length: 4);
        case 0x65:
          return (text: 'XOR.L $s,$d', length: 4);
        case 0x66:
          return (text: 'AND.L $s,$d', length: 4);
      }
      return _ill(w0);
    default:
      return _ill(w0);
  }
}

H8Disasm _disasmMovLong(int Function(int) peek, int addr) {
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);
  final w1 = wordAt(2);
  final b2 = w1 >> 8;
  final b3 = w1 & 0xFF;
  if ((b3 & 0x08) != 0) return _ill(wordAt(0));
  final store = (b3 & 0x80) != 0;
  final ern = _r32((b3 >> 4) & 7);
  final erd = _r32(b3 & 7);
  switch (b2) {
    case 0x69:
      return store
          ? (text: 'MOV.L $erd,@$ern', length: 4)
          : (text: 'MOV.L @$ern,$erd', length: 4);
    case 0x6B:
      final mode = (b3 >> 4) & 7;
      if (mode == 0) {
        final a = "@H'${_h6(_abs16(wordAt(4)))}:16";
        return store
            ? (text: 'MOV.L $erd,$a', length: 6)
            : (text: 'MOV.L $a,$erd', length: 6);
      }
      if (mode == 2) {
        final a =
            "@H'${_h6(((wordAt(4) & 0xFF) << 16) | wordAt(6))}:24";
        return store
            ? (text: 'MOV.L $erd,$a', length: 8)
            : (text: 'MOV.L $a,$erd', length: 8);
      }
      return _ill(wordAt(0));
    case 0x6D:
      final n = (b3 >> 4) & 7;
      if (store) {
        return n == 7
            ? (text: 'PUSH.L $erd', length: 4)
            : (text: 'MOV.L $erd,@-$ern', length: 4);
      }
      return n == 7
          ? (text: 'POP.L $erd', length: 4)
          : (text: 'MOV.L @$ern+,$erd', length: 4);
    case 0x6F:
      final at = "@(H'${_h4(wordAt(4))}:16,$ern)";
      return store
          ? (text: 'MOV.L $erd,$at', length: 6)
          : (text: 'MOV.L $at,$erd', length: 6);
    case 0x78:
      // The longword d:24 form sets bit 7 here for a store; the direction is
      // taken from the sub-opcode below.
      if ((b3 & 0x0F) != 0) return _ill(wordAt(0));
      final w2 = wordAt(4);
      if ((w2 >> 8) != 0x6B || (w2 & 0x78) != 0x20) return _ill(wordAt(0));
      final store2 = (w2 & 0x80) != 0;
      final erd2 = _r32(w2 & 7);
      final disp = ((wordAt(6) & 0xFF) << 16) | wordAt(8);
      final at = "@(H'${_h6(disp)}:24,$ern)";
      return store2
          ? (text: 'MOV.L $erd2,$at', length: 10)
          : (text: 'MOV.L $at,$erd2', length: 10);
    default:
      return _ill(wordAt(0));
  }
}

H8Disasm _disasmLdcStcW(int Function(int) peek, int addr) {
  int wordAt(int i) => (peek(addr + i) << 8) | peek(addr + i + 1);
  final w1 = wordAt(2);
  final b2 = w1 >> 8;
  final b3 = w1 & 0xFF;
  final store = (b3 & 0x80) != 0; // STC
  final ern = _r32((b3 >> 4) & 7);
  switch (b2) {
    case 0x69:
      if ((b3 & 0x0F) != 0) return _ill(wordAt(0));
      return store
          ? (text: 'STC.W CCR,@$ern', length: 4)
          : (text: 'LDC.W @$ern,CCR', length: 4);
    case 0x6B:
      if ((b3 & 0x0F) != 0) return _ill(wordAt(0));
      final mode = (b3 >> 4) & 7;
      if (mode == 0) {
        final a = "@H'${_h6(_abs16(wordAt(4)))}:16";
        return store
            ? (text: 'STC.W CCR,$a', length: 6)
            : (text: 'LDC.W $a,CCR', length: 6);
      }
      if (mode == 2) {
        final a =
            "@H'${_h6(((wordAt(4) & 0xFF) << 16) | wordAt(6))}:24";
        return store
            ? (text: 'STC.W CCR,$a', length: 8)
            : (text: 'LDC.W $a,CCR', length: 8);
      }
      return _ill(wordAt(0));
    case 0x6D:
      if ((b3 & 0x0F) != 0) return _ill(wordAt(0));
      return store
          ? (text: 'STC.W CCR,@-$ern', length: 4)
          : (text: 'LDC.W @$ern+,CCR', length: 4);
    case 0x6F:
      if ((b3 & 0x0F) != 0) return _ill(wordAt(0));
      final at = "@(H'${_h4(wordAt(4))}:16,$ern)";
      return store
          ? (text: 'STC.W CCR,$at', length: 6)
          : (text: 'LDC.W $at,CCR', length: 6);
    case 0x78:
      if ((b3 & 0x8F) != 0) return _ill(wordAt(0));
      final w2 = wordAt(4);
      if ((w2 >> 8) != 0x6B || (w2 & 0x7F) != 0x20) return _ill(wordAt(0));
      final store2 = (w2 & 0x80) != 0;
      final disp = ((wordAt(6) & 0xFF) << 16) | wordAt(8);
      final at = "@(H'${_h6(disp)}:24,$ern)";
      return store2
          ? (text: 'STC.W CCR,$at', length: 10)
          : (text: 'LDC.W $at,CCR', length: 10);
    default:
      return _ill(wordAt(0));
  }
}
