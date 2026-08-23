// A host serial port, for bridging one simulated SCI channel to real
// hardware.
//
// Reading is done by the package's own reader, which runs the blocking read
// on its own isolate and hands bytes back as a stream; they are collected
// here and drained by the simulation once a frame. Writing is non-blocking,
// so a port whose other end is not listening cannot stall the UI.
//
// Everything is wrapped in try/catch. The native library is loaded lazily by
// the binding, so a machine without it throws on the first call rather than
// at import, and that has to leave the app working with no ports offered.

import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_libserialport/flutter_libserialport.dart';

class SerialLink {
  SerialPort? _port;
  SerialPortReader? _reader;
  StreamSubscription<Uint8List>? _sub;

  final List<int> _received = [];
  String? _portName;
  int _baud = 0;
  int _bytesIn = 0;
  int _bytesOut = 0;
  String? _lastError;

  static List<String> availablePorts() {
    try {
      return SerialPort.availablePorts;
    } catch (_) {
      return const [];
    }
  }

  /// Human-readable names for [ports], worked out once.
  ///
  /// Each lookup allocates and frees a native port structure, so this must
  /// not be called from a widget build: the menu is rebuilt every frame and
  /// that would churn native handles continuously, next to a reader isolate
  /// holding a pointer into the same library.
  static Map<String, String> describePorts(List<String> ports) {
    final out = <String, String>{};
    for (final name in ports) {
      try {
        final p = SerialPort(name);
        final d = p.description;
        p.dispose();
        out[name] = (d == null || d.isEmpty) ? name : '$name  ($d)';
      } catch (_) {
        out[name] = name;
      }
    }
    return out;
  }

  bool get isOpen => _port != null;
  String? get portName => _portName;
  int get baud => _baud;
  int get bytesIn => _bytesIn;
  int get bytesOut => _bytesOut;
  String? get lastError => _lastError;

  /// Opens [port] at [rate]. Returns null on success, or why it failed.
  String? open(String port, int rate) {
    close();
    _lastError = null;
    try {
      final p = SerialPort(port);
      if (!p.openReadWrite()) {
        final e = SerialPort.lastError;
        p.dispose();
        return _lastError = 'cannot open $port: ${e?.message ?? "refused"}';
      }

      // 8N1, no flow control: what the machine's SCI is set to.
      //
      // Assigning to `config` hands the object to the port, which stores it
      // and frees it in dispose(). Disposing it here as well is a double
      // free -- that is what the crash on Linux was.
      p.config = SerialPortConfig()
        ..baudRate = rate
        ..bits = 8
        ..parity = SerialPortParity.none
        ..stopBits = 1
        ..setFlowControl(SerialPortFlowControl.none);

      _port = p;
      _portName = port;
      _baud = rate;
      _bytesIn = 0;
      _bytesOut = 0;

      // A short poll interval so the reader isolate returns to Dart often.
      // It blocks in a native wait for this long at a time, and a kill only
      // takes effect once it is back in Dart, so this bounds how long the
      // port has to stay alive after close().
      _reader = SerialPortReader(p, timeout: _readerPollMs);
      _sub = _reader!.stream.listen(
        (data) {
          _received.addAll(data);
          _bytesIn += data.length;
        },
        onError: (Object e) => _lastError = '$e',
        cancelOnError: false,
      );
      return null;
    } catch (e) {
      close();
      return _lastError = '$e';
    }
  }

  /// Follows the simulation's own bit rate. Called every frame, so it does
  /// nothing unless the rate has actually changed.
  void setBaud(int rate) {
    final p = _port;
    if (p == null || rate <= 0 || rate == _baud) return;
    try {
      // The getter hands back the port's own config rather than a copy, so
      // this edits it in place and hands the same object back. Nothing is
      // disposed: the port owns it.
      final cfg = p.config;
      cfg.baudRate = rate;
      p.config = cfg;
      _baud = rate;
    } catch (e) {
      _lastError = '$e';
    }
  }

  void send(List<int> data) {
    final p = _port;
    if (p == null || data.isEmpty) return;
    try {
      // A negative timeout is the non-blocking write.
      _bytesOut += p.write(Uint8List.fromList(data), timeout: -1);
    } catch (e) {
      _lastError = '$e';
    }
  }

  /// Hands over everything that has arrived since the last call.
  List<int> drain() {
    if (_received.isEmpty) return const [];
    final out = List<int>.from(_received);
    _received.clear();
    return out;
  }

  /// How long the reader blocks in one native wait, and how long the port is
  /// kept alive after closing so that the reader is certainly out of it.
  static const int _readerPollMs = 50;
  static const Duration _readerShutdown = Duration(milliseconds: 250);

  /// Closes the link. The port stops being usable immediately; freeing it
  /// happens a moment later.
  ///
  /// The reader runs a blocking read on its own isolate, holding a raw
  /// pointer to the port. Killing an isolate is not immediate -- and it is
  /// not even attempted until the spawn has completed, so a close that
  /// follows an open closely may not kill it at all. Freeing the port while
  /// that is still going reads through a dangling pointer, which is what was
  /// crashing after a connection. So the handles are detached now and freed
  /// after the reader has certainly stopped.
  void close() {
    final port = _port;
    final reader = _reader;
    final sub = _sub;

    _port = null;
    _reader = null;
    _sub = null;
    _portName = null;
    _baud = 0;
    _received.clear();

    if (port == null) return;
    unawaited(_release(port, reader, sub));
  }

  static Future<void> _release(SerialPort port, SerialPortReader? reader,
      StreamSubscription<Uint8List>? sub) async {
    try {
      await sub?.cancel(); // cancelling is what kills the reader isolate
      reader?.close();
      await Future<void>.delayed(_readerShutdown);
      port.close();
      port.dispose();
    } catch (_) {
      // An adapter unplugged mid-conversation makes all of these throw, and
      // there is nothing useful to do about it.
    }
  }
}
