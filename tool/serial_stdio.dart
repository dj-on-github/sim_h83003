// ignore_for_file: avoid_print — command-line tool; stderr is the UI.
// Runs the machine with one simulated SCI channel joined to this process's
// standard input and output.
//
// tool/serial_bridge.dart does the same thing against a real serial port
// through libserialport. This needs no port and no native library, which is
// what makes it usable as the far end of a pty in a test: something else
// creates the pty pair, points the software under test at the slave, and
// relays the master to this process's pipes.
//
// The application's entry pointer is left alone, unlike serial_bridge's
// default, because the point here is the real boot path: the ROM announces
// itself, waits for the handshake, and hands over if nobody answers.
//
//   dart run tool/serial_stdio.dart <image.bin> [--channel 0|1] [--phi HZ]
//        [--seconds N] [--quiet] [--dump FILE]
//
// --dump writes the machine's memory out when this stops, which is how a
// burn is checked: what came down the wire should be in the flash.

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:sim_h83003/flash.dart';
import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';
import 'package:sim_h83003/sci_bridge.dart';

H8Cpu? cpuRef;

void main(List<String> argv) async {
  String? opt(String name) {
    final i = argv.indexOf(name);
    return (i >= 0 && i + 1 < argv.length) ? argv[i + 1] : null;
  }

  final args = argv.where((a) => !a.startsWith('--')).toList();
  final skip = <String>{};
  for (final n in ['--channel', '--phi', '--seconds', '--dump']) {
    final v = opt(n);
    if (v != null) skip.add(v);
  }
  args.removeWhere(skip.contains);

  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/serial_stdio.dart <image.bin> '
        '[--channel 0|1] [--phi HZ] [--seconds N] [--quiet]');
    exit(2);
  }

  final channel = int.parse(opt('--channel') ?? '1');
  final phi = int.parse(opt('--phi') ?? '11059200');
  final seconds = double.parse(opt('--seconds') ?? '600');
  final quiet = argv.contains('--quiet');
  final dumpTo = opt('--dump');

  void writeDump() {
    if (dumpTo == null) return;
    final out = Uint8List(0x1000000);
    for (var a = 0; a < out.length; a++) {
      out[a] = cpuRef!.mem.peek(a) & 0xFF;
    }
    File(dumpTo).writeAsBytesSync(out);
  }

  void say(String s) {
    if (!quiet) stderr.writeln('serial_stdio: $s');
  }

  final cpu = H8Cpu();
  cpuRef = cpu;
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);

  // The machine's two flash devices, unless --no-flash. Without them the
  // address space is plain memory, the boot ROM's identify finds no Atmel
  // part and every programming command is refused -- which is what 'M'
  // answering 'V' means.
  if (!argv.contains('--no-flash')) {
    for (final r in artista180Flash) {
      cpu.attachFlash(JedecFlash(base: r.base, size: r.size));
    }
    say('flash attached: '
        '${artista180Flash.map((r) => r.name).join(', ')}');
  }

  cpu.reset();

  final sci = cpu.sci[channel];
  final outgoing = <int>[];
  sci.onTransmit = outgoing.add;

  // Everything arriving on stdin waits here until the next slice hands it to
  // the channel, which delivers it at the rate the firmware has programmed.
  final pending = <int>[];
  var closed = false;
  var inBytes = 0, outBytes = 0, slices = 0;
  final ready = <int>[];
  stdin.listen((d) {
    inBytes += d.length;
    pending.addAll(d);
  }, onDone: () => closed = true);

  // A heartbeat, so a stall says which side stopped rather than looking like
  // the machine has gone quiet.
  Timer.periodic(const Duration(seconds: 2), (_) {
    if (!quiet) {
      stderr.writeln('serial_stdio: in=$inBytes out=$outBytes '
          'queued=${pending.length} slices=$slices '
          'rx=${sci.rxQueue.length} pc=H\'${cpu.pc.toRadixString(16)}');
    }
  });

  say('bridged ${sci.name} to stdin/stdout');

  // Run in a loop that yields to the event loop between slices, rather than
  // on a timer.
  //
  // A timer is the wrong shape here. The slice has to be big enough to be
  // worth the overhead and small enough that bytes move promptly, and if the
  // callback ever takes longer than the period the timer saturates: it fires
  // back to back, stdin callbacks never get a turn, the pipe from the relay
  // fills, and the block travels back through it to the pty until the far
  // end's write hangs. That is a stall with no error anywhere -- it looks
  // like the machine has simply stopped answering -- and tuning the period
  // against the slice only moves where it happens.
  //
  // An explicit yield settles it: whatever the slice costs, pending I/O runs
  // before the next one starts.
  //
  // Simulated speed is not the constraint in any case. The machine runs here
  // at about four and a half million instructions a second, faster than the
  // part it stands for; what the far end waits on is round-trip latency,
  // two of them for every byte, because each hex digit is echoed before the
  // next goes out.
  final clock = Stopwatch()..start();
  var lastBaud = 0;
  var idle = 0;

  while (true) {
    slices++;
    for (var i = 0; i < 2000; i++) {
      if (cpu.halted && !cpu.sleeping) break;
      cpu.step();
    }

    pumpSciBridge(
      channel: sci,
      outgoing: outgoing,
      phiHz: phi,
      setBaud: (b) {
        if (b != lastBaud) {
          lastBaud = b;
          say('rate now $b baud');
        }
      },
      drain: () {
        final take = List<int>.from(pending);
        pending.clear();
        return take;
      },
      send: (data) {
        outBytes += data.length;
        ready.addAll(data);
      },
    );

    // Push what the machine transmitted all the way out to the pipe, and
    // wait for it to get there.
    //
    // stdout.flush() returns a future. Calling add() and dropping that future
    // -- which is what this did at first -- leaves the bytes in the sink's
    // own buffer, and a byte still sitting there when nothing follows it is
    // a deadlock: the far end is waiting on an echo it will never see, and
    // this end is waiting for the next character to arrive. It stalls at a
    // different place every run, because where it happens depends on where
    // the buffer boundaries fall, and it looks from the outside as though
    // the machine has stopped answering.
    if (ready.isNotEmpty) {
      stdout.add(List<int>.from(ready));
      ready.clear();
      await stdout.flush();
    }

    // And hand the event loop back so stdin is read.
    await Future.delayed(Duration.zero);

    if (closed && pending.isEmpty && outgoing.isEmpty) {
      if (++idle > 20000) {
        writeDump();
        exit(0);
      }
    } else {
      idle = 0;
    }

    if (clock.elapsed.inMilliseconds > seconds * 1000) {
      say('out of time after ${seconds}s');
      writeDump();
      exit(0);
    }
  }
}
