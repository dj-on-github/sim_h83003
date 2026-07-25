// Desktop/mobile implementation: reads a hex file path given on the command
// line. Used on every platform that has dart:io (everything except web).

import 'dart:io';

/// Result of trying to read a hex file named on the command line.
typedef StartupHex = ({String? path, String? contents, String? error});

/// Scans [args] for the first non-flag argument and, if it names a readable
/// file, returns its contents. Flags (anything starting with '-') are ignored.
StartupHex readStartupHexFile(List<String> args) {
  for (final a in args) {
    if (a.isEmpty || a.startsWith('-')) continue;
    try {
      final file = File(a);
      if (file.existsSync()) {
        return (path: a, contents: file.readAsStringSync(), error: null);
      }
      return (path: a, contents: null, error: 'file not found');
    } catch (e) {
      // e.g. a macOS sandbox denial for a path outside the app container.
      return (path: a, contents: null, error: '$e');
    }
  }
  return (path: null, contents: null, error: null);
}
