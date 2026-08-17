// Tests for the ITU model: register semantics from section 10 of the
// hardware manual, counter behaviour, and the interrupts a timer raises.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/itu.dart';

void main() {
  group('registers', () {
    test('reset values match the manual', () {
      final cpu = H8Cpu();
      cpu.reset();
      expect(cpu.readB(0xFFFF60), 0xE0); // TSTR
      expect(cpu.readB(0xFFFF64), 0x80); // TCR0
      expect(cpu.readB(0xFFFF66) & 0x07, 0); // TIER0 enables clear
      expect(cpu.readB(0xFFFF67) & 0x07, 0); // TSR0 flags clear
      expect(cpu.itu.channels.length, 5);
    });

    test('every channel sits at its documented address', () {
      final itu = Itu();
      // TCR of each channel, from the register list in appendix B.
      const tcrAddrs = [0xFFFF64, 0xFFFF6E, 0xFFFF78, 0xFFFF82, 0xFFFF92];
      for (var i = 0; i < 5; i++) {
        itu.write(tcrAddrs[i], 0x03 | (i << 4) & 0);
        expect(itu.channels[i].tcr & 0x07, 3,
            reason: 'channel $i TCR at ${tcrAddrs[i].toRadixString(16)}');
      }
      // Channels 3 and 4 have buffer registers; 0-2 do not.
      expect(itu.channels[2].hasBuffers, isFalse);
      expect(itu.channels[3].hasBuffers, isTrue);
      expect(itu.channels[4].hasBuffers, isTrue);
    });

    test('16-bit registers are accessible as high and low bytes', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFF6A, 0x12); // GRA0H
      cpu.writeB(0xFFFF6B, 0x34); // GRA0L
      expect(cpu.itu.channels[0].gra, 0x1234);
      cpu.writeB(0xFFFF68, 0xAB); // TCNT0H
      cpu.writeB(0xFFFF69, 0xCD);
      expect(cpu.itu.channels[0].tcnt, 0xABCD);
      expect(cpu.readB(0xFFFF68), 0xAB);
      expect(cpu.readB(0xFFFF69), 0xCD);
    });

    test('TSR flags can be cleared but not set', () {
      final cpu = H8Cpu();
      cpu.reset();
      final ch = cpu.itu.channels[0];
      ch.tsr |= ItuStatus.imfa;
      cpu.writeB(0xFFFF67, 0xFF); // writing ones must not set anything
      expect(cpu.readB(0xFFFF67) & ItuStatus.imfa, ItuStatus.imfa);
      cpu.writeB(0xFFFF67, 0xFF & ~ItuStatus.imfa); // zero clears
      expect(cpu.readB(0xFFFF67) & ItuStatus.imfa, 0);
    });

    test('register values are mirrored into memory for the views', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFF6A, 0x55);
      expect(cpu.mem.peek(0xFFFF6A), 0x55);
    });
  });

  group('counting', () {
    test('a counter only runs when its TSTR bit is set', () {
      final cpu = H8Cpu();
      cpu.reset();
      final ch = cpu.itu.channels[0];
      cpu.itu.tick(1000);
      expect(ch.tcnt, 0);
      cpu.writeB(0xFFFF60, 0x01); // start channel 0
      cpu.itu.tick(2000);
      expect(ch.tcnt, 1000); // phi/1
    });

    test('the prescaler divides the system clock', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFF64, 0x03); // TCR0: internal clock phi/8
      cpu.writeB(0xFFFF60, 0x01);
      cpu.itu.tick(800);
      expect(cpu.itu.channels[0].tcnt, 100);
      expect(cpu.itu.channels[0].clockSource, 'φ/8');
    });

    test('an external clock source leaves the counter still', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFF64, 0x04); // TCLKA — not modelled
      cpu.writeB(0xFFFF60, 0x01);
      cpu.itu.tick(10000);
      expect(cpu.itu.channels[0].tcnt, 0);
      expect(cpu.itu.channels[0].divisor, isNull);
    });

    test('compare match sets IMFA and can clear the counter', () {
      final cpu = H8Cpu();
      cpu.reset();
      final ch = cpu.itu.channels[0];
      cpu.writeB(0xFFFF6A, 0x00); // GRA0 = 100
      cpu.writeB(0xFFFF6B, 100);
      cpu.writeB(0xFFFF64, 0x80 | ItuControl.cclrGra); // clear on GRA match
      cpu.writeB(0xFFFF60, 0x01);
      cpu.itu.tick(100);
      expect(ch.tsr & ItuStatus.imfa, ItuStatus.imfa);
      expect(ch.tcnt, 0, reason: 'CCLR = GRA match should zero the counter');
      expect(ch.clearSource, 'GRA match');
    });

    test('a free-running counter overflows and sets OVF', () {
      final cpu = H8Cpu();
      cpu.reset();
      final ch = cpu.itu.channels[1];
      cpu.writeB(0xFFFF60, 0x02); // start channel 1
      cpu.itu.tick(0x10000);
      expect(ch.tcnt, 0);
      expect(ch.tsr & ItuStatus.ovf, ItuStatus.ovf);
    });

    test('counting is exact across a long interval with a match inside', () {
      final cpu = H8Cpu();
      cpu.reset();
      final ch = cpu.itu.channels[2];
      cpu.writeB(0xFFFF7E, 0x00); // GRA2 = 500
      cpu.writeB(0xFFFF7F, 0xF4);
      cpu.writeB(0xFFFF60, 0x04);
      cpu.itu.tick(1234); // well past the match, no counter clear
      expect(ch.tcnt, 1234);
      expect(ch.tsr & ItuStatus.imfa, ItuStatus.imfa);
    });
  });

  group('interrupts', () {
    test('a compare match requests IMIA only when enabled', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFF6A, 0x00);
      cpu.writeB(0xFFFF6B, 10); // GRA0 = 10
      cpu.writeB(0xFFFF60, 0x01);
      cpu.itu.tick(10);
      expect(cpu.itu.pendingVector(), isNull, reason: 'IMIEA is clear');
      cpu.writeB(0xFFFF66, ItuInterrupt.imiea);
      expect(cpu.itu.pendingVector(), 24); // IMIA0
    });

    test('each channel uses its own vector block', () {
      final itu = Itu();
      expect(itu.channels[0].vectorBase, 24); // IMIA0
      expect(itu.channels[1].vectorBase, 28);
      expect(itu.channels[2].vectorBase, 32);
      expect(itu.channels[3].vectorBase, 36); // IMIA3, used by the Bernina
      expect(itu.channels[4].vectorBase, 40); // IMIA4
    });

    test('a timer interrupt vectors through the table while running', () {
      final cpu = H8Cpu();
      // Reset vector -> H'1000, which just sleeps.
      cpu.mem.poke(2, 0x10);
      cpu.mem.poke(3, 0x00);
      cpu.mem.poke(0x1000, 0x01);
      cpu.mem.poke(0x1001, 0x80); // SLEEP
      // IMIA0 is vector 24 -> H'60.
      cpu.mem.poke(24 * 4 + 2, 0x20);
      cpu.mem.poke(24 * 4 + 3, 0x00);
      cpu.reset();
      cpu.er[7] = 0x00FFFF00;
      cpu.ccr = 0; // interrupts enabled

      cpu.writeB(0xFFFF6A, 0x00);
      cpu.writeB(0xFFFF6B, 0x20); // GRA0 = 32
      cpu.writeB(0xFFFF66, ItuInterrupt.imiea); // enable IMIA
      cpu.writeB(0xFFFF60, 0x01); // start

      for (var i = 0; i < 200 && cpu.pc != 0x2000; i++) {
        cpu.step();
      }
      expect(cpu.pc, 0x2000, reason: 'the compare match should vector');
      expect(cpu.halted, isFalse, reason: 'and wake the sleeping CPU');
    });
  });
}
