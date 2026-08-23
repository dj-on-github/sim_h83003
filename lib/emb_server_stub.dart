// No TCP listener: web, where dart:io is not available.

import 'emb_relay.dart';

class EmbServer {
  EmbServer({required EmbRelay relay});

  bool get listening => false;
  int get port => 0;
  String? get client => null;
  int get requestsHandled => 0;
  String? get lastError => 'a TCP listener is not available on this platform';

  Future<String?> start(int port) async => lastError;
  Future<void> stop() async {}
  void Function()? onChanged;
}
