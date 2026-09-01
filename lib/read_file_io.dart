// Reading a file the config named, where the platform has files.
import 'dart:io';
import 'dart:typed_data';

/// The file's bytes, or null if it is not there or cannot be read.
Uint8List? readFileBytes(String path) {
  try {
    final f = File(path);
    return f.existsSync() ? f.readAsBytesSync() : null;
  } catch (_) {
    return null;
  }
}
