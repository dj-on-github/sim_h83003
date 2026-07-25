// Smoke test: the app builds and shows the main scaffolding.

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
}
