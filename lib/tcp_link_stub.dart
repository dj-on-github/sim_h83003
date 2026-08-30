// No sockets: the web build. Every method is a no-op and the bridge cannot
// be switched on, the same way the serial stub behaves.

class TcpLink {
  bool get isOpen => false;
  bool get isConnected => false;
  int get port => 0;
  int get baud => 0;
  int get bytesIn => 0;
  int get bytesOut => 0;
  String? get lastError => 'sockets are not available on this platform';
  String describe() => 'unavailable';

  Future<String?> open(int port, int baud) async => lastError;
  void setBaud(int baud) {}
  void send(List<int> data) {}
  List<int> drain() => const [];
  Future<void> close() async {}
}
