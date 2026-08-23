// No host serial ports: web, and anywhere the native library is missing.
//
// Every method is a no-op and no ports are offered, so the SCI tab shows an
// empty list and the bridge cannot be switched on.

class SerialLink {
  static List<String> availablePorts() => const [];
  static Map<String, String> describePorts(List<String> ports) =>
      {for (final p in ports) p: p};

  bool get isOpen => false;
  String? get portName => null;
  int get baud => 0;
  int get bytesIn => 0;
  int get bytesOut => 0;
  String? get lastError => 'serial ports are not available on this platform';

  String? open(String port, int baud) => lastError;
  void setBaud(int baud) {}
  void send(List<int> data) {}
  List<int> drain() => const [];
  void close() {}
}
