// A TCP stand-in for a host serial port, where the platform has sockets.
export 'tcp_link_stub.dart' if (dart.library.io) 'tcp_link_io.dart';
