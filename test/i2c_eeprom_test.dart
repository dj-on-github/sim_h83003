// The serial EEPROM on port 4.
//
// Two ways of checking it. The first drives the two pins by hand, which is
// how a bug in the state machine is found and named. The second calls the
// machine's own I2C routines out of the ROM image and looks at what ends up
// in the array, which is the only check that says the model answers what the
// firmware actually sends -- a state machine can be self-consistent and
// still not be the one the other end is talking to.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/i2c_eeprom.dart';

const int p4dr = 0xFFFFC7;
const int p4ddr = 0xFFFFC5;
const int sda = 0x80;
const int scl = 0x40;

/// A machine with an EEPROM on it and both pins driven, as `i2c_init` leaves
/// them.
(H8Cpu, I2cEeprom) bench() {
  final cpu = H8Cpu();
  final e = I2cEeprom();
  cpu.attachEeprom(e);
  cpu.writeB(p4ddr, sda | scl); // both outputs
  cpu.writeB(p4dr, sda | scl); // both high
  return (cpu, e);
}

/// The master drives SDA to [high] and clocks once, most significant first.
void clock(H8Cpu cpu, {required bool high}) {
  final dr = cpu.mem.peek(p4dr);
  cpu.writeB(p4dr, high ? dr | sda : dr & ~sda & 0xFF);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) | scl);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
}

void start(H8Cpu cpu) {
  cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) | sda);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) | sda);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) | scl);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~sda & 0xFF); // SDA falls, SCL high
}

void stop(H8Cpu cpu) {
  cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) | sda);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~sda & 0xFF);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) | scl);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) | sda); // SDA rises, SCL high
}

/// Eight bits out, then the acknowledge clock with SDA released. Returns
/// true when the device pulled the line down over that ninth clock, which is
/// the only moment the answer is on the wire.
bool writeByte(H8Cpu cpu, int value) {
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
  cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) | sda);
  for (var i = 7; i >= 0; i--) {
    clock(cpu, high: ((value >> i) & 1) == 1);
  }
  cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) & ~sda & 0xFF); // release for the ack
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) | scl);
  final acked = (cpu.readB(p4dr) & sda) == 0;
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
  return acked;
}

/// Eight bits in with SDA released, then [ack] driven for the ninth clock.
int readByte(H8Cpu cpu, {required bool ack}) {
  cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) & ~sda & 0xFF);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
  var v = 0;
  for (var i = 0; i < 8; i++) {
    cpu.writeB(p4dr, cpu.mem.peek(p4dr) | scl);
    v = (v << 1) | ((cpu.readB(p4dr) & sda) != 0 ? 1 : 0);
    cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
  }
  cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) | sda);
  cpu.writeB(p4dr, ack
      ? cpu.mem.peek(p4dr) & ~sda & 0xFF
      : cpu.mem.peek(p4dr) | sda);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) | scl);
  cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
  return v;
}

String _hex(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');

void main() {
  group('the bus', () {
    test('a byte write puts the byte in the array', () {
      final (cpu, e) = bench();
      start(cpu);
      expect(writeByte(cpu, 0x50), isTrue, // control, write
          reason: 'the device should acknowledge');
      writeByte(cpu, 0xA9); // word address
      writeByte(cpu, 0x3C); // data
      stop(cpu);

      expect(e.data[0xA9], 0x3C);
      expect(e.writeCount, 1);
    });

    test('nothing is committed until the stop arrives', () {
      final (cpu, e) = bench();
      start(cpu);
      writeByte(cpu, 0x50);
      writeByte(cpu, 0x10);
      writeByte(cpu, 0x5A);
      expect(e.data[0x10], 0xFF, reason: 'still only in the page buffer');
      stop(cpu);
      expect(e.data[0x10], 0x5A);
    });

    test('a control byte for somebody else is not acknowledged', () {
      final (cpu, e) = bench();
      start(cpu);
      // A real 24Cxx would answer H'A0; the part this machine has does not.
      expect(writeByte(cpu, 0xA0), isFalse);
      writeByte(cpu, 0x10);
      writeByte(cpu, 0x5A);
      stop(cpu);
      expect(e.data[0x10], 0xFF, reason: 'not addressed, so nothing written');
    });

    test('a random read starts where the word address said', () {
      final (cpu, e) = bench();
      e.data[0x40] = 0x11;
      e.data[0x41] = 0x22;
      e.data[0x42] = 0x33;

      start(cpu);
      writeByte(cpu, 0x50);
      writeByte(cpu, 0x40);
      start(cpu); // repeated start
      writeByte(cpu, 0x51);
      expect(readByte(cpu, ack: true), 0x11);
      expect(readByte(cpu, ack: true), 0x22);
      expect(readByte(cpu, ack: false), 0x33);
      stop(cpu);
      expect(e.readCount, 3);
    });

    test('a current-address read follows on from the last one', () {
      final (cpu, e) = bench();
      e.data[0x00] = 0xAB;
      e.data[0x01] = 0xCD;

      start(cpu);
      writeByte(cpu, 0x51);
      expect(readByte(cpu, ack: false), 0xAB);
      stop(cpu);

      start(cpu);
      writeByte(cpu, 0x51);
      expect(readByte(cpu, ack: false), 0xCD);
      stop(cpu);
    });

    test('a page write wraps within its page', () {
      final (cpu, e) = bench();
      start(cpu);
      writeByte(cpu, 0x50);
      writeByte(cpu, 0x06); // page H'00-H'07, two from the end
      writeByte(cpu, 0x01);
      writeByte(cpu, 0x02);
      writeByte(cpu, 0x03); // wraps to H'00 rather than reaching H'08
      stop(cpu);

      expect(e.data[0x06], 0x01);
      expect(e.data[0x07], 0x02);
      expect(e.data[0x00], 0x03);
      expect(e.data[0x08], 0xFF);
    });

    test('the model lets go of the line when it is detached', () {
      final (cpu, e) = bench();
      start(cpu);
      // Eight bits of the control byte and no ninth clock, which leaves the
      // device holding SDA down for an acknowledge it has not been given.
      cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~scl & 0xFF);
      cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) | sda);
      for (var i = 7; i >= 0; i--) {
        clock(cpu, high: ((0x50 >> i) & 1) == 1);
      }
      cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) & ~sda & 0xFF);
      expect(cpu.readB(p4dr) & sda, 0, reason: 'the device is acknowledging');

      cpu.detachEeprom();
      expect(cpu.pinIsDriven(p4dr, 7), isFalse,
          reason: 'nothing should be holding SDA now');
      expect(e.phase, isNot(I2cPhase.idle),
          reason: 'the device keeps its state; it just stops driving');
    });

    test('releasing SDA while the clock is high is a stop, not a bit', () {
      final (cpu, e) = bench();
      start(cpu);
      writeByte(cpu, 0x50);
      writeByte(cpu, 0x10);
      writeByte(cpu, 0x5A);
      // SDA driven low with the clock high, then let go: the pull-up takes
      // it up, and that edge is a stop wherever it comes from.
      cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) | sda);
      cpu.writeB(p4dr, cpu.mem.peek(p4dr) & ~sda & ~scl & 0xFF);
      cpu.writeB(p4dr, cpu.mem.peek(p4dr) | scl);
      cpu.writeB(p4ddr, cpu.mem.peek(p4ddr) & ~sda & 0xFF);
      expect(e.phase, I2cPhase.idle);
      expect(e.data[0x10], 0x5A, reason: 'the stop committed the write');
    });
  });

  group('the file it lives in', () {
    test('what is written comes back', () {
      final a = I2cEeprom();
      a.data[0xA9] = 0x3C;
      a.data[0xAA] = 0x41;
      a.data[0x00] = 0x01;

      final b = I2cEeprom();
      expect(b.fromJson(a.toJson()), isNull);
      expect(b.data, a.data);
    });

    test('a flat map of bytes is accepted, for editing by hand', () {
      final e = I2cEeprom();
      expect(e.fromJson('{"bytes": {"A9": "3C", "AA": 65}}'), isNull);
      expect(e.data[0xA9], 0x3C);
      expect(e.data[0xAA], 65);
      expect(e.data[0x00], 0xFF);
    });

    test('a file that says nothing is refused rather than silently emptying',
        () {
      final e = I2cEeprom();
      e.data[0xA9] = 0x3C;
      expect(e.fromJson('{"device": "something else"}'), isNotNull);
      expect(e.data[0xA9], 0x3C, reason: 'the array is left alone');
    });

    test('rubbish in a row is named', () {
      final e = I2cEeprom();
      expect(e.fromJson('{"rows": ["A0: 11 ZZ 33"]}'), contains('ZZ'));
    });
  });

  group("against the machine's own code", () {
    // The addresses are the original's, from the reconstruction's listing.
    const int i2cInit = 0x200CB6;
    const int writeVerify = 0x200C72;
    const int readCurrent = 0x200BDE;
    const int sentinel = 0x00FFF800;
    const int sp = 0x00FFF600;

    H8Cpu? load() {
      final f = File('bernina_artista180/Bernina180_20260816.bin');
      if (!f.existsSync()) return null;
      final cpu = H8Cpu();
      final bytes = f.readAsBytesSync();
      for (var i = 0; i < bytes.length; i++) {
        if (bytes[i] != 0) cpu.mem.poke(i, bytes[i]);
      }
      return cpu;
    }

    /// Calls a routine the way the original's own compiler does: the first
    /// argument in ER6, the rest pushed, the result back in R6.
    int call(H8Cpu cpu, int address, {int er6 = 0, List<int> stack = const []}) {
      for (var i = 0; i < 4; i++) {
        cpu.mem.poke(sp + i, (sentinel >> (8 * (3 - i))) & 0xFF);
      }
      for (var i = 0; i < stack.length; i++) {
        cpu.mem.poke(sp + 4 + i * 2, 0);
        cpu.mem.poke(sp + 5 + i * 2, stack[i] & 0xFF);
      }
      cpu.er[6] = er6;
      cpu.er[7] = sp;
      cpu.pc = address;
      var steps = 0;
      while (cpu.pc != sentinel && steps < 2000000) {
        cpu.step();
        steps++;
      }
      expect(cpu.pc, sentinel, reason: 'the routine should have returned');
      return cpu.er[6] & 0xFF;
    }

    test('the firmware writes the byte it means to', () {
      final cpu = load();
      if (cpu == null) return; // no image in the tree; nothing to check
      final e = I2cEeprom();
      cpu.attachEeprom(e);

      call(cpu, i2cInit);
      call(cpu, writeVerify, er6: 0xA9, stack: [0x3C]);

      expect(e.data[0xA9], 0x3C);
      expect(e.data[0xAA], 0xFF, reason: 'only the byte it named');
    });

    test('and reads back what is in the part', () {
      final cpu = load();
      if (cpu == null) return;
      final e = I2cEeprom();
      cpu.attachEeprom(e);
      e.data[0x00] = 0x5A;

      call(cpu, i2cInit);
      expect(call(cpu, readCurrent), 0x5A);
    });

    test("the original's verify reads the byte after the one it wrote", () {
      final cpu = load();
      if (cpu == null) return;
      final e = I2cEeprom();
      cpu.attachEeprom(e);
      e.data[0xAA] = 0x99;

      call(cpu, i2cInit);
      final answer = call(cpu, writeVerify, er6: 0xA9, stack: [0x3C]);

      // The write went in, but the address counter has moved on, so what the
      // verify compares against H'3C is H'AA's H'99. This is why none of the
      // twelve call sites looks at the answer.
      expect(e.data[0xA9], 0x3C);
      expect(answer, 0, reason: 'the datasheet counter, so the verify says no');
    });

    test('and agrees with itself when the counter is made friendly', () {
      final cpu = load();
      if (cpu == null) return;
      final e = I2cEeprom()..verifyFriendly = true;
      cpu.attachEeprom(e);

      call(cpu, i2cInit);
      expect(call(cpu, writeVerify, er6: 0xA9, stack: [0x3C]), 1);
      expect(e.data[0xA9], 0x3C);
    });

    test('booting the machine leaves the foot calibration in the part',
        () {
      final cpu = load();
      if (cpu == null) return;
      cpu.reset(); // through the reset vector, as the machine starts
      final e = I2cEeprom();
      cpu.attachEeprom(e);

      // Far enough in for the start-up to have run: `config_to_eeprom` puts
      // the two configuration bytes from flash in, and the handwheel trim
      // writes them again with its H'28 added.
      for (var i = 0; i < 2500000 && !cpu.halted; i++) {
        cpu.step();
      }

      final fromFlash = cpu.mem.peek(0x57FF90);
      expect(e.data[0xA9], (fromFlash + 0x28) & 0xFF);
      expect(e.data[0xAA], (cpu.mem.peek(0x57FF91) + 0x28) & 0xFF);

      // And what went past on the wire says how it got there: each write is
      // followed by a read of the address *after* the one written, which is
      // the write-and-verify looking at the wrong byte.
      expect(e.log.map((t) => t.toString()).toList(), [
        "W 50 @A9  ${_hex(fromFlash)}",
        'R 51 @AA  FF',
        "W 50 @AA  ${_hex(cpu.mem.peek(0x57FF91))}",
        'R 51 @AB  FF',
        "W 50 @A9  ${_hex((fromFlash + 0x28) & 0xFF)}",
        "R 51 @AA  ${_hex(fromFlash)}",
        "W 50 @AA  ${_hex((cpu.mem.peek(0x57FF91) + 0x28) & 0xFF)}",
        'R 51 @AB  FF',
      ]);
    });

    test('every byte the firmware can name can be written and read', () {
      final cpu = load();
      if (cpu == null) return;
      final e = I2cEeprom();
      cpu.attachEeprom(e);
      call(cpu, i2cInit);

      for (final a in [0x00, 0x01, 0x7F, 0x80, 0xA9, 0xAA, 0xFE, 0xFF]) {
        call(cpu, writeVerify, er6: a, stack: [(a ^ 0x5A) & 0xFF]);
        expect(e.data[a], (a ^ 0x5A) & 0xFF, reason: 'at H\'${a.toRadixString(16)}');
      }
    });
  });
}
