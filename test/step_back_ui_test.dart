// The Back button, and the setting that makes it work.
//
// Recording costs speed, so it is off until asked for; the button says so by
// being disabled rather than by doing nothing when pressed.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/main.dart';

Future<void> openApp(WidgetTester tester) async {
  await tester.pumpWidget(const SimH8App());
  await tester.pumpAndSettle();
}

Future<void> turnHistoryOn(WidgetTester tester) async {
  await tester.tap(find.byTooltip('Settings'));
  await tester.pumpAndSettle();
  await tester.tap(find.byKey(const Key('historyEnableCheckbox')));
  await tester.pumpAndSettle();
  await tester.tap(find.text('OK'));
  await tester.pumpAndSettle();
}

bool enabled(WidgetTester tester, String label) {
  final button = tester.widget<ButtonStyleButton>(
      find.ancestor(of: find.text(label), matching: find.byType(OutlinedButton))
          .first);
  return button.onPressed != null;
}

void main() {
  testWidgets('Back is there, and off until history is kept', (tester) async {
    await openApp(tester);
    expect(find.text('Back'), findsOneWidget);
    expect(enabled(tester, 'Back'), isFalse,
        reason: 'nothing has been recorded, so there is nothing to go back to');
  });

  testWidgets('turning history on and stepping makes it available',
      (tester) async {
    await openApp(tester);
    await turnHistoryOn(tester);
    expect(enabled(tester, 'Back'), isFalse, reason: 'still nothing recorded');

    await tester.tap(find.text('Step'));
    await tester.pumpAndSettle();
    expect(enabled(tester, 'Back'), isTrue);
  });

  testWidgets('it puts the machine back where it was', (tester) async {
    await openApp(tester);
    await turnHistoryOn(tester);
    for (var i = 0; i < 5; i++) {
      await tester.tap(find.text('Step'));
      await tester.pumpAndSettle();
    }
    // The register panel shows the PC as six hex digits next to its label;
    // whatever it says now it should say again after three steps forward
    // and three back.
    String pc() {
      final row =
          find.ancestor(of: find.text('PC '), matching: find.byType(Row)).first;
      return tester
          .widgetList<Text>(find.descendant(of: row, matching: find.byType(Text)))
          .elementAt(1)
          .data!;
    }

    final after5 = pc();
    for (var i = 0; i < 3; i++) {
      await tester.tap(find.text('Step'));
      await tester.pumpAndSettle();
    }
    expect(pc(), isNot(after5));

    for (var i = 0; i < 3; i++) {
      await tester.tap(find.text('Back'));
      await tester.pumpAndSettle();
    }
    expect(pc(), after5);
  });

  testWidgets('it runs out when the history does', (tester) async {
    await openApp(tester);
    await turnHistoryOn(tester);
    await tester.tap(find.text('Step'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Back'));
    await tester.pumpAndSettle();
    expect(enabled(tester, 'Back'), isFalse);
  });

  testWidgets('the setting says how much is being held', (tester) async {
    await openApp(tester);
    await turnHistoryOn(tester);
    for (var i = 0; i < 10; i++) {
      await tester.tap(find.text('Step'));
      await tester.pumpAndSettle();
    }
    await tester.tap(find.byTooltip('Settings'));
    await tester.pumpAndSettle();
    expect(find.textContaining('instruction(s) held'), findsOneWidget);
    expect(find.byKey(const Key('historyDepthDropdown')), findsOneWidget);
  });
}
