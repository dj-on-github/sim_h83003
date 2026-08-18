// The Trace tab: adding, reading, editing and deleting a watched location,
// and the flash that marks a change.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/main.dart';

/// Pumps a TraceView over [cpu] with the given entries.
Future<void> pumpTrace(
  WidgetTester tester,
  H8Cpu cpu,
  List<TraceEntry> entries, {
  Map<String, int> symbols = const {},
  void Function(int)? onDelete,
}) async {
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(
      body: TraceView(
        cpu: cpu,
        entries: entries,
        resolve: (t) => symbols[t.trim()] ?? symbolAddress(t.trim()),
        onAdd: () {},
        onEdit: (_) {},
        onDelete: onDelete ?? (_) {},
        font: 'monospace',
      ),
    ),
  ));
}

/// The value chip's background, used to tell a flash from a resting row.
Color? valueBackground(WidgetTester tester) {
  final box = tester.widget<AnimatedContainer>(
    find.ancestor(
      of: find.textContaining("H'", findRichText: false).last,
      matching: find.byType(AnimatedContainer),
    ),
  );
  return (box.decoration as BoxDecoration?)?.color;
}

void main() {
  testWidgets('an empty list explains itself', (tester) async {
    await pumpTrace(tester, H8Cpu(), []);
    expect(find.textContaining('No locations traced'), findsOneWidget);
    expect(find.text('Add trace'), findsOneWidget);
  });

  testWidgets('a byte is read and shown in hex', (tester) async {
    final cpu = H8Cpu()..mem.poke(0x1234, 0x5A);
    await pumpTrace(tester, cpu, [TraceEntry(target: '1234')]);
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text("H'5A"), findsOneWidget);
    expect(find.textContaining("H'001234"), findsOneWidget);
  });

  testWidgets('a symbol name resolves to its address', (tester) async {
    final cpu = H8Cpu()..mem.poke(0xFFFEF0, 0xA0);
    await pumpTrace(
      tester,
      cpu,
      [TraceEntry(target: 'an4_sample')],
      symbols: {'an4_sample': 0xFFFEF0},
    );
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text("H'A0"), findsOneWidget);
    expect(find.textContaining("H'FFFEF0"), findsOneWidget);
  });

  testWidgets('an unresolved target says so instead of showing a value',
      (tester) async {
    await pumpTrace(tester, H8Cpu(), [TraceEntry(target: 'no_such_symbol')]);
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text('unknown symbol or address'), findsOneWidget);
    expect(find.text("H'--"), findsOneWidget);
  });

  group('width and byte order', () {
    testWidgets('16-bit big endian reads high byte first', (tester) async {
      final cpu = H8Cpu()
        ..mem.poke(0x2000, 0x12)
        ..mem.poke(0x2001, 0x34);
      await pumpTrace(
          tester, cpu, [TraceEntry(target: '2000', bits: 16)]);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.text("H'1234"), findsOneWidget);
    });

    testWidgets('16-bit little endian swaps them', (tester) async {
      final cpu = H8Cpu()
        ..mem.poke(0x2000, 0x12)
        ..mem.poke(0x2001, 0x34);
      await pumpTrace(tester, cpu,
          [TraceEntry(target: '2000', bits: 16, bigEndian: false)]);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.text("H'3412"), findsOneWidget);
    });

    testWidgets('32-bit reads four bytes each way', (tester) async {
      final cpu = H8Cpu()
        ..mem.poke(0x3000, 0xDE)
        ..mem.poke(0x3001, 0xAD)
        ..mem.poke(0x3002, 0xBE)
        ..mem.poke(0x3003, 0xEF);
      await pumpTrace(
          tester, cpu, [TraceEntry(target: '3000', bits: 32)]);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.text("H'DEADBEEF"), findsOneWidget);

      await pumpTrace(tester, cpu,
          [TraceEntry(target: '3000', bits: 32, bigEndian: false)]);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.text("H'EFBEADDE"), findsOneWidget);
    });

    testWidgets('an on-chip register is read through the peripheral model',
        (tester) async {
      final cpu = H8Cpu()..reset();
      cpu.adc.setInput8(4, 0x77);
      cpu.writeB(0xFFFFE8, 0x20 | 4); // start a conversion on AN4
      cpu.adc.tick(cpu.cycles + 300);
      await pumpTrace(tester, cpu, [TraceEntry(target: 'FFFFE0')]);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.text("H'77"), findsOneWidget, reason: 'ADDRAH, not memory');
    });
  });

  testWidgets('a change flashes and then settles', (tester) async {
    final cpu = H8Cpu()..mem.poke(0x40, 0x01);
    await pumpTrace(tester, cpu, [TraceEntry(target: '40')]);
    await tester.pump(const Duration(milliseconds: 100));
    final resting = valueBackground(tester);
    expect(find.text("H'01"), findsOneWidget);

    cpu.mem.poke(0x40, 0x02);
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text("H'02"), findsOneWidget);
    expect(valueBackground(tester), const Color(0xFFC62828),
        reason: 'the change should flash red');

    // Let the flash expire.
    await tester.pump(const Duration(milliseconds: 600));
    await tester.pump(const Duration(milliseconds: 200));
    expect(valueBackground(tester), resting);
  });

  testWidgets('the first sample is not treated as a change', (tester) async {
    final cpu = H8Cpu()..mem.poke(0x50, 0x99);
    await pumpTrace(tester, cpu, [TraceEntry(target: '50')]);
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text("H'99"), findsOneWidget);
    expect(valueBackground(tester), isNot(const Color(0xFFC62828)));
  });

  testWidgets('the delete button reports its row', (tester) async {
    var deleted = -1;
    await pumpTrace(
      tester,
      H8Cpu(),
      [TraceEntry(target: '10'), TraceEntry(target: '20')],
      onDelete: (i) => deleted = i,
    );
    await tester.pump(const Duration(milliseconds: 100));
    await tester.tap(find.byIcon(Icons.delete_outline).last);
    expect(deleted, 1);
  });
}
