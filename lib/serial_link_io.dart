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

  /// A human-readable name for the port, falling back to the device name.
  static String describe(String port) {
    try {
      final p = SerialPort(port);
      final d = p.description;
      p.dispose();
      return (d == null || d.isEmpty) ? port : '$port  ($d)';
    } catch (_) {
      return port;
    }
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
      final cfg = SerialPortConfig()
        ..baudRate = rate
        ..bits = 8
        ..parity = SerialPortParity.none
        ..stopBits = 1
        ..setFlowControl(SerialPortFlowControl.none);
      p.config = cfg;
      cfg.dispose();

      _port = p;
      _portName = port;
      _baud = rate;
      _bytesIn = 0;
      _bytesOut = 0;

      _reader = SerialPortReader(p);
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
      final cfg = p.config;
      cfg.baudRate = rate;
      p.config = cfg;
      cfg.dispose();
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

  void close() {
    try {
      _sub?.cancel();
      _reader?.close();
      _port?.close();
      _port?.dispose();
    } catch (_) {
      // Closing a port that has already gone (an unplugged adapter) throws;
      // there is nothing useful to do about it.
    }
    _sub = null;
    _reader = null;
    _port = null;
    _portName = null;
    _baud = 0;
    _received.clear();
  }
}
