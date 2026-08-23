// Turning Embroidery Relay requests into the machine's serial protocol.
//
// This is the Dart equivalent of EMB-Serial's SerialStack.js and the request
// handling in its Relay.js. The machine speaks a character-at-a-time protocol:
// every character of a command is echoed before the next may be sent, and the
// result follows the last echo. A single 32-byte read is therefore seven
// round trips, which is why the relay exists — the client sends one TCP
// request instead.
//
// Nothing here knows whether the other end is a real machine over a serial
// port or the simulated one: it is given a byte link and drives it.

import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:typed_data';

import 'emb_protocol.dart';

/// A bidirectional byte link to the machine.
abstract class EmbLink {
  /// Sends bytes towards the machine.
  void write(List<int> data);

  /// Bytes the machine has sent back.
  Stream<int> get received;
}

/// The machine did not answer, or answered with something else.
class EmbMachineException implements Exception {
  EmbMachineException(this.message, {this.timedOut = false});
  final String message;
  final bool timedOut;
  @override
  String toString() => message;
}

/// Buffers the link's bytes so they can be read a fixed number at a time.
class _Reader {
  _Reader(Stream<int> source) {
    _sub = source.listen(_add);
  }

  final Queue<int> _buffer = Queue<int>();
  late final StreamSubscription<int> _sub;
  Completer<void>? _waiting;

  void _add(int b) {
    _buffer.add(b);
    final w = _waiting;
    if (w != null && !w.isCompleted) {
      _waiting = null;
      w.complete();
    }
  }

  /// Drops anything already received. The stack does this before each command
  /// so a stale byte from an abandoned one cannot be mistaken for an echo.
  void flush() => _buffer.clear();

  int get pending => _buffer.length;

  Future<List<int>> read(int count, Duration timeout) async {
    final deadline = DateTime.now().add(timeout);
    while (_buffer.length < count) {
      final left = deadline.difference(DateTime.now());
      if (left <= Duration.zero) {
        throw EmbMachineException(
            'timed out waiting for $count byte(s); got ${_buffer.length}',
            timedOut: true);
      }
      final w = _waiting = Completer<void>();
      await w.future.timeout(left, onTimeout: () {
        if (!w.isCompleted) w.complete();
      });
    }
    return [for (var i = 0; i < count; i++) _buffer.removeFirst()];
  }

  Future<int> readByte(Duration timeout) async =>
      (await read(1, timeout)).first;

  Future<void> dispose() => _sub.cancel();
}

/// The machine's serial protocol, one operation per method.
class EmbSerialStack {
  EmbSerialStack(
    this.link, {
    this.charTimeout = const Duration(seconds: 2),
    this.slowTimeout = const Duration(seconds: 6),
    this.maxRetries = 2,
  }) : _reader = _Reader(link.received);

  final EmbLink link;
  final Duration charTimeout;

  /// For the steps that make the machine do real work — committing a flash
  /// page, opening a session — rather than just echoing.
  final Duration slowTimeout;
  final int maxRetries;

  final _Reader _reader;

  /// Counts what has gone over the link, for the UI.
  int commandsSent = 0;
  String lastCommand = '';

  Future<void> dispose() => _reader.dispose();

  /// Sends a command a character at a time, checking each echo.
  ///
  /// The machine echoes every character it accepts, so a mismatch means it
  /// was not in the state we thought. 'Q', '?' and '!' are its refusals, and
  /// are only errors when they are not what we sent — 'RF?' deliberately ends
  /// with a '?'.
  Future<void> sendCommand(String command, {Duration? lastCharTimeout}) async {
    _reader.flush();
    commandsSent++;
    lastCommand = command;

    for (var i = 0; i < command.length; i++) {
      final sent = command.codeUnitAt(i);
      link.write([sent]);
      final last = i == command.length - 1;
      final echo = await _reader
          .readByte(last ? (lastCharTimeout ?? charTimeout) : charTimeout);
      if (echo != sent) {
        throw EmbMachineException('echo mismatch at "${command[i]}" '
            '(position $i of "$command"): the machine sent '
            '"${_printable(echo)}"');
      }
    }
  }

  static String _printable(int b) => b >= 32 && b < 127
      ? String.fromCharCode(b)
      : "H'${b.toRadixString(16).toUpperCase().padLeft(2, '0')}";

  /// Puts the machine back in its idle state.
  ///
  /// 'R' starts a read and wants six address digits; 'F' is one, and '?' is
  /// not, which makes the machine abandon the command and echo the '?'. So
  /// this is a command that is always safe to send and always ends idle.
  Future<void> resync() => sendCommand('RF?');

  /// Runs [operation], and on failure resyncs and tries again.
  Future<T> _withRetry<T>(String what, Future<T> Function() operation) async {
    Object? last;
    for (var attempt = 0; attempt <= maxRetries; attempt++) {
      try {
        return await operation();
      } catch (e) {
        last = e;
        if (attempt == maxRetries) break;
        try {
          await resync();
        } catch (_) {
          break; // the link is not answering at all; stop trying
        }
      }
    }
    throw EmbMachineException('$what failed: $last');
  }

  /// 32 bytes, as 64 hex characters. Serial "R".
  Future<String> read(String address) {
    _requireHex(address, 6, 'address');
    return _withRetry('read $address', () async {
      await sendCommand('R${address.toUpperCase()}');
      final r = await _reader.read(65, slowTimeout);
      _requireTerminator(r, 64);
      return latin1.decode(r.sublist(0, 64));
    });
  }

  /// 256 bytes, raw. Serial "N".
  Future<Uint8List> largeRead(String address) {
    _requireHex(address, 6, 'address');
    return _withRetry('large read $address', () async {
      await sendCommand('N${address.toUpperCase()}');
      final r = await _reader.read(257, slowTimeout);
      _requireTerminator(r, 256);
      return Uint8List.fromList(r.sublist(0, 256));
    });
  }

  /// The 32-bit sum of a range. Serial "L".
  Future<int> sum(String address, String length) {
    _requireHex(address, 6, 'address');
    _requireHex(length, 6, 'length');
    return _withRetry('checksum $address+$length', () async {
      await sendCommand('L${address.toUpperCase()}${length.toUpperCase()}');
      final r = await _reader.read(9, slowTimeout);
      _requireTerminator(r, 8);
      return int.parse(latin1.decode(r.sublist(0, 8)), radix: 16);
    });
  }

  /// Writes bytes given as hex. Serial "W", which streams until a character
  /// that is not a hex digit stops it — hence the trailing '?'.
  Future<void> write(String address, String dataHex) {
    _requireHex(address, 6, 'address');
    if (dataHex.length.isOdd) {
      throw EmbMachineException('write data must be whole bytes');
    }
    return _withRetry('write $address', () async {
      // The '?' is the byte that commits the write, so the machine may take
      // noticeably longer over it than over the digits.
      await sendCommand('W${address.toUpperCase()}${dataHex.toUpperCase()}?',
          lastCharTimeout: slowTimeout);
    });
  }

  /// Programs one 256-byte page. Serial "PS", which answers "OE" to ask for
  /// the page, takes it raw, and answers 'O' when it has been committed.
  Future<void> upload(String address, List<int> data) {
    _requireHex(address, 4, 'address');
    if (data.length != 256) {
      throw EmbMachineException('a page is 256 bytes, got ${data.length}');
    }
    return _withRetry('upload $address', () async {
      await sendCommand('PS${address.toUpperCase()}');
      final ready = await _reader.read(2, slowTimeout);
      if (latin1.decode(ready) != 'OE') {
        throw EmbMachineException(
            'expected "OE" before the page, got "${latin1.decode(ready)}"');
      }
      link.write(data);
      final done = await _reader.readByte(slowTimeout);
      if (done != 0x4F) {
        throw EmbMachineException(
            'page not acknowledged: "${_printable(done)}"');
      }
    });
  }

  /// The embroidery session flag lives in the machine's memory.
  static const String sessionFlagAddress = '57FF80';
  static const int sessionClosedMarker = 0xB4;

  Future<bool> isSessionOpen() async {
    final data = await read(sessionFlagAddress);
    return int.parse(data.substring(0, 2), radix: 16) != sessionClosedMarker;
  }

  /// Opens the session, if it is not open already. Returns whether it had to.
  Future<bool> openSession() async {
    if (await isSessionOpen()) return false;
    await sendCommand('TrMEYQ', lastCharTimeout: slowTimeout);
    final ok = await _reader.readByte(slowTimeout);
    if (ok != 0x4F) {
      throw EmbMachineException(
          'session not confirmed: "${_printable(ok)}"');
    }
    return true;
  }

  /// Closes the session, if it is open. Returns whether it had to.
  Future<bool> closeSession() async {
    if (!await isSessionOpen()) return false;
    for (var attempt = 0; attempt < 3; attempt++) {
      try {
        await resync();
        await sendCommand('TrME');
        if (!await isSessionOpen()) return true;
      } catch (e) {
        if (attempt == 2) rethrow;
      }
    }
    throw EmbMachineException('session would not close');
  }

  /// Moves the link to 57600 baud. Serial "TrMEJ05": the machine changes rate
  /// after echoing the last character, announces itself again with "BOS", and
  /// wants the "EB" handshake and a "YQ" confirmation at the new rate.
  ///
  /// [onRateChange] is called once the command has been echoed, so a real
  /// serial port can follow. A simulated link has no rate of its own — the
  /// SCI model already paces itself from the divisor the firmware writes — so
  /// it passes nothing.
  Future<void> upgradeSpeed({void Function(int baud)? onRateChange}) async {
    await sendCommand('TrMEJ05');
    onRateChange?.call(57600);

    final bos = await _reader.read(3, slowTimeout);
    if (latin1.decode(bos) != 'BOS') {
      throw EmbMachineException(
          'expected "BOS" after the rate change, got "${latin1.decode(bos)}"');
    }
    await sendCommand('EBYQ', lastCharTimeout: slowTimeout);
    final ok = await _reader.readByte(slowTimeout);
    if (ok != 0x4F) {
      throw EmbMachineException(
          'rate change not confirmed: "${_printable(ok)}"');
    }
    await resync();
  }

  void _requireTerminator(List<int> r, int at) {
    if (r[at] != 0x4F) {
      throw EmbMachineException(
          'expected "O" to end the reply, got "${_printable(r[at])}"');
    }
  }

  static final _hex = RegExp(r'^[0-9A-Fa-f]+$');
  void _requireHex(String s, int length, String what) {
    if (s.length != length || !_hex.hasMatch(s)) {
      throw EmbMachineException('$what must be $length hex characters, '
          'got "$s"');
    }
  }
}

/// Answers Embroidery Relay requests by driving [stack].
///
/// One request in, one response out; the caller owns the transport. Failures
/// become ERRO responses rather than exceptions, because the client is
/// waiting for something with its request ID on it either way.
class EmbRelay {
  EmbRelay(this.stack, {this.relayVersion = '1.0.0', this.describeLink});

  final EmbSerialStack stack;
  final String relayVersion;

  /// What to report as the "serial port" in GCFG and RSTA. For the simulated
  /// machine this is the SCI channel rather than a device name.
  final String Function()? describeLink;

  /// The rate last agreed with the machine, reported in STAT and RCFG.
  int baudRate = 19200;

  /// Set once a session has been opened, so STAT can answer without a read.
  bool sessionOpen = false;

  /// The most recent request, for the UI.
  String lastRequest = '';

  Future<Uint8List> handle(EmbMessage m) async {
    lastRequest = m.type;
    try {
      switch (m.type) {
        case EmbType.getConfig:
          return EmbProtocol.encodeJson(EmbType.configResponse, m.requestId, {
            'serialPort': describeLink?.call() ?? 'simulated',
            'baudRate': baudRate,
            'relayVersion': relayVersion,
          });

        case EmbType.setConfig:
          // The link is whatever the simulator is bridged to; there is no
          // port to choose. The rate is accepted so a client that sets it
          // before doing anything else is not stopped.
          final requested = _jsonOf(m.payload);
          final rate = requested['baudRate'];
          if (rate is int) baudRate = rate;
          return EmbProtocol.encodeJson(EmbType.configResponse, m.requestId, {
            'success': true,
            'message': 'Configuration accepted',
          });

        case EmbType.getStatus:
          return EmbProtocol.encodeJson(EmbType.statusResponse, m.requestId, {
            'connected': true,
            'baudRate': baudRate,
            'sessionOpen': sessionOpen,
            'lastError': '',
          });

        case EmbType.read:
          final data = await stack.read(EmbProtocol.parseAddress(m.payload));
          return EmbProtocol.encodeText(EmbType.readData, m.requestId, data);

        case EmbType.largeRead:
          final data =
              await stack.largeRead(EmbProtocol.parseAddress(m.payload));
          return EmbProtocol.encode(EmbType.largeData, m.requestId, data);

        case EmbType.write:
          final w = EmbProtocol.parseWrite(m.payload);
          await stack.write(w.address, w.data);
          return EmbProtocol.encodeText(EmbType.writeAck, m.requestId, 'O');

        case EmbType.upload:
          final u = EmbProtocol.parseUpload(m.payload);
          await stack.upload(u.address, u.data);
          return EmbProtocol.encodeText(EmbType.uploadAck, m.requestId, 'O');

        case EmbType.checksum:
          final c = EmbProtocol.parseChecksum(m.payload);
          final total = await stack.sum(c.address, c.length);
          return EmbProtocol.encodeText(EmbType.sumResponse, m.requestId,
              total.toRadixString(16).toUpperCase().padLeft(8, '0'));

        case EmbType.sessionOpen:
          await stack.openSession();
          sessionOpen = true;
          return EmbProtocol.encodeText(EmbType.sessionAck, m.requestId, 'O');

        case EmbType.sessionClose:
          await stack.closeSession();
          sessionOpen = false;
          return EmbProtocol.encodeText(EmbType.sessionAck, m.requestId, 'O');

        case EmbType.baud:
          final rate = EmbProtocol.parseBaud(m.payload);
          if (rate != baudRate && rate == 57600) {
            await stack.upgradeSpeed();
          }
          baudRate = rate;
          return EmbProtocol.encodeText(EmbType.baudAck, m.requestId, 'O');

        case EmbType.reset:
          await stack.resync();
          return EmbProtocol.encodeText(EmbType.resetAck, m.requestId, 'O');

        default:
          return EmbProtocol.errorResponse(m.requestId,
              'Unknown message type "${m.type}"', EmbError.invalidFormat);
      }
    } on EmbFormatException catch (e) {
      return EmbProtocol.errorResponse(
          m.requestId, e.message, EmbError.invalidParameters);
    } on EmbMachineException catch (e) {
      return EmbProtocol.errorResponse(m.requestId, e.message,
          e.timedOut ? EmbError.machineTimeout : EmbError.machineError);
    } catch (e) {
      return EmbProtocol.errorResponse(
          m.requestId, '$e', EmbError.machineError);
    }
  }

  Map<String, dynamic> _jsonOf(Uint8List payload) {
    if (payload.isEmpty) return const {};
    try {
      final v = json.decode(latin1.decode(payload));
      return v is Map<String, dynamic> ? v : const {};
    } catch (_) {
      return const {};
    }
  }
}
