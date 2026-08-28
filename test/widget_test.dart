// Smoke tests: the app builds, the IO pane keeps its fixed width, and the
// memory view can be pinned instead of chasing the PC.

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

  group('following the PC in the memory view', () {
    Finder switchFinder() => find.byKey(const Key('followPcSwitch'));

    testWidgets('is on by default, next to Go to', (tester) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(const SimH8App());

      expect(find.text('Follow PC'), findsOneWidget);
      expect(switchFinder(), findsOneWidget);
      expect(tester.widget<Switch>(switchFinder()).value, isTrue);
    });

    testWidgets('can be switched off and back on', (tester) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(const SimH8App());

      await tester.tap(switchFinder());
      await tester.pumpAndSettle();
      expect(tester.widget<Switch>(switchFinder()).value, isFalse);

      await tester.tap(switchFinder());
      await tester.pumpAndSettle();
      expect(tester.widget<Switch>(switchFinder()).value, isTrue);
    });

    testWidgets('with it off, stepping leaves the view where it was',
        (tester) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(const SimH8App());

      // Park the view a long way from the PC.
      final list = find.byType(Scrollable).first;
      await tester.drag(list, const Offset(0, -4000));
      await tester.pumpAndSettle();
      final parked = tester.widget<Scrollable>(list).controller!.offset;

      await tester.tap(switchFinder());
      await tester.pumpAndSettle();

      await tester.tap(find.text('Step'));
      await tester.pumpAndSettle();
      expect(tester.widget<Scrollable>(list).controller!.offset, parked,
          reason: 'the view should not chase the PC while pinned');
    });

    testWidgets('with it off, resetting leaves the view where it was',
        (tester) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(const SimH8App());

      // Park the view a long way from the reset vector's target.
      final list = find.byType(Scrollable).first;
      await tester.drag(list, const Offset(0, -4000));
      await tester.pumpAndSettle();
      final parked = tester.widget<Scrollable>(list).controller!.offset;

      await tester.tap(switchFinder());
      await tester.pumpAndSettle();

      await tester.tap(find.byTooltip("Reset (load reset vector at H'000000)"));
      await tester.pumpAndSettle();
      expect(tester.widget<Scrollable>(list).controller!.offset, parked,
          reason: 'a reset should not move the view while it is pinned');
    });

    testWidgets('with it on, resetting brings the PC back into view',
        (tester) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(const SimH8App());

      final list = find.byType(Scrollable).first;
      await tester.drag(list, const Offset(0, -4000));
      await tester.pumpAndSettle();
      final parked = tester.widget<Scrollable>(list).controller!.offset;

      await tester.tap(find.byTooltip("Reset (load reset vector at H'000000)"));
      await tester.pumpAndSettle();
      expect(tester.widget<Scrollable>(list).controller!.offset,
          isNot(parked),
          reason: 'following the PC, the view should come back to it');
    });
  });
}
