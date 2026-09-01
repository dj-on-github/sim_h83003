// No dart:io, so no gzip. The web build has no files to write either, and
// Snapshot.decode falls back to plain JSON when the bytes are not gzipped,
// so an uncompressed snapshot still reads.
List<int> gzipEncode(List<int> bytes) => bytes;
List<int> gzipDecode(List<int> bytes) => bytes;
