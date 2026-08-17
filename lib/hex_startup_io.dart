// Desktop/mobile implementation: reads a program file named on the command
// line. Used on every platform that has dart:io (everything except web).
//
// The bytes are returned raw rather than as a string, because the file may be
// a flat binary memory dump as well as Intel HEX or S-record text.

import 'dart:io';
import 'dart:typed_data';

/// Result of trying to read a program file named on the command line.
typedef StartupHex = ({String? path, Uint8List? bytes, String? error});

/// Scans [args] for the first non-flag argument and, if it names a readable
/// file, returns its bytes. Flags (anything starting with '-') are ignored.
StartupHex readStartupHexFile(List<String> args) {
  for (final a in args) {
    if (a.isEmpty || a.startsWith('-')) continue;
    try {
      final file = File(a);
      if (file.existsSync()) {
        return (path: a, bytes: file.readAsBytesSync(), error: null);
      }
      return (path: a, bytes: null, error: 'file not found');
    } catch (e) {
      // e.g. a macOS sandbox denial for a path outside the app container.
      return (path: a, bytes: null, error: '$e');
    }
  }
  return (path: null, bytes: null, error: null);
}
