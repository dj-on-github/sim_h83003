// What the board hangs off each port pin, where that is known.
//
// The H8/3003 has no opinion about any of it, so every entry here is
// something worked out from the firmware or from the machine. A pin with no
// entry shows as its number, which is honest; a wrong name would be believed.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/keypad.dart';
import 'package:sim_h83003/pin_roles.dart';

void main() {
  sewingLightWiringTest();
  group('the sewing light', () {
    test('is port 4 bit 2, and a 1 turns it on', () {
      final r = pinRole('4', 2);
      expect(r, isNotNull);
      expect(r!.name, 'sewing light');
      expect(r.activeHigh, isTrue);
      expect(r.detail, contains('1 turns it on'));
    });
  });

  group('the table', () {
    test('an unknown pin has no name rather than a guessed one', () {
      expect(pinRole('4', 3), isNull);
      expect(pinRole('9', 0), isNull);
    });

    test('port names are matched either case', () {
      expect(pinRole('c', 0)?.name, 'key strobe 0');
      expect(pinRole('C', 0)?.name, 'key strobe 0');
    });

    test('no pin is named twice', () {
      final seen = <String>{};
      for (final r in artista180PinRoles) {
        expect(seen.add('${r.port.toUpperCase()}:${r.bit}'), isTrue,
            reason: '${r.where} appears more than once');
      }
    });

    test('every entry says something useful', () {
      for (final r in artista180PinRoles) {
        expect(r.name.trim(), isNotEmpty);
        expect(r.detail.trim().length, greaterThan(20),
            reason: '${r.where} needs a real explanation');
        expect(r.bit, inInclusiveRange(0, 7));
      }
    });
  });

  group('it agrees with the rest of the simulator', () {
    test('the key strobes are the ones the keypad drives', () {
      for (final bit in Keypad.strobeBits) {
        final r = pinRole('C', bit);
        expect(r, isNotNull, reason: 'port C bit $bit strobes a bank');
        expect(r!.name, startsWith('key strobe'));
        expect(r.activeHigh, isFalse, reason: 'a strobe is driven low');
      }
    });

    test('the knob pairs are named on the bits the knobs use', () {
      final pad = Keypad();
      for (final k in pad.knobs) {
        for (final bit in [k.shift, k.shift + 1]) {
          final r = pinRole('C', bit);
          expect(r, isNotNull, reason: 'port C bit $bit is half a knob');
          expect(r!.name, contains(k.name.split(' ').last),
              reason: 'bit $bit belongs to the ${k.name} knob');
        }
      }
    });

    test('reverse is named where the keypad drives it', () {
      final rev =
          Keypad.panelKeys.firstWhere((k) => k.wiring == KeyWiring.pin);
      expect(rev.pinDr, Keypad.p8Dr);
      final r = pinRole('8', rev.pinBit);
      expect(r, isNotNull);
      expect(r!.name, contains('reverse'));
    });
  });
}

/// The bulb on the Buttons panel reads this entry rather than a hard-coded
/// address, so the two cannot drift apart.
void sewingLightWiringTest() {
  test('the sewing light entry is what the panel indicator looks up', () {
    final r = artista180PinRoles
        .where((r) => r.name == 'sewing light')
        .toList();
    expect(r.length, 1,
        reason: 'the indicator finds it by name, so there must be exactly '
            'one');
    expect(r.single.port, '4');
    expect(r.single.bit, 2);
    expect(r.single.activeHigh, isTrue,
        reason: 'a 1 lights it, which is what the indicator compares against');
  });
}
