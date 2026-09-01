// Gzip for snapshots, where the platform provides it.
export 'snapshot_gzip_stub.dart' if (dart.library.io) 'snapshot_gzip_io.dart';
