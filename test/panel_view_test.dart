// The Buttons tab's handling: click and hold, double-click to latch, click a
// latched key to let it go, and drag a knob.
//
// These drive the widget and check what reaches the [Keypad], which is the
// thing wired to the CPU. keypad_test.dart covers the other half -- that what
// reaches the keypad reaches the firmware.

import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/keypad.dart';
import 'package:sim_h83003/panel_view.dart';

/// A keypad with its wires stubbed: these tests are about the view.
Keypad detachedPad() => Keypad()
  ..peek = ((_) => 0)
  ..hold = ((_, _, _) {})
  ..release = ((_, _) {});

Future<void> pumpPanel(WidgetTester tester, Keypad pad) async {
  await tester.binding.setSurfaceSize(const Size(1200, 1000));
  addTearDown(() => tester.binding.setSurfaceSize(null));
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(
      body: PanelView(keypad: pad, repaint: ValueNotifier<int>(0)),
    ),
  ));
  await tester.pumpAndSettle();
}

/// Where a point in the panel drawing lands on screen.
Offset at(WidgetTester tester, double x, double y) {
  final box =
      tester.renderObject<RenderBox>(find.byKey(const Key('panelCanvas')));
  final s = box.size;
  final scale =
      math.min(s.width / Keypad.artWidth, s.height / Keypad.artHeight);
  final dx = (s.width - Keypad.artWidth * scale) / 2;
  final dy = (s.height - Keypad.artHeight * scale) / 2;
  return box.localToGlobal(Offset(dx + x * scale, dy + y * scale));
}

Offset atKey(WidgetTester tester, int code) {
  final k = Keypad.panelKeys.firstWhere((k) => k.code == code);
  return at(tester, k.x, k.y);
}

void main() {
  group('pressing a key', () {
    testWidgets('goes down while held and comes up on release',
        (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);

      final g = await tester.startGesture(atKey(tester, 0x77));
      await tester.pump();
      expect(pad.isDown(0x77), isTrue, reason: 'down while the button is held');

      await g.up();
      await tester.pump();
      expect(pad.isDown(0x77), isFalse, reason: 'and up again on release');
    });

    testWidgets('a click somewhere empty presses nothing', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);
      await tester.tapAt(at(tester, 600, 1100));
      await tester.pump();
      expect(pad.down, isEmpty);
    });

    testWidgets('only the key under the pointer goes down', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);
      final g = await tester.startGesture(atKey(tester, 0x73));
      await tester.pump();
      expect(pad.down, {0x73});
      await g.up();
    });
  });

  group('latching', () {
    testWidgets('a double click leaves the key down', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);

      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump(const Duration(milliseconds: 60));
      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump();

      expect(pad.isDown(0x77), isTrue);
      expect(pad.latched.contains(0x77), isTrue,
          reason: 'the second click latches it, so it stays down');
    });

    testWidgets('a latched key is let go by clicking it again',
        (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);

      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump(const Duration(milliseconds: 60));
      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump();
      expect(pad.isDown(0x77), isTrue);

      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump();
      expect(pad.isDown(0x77), isFalse);
      expect(pad.latched, isEmpty);
    });

    testWidgets('two slow clicks do not latch', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);

      await tester.tapAt(atKey(tester, 0x77));
      // The double-click window is wall time, not the test's fake clock, so
      // this has to be a real wait.
      await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 500)));
      await tester.pump();
      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump();
      expect(pad.latched, isEmpty, reason: 'too far apart to be a double');
      expect(pad.isDown(0x77), isFalse);
    });

    testWidgets('Release all lets go of everything', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);

      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump(const Duration(milliseconds: 60));
      await tester.tapAt(atKey(tester, 0x77));
      await tester.pump();
      expect(pad.down, isNotEmpty);

      await tester.tap(find.text('Release all'));
      await tester.pump();
      expect(pad.down, isEmpty);
      expect(pad.latched, isEmpty);
    });
  });

  group('turning a knob', () {
    testWidgets('dragging down turns it clockwise', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);
      final knob = pad.knobs[0];

      final g = await tester.startGesture(at(tester, knob.x, knob.y));
      await g.moveBy(const Offset(0, 60));
      await tester.pump();
      await g.up();

      expect(knob.pending, greaterThan(0),
          reason: 'down is clockwise, which counts up');
    });

    testWidgets('dragging up turns it the other way', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);
      final knob = pad.knobs[0];

      final g = await tester.startGesture(at(tester, knob.x, knob.y));
      await g.moveBy(const Offset(0, -60));
      await tester.pump();
      await g.up();

      expect(knob.pending, lessThan(0));
    });

    testWidgets('each knob turns on its own', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);

      final g = await tester.startGesture(at(tester, pad.knobs[1].x, pad.knobs[1].y));
      await g.moveBy(const Offset(0, 60));
      await tester.pump();
      await g.up();

      expect(pad.knobs[1].pending, greaterThan(0));
      expect(pad.knobs[0].pending, 0);
    });

    testWidgets('a drag on a knob presses no key', (tester) async {
      final pad = detachedPad();
      await pumpPanel(tester, pad);

      final g = await tester.startGesture(at(tester, pad.knobs[0].x, pad.knobs[0].y));
      await g.moveBy(const Offset(0, 40));
      await tester.pump();
      await g.up();

      expect(pad.down, isEmpty);
    });
  });
}
