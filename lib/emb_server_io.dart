// The Embroidery Relay TCP listener.
//
// Accepts one client at a time, as the reference relay does: the machine
// protocol is strictly sequential, and two clients interleaving commands on
// it would corrupt each other's replies. For the same reason requests from a
// single client are handled one after another even if they arrive together —
// the client is allowed several in flight, and matches them up by request ID.

import 'dart:async';
import 'dart:io';

import 'emb_protocol.dart';
import 'emb_relay.dart';

class EmbServer {
  EmbServer({required this.relay});

  final EmbRelay relay;

  ServerSocket? _server;
  Socket? _socket;
  final List<int> _buffer = [];
  Future<void> _queue = Future<void>.value();

  int _requestsHandled = 0;
  String? _lastError;

  /// Called whenever anything a UI would show has changed.
  void Function()? onChanged;

  bool get listening => _server != null;
  int get port => _server?.port ?? 0;
  String? get client =>
      _socket?.remoteAddress.address;
  int get requestsHandled => _requestsHandled;
  String? get lastError => _lastError;

  /// Starts listening. Returns null on success, or why it could not.
  Future<String?> start(int port) async {
    await stop();
    _lastError = null;
    try {
      // Loopback only: this hands out unauthenticated control of the
      // machine's memory, so it should not be reachable from the network
      // without the user deciding so deliberately.
      _server = await ServerSocket.bind(InternetAddress.loopbackIPv4, port);
      _server!.listen(_accept, onError: (Object e) {
        _lastError = '$e';
        onChanged?.call();
      });
      onChanged?.call();
      return null;
    } catch (e) {
      _server = null;
      return _lastError = '$e';
    }
  }

  Future<void> stop() async {
    final socket = _socket;
    final server = _server;
    _socket = null;
    _server = null;
    _buffer.clear();
    try {
      await socket?.close();
      await server?.close();
    } catch (_) {
      // Already gone.
    }
    onChanged?.call();
  }

  void _accept(Socket socket) {
    if (_socket != null) {
      // One at a time.
      socket.write('busy\n');
      socket.close();
      return;
    }
    _socket = socket;
    _buffer.clear();
    onChanged?.call();

    socket.listen(
      _onData,
      onError: (Object e) {
        _lastError = '$e';
        _dropClient(socket);
      },
      onDone: () => _dropClient(socket),
      cancelOnError: true,
    );
  }

  void _dropClient(Socket socket) {
    if (!identical(_socket, socket)) return;
    _socket = null;
    _buffer.clear();
    try {
      socket.destroy();
    } catch (_) {
      // Already gone.
    }
    onChanged?.call();
  }

  void _onData(List<int> data) {
    _buffer.addAll(data);

    // A megabyte without a complete message means the other end is not
    // speaking this protocol.
    if (_buffer.length > 1 << 20) {
      _lastError = 'the client sent more than 1MB without a complete message';
      final s = _socket;
      if (s != null) _dropClient(s);
      return;
    }

    while (true) {
      EmbMessage? message;
      try {
        message = EmbProtocol.decode(_buffer);
      } catch (e) {
        _lastError = '$e';
        final s = _socket;
        if (s != null) _dropClient(s);
        return;
      }
      if (message == null) break; // wait for the rest
      _buffer.removeRange(0, message.totalLength);

      final m = message;
      final socket = _socket;
      // Serialised: the machine can only do one thing at a time.
      _queue = _queue.then((_) async {
        if (!identical(_socket, socket) || socket == null) return;
        final response = await relay.handle(m);
        try {
          socket.add(response);
        } catch (e) {
          _lastError = '$e';
          return;
        }
        _requestsHandled++;
        onChanged?.call();
      });
    }
  }
}
