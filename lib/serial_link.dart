// Host serial port access, where the platform has any.
//
// The simulator core never refers to this: the SCI model exposes a transmit
// hook and a receive queue, and the UI is what joins them to a real port.
export 'serial_link_stub.dart' if (dart.library.io) 'serial_link_io.dart';
