// The Embroidery Relay TCP protocol.
//
// This is the wire format spoken by EmbroideryCommunicator and the EMB-Serial
// relay (github.com/Ylianst/EMB-Serial). A client sends high-level requests
// over TCP; the relay turns each one into the machine's character-at-a-time
// serial protocol and answers with the result. Implementing this end lets the
// same client drive the simulated machine.
//
// Every message is
//
//     [Type:4][RequestID:8][PayloadLength:8][Payload:N]
//
// with the three header fields in ASCII, the length in uppercase hex, and the
// payload either ASCII or binary depending on the message. The request ID is
// the client's, echoed back, so several requests may be in flight.

import 'dart:convert';
import 'dart:typed_data';

/// Message types, request and response.
class EmbType {
  // Configuration and status.
  static const getConfig = 'GCFG';
  static const setConfig = 'SCFG';
  static const configResponse = 'RCFG';
  static const getStatus = 'STAT';
  static const statusResponse = 'RSTA';

  // Machine operations.
  static const read = 'READ'; // 32 bytes -> serial "R"
  static const readData = 'RDAT';
  static const largeRead = 'LRED'; // 256 bytes -> serial "N"
  static const largeData = 'LDAT';
  static const write = 'WRIT'; // -> serial "W"
  static const writeAck = 'WACK';
  static const upload = 'UPLD'; // 256-byte page -> serial "PS"
  static const uploadAck = 'UACK';
  static const checksum = 'CSUM'; // -> serial "L"
  static const sumResponse = 'RSUM';

  // Session and link.
  static const sessionOpen = 'SOPE'; // -> serial "TrMEYQ"
  static const sessionClose = 'SCLO'; // -> serial "TrME"
  static const sessionAck = 'SACK';
  static const baud = 'BAUD'; // -> serial "TrMEJ05"
  static const baudAck = 'BACK';
  static const reset = 'RSET'; // -> serial "RF?"
  static const resetAck = 'RACK';

  static const error = 'ERRO';
}

/// The error codes the protocol defines.
class EmbError {
  static const invalidFormat = 1001;
  static const portNotConfigured = 1002;
  static const portNotConnected = 1003;
  static const machineTimeout = 1004;
  static const machineError = 1005;
  static const invalidParameters = 1006;
  static const sessionAlreadyOpen = 1007;
  static const sessionNotOpen = 1008;
  static const baudChangeFailed = 1009;
}

/// One decoded message.
class EmbMessage {
  EmbMessage(this.type, this.requestId, this.payload);

  final String type;
  final String requestId;
  final Uint8List payload;

  /// Length on the wire, header included.
  int get totalLength => EmbProtocol.headerLength + payload.length;

  String get payloadText => latin1.decode(payload);

  @override
  String toString() => '$type $requestId (${payload.length} bytes)';
}

/// Thrown when a payload does not hold what its message type requires.
class EmbFormatException implements Exception {
  EmbFormatException(this.message);
  final String message;
  @override
  String toString() => message;
}

class EmbProtocol {
  static const int headerLength = 20;

  static String _hex8(int v) =>
      v.toRadixString(16).toUpperCase().padLeft(8, '0');

  static Uint8List encode(String type, String requestId, List<int> payload) {
    if (type.length != 4) {
      throw ArgumentError('message type must be 4 characters, got "$type"');
    }
    if (requestId.length != 8) {
      throw ArgumentError('request ID must be 8 characters, got "$requestId"');
    }
    final header = ascii.encode('$type$requestId${_hex8(payload.length)}');
    return Uint8List(header.length + payload.length)
      ..setRange(0, header.length, header)
      ..setRange(header.length, header.length + payload.length, payload);
  }

  static Uint8List encodeText(String type, String requestId, String payload) =>
      encode(type, requestId, latin1.encode(payload));

  static Uint8List encodeJson(String type, String requestId, Object payload) =>
      encodeText(type, requestId, json.encode(payload));

  static Uint8List errorResponse(String requestId, String message, int code) =>
      encodeJson(EmbType.error, requestId, {'error': message, 'code': code});

  /// Decodes the message at the head of [buffer], or null when it has not all
  /// arrived yet. The caller removes [EmbMessage.totalLength] bytes and tries
  /// again, since TCP gives no message boundaries of its own.
  static EmbMessage? decode(List<int> buffer) {
    if (buffer.length < headerLength) return null;

    final header = latin1.decode(buffer.sublist(0, headerLength));
    final length = int.tryParse(header.substring(12, 20), radix: 16);
    if (length == null) {
      throw EmbFormatException(
          'payload length is not hex: "${header.substring(12, 20)}"');
    }
    if (buffer.length < headerLength + length) return null;

    return EmbMessage(
      header.substring(0, 4),
      header.substring(4, 12),
      Uint8List.fromList(buffer.sublist(headerLength, headerLength + length)),
    );
  }

  // ---- payload parsing ---------------------------------------------------

  static final _hex = RegExp(r'^[0-9A-Fa-f]+$');

  /// The 6-hex-character address a READ, LRED, WRIT or CSUM starts with.
  static String parseAddress(Uint8List payload) {
    if (payload.length < 6) {
      throw EmbFormatException('address must be 6 hex characters');
    }
    final a = latin1.decode(payload.sublist(0, 6));
    if (!_hex.hasMatch(a)) {
      throw EmbFormatException('address is not hex: "$a"');
    }
    return a.toUpperCase();
  }

  /// WRIT: a 6-character address followed by an even number of hex digits.
  static ({String address, String data}) parseWrite(Uint8List payload) {
    final address = parseAddress(payload);
    final data = latin1.decode(payload.sublist(6));
    if (data.length.isOdd) {
      throw EmbFormatException('write data must be whole bytes');
    }
    if (data.isNotEmpty && !_hex.hasMatch(data)) {
      throw EmbFormatException('write data is not hex');
    }
    return (address: address, data: data.toUpperCase());
  }

  /// UPLD: a 4-character page address followed by exactly 256 binary bytes.
  static ({String address, Uint8List data}) parseUpload(Uint8List payload) {
    if (payload.length != 260) {
      throw EmbFormatException(
          'upload payload must be 260 bytes, got ${payload.length}');
    }
    final a = latin1.decode(payload.sublist(0, 4));
    if (!_hex.hasMatch(a)) {
      throw EmbFormatException('upload address is not hex: "$a"');
    }
    return (address: a.toUpperCase(), data: payload.sublist(4, 260));
  }

  /// CSUM: a 6-character address and a 6-character length.
  static ({String address, String length}) parseChecksum(Uint8List payload) {
    if (payload.length != 12) {
      throw EmbFormatException(
          'checksum payload must be 12 bytes, got ${payload.length}');
    }
    final a = latin1.decode(payload.sublist(0, 6));
    final n = latin1.decode(payload.sublist(6, 12));
    if (!_hex.hasMatch(a)) throw EmbFormatException('address is not hex: "$a"');
    if (!_hex.hasMatch(n)) throw EmbFormatException('length is not hex: "$n"');
    return (address: a.toUpperCase(), length: n.toUpperCase());
  }

  /// BAUD: the rate as five ASCII digits.
  static int parseBaud(Uint8List payload) {
    final t = latin1.decode(payload).trim();
    if (t != '19200' && t != '57600') {
      throw EmbFormatException('baud rate must be 19200 or 57600, got "$t"');
    }
    return int.parse(t);
  }
}
