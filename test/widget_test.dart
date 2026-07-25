// Smoke tests: the app builds, and the IO pane keeps its fixed width.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:sim_h83003/main.dart';

void main() {
  testWidgets('App builds and shows the register panel', (tester) async {
    await tester.pumpWidget(const SimH8App());
    expect(find.text('H8/3003 Simulator'), findsOneWidget);
    expect(find.text('PC '), findsOneWidget);
    expect(find.text('ER0 '), findsOneWidget);
    expect(find.text('CCR '), findsOneWidget);
  });

  testWidgets('IO pane stays 300 wide in the side-by-side layout',
      (tester) async {
    // A window far wider than the IO view needs: the pane must not grow
    // with it, leaving the extra room to the other views.
    await tester.binding.setSurfaceSize(const Size(1800, 1000));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    await tester.pumpWidget(const SimH8App());
    // Turn on the IO view (its toggle is off by default).
    await tester.tap(find.text('IO'));
    await tester.pumpAndSettle();

    expect(find.text('GPIO'), findsOneWidget);
    expect(tester.getSize(find.byKey(const Key('ioPane'))).width, 300.0);
  });
}
