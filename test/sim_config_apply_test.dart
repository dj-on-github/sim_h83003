// The config actually taking effect: a SimConfig handed to the app should
// come out as the state it describes.
//
// sim_config_test.dart covers the file and its parsing. This is the other
// half -- that what the file says reaches the running app -- which is the
// part that would rot quietly if a new setting were added to one side only.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/main.dart';
import 'package:sim_h83003/sim_config.dart';

Future<void> pumpWith(WidgetTester tester, SimConfig config,
    {Size size = const Size(1600, 1100)}) async {
  await tester.binding.setSurfaceSize(size);
  addTearDown(() => tester.binding.setSurfaceSize(null));
  await tester.pumpWidget(SimH8App(config: config));
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('with no file the app comes up as it always did',
      (tester) async {
    await pumpWith(tester, const SimConfig());
    // Memory and disassembly are the two the app has always opened with.
    expect(find.text('MEM'), findsOneWidget);
    expect(find.byKey(const Key('followPcSwitch')), findsOneWidget);
    expect(tester.widget<Switch>(find.byKey(const Key('followPcSwitch'))).value,
        isTrue);
  });

  testWidgets('the views it names are the ones that open', (tester) async {
    await pumpWith(
        tester,
        const SimConfig(
          views: ViewConfig(
              memory: false, disassembly: false, screen: true, buttons: true),
        ));
    // The Buttons pane is open: its header line says what is held.
    expect(find.textContaining('nothing down'), findsOneWidget);
    // And the memory pane is not.
    expect(find.byKey(const Key('followPcSwitch')), findsNothing);
  });

  testWidgets('follow-PC off, parked where the file says', (tester) async {
    await pumpWith(
        tester,
        const SimConfig(
          memory: MemoryConfig(followPc: false, address: 0x11B100),
        ));
    final sw =
        tester.widget<Switch>(find.byKey(const Key('followPcSwitch')));
    expect(sw.value, isFalse);
    // The memory header shows where the window sits.
    expect(find.textContaining('11B100'), findsWidgets);
  });

  testWidgets('breakpoints are set before anything runs', (tester) async {
    await pumpWith(
        tester,
        const SimConfig(
          dataBreakpoints: [0x11B10E, 0x11A6D3],
          instructionBreakpoints: [0x000400],
        ));
    final page = tester.state(find.byType(SimulatorPage));
    // ignore: avoid_dynamic_calls
    final cpu = (page as dynamic).cpu;
    expect(cpu.dataBreaks, containsAll(<int>[0x11B10E, 0x11A6D3]));
    expect(cpu.instrBreaks, contains(0x000400));
  });

  testWidgets('profiling comes on when it is asked for', (tester) async {
    await pumpWith(tester, const SimConfig(profiling: true));
    final page = tester.state(find.byType(SimulatorPage));
    // ignore: avoid_dynamic_calls
    expect((page as dynamic).cpu.profiling, isTrue);
  });

  testWidgets('a held key is down from the start', (tester) async {
    // clr held down is how the machine is put into service mode.
    await pumpWith(
        tester,
        const SimConfig(
          views: ViewConfig(memory: false, disassembly: false, buttons: true),
          heldKeys: [0x77],
        ));
    // The Buttons pane says what is down, which is how the user sees it too.
    expect(find.textContaining("down: H'77"), findsOneWidget,
        reason: 'clr is held from the start, not merely pressed');
  });

  testWidgets('a pin override is driven', (tester) async {
    await pumpWith(
        tester,
        const SimConfig(
          pins: [PinConfig(port: '4', bit: 5, level: 'low')],
        ));
    final page = tester.state(find.byType(SimulatorPage));
    // ignore: avoid_dynamic_calls
    final cpu = (page as dynamic).cpu;
    expect(cpu.pinIsDriven(0xFFFFC7, 5), isTrue);
    expect(cpu.pinIsHigh(0xFFFFC7, 5), isFalse);
  });

  testWidgets('traces named in the file are in the list', (tester) async {
    await pumpWith(
        tester,
        const SimConfig(
          views: ViewConfig(memory: false, disassembly: false, trace: true),
          traces: [
            TraceConfig(target: '11B10E', bits: 16),
            TraceConfig(target: '11A6D3'),
          ],
        ));
    // The Trace pane lists what it is watching.
    expect(find.textContaining('11B10E'), findsWidgets);
    expect(find.textContaining('11A6D3'), findsWidgets);
  });

  testWidgets('the SCI tab comes up on the transport it names',
      (tester) async {
    await pumpWith(
        tester,
        const SimConfig(
          views: ViewConfig(memory: false, disassembly: false, sci: true),
          sci: SciConfig(transport: 'tcp', tcpPort: 5599, channel: 0),
        ));
    await tester.scrollUntilVisible(find.text('HOST SERIAL PORT'), 300,
        scrollable: find.byType(Scrollable).first);
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('serialTcpPortField')), findsOneWidget);
    final field = tester
        .widget<TextField>(find.byKey(const Key('serialTcpPortField')));
    expect(field.controller?.text, '5599');
    final channel = tester.widget<DropdownButtonFormField<int>>(
        find.byKey(const Key('serialChannelDropdown')));
    expect(channel.initialValue, 0);
  });

  testWidgets('appearance is taken from the file', (tester) async {
    await pumpWith(
        tester,
        const SimConfig(
          appearance: AppearanceConfig(darkMode: false, fontFamily: 'serif'),
        ));
    final app = tester.widget<MaterialApp>(find.byType(MaterialApp));
    expect(app.themeMode, ThemeMode.light);
  });

  testWidgets('a broken file is reported rather than swallowed',
      (tester) async {
    await tester.binding.setSurfaceSize(const Size(1600, 1100));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(const SimH8App(
        configError: 'FormatException: unexpected character'));
    await tester.pumpAndSettle();
    expect(find.textContaining('could not be read'), findsOneWidget);
  });
}
