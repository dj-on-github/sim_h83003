// The Watch tab: "stop when this is true" and "who wrote that".
//
// The breakpoint answers "stop here". Neither of these is that question, and
// until this pane existed each one meant writing another program in tool/
// with the test compiled into it.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/main.dart';

/// Shows the watch pane on its own, with the other panes switched off so it
/// has the width to itself.
///
/// The test window is 800px whatever setSurfaceSize is asked for, which is
/// wide enough for the side-by-side layout either way; the call is kept for
/// consistency with the other tab tests.
Future<void> openWatchTab(WidgetTester tester) async {
  await tester.binding.setSurfaceSize(const Size(1600, 1000));
  addTearDown(() => tester.binding.setSurfaceSize(null));

  await tester.pumpWidget(const SimH8App());
  await tester.pumpAndSettle();
  await tester.tap(find.text('WATCH'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('MEM'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('DIS'));
  await tester.pumpAndSettle();
}

Future<void> type(WidgetTester tester, Key field, String text) async {
  await tester.enterText(find.byKey(field), text);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('opens with nothing armed and nothing watched', (tester) async {
    await openWatchTab(tester);
    expect(find.text('STOP WHEN'), findsOneWidget);
    expect(find.textContaining('Nothing armed'), findsOneWidget);
    expect(find.textContaining('Nothing watched'), findsOneWidget);
    expect(find.text('Nothing recorded yet.'), findsOneWidget);
  });

  testWidgets('a condition is armed and shown back', (tester) async {
    await openWatchTab(tester);
    await type(tester, const Key('conditionField'), '[11B10E].w == 77');
    await tester.tap(find.byKey(const Key('conditionArmButton')));
    await tester.pumpAndSettle();

    expect(find.textContaining('Armed: [11B10E].w == 77'), findsOneWidget);
    expect(find.text('Re-arm'), findsOneWidget);
  });

  testWidgets('a condition that will not parse says why, in place',
      (tester) async {
    // In place rather than in a snack: a snack has gone by the time the user
    // looks back at the field to fix the typo.
    await openWatchTab(tester);
    await type(tester, const Key('conditionField'), 'pc == ');
    await tester.tap(find.byKey(const Key('conditionArmButton')));
    await tester.pumpAndSettle();

    expect(find.textContaining('stops short'), findsOneWidget);
    expect(find.textContaining('Nothing armed'), findsOneWidget,
        reason: 'a condition that did not parse is not armed');
  });

  testWidgets('disarming puts it back to nothing armed', (tester) async {
    await openWatchTab(tester);
    await type(tester, const Key('conditionField'), 'pc == 100');
    await tester.tap(find.byKey(const Key('conditionArmButton')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('conditionDisarmButton')));
    await tester.pumpAndSettle();

    expect(find.textContaining('Nothing armed'), findsOneWidget);
    expect(find.text('Arm'), findsOneWidget);
  });

  testWidgets('an address is added to the watch list as a chip',
      (tester) async {
    await openWatchTab(tester);
    await type(tester, const Key('watchAddressField'), '11B10E-11B10F');
    await tester.tap(find.byKey(const Key('watchAddButton')));
    await tester.pumpAndSettle();

    expect(find.textContaining("H'11B10E-11B10F"), findsOneWidget);
    expect(find.textContaining('Nothing watched'), findsNothing);
  });

  testWidgets('one that is not an address is refused, not guessed at',
      (tester) async {
    await openWatchTab(tester);
    await type(tester, const Key('watchAddressField'), 'sausage');
    await tester.tap(find.byKey(const Key('watchAddButton')));
    await tester.pumpAndSettle();

    expect(find.textContaining('Not an address or range'), findsOneWidget);
    expect(find.textContaining('Nothing watched'), findsOneWidget);
  });

  testWidgets('running records the write and names the instruction',
      (tester) async {
    await openWatchTab(tester);
    // The demo program sums 1..10 and stores the total at H'FFFD10. The
    // store is the instruction at H'000112, which is the answer the log is
    // there to give.
    await type(tester, const Key('watchAddressField'), 'FFFD10');
    await tester.tap(find.byKey(const Key('watchAddButton')));
    await tester.pumpAndSettle();

    for (var i = 0; i < 60; i++) {
      await tester.tap(find.text('Step'));
      await tester.pumpAndSettle();
    }

    expect(find.text('WRITTEN BY'), findsOneWidget,
        reason: 'the table appears once there is something in it');
    expect(find.textContaining("H'000112"), findsOneWidget);
    expect(find.textContaining("H'00 -> H'37"), findsOneWidget,
        reason: 'the sum of 1..10, and what it displaced');
    expect(find.text('Nothing recorded yet.'), findsNothing);
  });

  testWidgets('the log can be cleared without disarming the watch',
      (tester) async {
    await openWatchTab(tester);
    await type(tester, const Key('watchAddressField'), 'FFFD10');
    await tester.tap(find.byKey(const Key('watchAddButton')));
    await tester.pumpAndSettle();
    for (var i = 0; i < 60; i++) {
      await tester.tap(find.text('Step'));
      await tester.pumpAndSettle();
    }
    expect(find.text('Nothing recorded yet.'), findsNothing,
        reason: 'there has to be something there for Clear to mean anything');

    await tester.tap(find.byKey(const Key('watchClearLogButton')));
    await tester.pumpAndSettle();

    expect(find.text('Nothing recorded yet.'), findsOneWidget);
    expect(find.textContaining("H'FFFD10"), findsOneWidget,
        reason: 'the watch stays armed; only what it caught is forgotten');
  });

  testWidgets('lays out in a narrow pane without overflowing',
      (tester) async {
    // This pane shares the window with the others in the wide layout, so it
    // has to survive being squeezed. A RenderFlex overflow fails the test.
    await tester.binding.setSurfaceSize(const Size(1600, 1000));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(const SimH8App());
    await tester.pumpAndSettle();
    await tester.tap(find.text('WATCH'));
    await tester.pumpAndSettle();

    // Every other pane left on, so the watch pane gets a sliver of the width.
    await type(tester, const Key('watchAddressField'), '11B10E-11B10F');
    await tester.tap(find.byKey(const Key('watchAddButton')));
    await tester.pumpAndSettle();
    await type(tester, const Key('conditionField'), '[11B10E].w == 77');
    await tester.tap(find.byKey(const Key('conditionArmButton')));
    await tester.pumpAndSettle();

    expect(tester.takeException(), isNull);
  });

  testWidgets('the fields shrink to the pane rather than being cut off',
      (tester) async {
    // With every pane on, the watch pane gets roughly a third of the window.
    // A fixed-width field wider than that is a field with its end off screen,
    // and a Wrap does not report that the way a Row would.
    await tester.pumpWidget(const SimH8App());
    await tester.pumpAndSettle();
    await tester.tap(find.text('WATCH'));
    await tester.pumpAndSettle();

    final pane = tester
        .getSize(find
            .ancestor(
                of: find.text('STOP WHEN'),
                matching: find.byType(SingleChildScrollView))
            .first)
        .width;
    for (final k in [const Key('conditionField'), const Key('watchAddressField')]) {
      expect(tester.getSize(find.byKey(k)).width, lessThanOrEqualTo(pane),
          reason: '$k is wider than the ${pane}px pane it sits in');
    }
  });
}
