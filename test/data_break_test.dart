// Data breakpoints report which address stopped the run.
//
// A watchpoint stops after the access completes, so the PC has already moved
// on. In the artista 180's A/D routines the next instruction writes ADCSR —
// an address that was never watched — so without the recorded address the
// halt looks like it happened somewhere it did not.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';

/// Assembles: MOV.B @H'FFFFE0:8,R5L ; MOV.B R5H,@H'FFFFE8:8
/// which is the shape of adc_start_ch0_ch4 at H'200954.
void loadReadThenWrite(H8Cpu cpu, int at) {
  var a = at;
  for (final b in [0x2D, 0xE0, 0x3D, 0xE8]) {
    cpu.mem.poke(a++, b);
  }
}

void main() {
  test('the tripped address and instruction are recorded', () {
    final cpu = H8Cpu();
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    loadReadThenWrite(cpu, 0x1000);
    cpu.reset();
    cpu.dataBreaks.add(0xFFFFE0); // ADDRAH only, not ADCSR

    cpu.clearBreakHit();
    cpu.step(); // the read
    expect(cpu.breakHit, isTrue);
    expect(cpu.breakAddr, 0xFFFFE0);
    expect(cpu.breakPc, 0x1000, reason: 'the instruction that read it');
    expect(cpu.pc, 0x1002, reason: 'the PC has already moved past it');
  });

  test('the next instruction touches an unwatched register and does not trip',
      () {
    final cpu = H8Cpu();
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    loadReadThenWrite(cpu, 0x1000);
    cpu.reset();
    cpu.dataBreaks.add(0xFFFFE0);

    cpu.clearBreakHit();
    cpu.step(); // read ADDRAH: trips
    cpu.clearBreakHit();
    cpu.step(); // write ADCSR: must not trip
    expect(cpu.breakHit, isFalse);
    expect(cpu.breakAddr, isNull);
  });

  test('a write trips it too', () {
    final cpu = H8Cpu();
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    // MOV.B R5H,@H'FFFFE8:8
    cpu.mem.poke(0x1000, 0x3D);
    cpu.mem.poke(0x1001, 0xE8);
    cpu.reset();
    cpu.dataBreaks.add(0xFFFFE8);
    cpu.clearBreakHit();
    cpu.step();
    expect(cpu.breakAddr, 0xFFFFE8);
    expect(cpu.breakPc, 0x1000);
  });

  test('the first watched address of an instruction is the one reported', () {
    final cpu = H8Cpu();
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    // MOV.W @H'FFFFE0:16,R0 reads FFFFE0 and FFFFE1.
    for (final e in [0x6B, 0x00, 0xFF, 0xE0].asMap().entries) {
      cpu.mem.poke(0x1000 + e.key, e.value);
    }
    cpu.reset();
    cpu.dataBreaks.addAll([0xFFFFE0, 0xFFFFE1]);
    cpu.clearBreakHit();
    cpu.step();
    expect(cpu.breakAddr, 0xFFFFE0);
  });

  test('clearing resets the recorded hit', () {
    final cpu = H8Cpu();
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    loadReadThenWrite(cpu, 0x1000);
    cpu.reset();
    cpu.dataBreaks.add(0xFFFFE0);
    cpu.clearBreakHit();
    cpu.step();
    expect(cpu.breakAddr, isNotNull);
    cpu.clearBreakHit();
    expect(cpu.breakHit, isFalse);
    expect(cpu.breakAddr, isNull);
    expect(cpu.breakPc, isNull);
  });

  test('a reset clears it', () {
    final cpu = H8Cpu();
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    loadReadThenWrite(cpu, 0x1000);
    cpu.reset();
    cpu.dataBreaks.add(0xFFFFE0);
    cpu.clearBreakHit();
    cpu.step();
    cpu.reset();
    expect(cpu.breakHit, isFalse);
    expect(cpu.breakAddr, isNull);
  });

  group('editing an on-chip register', () {
    test('poking the mirror does not reach the peripheral', () {
      final cpu = H8Cpu()..reset();
      cpu.mem.poke(0xFFFFB2, 0x30); // SCR0
      expect(cpu.peekBus(0xFFFFB2), isNot(0x30),
          reason: 'the model, not the mirror, answers for a register');
    });

    test('writing through the bus does', () {
      final cpu = H8Cpu()..reset();
      cpu.writeB(0xFFFFB2, 0x30);
      expect(cpu.peekBus(0xFFFFB2), 0x30);
    });

    test('an A/D result register stays read-only', () {
      final cpu = H8Cpu()..reset();
      cpu.writeB(0xFFFFE0, 0x5A);
      expect(cpu.peekBus(0xFFFFE0), 0x00,
          reason: 'ADDRAH is read-only; drive the analog input instead');
    });
  });
}
