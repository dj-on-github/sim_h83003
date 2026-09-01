// Snapshots: a whole machine written down and put back.
//
// The point of one is to skip the twenty-five million instructions it takes
// to get the artista 180 to a drawn screen, so the test that matters is the
// last group: boot the real firmware, snapshot it, put it into a fresh
// machine and check it carries on identically.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/i2c_eeprom.dart';
import 'package:sim_h83003/keypad.dart';
import 'package:sim_h83003/lcd.dart';
import 'package:sim_h83003/snapshot.dart';

/// A machine with something in it.
H8Cpu scratch() {
  final cpu = H8Cpu();
  cpu.mem.poke(0x000100, 0x0B);
  cpu.mem.poke(0x000101, 0x00); // ADDS #1,ER0
  cpu.mem.poke(0x000102, 0x40);
  cpu.mem.poke(0x000103, 0xFC); // BRA back
  for (var i = 0; i < 4; i++) {
    cpu.mem.poke(i, [0x00, 0x00, 0x01, 0x00][i]);
  }
  cpu.reset();
  return cpu;
}

Uint8List screen(H8Cpu cpu) {
  final g = Uint8List(LcdFormat.width * LcdFormat.height);
  var i = 0;
  for (var y = 0; y < LcdFormat.height; y++) {
    for (var x = 0; x < LcdFormat.width; x++) {
      final b = cpu.mem.peek(0x040000 + y * LcdFormat.stride + (x >> 2));
      g[i++] = ((b >> (6 - 2 * (x & 3))) & 3) * 85;
    }
  }
  return g;
}

void main() {
  group('what it carries', () {
    test('the registers and the cycle count come back', () {
      final cpu = scratch();
      for (var i = 0; i < 500; i++) {
        cpu.step();
      }
      final snap = Snapshot.capture(cpu);
      final pc = cpu.pc, er0 = cpu.er[0], cycles = cpu.cycles;

      final other = H8Cpu();
      snap.apply(other);
      expect(other.pc, pc);
      expect(other.er[0], er0);
      expect(other.cycles, cycles);
    });

    test('memory comes back, and only what was allocated', () {
      final cpu = scratch();
      cpu.mem.poke(0x200000, 0x5A);
      final snap = Snapshot.capture(cpu);
      expect(snap.memory.keys, isNotEmpty);
      expect(snap.memoryBytes, greaterThan(0));

      final other = H8Cpu();
      snap.apply(other);
      expect(other.mem.peek(0x200000), 0x5A);
      expect(other.mem.peek(0x000100), 0x0B);
      expect(other.mem.isAllocated(0x900000), isFalse,
          reason: 'a machine that never touched it does not carry it');
    });

    test('held pins come back', () {
      final cpu = scratch();
      cpu.setPin(0xFFFFC7, 5, false);
      cpu.setPin(0xFFFFC7, 3, true);
      final snap = Snapshot.capture(cpu);

      final other = H8Cpu();
      snap.apply(other);
      expect(other.pinIsDriven(0xFFFFC7, 5), isTrue);
      expect(other.pinIsHigh(0xFFFFC7, 5), isFalse);
      expect(other.pinIsHigh(0xFFFFC7, 3), isTrue);
      expect(other.pinIsDriven(0xFFFFC7, 0), isFalse);
    });

    test('a floating pin does not come back held', () {
      final cpu = scratch();
      cpu.setPin(0xFFFFC7, 5, true);
      cpu.releasePin(0xFFFFC7, 5);
      final other = H8Cpu();
      Snapshot.capture(cpu).apply(other);
      expect(other.pinIsDriven(0xFFFFC7, 5), isFalse);
    });

    test('the panel comes back as it was left', () {
      final cpu = scratch();
      final pad = Keypad();
      cpu.attachKeypad(pad);
      pad.setDown(Keypad.panelKeys.firstWhere((k) => k.code == 0x77), true);
      pad.latched.add(0x77);
      pad.knobs[0].phase = 2;
      pad.knobs[0].pending = -3;

      final snap = Snapshot.capture(cpu, panel: pad);
      final other = H8Cpu();
      final otherPad = Keypad();
      other.attachKeypad(otherPad);
      snap.apply(other, panel: otherPad);

      expect(otherPad.isDown(0x77), isTrue);
      expect(otherPad.latched, contains(0x77));
      expect(otherPad.knobs[0].phase, 2);
      expect(otherPad.knobs[0].pending, -3);
    });

    test('the flash write log comes back', () {
      final cpu = scratch();
      for (final r in artista180Flash) {
        cpu.attachFlash(JedecFlash.forRegion(r));
      }
      final app = cpu.flash.firstWhere((f) => f.base == 0x200000);
      app.write(0x205555, 0xAA);
      app.write(0x202AAA, 0x55);
      app.write(0x205555, 0xA0);
      for (var i = 0; i < app.pageSize; i++) {
        app.write(0x200000 + i, 0x33);
      }
      expect(app.written, isNotEmpty);

      final snap = Snapshot.capture(cpu);
      final other = H8Cpu();
      for (final r in artista180Flash) {
        other.attachFlash(JedecFlash.forRegion(r));
      }
      snap.apply(other);
      final restored = other.flash.firstWhere((f) => f.base == 0x200000);
      expect(restored.written.keys, app.written.keys);
      expect(restored.written[0x200000]!.pages,
          app.written[0x200000]!.pages);
    });
  });

  group('the EEPROM', () {
    test('its contents are carried, so the machine keeps its settings', () {
      final cpu = scratch();
      final ee = I2cEeprom();
      cpu.attachEeprom(ee);
      // Something the machine would have worked out about itself.
      for (var i = 0; i < 16; i++) {
        ee.data[i] = 0xA0 + i;
      }

      final snap = Snapshot.capture(cpu, eepromModel: ee);
      expect(snap.eeprom, isNotNull);
      expect(snap.eeprom!.length, ee.data.length);

      final other = H8Cpu();
      final otherEe = I2cEeprom();
      other.attachEeprom(otherEe);
      expect(otherEe.data[0], 0xFF, reason: 'a fresh part is erased');
      snap.apply(other, eepromModel: otherEe);
      for (var i = 0; i < 16; i++) {
        expect(otherEe.data[i], 0xA0 + i);
      }
    });

    test('survives the file', () {
      final cpu = scratch();
      final ee = I2cEeprom();
      ee.data[7] = 0x5A;
      final bytes = Snapshot.capture(cpu, eepromModel: ee).encode();

      final back = Snapshot.decode(bytes);
      final other = H8Cpu();
      final otherEe = I2cEeprom();
      back.apply(other, eepromModel: otherEe);
      expect(otherEe.data[7], 0x5A);
    });

    test('one taken without an EEPROM leaves an attached one alone', () {
      final cpu = scratch();
      final snap = Snapshot.capture(cpu); // no eepromModel
      expect(snap.eeprom, isNull);

      final other = H8Cpu();
      final ee = I2cEeprom();
      ee.data[3] = 0x77;
      snap.apply(other, eepromModel: ee);
      expect(ee.data[3], 0x77, reason: 'nothing to say, so nothing changed');
    });
  });

  group('the file', () {
    test('is gzipped, and much smaller than the machine', () {
      final cpu = scratch();
      for (var a = 0x200000; a < 0x210000; a++) {
        cpu.mem.poke(a, 0xFF);
      }
      final snap = Snapshot.capture(cpu);
      final bytes = snap.encode();
      expect(bytes.length, lessThan(snap.memoryBytes ~/ 4),
          reason: 'flash images compress to very little');
    });

    test('reads back plain JSON too, for one unpacked by hand', () {
      final cpu = scratch();
      final snap = Snapshot.capture(cpu);
      final plain = jsonBytes(snap);
      final back = Snapshot.decode(plain);
      expect(back.cpuState, snap.cpuState);
    });

    test('refuses something that is not a snapshot', () {
      expect(() => Snapshot.decode('{"format":"something else"}'.codeUnits),
          throwsFormatException);
      expect(() => Snapshot.decode('not json at all'.codeUnits),
          throwsA(anything));
    });

    test('refuses a version it cannot read', () {
      final cpu = scratch();
      final j = Snapshot.capture(cpu).toJson();
      j['version'] = kSnapshotVersion + 1;
      expect(() => Snapshot.fromJson(j), throwsFormatException);
    });

    test('carries its note', () {
      final cpu = scratch();
      final back = Snapshot.decode(
          Snapshot.capture(cpu, note: 'booted to the stitch screen').encode());
      expect(back.note, 'booted to the stitch screen');
    });
  });

  group('against the real machine', () {
    final dump = File('bernina_artista180/Bernina180_20260816.bin');
    final have = dump.existsSync();

    test('a booted machine comes back and carries on the same', () {
      H8Cpu build() {
        final cpu = H8Cpu();
        loadRawBinary(dump.readAsBytesSync(), 0, cpu.mem.poke);
        cpu.attachKeypad(Keypad());
        for (final r in artista180Flash) {
          cpu.attachFlash(JedecFlash.forRegion(r));
        }
        cpu.reset();
        return cpu;
      }

      // Boot it once, the expensive way.
      final booted = build();
      for (var i = 0; i < 25000000 && !(booted.halted && !booted.sleeping);
          i++) {
        booted.step();
      }
      expect(booted.cycles, greaterThan(20000000),
          reason: 'it really did boot, rather than halting early');
      expect(drawnHasInk(screen(booted)), isTrue,
          reason: 'and drew something');
      final drawn = screen(booted);
      final snap = Snapshot.decode(
          Snapshot.capture(booted, note: 'booted').encode());

      // And again from the snapshot, into a machine that has never run.
      final restored = H8Cpu();
      restored.attachKeypad(Keypad());
      for (final r in artista180Flash) {
        restored.attachFlash(JedecFlash.forRegion(r));
      }
      snap.apply(restored, panel: Keypad());

      expect(restored.pc, booted.pc);
      expect(restored.cycles, booted.cycles);
      expect(screen(restored), drawn,
          reason: 'the screen it was left showing');

      // Now run both on, and they must stay in step.
      for (var i = 0; i < 200000; i++) {
        booted.step();
        restored.step();
      }
      expect(restored.pc, booted.pc,
          reason: 'a restored machine carries on identically');
      expect(screen(restored), screen(booted));
    }, skip: have ? false : 'needs the machine dump');
  });
}

/// True when the screen has anything on it, so a test cannot pass by
/// comparing two blank ones.
bool drawnHasInk(Uint8List g) => g.any((v) => v != g.first);

/// The snapshot as plain, ungzipped JSON bytes, which decode() also accepts
/// so that a file unpacked by hand still loads.
List<int> jsonBytes(Snapshot s) => utf8.encode(jsonEncode(s.toJson()));
