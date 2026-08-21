// The Flash tab: switching the model on and off, and pointing it at a file.
//
// With the model off every address is ordinary RAM, which is how the
// simulator has always behaved; with it on, the two regions hold what the
// image file holds and only a proper erase or program sequence changes them.

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/main.dart';

/// Writes a flash image holding the two devices back to back, with [fill]
/// repeated through both.
File writeImage(Directory dir, int fill) {
  final f = File('${dir.path}/flash.bin');
  f.writeAsBytesSync(Uint8List(flashImageSize())..fillRange(0, flashImageSize(), fill));
  return f;
}

/// Flips the enable switch. The handler reads the image file, which is real
/// asynchronous I/O and so cannot complete inside the fake-async zone
/// testWidgets normally runs in -- hence runAsync.
Future<void> toggleFlash(WidgetTester tester) async {
  await tester.runAsync(() async {
    await tester.tap(find.byKey(const Key('flashEnableSwitch')));
    // The handler is not awaited by the tap, so the real read has to be
    // given room to finish before the fake-async zone resumes.
    await Future<void>.delayed(const Duration(milliseconds: 200));
    await tester.pump();
  });
  await tester.pumpAndSettle();
}

/// Shows the flash pane on its own. The window is made wide so the app uses
/// the side-by-side layout, then the other panes are switched off, which
/// leaves the flash view the whole width to lay out in.
Future<void> openFlashTab(WidgetTester tester) async {
  await tester.binding.setSurfaceSize(const Size(1600, 1000));
  addTearDown(() => tester.binding.setSurfaceSize(null));

  await tester.pumpWidget(const SimH8App());
  await tester.pumpAndSettle();
  await tester.tap(find.text('FLASH'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('MEM'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('DIS'));
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('the tab starts with the model off', (tester) async {
    await openFlashTab(tester);

    final sw = tester.widget<Switch>(find.byKey(const Key('flashEnableSwitch')));
    expect(sw.value, isFalse);
    expect(find.textContaining('behaves as RAM'), findsOneWidget);
    expect(find.byKey(const Key('flashPathField')), findsOneWidget);
    expect(find.byKey(const Key('flashBrowseButton')), findsOneWidget);
  });

  testWidgets('the image can be saved whether the model is on or off',
      (tester) async {
    await openFlashTab(tester);

    // Saving reads memory, not the devices, so it is useful with the model
    // off too: that is how a starting image is made from a memory dump.
    final save = tester.widget<OutlinedButton>(
        find.byKey(const Key('flashSaveButton')));
    expect(save.onPressed, isNotNull);

    // Reload only makes sense once an image is loaded.
    final reload = tester.widget<OutlinedButton>(
        find.byKey(const Key('flashReloadButton')));
    expect(reload.onPressed, isNull);
  });

  testWidgets('the regions are shown so the layout can be checked',
      (tester) async {
    await openFlashTab(tester);

    expect(find.text('boot'), findsOneWidget);
    expect(find.text('application'), findsOneWidget);
    expect(find.text("H'000000"), findsWidgets);
    expect(find.text("H'200000"), findsWidgets);
  });

  testWidgets('switching on without a file says so and stays off',
      (tester) async {
    await openFlashTab(tester);

    await toggleFlash(tester);

    expect(find.textContaining('Choose a flash image file'), findsOneWidget);
    final sw = tester.widget<Switch>(find.byKey(const Key('flashEnableSwitch')));
    expect(sw.value, isFalse);
  });

  testWidgets('an unreadable path is reported and leaves the model off',
      (tester) async {
    await openFlashTab(tester);

    await tester.enterText(
        find.byKey(const Key('flashPathField')), '/no/such/flash.bin');
    await toggleFlash(tester);

    expect(find.textContaining('Could not read'), findsOneWidget);
    final sw = tester.widget<Switch>(find.byKey(const Key('flashEnableSwitch')));
    expect(sw.value, isFalse);
  });

  testWidgets('a two-device image is loaded and the model comes on',
      (tester) async {
    final dir = Directory.systemTemp.createTempSync('flashtab');
    addTearDown(() => dir.deleteSync(recursive: true));
    final image = writeImage(dir, 0x5A);

    await openFlashTab(tester);
    await tester.enterText(find.byKey(const Key('flashPathField')), image.path);
    await toggleFlash(tester);

    final sw = tester.widget<Switch>(find.byKey(const Key('flashEnableSwitch')));
    expect(sw.value, isTrue);
    expect(find.textContaining('two devices back to back'), findsOneWidget);
    expect(find.textContaining('erase or program sequence'), findsOneWidget);

    // And off again. The contents stay put; the devices simply stop
    // intercepting.
    await toggleFlash(tester);
    expect(find.textContaining('plain memory again'), findsOneWidget);
    final off = tester.widget<Switch>(find.byKey(const Key('flashEnableSwitch')));
    expect(off.value, isFalse);
  });

  testWidgets('a short file is padded with erased bytes and says so',
      (tester) async {
    final dir = Directory.systemTemp.createTempSync('flashtab');
    addTearDown(() => dir.deleteSync(recursive: true));
    final f = File('${dir.path}/short.bin')
      ..writeAsBytesSync(Uint8List(0x100));

    await openFlashTab(tester);
    await tester.enterText(find.byKey(const Key('flashPathField')), f.path);
    await toggleFlash(tester);

    expect(find.textContaining("filled with H'FF"), findsOneWidget);
  });

  group('image layout', () {
    test('the two devices sit back to back in a plain image', () {
      expect(flashImageSize(), 0x208000);
      expect(flashImageOffset(artista180Flash[0], false), 0x000000);
      expect(flashImageOffset(artista180Flash[1], false), 0x008000);
    });

    test('a full memory dump is recognised and read by address', () {
      expect(flashImageIsAddressed(0x208000), isFalse);
      expect(flashImageIsAddressed(0x1000000), isTrue);
      expect(flashImageOffset(artista180Flash[1], true), 0x200000);
    });
  });
}
