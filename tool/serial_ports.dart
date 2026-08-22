// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Lists the serial ports this machine offers, which is what the SCI tab's
// port menu shows. Useful on its own for checking that a USB adapter has
// been seen, and for confirming the native library is reachable.

import 'package:sim_h83003/serial_link.dart';

void main(List<String> args) {
  final ports = SerialLink.availablePorts();
  if (ports.isEmpty) {
    print('no serial ports');
    return;
  }
  for (final p in ports) {
    print('  ${SerialLink.describe(p)}');
  }
}
