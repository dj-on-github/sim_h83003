// The memory-mapped input latch at H'080000.
//
// The artista 180 reads its digital inputs from a window that returns the
// same byte at every address, because the board decodes it without the low
// address bits. It is not a CPU port, so nothing in the port model reaches
// it — which is why wiggling port pins alone never finds these inputs.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';

const int latch = 0x080000;

void main() {
  test('the window is where the firmware reads it', () {
    final cpu = H8Cpu();
    final e = cpu.externalInputFor(latch);
    expect(e, isNotNull);
    expect(e!.name, 'digital inputs');
    expect(cpu.externalInputFor(latch + 0x1FFFF), same(e),
        reason: 'the window spans both banks');
    expect(cpu.externalInputFor(0x0A0000), isNull,
        reason: 'the output latch is not an input');
    expect(cpu.externalInputFor(0x040000), isNull,
        reason: 'the frame buffer is ordinary memory');
  });

  test('undriven, it reads out of memory', () {
    final cpu = H8Cpu();
    cpu.mem.poke(latch, 0x70);
    cpu.reset();
    expect(cpu.readB(latch), 0x70);
    expect(cpu.peekBus(latch), 0x70);
    expect(cpu.externalInputFor(latch)!.driven, isFalse);
  });

  test('driving it overrides every address in the window', () {
    final cpu = H8Cpu();
    cpu.mem.poke(latch, 0x70);
    cpu.mem.poke(latch + 0x1234, 0x70);
    cpu.reset();
    cpu.externalInputFor(latch)!.value = 0x74;
    expect(cpu.readB(latch), 0x74);
    expect(cpu.readB(latch + 0x1234), 0x74,
        reason: 'the decode ignores the low address bits');
    expect(cpu.readB(latch + 0x1FFFF), 0x74);
  });

  test('releasing goes back to memory', () {
    final cpu = H8Cpu();
    cpu.mem.poke(latch, 0x70);
    cpu.reset();
    final e = cpu.externalInputFor(latch)!;
    e.value = 0x00;
    expect(cpu.readB(latch), 0x00);
    e.value = null;
    expect(cpu.readB(latch), 0x70);
    expect(e.driven, isFalse);
  });

  test('addresses outside the window are untouched', () {
    final cpu = H8Cpu();
    cpu.mem.poke(0x0A0000, 0x11);
    cpu.mem.poke(0x07FFFF, 0x22);
    cpu.reset();
    cpu.externalInputFor(latch)!.value = 0xFF;
    expect(cpu.readB(0x0A0000), 0x11);
    expect(cpu.readB(0x07FFFF), 0x22);
  });

  test('executing code sees the driven value', () {
    final cpu = H8Cpu();
    cpu.mem.poke(2, 0x10);
    cpu.mem.poke(3, 0x00);
    // MOV.B @H'080000:24,R6L ; BTST #2,R6L ; BEQ +2 ; MOV.B #1,R0L
    var a = 0x1000;
    for (final b in [
      0x6A, 0x2E, 0x00, 0x08, 0x00, 0x00, //
      0x73, 0x2E, //
      0x47, 0x02, //
      0xF8, 0x01, //
    ]) {
      cpu.mem.poke(a++, b);
    }
    cpu.mem.poke(latch, 0x70); // bit 2 clear, as the machine idles
    cpu.reset();
    cpu.externalInputFor(latch)!.value = 0x74; // bit 2 set
    cpu.step(); // MOV
    cpu.step(); // BTST
    cpu.step(); // BEQ, not taken
    cpu.step(); // MOV #1
    expect(cpu.er[0] & 0xFF, 1, reason: 'the driven bit steered the branch');
  });
}
