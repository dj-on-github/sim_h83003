// Tests for the A/D converter: register semantics from section 14 of the
// hardware manual, conversion timing, and the channel-to-result-register
// mapping the artista 180's touch panel relies on.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/adc.dart';
import 'package:sim_h83003/h8300h.dart';

void main() {
  group('registers', () {
    test('reset values and addresses match the manual', () {
      final cpu = H8Cpu();
      cpu.reset();
      expect(cpu.readB(0xFFFFE8), 0x00); // ADCSR
      expect(cpu.adc.owns(0xFFFFE0), isTrue);
      expect(cpu.adc.owns(0xFFFFE9), isTrue);
      expect(cpu.adc.owns(0xFFFFEA), isFalse);
    });

    test('channels map to result registers as the hardware does', () {
      // ADDRA holds AN0 or AN4, ADDRB AN1/AN5, ADDRC AN2/AN6, ADDRD AN3/AN7.
      expect(AdConverter.registerFor(0), 0);
      expect(AdConverter.registerFor(4), 0);
      expect(AdConverter.registerFor(1), 1);
      expect(AdConverter.registerFor(5), 1);
      expect(AdConverter.registerFor(2), 2);
      expect(AdConverter.registerFor(6), 2);
      expect(AdConverter.registerFor(3), 3);
      expect(AdConverter.registerFor(7), 3);
    });

    test('ADF can be cleared but not set by software', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.adc.adcsr |= AdcStatus.adf;
      cpu.writeB(0xFFFFE8, 0xFF); // must not keep setting it deliberately
      expect(cpu.readB(0xFFFFE8) & AdcStatus.adf, AdcStatus.adf,
          reason: 'writing 1 leaves it as it was');
      cpu.writeB(0xFFFFE8, 0xFF & ~AdcStatus.adf);
      expect(cpu.readB(0xFFFFE8) & AdcStatus.adf, 0);
    });

    test('result registers are read-only and left-justified', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.adc.setInput(2, 0x3FF); // full scale on AN2
      cpu.writeB(0xFFFFE8, AdcStatus.adst | 2); // single mode, AN2
      cpu.adc.tick(cpu.cycles + 300);
      // 10 bits left-justified in 16: high byte is the top 8 bits.
      expect(cpu.readB(0xFFFFE4), 0xFF); // ADDRCH
      cpu.writeB(0xFFFFE4, 0x00); // read-only
      expect(cpu.readB(0xFFFFE4), 0xFF);
    });
  });

  group('conversion', () {
    test('single mode converts once and stops, setting ADF', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.adc.setInput8(4, 0x9A);
      cpu.writeB(0xFFFFE8, AdcStatus.adst | 4); // start AN4

      // Not finished before the conversion time has elapsed.
      cpu.adc.tick(cpu.cycles + 100);
      expect(cpu.adc.adcsr & AdcStatus.adf, 0);

      cpu.adc.tick(cpu.cycles + 266);
      expect(cpu.adc.adcsr & AdcStatus.adf, AdcStatus.adf);
      expect(cpu.adc.adcsr & AdcStatus.adst, 0, reason: 'stops after one');
      expect(cpu.readB(0xFFFFE0), 0x9A); // ADDRAH, since AN4 -> ADDRA
    });

    test('CKS selects the shorter conversion time', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFE8, AdcStatus.adst | AdcStatus.cks);
      expect(cpu.adc.conversionStates, 134);
      cpu.adc.tick(cpu.cycles + 133);
      expect(cpu.adc.adcsr & AdcStatus.adf, 0);
      cpu.adc.tick(cpu.cycles + 134);
      expect(cpu.adc.adcsr & AdcStatus.adf, AdcStatus.adf);
    });

    test('scan mode converts the group and keeps running', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.adc.setInput8(0, 0x11);
      cpu.adc.setInput8(1, 0x22);
      cpu.adc.setInput8(2, 0x33);
      // Scan AN0..AN2.
      cpu.writeB(0xFFFFE8, AdcStatus.adst | AdcStatus.scan | 2);
      cpu.adc.tick(cpu.cycles + 266);
      expect(cpu.readB(0xFFFFE0), 0x11);
      expect(cpu.readB(0xFFFFE2), 0x22);
      expect(cpu.readB(0xFFFFE4), 0x33);
      expect(cpu.adc.adcsr & AdcStatus.adst, AdcStatus.adst,
          reason: 'scan keeps going until ADST is cleared');
    });

    test('clearing ADST abandons the conversion', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.adc.setInput8(0, 0x55);
      cpu.writeB(0xFFFFE8, AdcStatus.adst);
      cpu.writeB(0xFFFFE8, 0); // stop
      cpu.adc.tick(cpu.cycles + 1000);
      expect(cpu.adc.adcsr & AdcStatus.adf, 0);
      expect(cpu.readB(0xFFFFE0), 0x00);
    });
  });

  group('interrupts', () {
    test('ADI is requested only when ADIE is set', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.writeB(0xFFFFE8, AdcStatus.adst);
      cpu.adc.tick(cpu.cycles + 266);
      expect(cpu.adc.pendingVector(), isNull);
      cpu.writeB(0xFFFFE8, cpu.adc.adcsr | AdcStatus.adie);
      expect(cpu.adc.pendingVector(), 60); // ADI
    });

    test('a conversion runs and vectors while the CPU executes', () {
      final cpu = H8Cpu();
      cpu.mem.poke(2, 0x10);
      cpu.mem.poke(3, 0x00);
      cpu.mem.poke(0x1000, 0x01);
      cpu.mem.poke(0x1001, 0x80); // SLEEP
      cpu.mem.poke(60 * 4 + 2, 0x20); // ADI vector -> H'2000
      cpu.mem.poke(60 * 4 + 3, 0x00);
      cpu.reset();
      cpu.er[7] = 0x00FFFF00;
      cpu.ccr = 0;
      cpu.adc.setInput8(6, 0x7E);
      cpu.writeB(0xFFFFE8, AdcStatus.adst | AdcStatus.adie | 6);

      for (var i = 0; i < 400 && cpu.pc != 0x2000; i++) {
        cpu.step();
      }
      expect(cpu.pc, 0x2000, reason: 'the end-of-conversion should vector');
      expect(cpu.readB(0xFFFFE4), 0x7E, reason: 'AN6 lands in ADDRC');
    });
  });

  group('touch panel inputs', () {
    test('AN4 and AN6 carry the two touch axes independently', () {
      final cpu = H8Cpu();
      cpu.reset();
      cpu.adc.setInput8(4, 0x40); // X
      cpu.adc.setInput8(6, 0xC0); // Y
      cpu.writeB(0xFFFFE8, AdcStatus.adst | 4);
      cpu.adc.tick(cpu.cycles + 266);
      cpu.writeB(0xFFFFE8, AdcStatus.adst | 6);
      cpu.adc.tick(cpu.cycles + 600);
      expect(cpu.readB(0xFFFFE0), 0x40, reason: 'AN4 -> ADDRAH');
      expect(cpu.readB(0xFFFFE4), 0xC0, reason: 'AN6 -> ADDRCH');
    });

    test('setInput8 round-trips the value firmware reads', () {
      final adc = AdConverter();
      // A conversion starts when ADCSR is written, so drive it through the
      // register rather than poking the field.
      var cycles = 0;
      for (final v in [0, 1, 0x4C, 0x80, 0xFF]) {
        adc.setInput8(4, v);
        adc.write(0xFFFFE8, AdcStatus.adst | 4);
        cycles += 300;
        adc.tick(cycles);
        expect(adc.result8(4), v, reason: 'value H\'${v.toRadixString(16)}');
      }
    });
  });
}
