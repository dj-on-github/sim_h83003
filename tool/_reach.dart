// ignore_for_file: avoid_print
import 'dart:io';
import 'dart:typed_data';
import 'package:sim_h83003/h8disasm.dart';
String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
class Img { Img(this.b); final Uint8List b;
  int peek(int a) => (a >= 0 && a < b.length) ? b[a] : 0;
  bool has(int a) => a >= 0 && a < b.length;
  int word(int a) => (peek(a) << 8) | peek(a + 1); }
int? callTarget(Img img, int pc) {
  final b0 = img.peek(pc), b1 = img.peek(pc + 1);
  if (b0 == 0x55) return (pc + 2 + (b1 < 0x80 ? b1 : b1 - 0x100)) & 0xFFFFFF;
  if (b0 == 0x5C && b1 == 0x00) { final w = img.word(pc + 2);
    return (pc + 4 + (w < 0x8000 ? w : w - 0x10000)) & 0xFFFFFF; }
  if (b0 == 0x5E) return ((b1 << 16) | img.word(pc + 2)) & 0xFFFFFF;
  return null; }
List<int> callsFrom(Img img, int entry, int limit) {
  final seen = <int>{}; final work = <int>[entry]; final sites = <int, int>{};
  while (work.isNotEmpty) { var pc = work.removeLast();
    while (true) {
      if (pc < entry || pc >= limit || seen.contains(pc) || !img.has(pc)) break;
      seen.add(pc);
      final ins = disassembleH8(img.peek, pc);
      final b0 = img.peek(pc), b1 = img.peek(pc + 1); final next = pc + ins.length;
      final callee = callTarget(img, pc);
      if (callee != null) { sites[pc] = callee; pc = next; continue; }
      if (b0 >= 0x40 && b0 <= 0x4F) {
        work.add((pc + 2 + (b1 < 0x80 ? b1 : b1 - 0x100)) & 0xFFFFFF);
        if (b0 == 0x40) break; pc = next; continue; }
      if (b0 == 0x58) { final w = img.word(pc + 2);
        work.add((pc + 4 + (w < 0x8000 ? w : w - 0x10000)) & 0xFFFFFF);
        if ((b1 >> 4) == 0) break; pc = next; continue; }
      if (b0 == 0x54 || b0 == 0x56) break;
      if (b0 == 0x59 || b0 == 0x5A || b0 == 0x5B) break;
      if (b0 == 0x01 && b1 == 0x00 && img.peek(pc + 2) == 0x00) break;
      pc = next; } }
  final o = sites.keys.toList()..sort();
  return [for (final pc in o) sites[pc]!]; }
void main(List<String> args) {
  final img = Img(File(args[0]).readAsBytesSync());
  final roots = args.sublist(1).map((s) => int.parse(s, radix: 16)).toList();
  final entries = <int>{...roots};
  for (var pc = 0x200000; pc < 0x251000; pc++) {
    final t = callTarget(img, pc);
    if (t != null && t >= 0x200000 && t < 0x251000) entries.add(t); }
  final sorted = entries.toList()..sort();
  int limitFor(int e) { for (final s in sorted) { if (s > e) return s; } return e + 0x1000; }
  final src = File('bernina_artista180/application/app.c').readAsStringSync();
  final done = <int>{};
  for (final m in RegExp(r"H'(2[0-9A-F]{5})").allMatches(src)) {
    done.add(int.parse(m.group(1)!, radix: 16)); }
  final reach = <int>{}; final work = <int>[...roots];
  final edges = <int, List<int>>{};
  while (work.isNotEmpty) { final e = work.removeLast();
    if (!reach.add(e)) continue;
    final cs = callsFrom(img, e, limitFor(e)); edges[e] = cs;
    for (final c in cs) {
      if (c >= 0x200000 && c < 0x251000 && !reach.contains(c)) work.add(c); } }
  final missing = reach.where((a) => !done.contains(a)).toList()..sort();
  print('reached ${reach.length}, missing ${missing.length}');
  for (final m in missing) {
    final callers = edges.entries.where((e) => e.value.contains(m))
        .map((e) => hex6(e.key)).take(3).join(' ');
    print("${hex6(m)}  <- $callers"); } }
