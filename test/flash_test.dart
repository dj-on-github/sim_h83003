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
  mirroringTests();
  writeLogTests();
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

  group('images', () {
    test('building and applying an image is lossless', () {
      final source = H8Cpu();
      for (final r in artista180Flash) {
        for (var i = 0; i < r.size; i += 7) {
          source.mem.poke(r.base + i, (r.base + i) & 0xFF);
        }
      }

      final image = buildFlashImage(source.mem.peek);
      expect(image.length, flashImageSize());

      final target = H8Cpu();
      final padded = applyFlashImage(image, target.mem.poke);
      expect(padded, 0);

      for (final r in artista180Flash) {
        for (var i = 0; i < r.size; i += 997) {
          expect(target.mem.peek(r.base + i), source.mem.peek(r.base + i),
              reason: "at H'${(r.base + i).toRadixString(16)}");
        }
      }
    });

    test('a byte programmed into flash survives a save and a reload', () {
      final cpu = withFlash();
      cpu.mem.poke(base + 0x40, 0x00);

      // Program a page the way the boot ROM does: unlock, then the data.
      command(cpu, 0xA0);
      for (var i = 0; i < 0x100; i++) {
        cpu.writeB(base + i, i == 0x40 ? 0xC3 : 0xFF);
      }
      expect(cpu.readB(base + 0x40), 0xC3);

      final image = buildFlashImage(cpu.mem.peek);
      final reloaded = withFlash();
      applyFlashImage(image, reloaded.mem.poke);
      expect(reloaded.readB(base + 0x40), 0xC3);

      // And it is still flash afterwards: a bare store does nothing.
      reloaded.writeB(base + 0x40, 0x00);
      expect(reloaded.readB(base + 0x40), 0xC3);
    });

    test('a short image is padded with erased bytes', () {
      final target = H8Cpu();
      final padded = applyFlashImage(const [1, 2, 3], target.mem.poke,
          addressed: false);
      expect(padded, flashImageSize() - 3);
      expect(target.mem.peek(artista180Flash[0].base), 1);
      expect(target.mem.peek(artista180Flash[0].base + 3), 0xFF);
    });
  });

  test('nothing is attached by default, so memory stays plain memory', () {
    final cpu = H8Cpu();
    expect(cpu.flash, isEmpty);
    cpu.writeB(base + 0x40, 0x99);
    expect(cpu.readB(base + 0x40), 0x99);
  });
}

/// The write log behind the Flash tab's "written banks" list.
///
/// It is filled by the device rather than by whatever asked for the write,
/// so it shows a burn from the machine's side: which banks a stream has
/// reached, and how far into the last one it got.
void writeLogTests() {
  /// A device with somewhere to put its bytes.
  JedecFlash device({int base = 0x200000, int size = 0x200000}) {
    final mem = <int, int>{};
    return JedecFlash(base: base, size: size)
      ..peek = ((a) => mem[a] ?? 0xFF)
      ..poke = ((a, v) => mem[a] = v);
  }

  /// Programs one page the way the boot ROM does.
  void programPage(JedecFlash f, int addr, int fill) {
    f.write(f.base + 0x5555, 0xAA);
    f.write(f.base + 0x2AAA, 0x55);
    f.write(f.base + 0x5555, 0xA0);
    for (var i = 0; i < f.pageSize; i++) {
      f.write(addr + i, fill);
    }
  }

  group('the flash write log', () {
    test('starts empty', () {
      expect(device().written, isEmpty);
    });

    test('a page write shows up under its bank', () {
      final f = device();
      programPage(f, 0x200000, 0x5A);
      expect(f.written.keys, [0x200000]);
      final b = f.written[0x200000]!;
      expect(b.pages, 1);
      expect(b.bytes, f.pageSize);
      expect(b.lowest, 0x200000);
      expect(b.highest, 0x2000FF);
    });

    test('the touched span follows the burn', () {
      final f = device();
      for (var p = 0; p < 4; p++) {
        programPage(f, 0x200000 + p * 0x100, 0x11);
      }
      final b = f.written[0x200000]!;
      expect(b.pages, 4);
      expect(b.lowest, 0x200000);
      expect(b.highest, 0x2003FF,
          reason: 'the span is what landed, not the whole bank');
    });

    test('a stream across banks lists each one', () {
      final f = device();
      programPage(f, 0x200000, 0x11);
      programPage(f, 0x210000, 0x22);
      programPage(f, 0x230000, 0x33);
      final banks = f.written.keys.toList()..sort();
      expect(banks, [0x200000, 0x210000, 0x230000]);
      expect(f.written[0x230000]!.pages, 1);
    });

    test('clearing forgets the log and not the contents', () {
      final f = device();
      programPage(f, 0x200000, 0x7E);
      final wrote = f.peek(0x200010);
      f.clearWriteLog();
      expect(f.written, isEmpty);
      expect(f.peek(0x200010), wrote, reason: 'the device is untouched');
    });

    test('a reset leaves the log alone', () {
      final f = device();
      programPage(f, 0x200000, 0x01);
      f.reset();
      expect(f.written, isNotEmpty,
          reason: 'the log records what the host did, not device state');
    });

    test('the boot device keeps its own log', () {
      final boot = device(base: 0, size: 0x8000);
      final app = device();
      programPage(boot, 0x000800, 0x99);
      expect(boot.written.keys, [0x000000]);
      expect(app.written, isEmpty);
    });
  });
}

/// The boot flash is 32K of storage wired across 128K of address space, so
/// the same bytes answer at H'000000, H'008000, H'010000 and H'018000. It is
/// one device seen four times, not four copies: a write through any of them
/// is a write to all of them.
void mirroringTests() {
  JedecFlash bootDevice(Map<int, int> mem) => JedecFlash.forRegion(
      artista180Flash.firstWhere((r) => r.name == 'boot'))
    ..peek = ((a) => mem[a] ?? 0xFF)
    ..poke = ((a, v) => mem[a] = v);

  void programPage(JedecFlash f, int addr, int fill) {
    f.write(0x5555, 0xAA);
    f.write(0x2AAA, 0x55);
    f.write(0x5555, 0xA0);
    for (var i = 0; i < f.pageSize; i++) {
      f.write(addr + i, fill);
    }
  }

  group('the boot flash mirrors', () {
    test('the region says 32K of storage across 128K', () {
      final boot = artista180Flash.firstWhere((r) => r.name == 'boot');
      expect(boot.size, 0x008000);
      expect(boot.decodes, 0x020000);
      expect(boot.mirrored, isTrue);
    });

    test('the other devices do not mirror', () {
      for (final r in artista180Flash.where((r) => r.name != 'boot')) {
        expect(r.mirrored, isFalse, reason: '${r.name} is fully decoded');
        expect(r.decodes, r.size);
      }
    });

    test('it answers across the whole window', () {
      final f = bootDevice({});
      for (final a in [0x000000, 0x007FFF, 0x008000, 0x010000, 0x018000,
                       0x01FFFF]) {
        expect(f.owns(a), isTrue, reason: 'should answer at $a');
      }
      expect(f.owns(0x020000), isFalse, reason: 'the LCD registers are there');
    });

    test('the mirrors fold onto the same storage', () {
      final f = bootDevice({});
      expect(f.fold(0x008000), 0x000000);
      expect(f.fold(0x010123), 0x000123);
      expect(f.fold(0x01FFFF), 0x007FFF);
    });

    test('a byte written at the base reads back from all four', () {
      final mem = <int, int>{};
      final f = bootDevice(mem);
      programPage(f, 0x000100, 0x5A);
      for (final copy in [0x000000, 0x008000, 0x010000, 0x018000]) {
        expect(f.read(copy + 0x100), 0x5A,
            reason: "the copy at H'${copy.toRadixString(16)} is the same "
                'storage');
      }
    });

    test('a write through a mirror lands in the one device', () {
      final mem = <int, int>{};
      final f = bootDevice(mem);
      // Program through the third copy; it must show at the base.
      programPage(f, 0x010200, 0x3C);
      expect(f.read(0x000200), 0x3C);
      expect(f.read(0x018200), 0x3C);
      expect(mem.keys.every((a) => a < 0x8000), isTrue,
          reason: 'only the 32K it has was written');
    });

    test('the unlock offsets work through any of the copies', () {
      final mem = <int, int>{};
      final f = bootDevice(mem);
      // The same sequence, addressed through the second copy.
      f.write(0x00D555, 0xAA); // H'8000 + H'5555
      f.write(0x00AAAA, 0x55); // H'8000 + H'2AAA
      f.write(0x00D555, 0xA0);
      for (var i = 0; i < f.pageSize; i++) {
        f.write(0x008300 + i, 0x77);
      }
      expect(f.read(0x000300), 0x77,
          reason: 'only fifteen address lines reach it, which is exactly '
              'what the unlock offsets need');
    });
  });
}
