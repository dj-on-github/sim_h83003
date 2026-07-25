// Web fallback: no direct path writing (file_picker's saveFile handles the
// download itself when given bytes).
Future<bool> writeBytesToPath(String path, List<int> data) async => false;

// Web fallback: no filesystem, so a sibling file can't be read by path.
Future<List<int>?> readBytesFromPath(String path) async => null;
