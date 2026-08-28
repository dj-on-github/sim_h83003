// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Update the boot flash of a v5 machine, from code running in the
// application flash, and check that an interrupted update is harmless.
//
// The updater is reached the way it would be on a real machine: it is placed
// in the spare space at the top of the application flash, the entry longword
// at H'200004 is pointed at it, and the boot ROM's 'G' command jumps there.
// Getting it into the application flash in the first place is the ROM's
// ordinary 'P' path, which is safe because it targets the other device;
// here it is placed directly, since the burner already proves that path.
//
//   dart run tool/boot_update.dart <v5image.bin> <dir-with-updater.bin>
//     [--cut N]   stop the machine N pages into the write, as a power failure
//     [--busy N]  reads of the boot device that report busy after a write
//     [--out F]   write the machine as it stands afterwards
//     [--trace]   report the updater's progress as it runs
//
// Without --cut the update runs to completion and the machine is restarted
// to show it comes up on the new image. With --cut the half-written image is
// kept and the machine restarted from it, which is the case that matters:
// the new image fails its checksum and stage-0 falls back to the old one.

import 'dart:io';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

const int updaterBase = 0x3E8000;
const int payloadBase = 0x3EA000;
const int appEntryAlt = 0x200004;
const int statusByte = 0xFFF7F0;
const int pagesDone = 0xFFF7F4;
const int activeBase = 0xFFF710;
const int imageA = 0x000800;
const int imageB = 0x004000;

String h(int v) => 'H${v.toRadixString(16).toUpperCase().padLeft(6, '0')}';

class Machine {
  Machine(List<int> image, {int busy = 0, bool download = true}) {
    loadRawBinary(image, 0, cpu.mem.poke);
    if (download) cpu.mem.poke(0x200000, 0x01);
    for (final r in artista180Flash) {
      final d = JedecFlash(base: r.base, size: r.size);
      if (r.name == 'boot') d.busyReads = busy;
      cpu.attachFlash(d);
    }
    cpu.reset();
  }
  final H8Cpu cpu = H8Cpu();

  void run(int n) {
    for (var i = 0; i < n && !(cpu.halted && !cpu.sleeping); i++) {
      cpu.step();
    }
  }

  int u32(int a) =>
      (cpu.peekBus(a) << 24) |
      (cpu.peekBus(a + 1) << 16) |
      (cpu.peekBus(a + 2) << 8) |
      cpu.peekBus(a + 3);

  List<int> snapshot(int base, int len) =>
      List<int>.generate(len, (i) => cpu.peekBus(base + i));
}

void main(List<String> args) {
  String? opt(String n) =>
      args.contains(n) ? args[args.indexOf(n) + 1] : null;
  final cut = int.tryParse(opt('--cut') ?? '');
  final busy = int.parse(opt('--busy') ?? '400');

  final files = args.where((a) => !a.startsWith('--')).toList();
  final base = File(files[0]).readAsBytesSync().toList();
  final dir = files.length > 1 ? files[1] : '.';
  final updater = File('$dir/updater.bin').readAsBytesSync();
  final payload = File('$dir/imageN.bin').readAsBytesSync();

  // Place the updater and the image it carries, and point 'G' at it.
  final image = List<int>.from(base);
  image.setRange(updaterBase, updaterBase + updater.length, updater);
  image.setRange(payloadBase, payloadBase + payload.length, payload);
  image[appEntryAlt] = (updaterBase >> 24) & 0xFF;
  image[appEntryAlt + 1] = (updaterBase >> 16) & 0xFF;
  image[appEntryAlt + 2] = (updaterBase >> 8) & 0xFF;
  image[appEntryAlt + 3] = updaterBase & 0xFF;

  final m = Machine(image, busy: busy);
  print('boot device busy for $busy reads after each page write');

  // Reach the download loop -- the handshake offers the link for 500 rounds
  // before giving up -- and note which image stage-0 chose.
  m.run(14000000);
  final started = m.u32(activeBase);
  print('running image ${started == imageA ? 'A' : 'B'} at ${h(started)} '
      '(generation ${m.u32(started + 4)})');

  m.cpu.sci1.receive('G'.codeUnits);

  var stoppedAt = -1;
  for (var i = 0; i < 60000000; i++) {
    m.cpu.step();
    if (args.contains('--trace') && i % 8000000 == 0) {
      print('   ${i ~/ 1000000}M: pc=${h(m.cpu.pc)} '
          'status=H${m.cpu.peekBus(statusByte).toRadixString(16)} '
          'pages=${m.u32(pagesDone)}');
    }
    final st = m.cpu.peekBus(statusByte);
    if (cut != null && m.u32(pagesDone) >= cut) {
      stoppedAt = m.u32(pagesDone);
      break; // the power fails here, mid-update
    }
    if (st == 0x5A || st == 0x02 || st == 0x03) break;
  }

  final st = m.cpu.peekBus(statusByte);
  final target = started == imageA ? imageB : imageA;
  print('updater wrote slot ${target == imageA ? 'A' : 'B'} at ${h(target)}, '
      '${m.u32(pagesDone)} pages');

  if (stoppedAt >= 0) {
    print('POWER CUT after $stoppedAt pages -- the machine stops here');
  } else {
    print('updater status: ${st == 0x5A ? 'done' : st == 0x02
        ? 'PROGRAM FAILED' : st == 0x03 ? 'VERIFY FAILED' : 'still running'}');
    final got = m.snapshot(target, payload.length);
    final same = List.generate(payload.length, (i) => got[i] == payload[i])
        .every((x) => x);
    print('slot contents vs the payload: ${same ? 'IDENTICAL' : 'DIFFER'}');
  }

  // Whatever state the flash is in, restart from it and see what comes up.
  final after = m.snapshot(0, 0x8000);
  final restart = List<int>.from(image);
  restart.setRange(0, 0x8000, after);
  for (var copy = 0x8000; copy < 0x20000; copy += 0x8000) {
    restart.setRange(copy, copy + 0x8000, after); // the device is mirrored
  }
  final out = opt('--out');
  if (out != null) {
    File(out).writeAsBytesSync(restart);
    print('wrote the machine as it now stands to $out');
  }
  final m2 = Machine(restart, busy: 0);
  m2.run(14000000);
  final now = m2.u32(activeBase);
  print('after a reset: ${now == 0 ? 'NO VALID IMAGE' : 'image '
      '${now == imageA ? 'A' : 'B'} at ${h(now)}, '
      'generation ${m2.u32(now + 4)}'}');
}
