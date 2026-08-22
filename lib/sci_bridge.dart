// Joining one simulated SCI channel to a serial port on the host.
//
// The byte movement and the rate tracking live here rather than in the UI,
// so that both the SCI tab and the command-line bridge run the same code and
// so that it can be tested without a serial port: everything that touches
// real hardware is passed in as a callback.

import 'sci.dart';

/// The bit rate [channel] is currently programmed to, at a system clock of
/// [phiHz], rounded to whole baud. Zero when the divisors do not describe a
/// usable rate — which is what they look like before the firmware has set
/// the channel up.
int sciBaudAt(SciChannel channel, int phiHz) {
  if (phiHz <= 0) return 0;
  final b = channel.baudAt(phiHz.toDouble());
  return (b.isFinite && b > 0) ? b.round() : 0;
}

/// Moves bytes in both directions between [channel] and a host port, and
/// keeps the port's rate matching the channel's.
///
/// [outgoing] is where the channel's transmit hook has been collecting
/// bytes; it is emptied here. Bytes read from the port are handed to the
/// channel's receiver, which delivers them at the modelled line rate rather
/// than all at once, so the firmware's own polling still governs the pace.
///
/// The rate is re-applied every call because the download protocol changes
/// it mid-conversation: the host has already followed the machine to the new
/// rate, so the port has to follow too or what arrives is noise. [setBaud]
/// is expected to ignore a rate it is already at.
void pumpSciBridge({
  required SciChannel channel,
  required List<int> outgoing,
  required int phiHz,
  required void Function(int baud) setBaud,
  required List<int> Function() drain,
  required void Function(List<int> data) send,
}) {
  final baud = sciBaudAt(channel, phiHz);
  if (baud > 0) setBaud(baud);

  final incoming = drain();
  if (incoming.isNotEmpty) channel.receive(incoming);

  if (outgoing.isNotEmpty) {
    send(List<int>.from(outgoing));
    outgoing.clear();
  }
}
