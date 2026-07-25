// Desktop/mobile file writing for SAVE (everything with dart:io).
import 'dart:io';

Future<bool> writeBytesToPath(String path, List<int> data) async {
  try {
    await File(path).writeAsBytes(data, flush: true);
    return true;
  } catch (_) {
    return false;
  }
}

/// Reads a file's bytes if it exists; returns null when missing or unreadable.
Future<List<int>?> readBytesFromPath(String path) async {
  try {
    final file = File(path);
    if (!await file.exists()) return null;
    return await file.readAsBytes();
  } catch (_) {
    return null;
  }
}
