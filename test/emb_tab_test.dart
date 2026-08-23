// The Network Connection section at the bottom of the SCI tab.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/main.dart';

Future<void> openSciView(WidgetTester tester) async {
  await tester.binding.setSurfaceSize(const Size(1600, 1400));
  addTearDown(() => tester.binding.setSurfaceSize(null));

  await tester.pumpWidget(const SimH8App());
  await tester.pumpAndSettle();
  await tester.tap(find.text('SCI'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('MEM'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('DIS'));
  await tester.pumpAndSettle();
  await tester.scrollUntilVisible(
      find.text('NETWORK CONNECTION'), 300,
      scrollable: find.byType(Scrollable).first);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('the section offers a port and a start button', (tester) async {
    await openSciView(tester);

    expect(find.byKey(const Key('relayPortField')), findsOneWidget);
    final portField =
        tester.widget<TextField>(find.byKey(const Key('relayPortField')));
    expect(portField.controller?.text, '8888');
    expect(find.text('Start relay'), findsOneWidget);
  });

  testWidgets('an unusable port number is refused', (tester) async {
    await openSciView(tester);

    await tester.enterText(find.byKey(const Key('relayPortField')), 'nope');
    await tester.tap(find.byKey(const Key('relayEnableButton')));
    await tester.pumpAndSettle();

    expect(find.textContaining('not a usable port'), findsOneWidget);
    expect(find.text('Start relay'), findsOneWidget,
        reason: 'still stopped, so the button still offers to start');
  });

  testWidgets('starting binds a listener and stopping releases it',
      (tester) async {
    await openSciView(tester);

    // A real bind, so this needs the loopback interface; use a high port to
    // avoid colliding with anything the machine already runs.
    await tester.enterText(find.byKey(const Key('relayPortField')), '8913');
    await tester.runAsync(() async {
      await tester.tap(find.byKey(const Key('relayEnableButton')));
      await Future<void>.delayed(const Duration(milliseconds: 300));
    });
    await tester.pumpAndSettle();

    expect(find.textContaining('Listening on 127.0.0.1:8913'), findsOneWidget);
    expect(find.text('Stop relay'), findsOneWidget);

    await tester.runAsync(() async {
      await tester.tap(find.byKey(const Key('relayEnableButton')));
      await Future<void>.delayed(const Duration(milliseconds: 300));
    });
    await tester.pumpAndSettle();

    expect(find.text('Start relay'), findsOneWidget);
    expect(find.textContaining('Relay stopped'), findsOneWidget);
  });
}
