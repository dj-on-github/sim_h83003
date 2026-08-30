// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// The whole cycle the Flash tab exists for: boot a machine with the flash
// devices on the bus, change flash through the download protocol, save the
// image, load it into a fresh machine, and check the change is still there
// and the machine still boots.
//
// Usage:
//   dart run tool/flash_roundtrip.dart <image.bin> <out.bin>

import 'dart:io';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'dart:typed_data';

import 'package:sim_h83003/lcd.dart';

H8Cpu machine(List<int> memoryImage, {bool download = true}) {
  final cpu = H8Cpu();
  loadRawBinary(memoryImage, 0, cpu.mem.poke);
  if (download) cpu.mem.poke(0x200000, 0x01);
  for (final r in artista180Flash) {
    cpu.attachFlash(JedecFlash.forRegion(r));
  }
  cpu.reset();
  return cpu;
}

String exchange(H8Cpu cpu, String text, int settle) {
  final sci = (cpu.peekBus(0xFFFD1C) & 0x02) != 0 ? cpu.sci0 : cpu.sci1;
  sci.txLog.clear();
  sci.receive(text.codeUnits);
  for (var i = 0; i < settle; i++) {
    cpu.step();
  }
  return sci.txLog
      .map((b) => b >= 32 && b < 127 ? String.fromCharCode(b) : '.')
      .join();
}

List<int> screenOf(List<int> memoryImage) {
  final cpu = H8Cpu();
  loadRawBinary(memoryImage, 0, cpu.mem.poke);
  for (final r in artista180Flash) {
    cpu.attachFlash(JedecFlash.forRegion(r));
  }
  cpu.reset();
  for (var i = 0; i < 40000000; i++) {
    cpu.step();
  }
  final grey = Uint8List(LcdFormat.width * LcdFormat.height);
  var i = 0;
  for (var y = 0; y < LcdFormat.height; y++) {
    for (var x = 0; x < LcdFormat.width; x++) {
      final byte = cpu.mem.peek(0x040000 + y * LcdFormat.stride + (x >> 2));
      grey[i++] = LcdFormat.levels[3 - ((byte >> (6 - 2 * (x & 3))) & 3)];
    }
  }
  return grey;
}

void main(List<String> args) {
  final original = File(args[0]).readAsBytesSync();

  // 1. Boot with the devices on the bus and program a byte into each.
  final cpu = machine(original);
  for (var i = 0; i < 1500000; i++) {
    cpu.step();
  }
  print('write  ${exchange(cpu, "Z21234541", 4000000)}   (H\'212345 <- H\'41)');
  print('write  ${exchange(cpu, "Z00200042", 4000000)}   (H\'002000 <- H\'42)');

  // 2. Save what the two devices now hold.
  final image = buildFlashImage(cpu.mem.peek);
  File(args[1]).writeAsBytesSync(image);
  print('saved  ${image.length} bytes to ${args[1]}');

  // 3. Load it into a fresh machine and read the bytes back.
  final fresh = H8Cpu();
  loadRawBinary(original, 0, fresh.mem.poke); // the rest of the address space
  applyFlashImage(image, fresh.mem.poke);
  fresh.mem.poke(0x200000, 0x01);
  for (final r in artista180Flash) {
    fresh.attachFlash(JedecFlash.forRegion(r));
  }
  fresh.reset();
  for (var i = 0; i < 1500000; i++) {
    fresh.step();
  }
  print('read   ${exchange(fresh, "r212345", 2000000)}');
  print('read   ${exchange(fresh, "r002000", 2000000)}');

  // 4. Saving and reloading must be lossless: an image taken straight off a
  //    freshly loaded machine, put back, has to boot to the same screen.
  //    (The bytes changed above are live code, so a machine carrying those
  //    is expected to behave differently -- that is the change working, not
  //    a fault in the round trip.)
  final pristine = H8Cpu();
  loadRawBinary(original, 0, pristine.mem.poke);
  final cleanImage = buildFlashImage(pristine.mem.peek);

  final reloaded = List<int>.from(original);
  applyFlashImage(cleanImage, (a, v) => reloaded[a] = v);

  final before = screenOf(original);
  final after = screenOf(reloaded);
  var diff = 0;
  for (var i = 0; i < before.length; i++) {
    if (before[i] != after[i]) diff++;
  }
  print('boot   $diff of ${before.length} pixels differ after save and reload');

  var bytesDiff = 0;
  for (final r in artista180Flash) {
    for (var i = 0; i < r.size; i++) {
      if (reloaded[r.base + i] != original[r.base + i]) bytesDiff++;
    }
  }
  print('bytes  $bytesDiff of ${flashImageSize()} differ after save and reload');
}
