// Gzip, where dart:io has it.
import 'dart:io' as io;

List<int> gzipEncode(List<int> bytes) => io.gzip.encode(bytes);
List<int> gzipDecode(List<int> bytes) => io.gzip.decode(bytes);
