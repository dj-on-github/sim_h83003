// The EEPROM tab: switching the model on, pointing it at a file, and what
// it shows of what is in the part.
//
// With the model off the two port pins are ordinary port bits and the
// firmware's writes go nowhere, which is how the simulator behaved before
// this existed. With it on the device answers, and the array is kept in a
// JSON file so that what one session wrote is there in the next.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/main.dart';

/// Flips the enable switch. The handler reads the file, which is real
/// asynchronous I/O and cannot finish inside the fake-async zone
/// testWidgets normally runs in -- hence runAsync.
Future<void> toggleEeprom(WidgetTester tester) async {
  await tester.runAsync(() async {
    await tester.tap(find.byKey(const Key('eepromEnableSwitch')));
    await Future<void>.delayed(const Duration(milliseconds: 200));
    await tester.pump();
  });
  await tester.pumpAndSettle();
}

Future<void> pressAsync(WidgetTester tester, Key key) async {
  await tester.runAsync(() async {
    await tester.tap(find.byKey(key));
    await Future<void>.delayed(const Duration(milliseconds: 200));
    await tester.pump();
  });
  await tester.pumpAndSettle();
}

/// Shows the EEPROM pane on its own, the way the flash tab's tests do: a
/// wide window so the side-by-side layout is used, then the other panes
/// switched off so this one has the width to lay out in.
Future<void> openEepromTab(WidgetTester tester) async {
  await tester.binding.setSurfaceSize(const Size(1600, 1200));
  addTearDown(() => tester.binding.setSurfaceSize(null));

  await tester.pumpWidget(const SimH8App());
  await tester.pumpAndSettle();
  await tester.tap(find.text('EEPROM'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('MEM'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('DIS'));
  await tester.pumpAndSettle();
}

/// A JSON file holding one byte at one address.
File writeContents(Directory dir, Map<String, String> bytes) {
  final f = File('${dir.path}/eeprom.json');
  f.writeAsStringSync(json.encode({'bytes': bytes}));
  return f;
}

void main() {
  testWidgets('the tab starts off, pointed at eeprom.json', (tester) async {
    await openEepromTab(tester);

    final sw =
        tester.widget<Switch>(find.byKey(const Key('eepromEnableSwitch')));
    expect(sw.value, isFalse);
    expect(find.textContaining('ordinary port bits'), findsOneWidget);

    final field =
        tester.widget<TextField>(find.byKey(const Key('eepromPathField')));
    expect(field.controller!.text, 'eeprom.json');
    expect(find.byKey(const Key('eepromBrowseButton')), findsOneWidget);
  });

  testWidgets('the addresses the firmware uses are named', (tester) async {
    await openEepromTab(tester);

    expect(find.text("H'A9"), findsWidgets);
    expect(find.text("H'AA"), findsWidgets);
    expect(find.textContaining('Foot lift'), findsWidgets);
    // An untouched part reads H'FF everywhere, and says so rather than
    // showing 255 as though it meant something.
    expect(find.textContaining('never written'), findsWidgets);
  });

  testWidgets('the contents are shown as a dump', (tester) async {
    await openEepromTab(tester);

    final dump = tester.widget<SelectableText>(find.byKey(const Key('eepromDump')));
    final text = dump.data!;
    expect(text.split('\n').length, 16, reason: '256 bytes, sixteen a row');
    expect(text, startsWith('00  FF FF'));
  });

  testWidgets('switching on with no file starts blank and says so',
      (tester) async {
    final dir = Directory.systemTemp.createTempSync('eepromtab');
    addTearDown(() => dir.deleteSync(recursive: true));

    await openEepromTab(tester);
    await tester.enterText(
        find.byKey(const Key('eepromPathField')), '${dir.path}/fresh.json');
    await toggleEeprom(tester);

    expect(find.textContaining('Starting blank'), findsOneWidget);
    final sw =
        tester.widget<Switch>(find.byKey(const Key('eepromEnableSwitch')));
    expect(sw.value, isTrue);
  });

  testWidgets('a file that is there is loaded and shown', (tester) async {
    final dir = Directory.systemTemp.createTempSync('eepromtab');
    addTearDown(() => dir.deleteSync(recursive: true));
    final f = writeContents(dir, {'A9': '3C', 'AA': '41'});

    await openEepromTab(tester);
    await tester.enterText(find.byKey(const Key('eepromPathField')), f.path);
    await toggleEeprom(tester);

    expect(find.textContaining('Loaded 256 bytes'), findsOneWidget);
    // H'3C is H'28 above the value the machine works in, so both are shown.
    expect(find.textContaining("H'3C (60)"), findsOneWidget);
    expect(find.textContaining('less the H\'28 trim: 20'), findsOneWidget);

    final dump =
        tester.widget<SelectableText>(find.byKey(const Key('eepromDump')));
    expect(dump.data, contains('A0  FF FF FF FF FF FF FF FF  FF 3C 41'));
  });

  testWidgets('a file that cannot be read leaves the model off',
      (tester) async {
    final dir = Directory.systemTemp.createTempSync('eepromtab');
    addTearDown(() => dir.deleteSync(recursive: true));
    final f = File('${dir.path}/bad.json')..writeAsStringSync('not json');

    await openEepromTab(tester);
    await tester.enterText(find.byKey(const Key('eepromPathField')), f.path);
    await toggleEeprom(tester);

    expect(find.textContaining('Could not read'), findsOneWidget);
    expect(find.textContaining('Nothing has been changed'), findsOneWidget);
    final sw =
        tester.widget<Switch>(find.byKey(const Key('eepromEnableSwitch')));
    expect(sw.value, isFalse,
        reason: 'carrying on would overwrite the file with a blank array');
  });

  testWidgets('erasing writes the file, so the change survives the session',
      (tester) async {
    final dir = Directory.systemTemp.createTempSync('eepromtab');
    addTearDown(() => dir.deleteSync(recursive: true));
    final f = writeContents(dir, {'A9': '3C'});

    await openEepromTab(tester);
    await tester.enterText(find.byKey(const Key('eepromPathField')), f.path);
    await toggleEeprom(tester);
    expect(find.textContaining("H'3C"), findsWidgets);

    await pressAsync(tester, const Key('eepromEraseButton'));

    expect(find.textContaining('Erased'), findsOneWidget);
    final written = json.decode(f.readAsStringSync()) as Map<String, dynamic>;
    expect(written['rows'], isA<List>());
    expect((written['rows'] as List).first, startsWith('00: FF FF'));
    expect(written['fields'].toString(), contains('never written'));
  });

  testWidgets('reload takes the file back off the disk', (tester) async {
    final dir = Directory.systemTemp.createTempSync('eepromtab');
    addTearDown(() => dir.deleteSync(recursive: true));
    final f = writeContents(dir, {'A9': '3C'});

    await openEepromTab(tester);
    await tester.enterText(find.byKey(const Key('eepromPathField')), f.path);
    await toggleEeprom(tester);

    // Changed by something else while the simulator is running -- which is
    // the reason the path is kept rather than the bytes.
    f.writeAsStringSync(json.encode({
      'bytes': {'A9': '77'}
    }));
    await pressAsync(tester, const Key('eepromReloadButton'));

    expect(find.textContaining("H'77"), findsWidgets);
  });

  testWidgets('the address counter can be made to agree with the verify',
      (tester) async {
    await openEepromTab(tester);

    final box = find.byKey(const Key('eepromVerifyFriendlyCheck'));
    expect(tester.widget<CheckboxListTile>(box).value, isFalse);
    await tester.tap(box);
    await tester.pumpAndSettle();
    expect(tester.widget<CheckboxListTile>(box).value, isTrue);
  });
}
