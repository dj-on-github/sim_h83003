// The front panel: the scanned key matrix, the reverse key on its port pin,
// and the two quadrature knobs.
//
// The unit tests below drive the port registers by hand, the way
// `key_banks_read` does. The last group runs the real firmware and checks
// that a key held down in the panel model comes out of `key_scan` as the
// right code in H'11B10E, which is the only thing that really matters.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/keypad.dart';

PanelKey keyFor(int code) =>
    Keypad.panelKeys.firstWhere((k) => k.code == code);

/// Drives one strobe low and reads the latch, as one pass of the scan does.
int readBank(H8Cpu cpu, int bank) {
  final bit = Keypad.strobeBits[bank];
  cpu.writeB(Keypad.pcDdr, 1 << bit); // that pin an output
  cpu.writeB(Keypad.pcDr, 0xFF & ~(1 << bit)); // and low
  return cpu.readB(Keypad.latchBase);
}

H8Cpu withKeypad(Keypad pad) {
  final cpu = H8Cpu();
  cpu.attachKeypad(pad);
  return cpu;
}

void main() {
  group('the key matrix', () {
    test('reads all ones with nothing pressed', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      for (var bank = 0; bank < 3; bank++) {
        expect(readBank(cpu, bank), 0xFF,
            reason: 'the latch is active low, so idle is all ones');
      }
    });

    test('a pressed key pulls its return line low, on its bank only', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      // clr: bank 0, bit 2.
      pad.setDown(keyFor(0x77), true);
      expect(readBank(cpu, 0), 0xFF & ~0x04);
      expect(readBank(cpu, 1), 0xFF, reason: 'nothing on the other strobes');
      expect(readBank(cpu, 2), 0xFF);
    });

    test('shows nothing while no strobe is low', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      pad.setDown(keyFor(0x77), true);
      cpu.writeB(Keypad.pcDdr, 0x00); // all three pins inputs
      cpu.writeB(Keypad.pcDr, 0xFF);
      expect(cpu.readB(Keypad.latchBase), 0xFF,
          reason: 'a key only shows while its own strobe is being driven');
    });

    test('two keys on one bank both show', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      pad.setDown(keyFor(0x77), true); // bank 0 bit 2
      pad.setDown(keyFor(0x7C), true); // bank 0 bit 7
      expect(readBank(cpu, 0), 0xFF & ~0x84);
    });

    test('releasing puts the line back', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      final clr = keyFor(0x77);
      pad.setDown(clr, true);
      pad.setDown(clr, false);
      expect(readBank(cpu, 0), 0xFF);
    });

    test('every key in the table has a distinct place on the matrix', () {
      final seen = <String>{};
      for (final k in Keypad.panelKeys) {
        if (k.wiring != KeyWiring.matrix) continue;
        expect(seen.add('${k.bank}:${k.bit}'), isTrue,
            reason: 'two keys share bank ${k.bank} bit ${k.bit}');
      }
    });
  });

  group('reverse, which is not on the matrix', () {
    test('is driven high on port 8 bit 1, and released', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      final rev = keyFor(0x7B);
      expect(rev.wiring, KeyWiring.pin);

      cpu.writeB(0xFFFFCD, 0x00); // port 8 bit 1 an input
      pad.setDown(rev, true);
      expect(cpu.readB(Keypad.p8Dr) & 0x02, 0x02);

      pad.setDown(rev, false);
      expect(cpu.readB(Keypad.p8Dr) & 0x02, 0x00);
    });

    test('does not disturb the latch', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      pad.setDown(keyFor(0x7B), true);
      expect(readBank(cpu, 0), 0xFF);
    });
  });

  group('the knobs', () {
    /// Reads a knob's pair the way knob_track does.
    int pair(H8Cpu cpu, PanelKnob k) =>
        (cpu.readB(Keypad.pcDr) >> k.shift) & 0x03;

    test('sit still until asked to turn', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      cpu.writeB(Keypad.pcDdr, 0x00);
      final before = pair(cpu, pad.knobs[0]);
      cpu.cycles = 10 * pad.knobStepCycles;
      expect(pair(cpu, pad.knobs[0]), before);
    });

    test('clockwise walks the pair 00-01-11-10', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      cpu.writeB(Keypad.pcDdr, 0x00);
      final knob = pad.knobs[0];
      pad.turn(knob, 4);

      final seen = <int>[];
      for (var i = 0; i < 4; i++) {
        cpu.cycles += pad.knobStepCycles;
        seen.add(pair(cpu, knob)); // the read is what lets it step
      }
      expect(seen, [0x1, 0x3, 0x2, 0x0]);
    });

    test('anticlockwise walks it the other way', () {
      final pad = Keypad();
      final cpu = withKeypad(pad);
      cpu.writeB(Keypad.pcDdr, 0x00);
      final knob = pad.knobs[0];
      pad.turn(knob, -4);

      final seen = <int>[];
      for (var i = 0; i < 4; i++) {
        cpu.cycles += pad.knobStepCycles;
        seen.add(pair(cpu, knob));
      }
      expect(seen, [0x2, 0x3, 0x1, 0x0]);
    });

    test('never moves more than one step between reads', () {
      // The firmware drops a two-step jump, so a fast drag must not produce
      // one however many detents are queued.
      final pad = Keypad();
      final cpu = withKeypad(pad);
      cpu.writeB(Keypad.pcDdr, 0x00);
      final knob = pad.knobs[0];
      pad.turn(knob, 200);

      var was = pair(cpu, knob);
      for (var i = 0; i < 50; i++) {
        cpu.cycles += pad.knobStepCycles;
        final now = pair(cpu, knob);
        const order = [0x0, 0x1, 0x3, 0x2];
        final a = order.indexOf(was), b = order.indexOf(now);
        expect((b - a) & 3, anyOf(0, 1),
            reason: 'went from $was to $now, which is more than one step');
        was = now;
      }
    });

    test('the two knobs are on separate pairs', () {
      final pad = Keypad();
      expect(pad.knobs[0].shift, 2, reason: 'knob A is port C bits 2-3');
      expect(pad.knobs[1].shift, 4, reason: 'knob B is bits 4-5');
    });
  });

  group('against the real firmware', () {
    // The reconstruction and these RAM addresses are of the 3.01 machine.
    // The 2.08 dump lays its work RAM out differently, so it is the wrong
    // image to check these against.
    final dump = File('bernina_artista180/Bernina180_20260816.bin');
    final have = dump.existsSync();

    /// Boots a machine with the panel on it, up to a drawn screen.
    (H8Cpu, Keypad) booted() {
      final cpu = H8Cpu();
      loadRawBinary(dump.readAsBytesSync(), 0, cpu.mem.poke);
      final pad = Keypad();
      cpu.attachKeypad(pad);
      cpu.reset();
      for (var i = 0; i < 25000000 && !(cpu.halted && !cpu.sleeping); i++) {
        cpu.step();
      }
      return (cpu, pad);
    }

    int asked(H8Cpu cpu) =>
        (cpu.peekBus(0x11B10E) << 8) | cpu.peekBus(0x11B10F);

    test('a held key comes out of key_scan as its code', () {
      final (cpu, pad) = booted();
      expect(asked(cpu), 0xFFFF,
          reason: 'nothing should be down before we press anything');

      // The scan only believes a key that has read the same ten passes
      // running, so hold it down and let the scan come round.
      pad.setDown(keyFor(0x77), true);
      var saw = false;
      for (var i = 0; i < 4000000 && !saw; i++) {
        cpu.step();
        if (asked(cpu) == 0x0077) saw = true;
      }
      expect(saw, isTrue, reason: "the firmware should report clr as H'77");
      expect(cpu.peekBus(0xFFFEDB) & 0x04, 0x04,
          reason: 'and should have published it in bank 0, bit 2');
    }, skip: have ? false : 'needs the machine dump');

    test('a key on another bank comes through as itself', () {
      final (cpu, pad) = booted();
      pad.setDown(keyFor(0x73), true); // help: bank 1, bit 3
      var saw = false;
      for (var i = 0; i < 4000000 && !saw; i++) {
        cpu.step();
        if (asked(cpu) == 0x0073) saw = true;
      }
      expect(saw, isTrue);
    }, skip: have ? false : 'needs the machine dump');

    test('turning a knob moves the count it feeds, and by how much', () {
      final (cpu, pad) = booted();
      // Knob A drives H'11A6D3 outside the calibration screens, knob B
      // H'11A6D5. Both start wherever the boot left them.
      final beforeA = cpu.peekBus(0x11A6D3);
      pad.turn(pad.knobs[0], 12);
      for (var i = 0; i < 8000000; i++) {
        cpu.step();
      }
      expect(cpu.peekBus(0x11A6D3), beforeA + 12,
          reason: 'twelve detents clockwise should count twelve, no more '
              'and none dropped');
      expect(pad.knobs[0].pending, 0, reason: 'and all of them paid out');

      pad.turn(pad.knobs[0], -6);
      for (var i = 0; i < 8000000; i++) {
        cpu.step();
      }
      expect(cpu.peekBus(0x11A6D3), beforeA + 6,
          reason: 'and six back the other way should undo six');
    }, skip: have ? false : 'needs the machine dump');

    test('the two knobs move their own counts only', () {
      final (cpu, pad) = booted();
      final beforeA = cpu.peekBus(0x11A6D3);
      final beforeB = cpu.peekBus(0x11A6D5);
      pad.turn(pad.knobs[1], 9);
      for (var i = 0; i < 8000000; i++) {
        cpu.step();
      }
      expect(cpu.peekBus(0x11A6D5), beforeB + 9);
      expect(cpu.peekBus(0x11A6D3), beforeA, reason: 'knob A should not move');
    }, skip: have ? false : 'needs the machine dump');
  });
}
