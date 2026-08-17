// Program-file parsing and generation for the H8/3003 simulator.
//
// Two formats are supported for loading:
//
//   Intel HEX  (:LLAAAATT[DD...]CC) — with type 02/04 extended-address
//              records so the full 24-bit space is reachable.
//   Motorola S-records (S1/S2/S3 with 16/24/32-bit addresses) — the
//              format Renesas/GNU H8 toolchains emit; S7/S8/S9 records
//              supply the entry address.
//
// The format is auto-detected from the first record character. Saving
// always writes Intel HEX with type-04 records, covering only the
// allocated regions of the sparse memory.

import 'sparse_memory.dart';

/// Outcome of parsing a program file.
class HexResult {
  /// Number of data bytes written to memory.
  final int bytesLoaded;

  /// Lowest / highest addresses written (null if nothing was written).
  final int? minAddress;
  final int? maxAddress;

  /// Execution start address from a start record, if present.
  final int? startAddress;

  /// True when a proper end-of-file record terminated the file.
  final bool sawEof;

  /// Human-readable problems encountered (bad checksums, malformed lines).
  final List<String> errors;

  const HexResult({
    required this.bytesLoaded,
    required this.minAddress,
    required this.maxAddress,
    required this.startAddress,
    required this.sawEof,
    required this.errors,
  });

  bool get isEmpty => bytesLoaded == 0;
}

/// Parses [text] as Intel HEX or Motorola S-records (auto-detected),
/// calling [write] for each decoded byte. Addresses are masked to the
/// 24-bit space.
HexResult parseHexFile(String text, void Function(int addr, int value) write) {
  final trimmed = text.trimLeft();
  if (trimmed.startsWith('S')) return _parseSrec(text, write);
  return _parseIntelHex(text, write);
}

/// What a program file turned out to be.
enum ProgramFormat {
  intelHex,
  srecord,

  /// A flat binary — a memory dump, or a ROM image. Carries no addresses of
  /// its own, so the caller has to say where it belongs.
  raw,
}

/// Works out how to read a program file from its first bytes.
///
/// Intel HEX and S-records are printable ASCII beginning with ':' or 'S';
/// anything with bytes outside that set, or with a different first
/// character, is treated as a flat binary.
ProgramFormat detectProgramFormat(List<int> bytes) {
  var i = 0;
  // Skip leading whitespace.
  while (i < bytes.length &&
      (bytes[i] == 0x20 || bytes[i] == 0x09 || bytes[i] == 0x0D ||
          bytes[i] == 0x0A)) {
    i++;
  }
  if (i >= bytes.length) return ProgramFormat.raw;

  // A record file is entirely printable; a dump almost never is. Checking a
  // slice is enough and keeps this cheap for a 16-Mbyte image.
  final limit = (i + 512).clamp(0, bytes.length);
  for (var j = i; j < limit; j++) {
    final b = bytes[j];
    final printable =
        (b >= 0x20 && b < 0x7F) || b == 0x09 || b == 0x0D || b == 0x0A;
    if (!printable) return ProgramFormat.raw;
  }

  if (bytes[i] == 0x3A) return ProgramFormat.intelHex; // ':'
  if (bytes[i] == 0x53 || bytes[i] == 0x73) {
    // 'S' or 's', followed by a record-type digit.
    if (i + 1 < bytes.length &&
        bytes[i + 1] >= 0x30 &&
        bytes[i + 1] <= 0x39) {
      return ProgramFormat.srecord;
    }
  }
  return ProgramFormat.raw;
}

/// Loads a flat binary at [base], calling [write] for each byte.
///
/// 64K blocks that are entirely zero are skipped: unwritten memory already
/// reads as zero, and skipping them keeps a full 16-Mbyte address-space dump
/// from allocating every bank. Blocks of H'FF are loaded normally, since
/// those are a real value the firmware can see.
HexResult loadRawBinary(
  List<int> bytes,
  int base,
  void Function(int addr, int value) write,
) {
  const block = SparseMemory.bankSize;
  int? minA, maxA;
  var count = 0;
  for (var start = 0; start < bytes.length; start += block) {
    final end = (start + block) < bytes.length ? start + block : bytes.length;
    var allZero = true;
    for (var i = start; i < end; i++) {
      if (bytes[i] != 0) {
        allZero = false;
        break;
      }
    }
    if (allZero) continue;
    for (var i = start; i < end; i++) {
      final addr = (base + i) & SparseMemory.addrMask;
      write(addr, bytes[i]);
      minA = (minA == null || addr < minA) ? addr : minA;
      maxA = (maxA == null || addr > maxA) ? addr : maxA;
      count++;
    }
  }
  return HexResult(
    bytesLoaded: count,
    minAddress: minA,
    maxAddress: maxA,
    startAddress: null,
    sawEof: true,
    errors: const [],
  );
}

HexResult _parseIntelHex(
    String text, void Function(int addr, int value) write) {
  var upperBase = 0; // contributed by type 02/04 records
  int? minA, maxA, startA;
  var count = 0;
  var sawEof = false;
  final errors = <String>[];

  final lines = text.split(RegExp(r'[\r\n]+'));
  for (var i = 0; i < lines.length; i++) {
    final lineNo = i + 1;
    final line = lines[i].trim();
    if (line.isEmpty) continue;
    if (!line.startsWith(':')) {
      errors.add('Line $lineNo: does not start with ":"');
      continue;
    }
    final bytes = _hexPairs(line.substring(1));
    if (bytes == null || bytes.length < 5) {
      errors.add('Line $lineNo: malformed record');
      continue;
    }
    final len = bytes[0];
    if (bytes.length != len + 5) {
      errors.add('Line $lineNo: byte-count mismatch');
      continue;
    }
    if (bytes.fold<int>(0, (a, b) => a + b) & 0xFF != 0) {
      errors.add('Line $lineNo: bad checksum');
      continue;
    }
    final addr = (bytes[1] << 8) | bytes[2];
    final type = bytes[3];
    final data = bytes.sublist(4, 4 + len);
    switch (type) {
      case 0x00: // data
        for (var k = 0; k < data.length; k++) {
          final full = (upperBase + addr + k) & SparseMemory.addrMask;
          write(full, data[k]);
          minA = (minA == null || full < minA) ? full : minA;
          maxA = (maxA == null || full > maxA) ? full : maxA;
          count++;
        }
        break;
      case 0x01: // end of file
        sawEof = true;
        return HexResult(
          bytesLoaded: count,
          minAddress: minA,
          maxAddress: maxA,
          startAddress: startA,
          sawEof: true,
          errors: errors,
        );
      case 0x02: // extended segment address
        upperBase = (((data[0] << 8) | data[1]) << 4);
        break;
      case 0x04: // extended linear address
        upperBase = (((data[0] << 8) | data[1]) << 16);
        break;
      case 0x03: // start segment address (CS:IP)
        if (data.length >= 4) {
          startA = (((data[0] << 8) | data[1]) << 4) +
              ((data[2] << 8) | data[3]);
          startA &= SparseMemory.addrMask;
        }
        break;
      case 0x05: // start linear address
        if (data.length >= 4) {
          startA = ((data[0] << 24) |
                  (data[1] << 16) |
                  (data[2] << 8) |
                  data[3]) &
              SparseMemory.addrMask;
        }
        break;
      default:
        errors.add('Line $lineNo: unsupported record type '
            '0x${type.toRadixString(16).padLeft(2, '0')}');
    }
  }
  if (!sawEof) errors.add('No end-of-file (:00000001FF) record found');
  return HexResult(
    bytesLoaded: count,
    minAddress: minA,
    maxAddress: maxA,
    startAddress: startA,
    sawEof: sawEof,
    errors: errors,
  );
}

HexResult _parseSrec(String text, void Function(int addr, int value) write) {
  int? minA, maxA, startA;
  var count = 0;
  var sawEof = false;
  final errors = <String>[];

  final lines = text.split(RegExp(r'[\r\n]+'));
  for (var i = 0; i < lines.length; i++) {
    final lineNo = i + 1;
    final line = lines[i].trim();
    if (line.isEmpty) continue;
    if (line.length < 4 || (line[0] != 'S' && line[0] != 's')) {
      errors.add('Line $lineNo: not an S-record');
      continue;
    }
    final type = line[1];
    final bytes = _hexPairs(line.substring(2));
    if (bytes == null || bytes.length < 3) {
      errors.add('Line $lineNo: malformed record');
      continue;
    }
    final len = bytes[0];
    if (bytes.length != len + 1) {
      errors.add('Line $lineNo: byte-count mismatch');
      continue;
    }
    // Checksum: one's complement of the sum of length, address and data.
    final sum = bytes.sublist(0, bytes.length - 1).fold<int>(0, (a, b) => a + b);
    if (((sum & 0xFF) ^ 0xFF) != bytes.last) {
      errors.add('Line $lineNo: bad checksum');
      continue;
    }
    int addrBytes;
    switch (type) {
      case '1':
        addrBytes = 2;
        break;
      case '2':
        addrBytes = 3;
        break;
      case '3':
        addrBytes = 4;
        break;
      case '7': // start address (32-bit)
      case '8': // start address (24-bit)
      case '9': // start address (16-bit)
        addrBytes = type == '7' ? 4 : (type == '8' ? 3 : 2);
        var a = 0;
        for (var k = 0; k < addrBytes; k++) {
          a = (a << 8) | bytes[1 + k];
        }
        startA = a & SparseMemory.addrMask;
        sawEof = true;
        continue;
      case '0': // header
      case '4':
      case '5': // record count
      case '6':
        continue;
      default:
        errors.add('Line $lineNo: unsupported S-record type S$type');
        continue;
    }
    var addr = 0;
    for (var k = 0; k < addrBytes; k++) {
      addr = (addr << 8) | bytes[1 + k];
    }
    final data = bytes.sublist(1 + addrBytes, bytes.length - 1);
    for (var k = 0; k < data.length; k++) {
      final full = (addr + k) & SparseMemory.addrMask;
      write(full, data[k]);
      minA = (minA == null || full < minA) ? full : minA;
      maxA = (maxA == null || full > maxA) ? full : maxA;
      count++;
    }
  }
  return HexResult(
    bytesLoaded: count,
    minAddress: minA,
    maxAddress: maxA,
    startAddress: startA,
    sawEof: sawEof,
    errors: errors,
  );
}

/// Decodes a run of hex pairs, or null on bad characters / odd length.
List<int>? _hexPairs(String s) {
  if (s.length.isOdd) return null;
  final out = <int>[];
  for (var i = 0; i < s.length; i += 2) {
    final b = int.tryParse(s.substring(i, i + 2), radix: 16);
    if (b == null) return null;
    out.add(b);
  }
  return out;
}

/// Generates Intel HEX text for the allocated regions of [mem], using
/// type-04 extended linear address records to cover the 24-bit space.
/// Runs of zero bytes longer than a record are skipped to keep files
/// compact (unallocated memory reads as zero anyway).
String memoryToIntelHex(SparseMemory mem, {int recordLength = 16}) {
  String hex2(int v) =>
      (v & 0xFF).toRadixString(16).toUpperCase().padLeft(2, '0');

  final sb = StringBuffer();
  var currentUpper = -1;

  void emitRecord(int addr, List<int> data) {
    final upper = addr >> 16;
    if (upper != currentUpper) {
      currentUpper = upper;
      final rec = [2, 0, 0, 0x04, (upper >> 8) & 0xFF, upper & 0xFF];
      var sum = 0;
      for (final b in rec) {
        sum += b;
      }
      sb.write(':');
      for (final b in rec) {
        sb.write(hex2(b));
      }
      sb.write(hex2((-sum) & 0xFF));
      sb.write('\r\n');
    }
    final rec = [data.length, (addr >> 8) & 0xFF, addr & 0xFF, 0x00, ...data];
    var sum = 0;
    for (final b in rec) {
      sum += b;
    }
    sb.write(':');
    for (final b in rec) {
      sb.write(hex2(b));
    }
    sb.write(hex2((-sum) & 0xFF));
    sb.write('\r\n');
  }

  for (final (start, end) in mem.regions()) {
    for (var addr = start; addr < end; addr += recordLength) {
      final len = (addr + recordLength <= end) ? recordLength : end - addr;
      final data = [for (var i = 0; i < len; i++) mem.peek(addr + i)];
      // Skip records that are entirely zero — unallocated memory already
      // reads as zero, so they carry no information.
      if (data.every((b) => b == 0)) continue;
      // A record must not cross a 64K boundary (the AAAA field is 16-bit).
      emitRecord(addr, data);
    }
  }
  sb.write(':00000001FF\r\n');
  return sb.toString();
}
