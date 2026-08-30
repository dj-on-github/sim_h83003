// Reading and writing ~/.h8simrc, where the platform has a home directory.
//
// $HOME on Linux and macOS; USERPROFILE is honoured too so a Windows build
// puts it somewhere sensible rather than nowhere. If none of them is set the
// path is null and the app runs on its built-in defaults, which is what
// happens in a test run.

import 'dart:io';

import 'sim_config.dart';

/// Where the file lives, or null if there is no home directory to put it in.
String? simConfigPath() {
  final env = Platform.environment;
  final home = env['HOME'] ?? env['USERPROFILE'];
  if (home == null || home.isEmpty) return null;
  return '$home${Platform.pathSeparator}$kConfigFileName';
}

/// Reads the config. Returns null when there is no file -- that is the
/// ordinary first run, not a problem. A file that is there but unreadable or
/// malformed throws, because silently ignoring it would leave the user
/// wondering why their settings did nothing.
SimConfig? loadSimConfig() {
  final path = simConfigPath();
  if (path == null) return null;
  final f = File(path);
  if (!f.existsSync()) return null;
  return SimConfig.parse(f.readAsStringSync());
}

/// Writes the config, and returns where it went.
String saveSimConfig(SimConfig config) {
  final path = simConfigPath();
  if (path == null) {
    throw const FileSystemException('no home directory to write it in');
  }
  File(path).writeAsStringSync(config.toText());
  return path;
}
