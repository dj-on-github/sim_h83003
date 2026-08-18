// External pin levels on the I/O ports.
//
// A port data register on its own cannot represent an input: reading it just
// returns whatever was last written, so nothing outside the CPU can be seen.
// These tests pin down the override layer, and in particular that it changes
// nothing until a pin is deliberately driven — firmware that writes a port
// and reads it back must keep working.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';

const int p4ddr = 0xFFFFC5;
const int p4dr = 0xFFFFC7;
const int p7dr = 0xFFFFCE; // input only, no DDR

void main() {
  group('without any pin driven', () {
    test('a port reads back what was written, as before', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(p4ddr, 0x00); // all inputs
      cpu.writeB(p4dr, 0x5A);
      expect(cpu.readB(p4dr), 0x5A);
      expect(cpu.peekBus(p4dr), 0x5A);
    });

    test('the input-only port reads back too', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(p7dr, 0xC3);
      expect(cpu.readB(p7dr), 0xC3);
    });
  });

  group('driving a pin', () {
    test('an input bit reads the held level, not the data register', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(p4ddr, 0x00);
      cpu.writeB(p4dr, 0xFF);
      cpu.setPin(p4dr, 5, false);
      expect(cpu.readB(p4dr), 0xDF, reason: 'bit 5 held low');
      cpu.setPin(p4dr, 5, true);
      expect(cpu.readB(p4dr), 0xFF);
      cpu.writeB(p4dr, 0x00);
      expect(cpu.readB(p4dr), 0x20, reason: 'still held high over a write');
    });

    test('only the driven bits are substituted', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(p4ddr, 0x00);
      cpu.writeB(p4dr, 0xF0);
      cpu.setPin(p4dr, 0, true);
      expect(cpu.readB(p4dr), 0xF1);
    });

    test('an output bit ignores the pin, as the hardware does', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(p4ddr, 0xFF); // all outputs
      cpu.writeB(p4dr, 0x00);
      cpu.setPin(p4dr, 3, true);
      expect(cpu.readB(p4dr), 0x00);
      // Turn that bit into an input and the held level appears.
      cpu.writeB(p4ddr, 0xF7);
      expect(cpu.readB(p4dr), 0x08);
    });

    test('the input-only port needs no DDR', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(p7dr, 0x00);
      cpu.setPin(p7dr, 7, true);
      expect(cpu.readB(p7dr), 0x80);
    });

    test('releasing restores read-back', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(p4ddr, 0x00);
      cpu.writeB(p4dr, 0xAA);
      cpu.setPin(p4dr, 1, false);
      expect(cpu.readB(p4dr), 0xA8);
      cpu.releasePin(p4dr, 1);
      expect(cpu.readB(p4dr), 0xAA);
      expect(cpu.pinIsDriven(p4dr, 1), isFalse);
    });

    test('pins survive a CPU reset, the way a switch position does', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.setPin(p4dr, 2, true);
      cpu.reset();
      cpu.writeB(p4ddr, 0x00);
      expect(cpu.readB(p4dr) & 0x04, 0x04);
      cpu.releaseAllPins();
      expect(cpu.pinIsDriven(p4dr, 2), isFalse);
    });
  });

  test('executing code sees the held pin', () {
    final cpu = H8Cpu();
    // BTST #1,@H'FFFFCF:8 ; BEQ +2 ; (fall through sets R0L)
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    var a = 0x1000;
    for (final b in [
      0x7E, 0xCF, 0x73, 0x10, // BTST #1,@H'FFFFCF:8
      0x47, 0x02, //             BEQ +2
      0xF8, 0x01, //             MOV.B #1,R0L
    ]) {
      cpu.mem.poke(a++, b);
    }
    cpu.reset();
    cpu.writeB(0xFFFFCD, 0x00); // P8DDR: all inputs
    cpu.writeB(0xFFFFCF, 0x00); // P8DR clear, so the bit would read 0
    cpu.setPin(0xFFFFCF, 1, true);
    cpu.step(); // BTST
    cpu.step(); // BEQ, not taken because the pin is high
    cpu.step(); // MOV
    expect(cpu.er[0] & 0xFF, 1, reason: 'the held pin steered the branch');
  });
}
