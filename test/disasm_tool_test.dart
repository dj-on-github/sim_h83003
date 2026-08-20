// Address parsing for the disassembler tool. The listing itself is the
// disassembler's job and is covered elsewhere; what is new here is accepting
// an address in whichever notation the reader has to hand.

import 'package:flutter_test/flutter_test.dart';

import '../tool/disasm.dart';

void main() {
  group('address arguments', () {
    test('the H8 assembler notation the rest of the project uses', () {
      expect(parseAddr("H'20E062"), 0x20E062);
      expect(parseAddr("h'44c"), 0x44C);
    });

    test('C and assembler notations, so pasted addresses just work', () {
      expect(parseAddr('0x20E062'), 0x20E062);
      expect(parseAddr('0X20e062'), 0x20E062);
      expect(parseAddr(r'$20E062'), 0x20E062);
    });

    test('bare hex, which is what most people type', () {
      expect(parseAddr('20E062'), 0x20E062);
      expect(parseAddr('  200000  '), 0x200000);
    });

    test('nothing given means nothing chosen', () {
      expect(parseAddr(null), isNull);
    });

    test('a value that is not hex is rejected rather than half-read', () {
      expect(parseAddr('not-an-address'), isNull);
      expect(parseAddr(''), isNull);
      // Decimal is deliberately not guessed at: "20" is ambiguous, and
      // reading it as decimal would silently disassemble the wrong place.
      expect(parseAddr('20'), 0x20);
    });
  });
}
