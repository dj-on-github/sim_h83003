// The JEDEC flash model.
//
// Plain memory answers a bare store, which is exactly what real flash does
// not do: the array only changes after an unlock sequence and a command. An
// unmodelled bus therefore makes the boot ROM's identify conclude "not
// flash" and refuse every programming command before it starts, so none of
// the programming paths can be exercised without this.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';

const int base = 0x200000;

H8Cpu withFlash({bool pageWrite = true, int busyReads = 0}) {
  final cpu = H8Cpu();
  cpu.attachFlash(JedecFlash(
    base: base,
    size: 0x80000,
    pageWrite: pageWrite,
    busyReads: busyReads,
  ));
  return cpu;
}

/// H'AA to H'5555, H'55 to H'2AAA, then the command back at H'5555.
void command(H8Cpu cpu, int cmd, {int at = base}) {
  final bank = at & 0xFF0000;
  cpu.writeB(bank + 0x5555, 0xAA);
  cpu.writeB(bank + 0x2AAA, 0x55);
  cpu.writeB(bank + 0x5555, cmd);
}

void main() {
  test('a bare store does not change the array', () {
    final cpu = withFlash();
    cpu.mem.poke(base + 0x40, 0x12);
    cpu.writeB(base + 0x40, 0x99);
    expect(cpu.readB(base + 0x40), 0x12);
  });

  test('the unlock bytes are not stored either', () {
    final cpu = withFlash();
    cpu.mem.poke(base + 0x5555, 0x11);
    cpu.mem.poke(base + 0x2AAA, 0x22);
    command(cpu, 0xF0);
    expect(cpu.readB(base + 0x5555), 0x11);
    expect(cpu.readB(base + 0x2AAA), 0x22);
  });

  group('autoselect', () {
    test('reports the manufacturer and device, and F0 ends it', () {
      final cpu = withFlash();
      cpu.mem.poke(base, 0x00);
      cpu.mem.poke(base + 1, 0x01);

      command(cpu, 0x90);
      expect(cpu.readB(base), 0x1F, reason: 'Atmel');
      expect(cpu.readB(base + 1), 0xA4);

      command(cpu, 0xF0);
      expect(cpu.readB(base), 0x00);
      expect(cpu.readB(base + 1), 0x01);
    });

    test('only the first two bytes of the bank are substituted', () {
      final cpu = withFlash();
      cpu.mem.poke(base + 2, 0x5A);
      command(cpu, 0x90);
      expect(cpu.readB(base + 2), 0x5A);
    });

    test('a broken unlock sequence is not a command', () {
      final cpu = withFlash();
      cpu.writeB(base + 0x5555, 0xAA);
      cpu.writeB(base + 0x2AAA, 0x54); // wrong
      cpu.writeB(base + 0x5555, 0x90);
      expect(cpu.readB(base), isNot(0x1F));
    });
  });

  group('page programming', () {
    test('a full page is written, and untouched bytes come back erased', () {
      final cpu = withFlash();
      for (var i = 0; i < 0x100; i++) {
        cpu.mem.poke(base + 0x300 + i, 0x11);
      }
      command(cpu, 0xA0);
      for (var i = 0; i < 0x100; i++) {
        cpu.writeB(base + 0x300 + i, i);
      }
      expect(cpu.readB(base + 0x300), 0x00);
      expect(cpu.readB(base + 0x3FF), 0xFF);
      expect(cpu.readB(base + 0x342), 0x42);

      // A partial page: the rest of it is erased, not preserved.
      command(cpu, 0xA0);
      cpu.writeB(base + 0x300, 0x7E);
      command(cpu, 0xF0); // anything outside the page commits it
      expect(cpu.readB(base + 0x300), 0x7E);
      expect(cpu.readB(base + 0x301), 0xFF);
    });

    test('writes without a command in flight are still ignored', () {
      final cpu = withFlash();
      cpu.mem.poke(base + 0x300, 0x11);
      command(cpu, 0xA0);
      for (var i = 0; i < 0x100; i++) {
        cpu.writeB(base + 0x300 + i, 0x22);
      }
      cpu.writeB(base + 0x300, 0x33); // page already committed
      expect(cpu.readB(base + 0x300), 0x22);
    });
  });

  test('a byte-write part clears bits rather than replacing a page', () {
    final cpu = withFlash(pageWrite: false);
    cpu.mem.poke(base + 0x10, 0xFF);
    cpu.mem.poke(base + 0x11, 0xAB);
    command(cpu, 0xA0);
    cpu.writeB(base + 0x10, 0x0F);
    expect(cpu.readB(base + 0x10), 0x0F);
    expect(cpu.readB(base + 0x11), 0xAB, reason: 'its neighbour is untouched');
  });

  group('erase', () {
    test('sector erase fills the sector and leaves the next one alone', () {
      final cpu = withFlash();
      cpu.mem.poke(base + 0x20, 0x00);
      cpu.mem.poke(base + 0x10000, 0x00);
      cpu.writeB(base + 0x5555, 0xAA);
      cpu.writeB(base + 0x2AAA, 0x55);
      cpu.writeB(base + 0x5555, 0x80);
      cpu.writeB(base + 0x5555, 0xAA);
      cpu.writeB(base + 0x2AAA, 0x55);
      cpu.writeB(base, 0x30);
      expect(cpu.readB(base + 0x20), 0xFF);
      expect(cpu.readB(base + 0x10000), 0x00);
    });

    test('chip erase fills the whole device', () {
      final cpu = withFlash();
      cpu.mem.poke(base + 0x20, 0x00);
      cpu.mem.poke(base + 0x7FFFF, 0x00);
      cpu.writeB(base + 0x5555, 0xAA);
      cpu.writeB(base + 0x2AAA, 0x55);
      cpu.writeB(base + 0x5555, 0x80);
      cpu.writeB(base + 0x5555, 0xAA);
      cpu.writeB(base + 0x2AAA, 0x55);
      cpu.writeB(base + 0x5555, 0x10);
      expect(cpu.readB(base + 0x20), 0xFF);
      expect(cpu.readB(base + 0x7FFFF), 0xFF);
    });
  });

  test('while busy, DQ6 toggles and then settles', () {
    final cpu = withFlash(busyReads: 4);
    command(cpu, 0xA0);
    for (var i = 0; i < 0x100; i++) {
      cpu.writeB(base + i, 0x5A);
    }
    final polls = [for (var i = 0; i < 4; i++) cpu.readB(base) & 0x40];
    expect(polls[0], isNot(polls[1]), reason: 'toggling means still busy');
    expect(polls[2], isNot(polls[3]));
    // Once the busy reads are used up the data is stable.
    expect(cpu.readB(base), cpu.readB(base));
    expect(cpu.readB(base), 0x5A);
  });

  test('nothing is attached by default, so memory stays plain memory', () {
    final cpu = H8Cpu();
    expect(cpu.flash, isEmpty);
    cpu.writeB(base + 0x40, 0x99);
    expect(cpu.readB(base + 0x40), 0x99);
  });
}
