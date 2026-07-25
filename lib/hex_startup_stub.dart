// Fallback used on platforms without dart:io (e.g. web). There is no
// command-line file access, so this always reports "nothing to load".

/// Result of trying to read a hex file named on the command line.
typedef StartupHex = ({String? path, String? contents, String? error});

StartupHex readStartupHexFile(List<String> args) =>
    (path: null, contents: null, error: null);
