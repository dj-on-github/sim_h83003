// A TCP socket standing in for a host serial port.
//
// libserialport is the only way the SCI tab could reach the outside world,
// and on macOS it will not open a pty: sp_list_ports walks the IOKit
// registry, which knows only real serial hardware, and sp_open fails on a
// pty even when nothing else holds it. Plain POSIX open on the same device
// is fine, so the limitation is the library's rather than the system's --
// which means no amount of letting the user type a path would have helped.
//
// A socket sidesteps it entirely. This listens, and socat joins a pty to it:
//
//     socat pty,raw,echo=0,link=/tmp/artista TCP:localhost:5555
//
// after which /tmp/artista behaves like a machine on a wire, and anything
// that opens a serial port -- the burner, a terminal -- can talk to it.
//
// One connection at a time. A second caller is closed straight away rather
// than queued, because two hosts on one SCI channel is not a thing the
// machine could do.

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

class TcpLink {
  ServerSocket? _server;
  Socket? _client;
  StreamSubscription<Uint8List>? _sub;

  final List<int> _received = [];
  int _port = 0;
  int _baud = 0;
  int _bytesIn = 0;
  int _bytesOut = 0;
  String? _lastError;
  bool _connected = false;

  bool get isOpen => _server != null;

  /// True once something has actually connected. The bridge is useful before
  /// this -- the machine can run with nothing listening, exactly as it would
  /// with an unplugged cable -- so it is reported rather than waited for.
  bool get isConnected => _connected;

  int get port => _port;
  int get baud => _baud;
  int get bytesIn => _bytesIn;
  int get bytesOut => _bytesOut;
  String? get lastError => _lastError;

  String describe() => _connected
      ? 'port $_port, connected'
      : 'port $_port, waiting for a connection';

  /// Starts listening on [port]. Returns null on success, or why it failed.
  Future<String?> open(int port, int baud) async {
    await close();
    _lastError = null;
    _baud = baud;
    _bytesIn = 0;
    _bytesOut = 0;
    try {
      // Loopback only. This hands out control of the simulated machine, so
      // it has no business being reachable from the network.
      final s = await ServerSocket.bind(InternetAddress.loopbackIPv4, port);
      _server = s;
      _port = s.port;
      s.listen(_accept, onError: (Object e) => _lastError = '$e');
      return null;
    } catch (e) {
      await close();
      return _lastError = '$e';
    }
  }

  void _accept(Socket socket) {
    if (_client != null) {
      // Already busy: say so on the wire rather than leaving them wondering.
      socket.write('the simulator already has a connection\r\n');
      socket.destroy();
      return;
    }
    socket.setOption(SocketOption.tcpNoDelay, true);
    _client = socket;
    _connected = true;
    _sub = socket.listen(
      (data) {
        _received.addAll(data);
        _bytesIn += data.length;
      },
      onError: (Object e) {
        _lastError = '$e';
        _dropClient();
      },
      onDone: _dropClient,
      cancelOnError: false,
    );
  }

  void _dropClient() {
    _sub?.cancel();
    _sub = null;
    _client?.destroy();
    _client = null;
    _connected = false;
  }

  /// The simulated channel's rate, recorded so the tab can show it.
  ///
  /// Nothing is done with it. A socket has no bit rate, so the 'J' command's
  /// change of speed part-way through a download -- which a real port has to
  /// follow or the link turns to noise -- costs nothing here.
  void setBaud(int rate) {
    if (rate > 0) _baud = rate;
  }

  void send(List<int> data) {
    final c = _client;
    if (c == null || data.isEmpty) return;
    try {
      c.add(data);
      _bytesOut += data.length;
    } catch (e) {
      _lastError = '$e';
      _dropClient();
    }
  }

  /// Hands over everything that has arrived since the last call.
  List<int> drain() {
    if (_received.isEmpty) return const [];
    final out = List<int>.from(_received);
    _received.clear();
    return out;
  }

  Future<void> close() async {
    _dropClient();
    final s = _server;
    _server = null;
    _received.clear();
    if (s != null) {
      try {
        await s.close();
      } catch (_) {
        // Already gone; nothing to report.
      }
    }
  }
}
