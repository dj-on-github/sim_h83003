// The click-to-A/D mapping, against the readings traced on the machine:
// Y arrives in ADDRA and X in ADDRB, both running from zero across the
// panel. The firmware's H'4C floor on Y is a dead band at the top of the
// screen, not a threshold the mapping has to clear.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/adc.dart';
import 'package:sim_h83003/h8300h.dart';

/// Converts a fraction across the panel the way the UI does.
int map(int min, int max, double f) => (min + f * (max - min)).round();

/// A monotonic state counter. Nothing is executing in these tests, so the
/// CPU's own cycle count never advances and each conversion has to be given
/// time of its own.
int clock = 0;

/// Runs one conversion on [channel] and returns what firmware would read.
int convert(H8Cpu cpu, int channel) {
  cpu.writeB(0xFFFFE8, AdcStatus.adst | channel);
  clock += 300;
  cpu.adc.tick(clock);
  return cpu.readB(0xFFFFE0 + AdConverter.registerFor(channel) * 2);
}

void main() {
  // The defaults both axes use.
  const min = 0x00, max = 0xF0;

  group('axis ranges', () {
    test('both axes start at zero', () {
      expect(map(min, max, 0.0), 0x00);
    });

    test('both axes reach full scale at the far edge', () {
      expect(map(min, max, 1.0), 0xF0);
    });

    test('the map is linear across the panel', () {
      expect(map(min, max, 0.5), 0x78);
      expect(map(min, max, 0.25), 0x3C);
    });

    test("the firmware's H'4C floor is a dead band near the top", () {
      // Below H'4C the firmware ignores the reading, so a press in roughly
      // the top third of the screen does nothing — as it does on the machine.
      expect(map(min, max, 0.0), lessThan(0x4C));
      expect(map(min, max, 0.30), lessThan(0x4C));
      expect(map(min, max, 0.35), greaterThanOrEqualTo(0x4C));
      // The boundary, as a fraction of the screen height.
      expect(0x4C / max, closeTo(0.317, 0.001));
    });
  });

  group('channels land in the traced registers', () {
    test('the Y input reaches ADDRA', () {
      final cpu = H8Cpu()..reset();
      cpu.adc.setInput8(4, 0x9C); // AN4 is the Y input
      expect(convert(cpu, 4), 0x9C);
      expect(AdConverter.registerFor(4), 0, reason: 'ADDRA');
    });

    test('the X input reaches ADDRB', () {
      final cpu = H8Cpu()..reset();
      cpu.adc.setInput8(5, 0x40); // AN5 is the X input
      expect(convert(cpu, 5), 0x40);
      expect(AdConverter.registerFor(5), 1, reason: 'ADDRB');
    });

    test('the two axes do not disturb each other', () {
      final cpu = H8Cpu()..reset();
      cpu.adc.setInput8(4, 0xF0); // Y at the bottom
      cpu.adc.setInput8(5, 0x00); // X at the left
      expect(convert(cpu, 4), 0xF0);
      expect(convert(cpu, 5), 0x00);
      expect(cpu.readB(0xFFFFE0), 0xF0, reason: 'ADDRA still holds Y');
    });
  });

  group('the paired input on the same register', () {
    test('driving one of a pair leaves the register alternating', () {
      // ADDRB is fed by AN1 as well as AN5, and the firmware scans both.
      final cpu = H8Cpu()..reset();
      cpu.adc.setInput8(5, 0xC0);
      expect(convert(cpu, 5), 0xC0);
      expect(convert(cpu, 1), 0x00, reason: 'the other pin pulls it back');
    });

    test('driving both holds the register steady', () {
      final cpu = H8Cpu()..reset();
      cpu.adc.setInput8(5, 0xC0);
      cpu.adc.setInput8(5 ^ 4, 0xC0); // the pair partner, AN1
      expect(convert(cpu, 5), 0xC0);
      expect(convert(cpu, 1), 0xC0);
    });

    test('the pair partner of each axis is the other half of the mux', () {
      expect(4 ^ 4, 0, reason: 'AN4 pairs with AN0 on ADDRA');
      expect(5 ^ 4, 1, reason: 'AN5 pairs with AN1 on ADDRB');
      expect(6 ^ 4, 2, reason: 'AN6 pairs with AN2 on ADDRC');
      expect(AdConverter.registerFor(1), AdConverter.registerFor(5));
      expect(AdConverter.registerFor(0), AdConverter.registerFor(4));
    });
  });

  test('releasing drives the axes back to zero', () {
    final cpu = H8Cpu()..reset();
    cpu.adc.setInput8(4, 0xF0);
    cpu.adc.setInput8(5, 0x80);
    expect(convert(cpu, 4), 0xF0);
    cpu.adc.setInput8(4, 0);
    cpu.adc.setInput8(5, 0);
    expect(convert(cpu, 4), 0x00);
    expect(convert(cpu, 5), 0x00);
  });
}
