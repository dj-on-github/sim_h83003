// Tests for the SCI model: the register semantics from section 13 of the
// hardware manual, and the polled-transmit sequence firmware actually uses.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/sci.dart';

/// Runs [n] instructions (or until halted).
void run(H8Cpu cpu, int n) {
  for (var i = 0; i < n && !cpu.halted; i++) {
    cpu.step();
  }
}

/// Loads a program at H'1000 with the reset vector pointing at it.
H8Cpu cpuWithProgram(List<int> code) {
  final cpu = H8Cpu();
  const start = 0x1000;
  cpu.mem.poke(2, start >> 8);
  cpu.mem.poke(3, start & 0xFF);
  for (var i = 0; i < code.length; i++) {
    cpu.mem.poke(start + i, code[i]);
  }
  cpu.reset();
  cpu.er[7] = 0x00FFFF00;
  return cpu;
}

void main() {
  group('registers', () {
    test('reset values match the manual', () {
      final cpu = H8Cpu();
      cpu.reset();
      final s = cpu.sci0;
      expect(s.smr, 0x00);
      expect(s.brr, 0xFF);
      expect(s.scr, 0x00);
      expect(s.tdr, 0xFF);
      expect(s.ssr, 0x84); // TDRE | TEND
      expect(cpu.readB(0xFFFFB4), 0x84);
    });

    test('the two channels are at the documented addresses', () {
      final cpu = H8Cpu();
      expect(cpu.sci0.smrAddr, 0xFFFFB0);
      expect(cpu.sci0.rdrAddr, 0xFFFFB5);
      expect(cpu.sci1.smrAddr, 0xFFFFB8);
      expect(cpu.sci1.rdrAddr, 0xFFFFBD);
      // The gap between the channels is not owned by either.
      expect(cpu.sci0.owns(0xFFFFB6), isFalse);
      expect(cpu.sci1.owns(0xFFFFB6), isFalse);
    });

    test('SSR status flags can be cleared but never set by the CPU', () {
      final cpu = H8Cpu();
      cpu.reset();
      // Writing all ones must not set RDRF or the error flags.
      cpu.writeB(0xFFFFB4, 0xFF);
      expect(cpu.readB(0xFFFFB4) & SciStatus.rdrf, 0);
      expect(cpu.readB(0xFFFFB4) & SciStatus.orer, 0);
      // TDRE was set; writing a zero in that position clears it.
      expect(cpu.readB(0xFFFFB4) & SciStatus.tdre, SciStatus.tdre);
      cpu.writeB(0xFFFFB4, 0xFF & ~SciStatus.tdre);
      expect(cpu.readB(0xFFFFB4) & SciStatus.tdre, 0);
    });

    test('register values are mirrored into memory for the views', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFB1, 0x11); // BRR
      expect(cpu.mem.peek(0xFFFFB1), 0x11);
      expect(cpu.mem.peek(0xFFFFB4), cpu.sci0.ssr);
    });
  });

  group('bit rate', () {
    test('states per bit follow BRR and the clock select', () {
      final s = SciChannel(name: 'x', base: 0xFFFFB0, vectorBase: 52);
      s.smr = 0x00; // CKS = 0
      s.brr = 17;
      expect(s.statesPerBit, 32 * 18); // 576
      expect(s.bitsPerChar, 10); // start + 8 data + 1 stop
      expect(s.statesPerChar, 5760);
      // 11.0592 MHz / 576 = 19200 baud.
      expect(s.baudAt(11059200), closeTo(19200, 0.5));
      s.smr = 0x01; // CKS = 1 -> divide by four times as much
      expect(s.statesPerBit, 128 * 18);
    });

    test('framing string reflects SMR', () {
      final s = SciChannel(name: 'x', base: 0xFFFFB0, vectorBase: 52);
      s.smr = 0x00;
      expect(s.framing, '8N1');
      s.smr = SciMode.pe | SciMode.oe | SciMode.stop | SciMode.chr;
      expect(s.framing, '7O2');
    });
  });

  group('transmission', () {
    // The polled sequence the Bernina boot ROM uses: wait for TDRE, write
    // the byte, clear TDRE. Without a working SCI this spins forever.
    List<int> pollingTransmit(int byte) => [
          0xF6, byte, //            MOV.B #byte,R6H
          0x7E, 0xB4, 0x73, 0x70, // wait: BTST #7,@SSR0:8
          0x47, 0xFA, //            BEQ wait
          0x36, 0xB3, //            MOV.B R6H,@TDR0:8
          0x7F, 0xB4, 0x72, 0x70, // BCLR #7,@SSR0:8
          0x01, 0x80, //            SLEEP
        ];

    test('a polled transmit completes and the byte is logged', () {
      final cpu = cpuWithProgram(pollingTransmit(0x5A));
      cpu.writeB(0xFFFFB1, 17); // BRR
      cpu.writeB(0xFFFFB2, SciControl.te); // enable the transmitter
      run(cpu, 200);
      expect(cpu.halted, isTrue, reason: 'should reach SLEEP, not spin');
      expect(cpu.sci0.txLog, [0x5A]);
      expect(cpu.sci0.txCount, 1);
    });

    test('TDRE comes back and TEND follows a whole character time', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFB1, 17);
      cpu.writeB(0xFFFFB2, SciControl.te);
      cpu.writeB(0xFFFFB3, 0xA5); // TDR
      cpu.writeB(0xFFFFB4, ~SciStatus.tdre & 0xFF); // clear TDRE -> transmit

      cpu.sci0.tick(cpu.cycles);
      // TDR has been copied into the shift register: free again, but the
      // character is still going out.
      expect(cpu.sci0.ssr & SciStatus.tdre, SciStatus.tdre);
      expect(cpu.sci0.ssr & SciStatus.tend, 0);
      expect(cpu.sci0.transmitting, isTrue);
      expect(cpu.sci0.txLog, [0xA5]);

      // Not finished a bit later...
      cpu.sci0.tick(cpu.cycles + 100);
      expect(cpu.sci0.ssr & SciStatus.tend, 0);
      // ...but finished after a full character.
      cpu.sci0.tick(cpu.cycles + cpu.sci0.statesPerChar);
      expect(cpu.sci0.ssr & SciStatus.tend, SciStatus.tend);
      expect(cpu.sci0.transmitting, isFalse);
    });

    test('nothing is transmitted while TE is clear', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFB3, 0x42);
      cpu.writeB(0xFFFFB4, ~SciStatus.tdre & 0xFF);
      for (var i = 0; i < 10; i++) {
        cpu.sci0.tick(cpu.cycles + i * 1000);
      }
      expect(cpu.sci0.txLog, isEmpty);
    });

    test('clearing TE parks the transmitter', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFB2, SciControl.te);
      cpu.writeB(0xFFFFB3, 0x01);
      cpu.writeB(0xFFFFB4, ~SciStatus.tdre & 0xFF);
      cpu.sci0.tick(cpu.cycles);
      cpu.writeB(0xFFFFB2, 0); // TE off
      expect(cpu.sci0.ssr & SciStatus.tdre, SciStatus.tdre);
      expect(cpu.sci0.ssr & SciStatus.tend, SciStatus.tend);
    });

    test('the two channels are independent', () {
      final cpu = H8Cpu();
      cpu.reset();
      for (final addr in [0xFFFFB2, 0xFFFFBA]) {
        cpu.writeB(addr, SciControl.te);
      }
      cpu.writeB(0xFFFFB3, 0x11); // TDR0
      cpu.writeB(0xFFFFB4, ~SciStatus.tdre & 0xFF);
      cpu.writeB(0xFFFFBB, 0x22); // TDR1
      cpu.writeB(0xFFFFBC, ~SciStatus.tdre & 0xFF);
      cpu.sci0.tick(cpu.cycles);
      cpu.sci1.tick(cpu.cycles);
      expect(cpu.sci0.txLog, [0x11]);
      expect(cpu.sci1.txLog, [0x22]);
    });
  });

  group('reception and interrupts', () {
    test('a queued byte appears in RDR and sets RDRF', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFB1, 17);
      cpu.writeB(0xFFFFB2, SciControl.re);
      cpu.sci0.receive([0x7E]);
      cpu.sci0.tick(cpu.cycles);
      expect(cpu.sci0.ssr & SciStatus.rdrf, SciStatus.rdrf);
      expect(cpu.readB(0xFFFFB5), 0x7E);
    });

    test('RXI is requested only when RIE is set', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFB2, SciControl.re);
      cpu.sci0.receive([0x01]);
      cpu.sci0.tick(cpu.cycles);
      expect(cpu.sci0.pendingVector(), isNull);
      cpu.writeB(0xFFFFB2, SciControl.re | SciControl.rie);
      expect(cpu.sci0.pendingVector(), 53); // RXI0
    });

    test('TXI is requested while TIE is set and TDRE is up', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFB2, SciControl.te | SciControl.tie);
      expect(cpu.sci0.pendingVector(), 54); // TXI0
      expect(cpu.sci1.pendingVector(), isNull);
    });

    test('an SCI interrupt vectors through the table and wakes SLEEP', () {
      final cpu = cpuWithProgram([0x01, 0x80]); // SLEEP
      // RXI0 is vector 53, so its entry is at H'D4.
      cpu.mem.poke(53 * 4 + 2, 0x20);
      cpu.mem.poke(53 * 4 + 3, 0x00);
      cpu.writeB(0xFFFFB2, SciControl.re | SciControl.rie);
      cpu.sci0.receive([0x99]);

      // Reset leaves the I bit set, so the byte arrives but the interrupt is
      // masked and the CPU goes to sleep.
      cpu.step();
      expect(cpu.halted, isTrue);
      expect(cpu.sleeping, isTrue);
      expect(cpu.sci0.pendingVector(), 53);

      // Unmasking lets it through, which wakes the CPU and vectors.
      cpu.ccr = 0;
      cpu.step();
      expect(cpu.halted, isFalse, reason: 'the interrupt should wake it');
      expect(cpu.sleeping, isFalse);
      expect(cpu.pc, 0x2000);
    });
  });
}
