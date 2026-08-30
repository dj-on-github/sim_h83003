// The host-port section at the bottom of the SCI tab.
//
// The port list comes from the native library, which is not present in a
// test run, so the list is empty here — which is exactly the case the UI has
// to handle without falling over. That is also the everyday case on macOS
// for anyone bridging to a pty, since libserialport will not open one: hence
// the TCP transport, which the last two tests here cover.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/main.dart';

/// Shows the SCI pane on its own, wide enough to lay out.
Future<void> openSciView(WidgetTester tester) async {
  await tester.binding.setSurfaceSize(const Size(1600, 1200));
  addTearDown(() => tester.binding.setSurfaceSize(null));

  await tester.pumpWidget(const SimH8App());
  await tester.pumpAndSettle();
  // The pane lays out in a narrow column on the way here, which is the
  // width the side-by-side layout gives it with three panes showing.
  await tester.tap(find.text('SCI'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('MEM'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('DIS'));
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('the section offers a port, a channel and a switch',
      (tester) async {
    await openSciView(tester);
    await tester.scrollUntilVisible(
        find.text('HOST SERIAL PORT'), 300,
        scrollable: find.byType(Scrollable).first);
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('serialTransportDropdown')), findsOneWidget);
    expect(find.byKey(const Key('serialPortDropdown')), findsOneWidget);
    expect(find.byKey(const Key('serialChannelDropdown')), findsOneWidget);
    expect(find.byKey(const Key('serialPhiField')), findsOneWidget);
    expect(find.byKey(const Key('serialRefreshButton')), findsOneWidget);

    final sw =
        tester.widget<Switch>(find.byKey(const Key('serialEnableSwitch')));
    expect(sw.value, isFalse, reason: 'the bridge starts off');
  });

  testWidgets('it defaults to SCI1 and shows the rate the ROM programs',
      (tester) async {
    await openSciView(tester);
    await tester.scrollUntilVisible(
        find.text('HOST SERIAL PORT'), 300,
        scrollable: find.byType(Scrollable).first);
    await tester.pumpAndSettle();

    final channel = tester.widget<DropdownButtonFormField<int>>(
        find.byKey(const Key('serialChannelDropdown')));
    expect(channel.initialValue, 1, reason: 'SCI1 is the machine\'s PC port');

    final phi =
        tester.widget<TextField>(find.byKey(const Key('serialPhiField')));
    expect(phi.controller?.text, '11059200');
  });

  testWidgets('switching on without a port says so and stays off',
      (tester) async {
    await openSciView(tester);
    await tester.scrollUntilVisible(
        find.text('HOST SERIAL PORT'), 300,
        scrollable: find.byType(Scrollable).first);
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const Key('serialEnableSwitch')));
    await tester.pumpAndSettle();

    expect(find.textContaining('Choose a serial port'), findsOneWidget);
    final sw =
        tester.widget<Switch>(find.byKey(const Key('serialEnableSwitch')));
    expect(sw.value, isFalse);
  });

  testWidgets('choosing TCP swaps the port list for a port number',
      (tester) async {
    await openSciView(tester);
    await tester.scrollUntilVisible(find.text('HOST SERIAL PORT'), 300,
        scrollable: find.byType(Scrollable).first);
    await tester.pumpAndSettle();

    // Serial to begin with: the list and its Refresh button.
    expect(find.byKey(const Key('serialPortDropdown')), findsOneWidget);
    expect(find.byKey(const Key('serialTcpPortField')), findsNothing);

    await tester.tap(find.byKey(const Key('serialTransportDropdown')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('TCP').last);
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('serialTcpPortField')), findsOneWidget);
    expect(find.byKey(const Key('serialPortDropdown')), findsNothing,
        reason: 'the port list is meaningless over a socket');
    expect(find.byKey(const Key('serialRefreshButton')), findsNothing);

    final field =
        tester.widget<TextField>(find.byKey(const Key('serialTcpPortField')));
    expect(field.controller?.text, '5555');
  });

  testWidgets('over TCP it does not ask for a serial port', (tester) async {
    await openSciView(tester);
    await tester.scrollUntilVisible(find.text('HOST SERIAL PORT'), 300,
        scrollable: find.byType(Scrollable).first);
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const Key('serialTransportDropdown')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('TCP').last);
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const Key('serialEnableSwitch')));
    await tester.pumpAndSettle();

    expect(find.textContaining('Choose a serial port'), findsNothing,
        reason: 'that complaint belongs to the serial transport only');
  });
}
