// Tests for the DMA controller: register layout from section 8 of the
// hardware manual, and transfers actually happening when the programmed
// activation source fires.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/dmac.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/itu.dart';
import 'package:sim_h83003/sci.dart';

/// Writes a 24-bit memory address register (MARxxE/H/L).
void setMar(H8Cpu cpu, int halfBase, int addr) {
  cpu.writeB(halfBase + 1, (addr >> 16) & 0xFF);
  cpu.writeB(halfBase + 2, (addr >> 8) & 0xFF);
  cpu.writeB(halfBase + 3, addr & 0xFF);
}

/// Writes a 16-bit transfer count register.
void setEtcr(H8Cpu cpu, int halfBase, int count) {
  cpu.writeB(halfBase + 4, (count >> 8) & 0xFF);
  cpu.writeB(halfBase + 5, count & 0xFF);
}

void main() {
  group('registers', () {
    test('channels sit at their documented addresses', () {
      final d = Dmac();
      expect(d.channels.length, 4);
      expect(d.channels[0].base, 0xFFFF20);
      expect(d.channels[1].base, 0xFFFF30);
      expect(d.channels[2].base, 0xFFFF40);
      expect(d.channels[3].base, 0xFFFF50);
      expect(d.owns(0xFFFF20), isTrue);
      expect(d.owns(0xFFFF5F), isTrue);
      expect(d.owns(0xFFFF60), isFalse); // that is the ITU
    });

    test('MAR is 24 bits across three writable bytes', () {
      final cpu = H8Cpu();
      cpu.reset();
      setMar(cpu, 0xFFFF20, 0x123456);
      expect(cpu.dmac.channels[0].a.mar, 0x123456);
      expect(cpu.readB(0xFFFF21), 0x12);
      expect(cpu.readB(0xFFFF22), 0x34);
      expect(cpu.readB(0xFFFF23), 0x56);
      // MARxxR is reserved and reads as all ones.
      expect(cpu.readB(0xFFFF20), 0xFF);
    });

    test('DEND vectors follow the channel', () {
      final d = Dmac();
      expect(d.channels[0].vectorBase, 44); // DEND0A
      expect(d.channels[1].vectorBase, 46);
      expect(d.channels[3].vectorBase, 50);
    });

    test('full address mode is selected by DTS2A and DTS1A', () {
      final d = Dmac();
      d.channels[0].a.dtcr = 0x06;
      expect(d.channels[0].fullAddress, isTrue);
      d.channels[0].a.dtcr = 0x04; // SCI transmit: short address mode
      expect(d.channels[0].fullAddress, isFalse);
      expect(d.channels[0].modeName, 'short address');
    });

    test('register values are mirrored into memory for the views', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFF26, 0xB3); // IOAR0A
      expect(cpu.mem.peek(0xFFFF26), 0xB3);
    });
  });

  group('short address mode', () {
    test('an ITU compare match moves a byte and advances MAR', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.mem.poke(0x2000, 0x5A);
      setMar(cpu, 0xFFFF20, 0x2000);
      setEtcr(cpu, 0xFFFF20, 2);
      cpu.writeB(0xFFFF26, 0x10); // IOAR0A -> H'FFFF10 (plain memory)
      // DTE, byte, increment, I/O mode, activated by ITU channel 0 match A.
      cpu.writeB(0xFFFF27, DmacControl.dte | DmacSource.ituMatch0);

      // Nothing happens until the timer signals.
      cpu.dmac.service();
      expect(cpu.dmac.transferCount, 0);

      cpu.itu.channels[0].tsr |= ItuStatus.imfa;
      cpu.dmac.service();
      expect(cpu.mem.peek(0xFFFF10), 0x5A);
      expect(cpu.dmac.channels[0].a.mar, 0x2001);
      expect(cpu.dmac.channels[0].a.etcr, 1);
      // The transfer consumed the compare-match flag.
      expect(cpu.itu.channels[0].tsr & ItuStatus.imfa, 0);
    });

    test('DTE clears when the count runs out, and DEND is requested', () {
      final cpu = H8Cpu();
      cpu.reset();
      setMar(cpu, 0xFFFF20, 0x2000);
      setEtcr(cpu, 0xFFFF20, 1);
      cpu.writeB(0xFFFF26, 0x10);
      cpu.writeB(0xFFFF27,
          DmacControl.dte | DmacControl.dtie | DmacSource.ituMatch0);

      cpu.itu.channels[0].tsr |= ItuStatus.imfa;
      final vector = cpu.dmac.service();
      expect(cpu.dmac.channels[0].a.enabled, isFalse, reason: 'DTE clears');
      expect(vector, 44, reason: 'DEND0A');
    });

    test('repeat mode reloads MAR and the count', () {
      final cpu = H8Cpu();
      cpu.reset();
      setMar(cpu, 0xFFFF20, 0x2000);
      setEtcr(cpu, 0xFFFF20, 1);
      cpu.writeB(0xFFFF26, 0x10);
      cpu.writeB(0xFFFF27,
          DmacControl.dte | DmacControl.rpe | DmacSource.ituMatch0);

      cpu.itu.channels[0].tsr |= ItuStatus.imfa;
      cpu.dmac.service();
      expect(cpu.dmac.channels[0].a.enabled, isTrue, reason: 'still running');
      expect(cpu.dmac.channels[0].a.mar, 0x2000, reason: 'MAR reloaded');
      expect(cpu.dmac.channels[0].a.etcr, 1, reason: 'count reloaded');
    });

    test('DTID makes MAR count down', () {
      final cpu = H8Cpu();
      cpu.reset();
      setMar(cpu, 0xFFFF20, 0x2000);
      setEtcr(cpu, 0xFFFF20, 4);
      cpu.writeB(0xFFFF26, 0x10);
      cpu.writeB(0xFFFF27,
          DmacControl.dte | DmacControl.dtid | DmacSource.ituMatch0);
      cpu.itu.channels[0].tsr |= ItuStatus.imfa;
      cpu.dmac.service();
      expect(cpu.dmac.channels[0].a.mar, 0x1FFF);
    });

    test('a transmit channel feeds the SCI and clears TDRE', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.mem.poke(0x3000, 0x41); // 'A'
      cpu.mem.poke(0x3001, 0x42); // 'B'
      setMar(cpu, 0xFFFF20, 0x3000);
      setEtcr(cpu, 0xFFFF20, 2);
      cpu.writeB(0xFFFF26, 0xB3); // IOAR0A -> TDR0
      cpu.writeB(0xFFFF27, DmacControl.dte | DmacSource.sciTransmit);
      cpu.writeB(0xFFFFB1, 17); // BRR0
      cpu.writeB(0xFFFFB2, SciControl.te);

      // TDRE is set after reset, so the channel is triggered straight away.
      cpu.dmac.service();
      expect(cpu.sci0.tdr, 0x41);
      expect(cpu.sci0.ssr & SciStatus.tdre, 0, reason: 'DMAC clears TDRE');

      // The SCI then shifts it out and frees TDR for the next byte.
      cpu.sci0.tick(cpu.cycles);
      expect(cpu.sci0.txLog, [0x41]);
      cpu.dmac.service();
      expect(cpu.sci0.tdr, 0x42);
    });

    test('a receive channel takes bytes from the SCI into memory', () {
      final cpu = H8Cpu();
      cpu.reset();
      setMar(cpu, 0xFFFF28, 0x4000); // channel 0B
      setEtcr(cpu, 0xFFFF28, 2);
      cpu.writeB(0xFFFF2E, 0xB5); // IOAR0B -> RDR0
      cpu.writeB(0xFFFF2F, DmacControl.dte | DmacSource.sciReceive);
      cpu.writeB(0xFFFFB2, SciControl.re);

      cpu.sci0.receive([0x77]);
      cpu.sci0.tick(cpu.cycles); // delivers into RDR, sets RDRF
      cpu.dmac.service();
      expect(cpu.mem.peek(0x4000), 0x77);
      expect(cpu.sci0.ssr & SciStatus.rdrf, 0, reason: 'DMAC clears RDRF');
      expect(cpu.dmac.channels[0].b.mar, 0x4001);
    });
  });

  group('full address mode', () {
    test('auto-request burst copies a whole block', () {
      final cpu = H8Cpu();
      cpu.reset();
      for (var i = 0; i < 8; i++) {
        cpu.mem.poke(0x5000 + i, 0x10 + i);
      }
      setMar(cpu, 0xFFFF20, 0x5000); // MARA: source
      setMar(cpu, 0xFFFF28, 0x6000); // MARB: destination
      setEtcr(cpu, 0xFFFF20, 8);
      // DTE, source increments, full address mode, normal transfer.
      cpu.writeB(0xFFFF27,
          DmacControl.dte | DmacControlA.saide | DmacControl.fullAddress);
      // DTME, destination increments, auto-request burst.
      cpu.writeB(0xFFFF2F, DmacControlB.dtme | DmacControlB.daide);

      expect(cpu.dmac.channels[0].fullEnabled, isTrue);
      cpu.dmac.service();

      for (var i = 0; i < 8; i++) {
        expect(cpu.mem.peek(0x6000 + i), 0x10 + i, reason: 'byte $i');
      }
      expect(cpu.dmac.channels[0].a.enabled, isFalse, reason: 'count spent');
      expect(cpu.dmac.channels[0].modeName, 'full address, normal');
    });

    test('a fixed destination address leaves MARB alone', () {
      final cpu = H8Cpu();
      cpu.reset();
      for (var i = 0; i < 4; i++) {
        cpu.mem.poke(0x5000 + i, 0xA0 + i);
      }
      setMar(cpu, 0xFFFF20, 0x5000);
      setMar(cpu, 0xFFFF28, 0x6000);
      setEtcr(cpu, 0xFFFF20, 4);
      cpu.writeB(0xFFFF27,
          DmacControl.dte | DmacControlA.saide | DmacControl.fullAddress);
      cpu.writeB(0xFFFF2F, DmacControlB.dtme); // DAIDE clear: fixed
      cpu.dmac.service();
      expect(cpu.dmac.channels[0].b.mar, 0x6000);
      expect(cpu.mem.peek(0x6000), 0xA3, reason: 'last byte written');
    });

    test('nothing moves until both DTE and DTME are set', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.mem.poke(0x5000, 0x99);
      setMar(cpu, 0xFFFF20, 0x5000);
      setMar(cpu, 0xFFFF28, 0x6000);
      setEtcr(cpu, 0xFFFF20, 1);
      cpu.writeB(0xFFFF27, DmacControl.dte | DmacControl.fullAddress);
      cpu.dmac.service(); // DTME still clear
      expect(cpu.mem.peek(0x6000), 0);
      cpu.writeB(0xFFFF2F, DmacControlB.dtme);
      cpu.dmac.service();
      expect(cpu.mem.peek(0x6000), 0x99);
    });
  });

  group('running under the CPU', () {
    test('a programmed channel transfers while the CPU executes', () {
      final cpu = H8Cpu();
      // Reset vector -> H'1000: SLEEP, so the CPU idles while DMA runs.
      cpu.mem.poke(2, 0x10);
      cpu.mem.poke(3, 0x00);
      cpu.mem.poke(0x1000, 0x01);
      cpu.mem.poke(0x1001, 0x80);
      cpu.reset();
      for (var i = 0; i < 4; i++) {
        cpu.mem.poke(0x7000 + i, 0xC0 + i);
      }
      setMar(cpu, 0xFFFF20, 0x7000);
      setMar(cpu, 0xFFFF28, 0x7100);
      setEtcr(cpu, 0xFFFF20, 4);
      cpu.writeB(0xFFFF27,
          DmacControl.dte | DmacControlA.saide | DmacControl.fullAddress);
      cpu.writeB(0xFFFF2F, DmacControlB.dtme | DmacControlB.daide);

      for (var i = 0; i < 20; i++) {
        cpu.step();
      }
      for (var i = 0; i < 4; i++) {
        expect(cpu.mem.peek(0x7100 + i), 0xC0 + i);
      }
      expect(cpu.dmac.transferCount, 4);
    });
  });
}
