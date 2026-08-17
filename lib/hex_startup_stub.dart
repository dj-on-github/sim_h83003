// Fallback used on platforms without dart:io (e.g. web). There is no
// command-line file access, so this always reports "nothing to load".

import 'dart:typed_data';

/// Result of trying to read a program file named on the command line.
typedef StartupHex = ({String? path, Uint8List? bytes, String? error});

StartupHex readStartupHexFile(List<String> args) =>
    (path: null, bytes: null, error: null);
