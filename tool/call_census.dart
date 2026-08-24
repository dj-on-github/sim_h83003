// ignore_for_file: avoid_print — command-line tool; stdout is the UI.
// Which routines run between two points, how often, and how deep.
//
// A static call tree includes everything the encoding mentions; this records
// what is actually called on the way to a given event, which is the work list
// for rebuilding that path. Calls are recognised from the instruction stream
// rather than from symbols, so a routine reached through a computed address
// still shows up.
//
//   dart run tool/call_census.dart <image.bin> <fromPC> <watchAddr> [steps]
//   dart run tool/call_census.dart <image.bin> <fromPC> return [steps]
//
// Counting starts when the CPU first reaches fromPC. It stops either when
// watchAddr is read or written, or -- with "return" -- when the routine at
// fromPC returns, which is what bounds a subtree that has no event to watch
// for. Returning is detected from the stack pointer rising back above where
// it was on entry, which does not care how the routine got there.

import 'dart:io';

import 'package:sim_h83003/h8300h.dart';
import 'package:sim_h83003/hex_files.dart';

String h6(int v) => "H'${v.toRadixString(16).toUpperCase().padLeft(6, '0')}";

void main(List<String> args) {
  final cpu = H8Cpu();
  loadRawBinary(File(args[0]).readAsBytesSync(), 0, cpu.mem.poke);
  cpu.reset();

  final from = int.parse(args[1], radix: 16);
  final untilReturn = args[2] == 'return';
  final watch = untilReturn ? -1 : int.parse(args[2], radix: 16);
  final steps = int.parse(args.length > 3 ? args[3] : '40000000');

  // Run up to the starting point without recording.
  var i = 0;
  for (; i < steps && cpu.pc != from; i++) {
    cpu.step();
  }
  if (cpu.pc != from) {
    print('never reached ${h6(from)}');
    exit(1);
  }
  print('recording from ${h6(from)} at step $i');

  if (!untilReturn) cpu.dataBreaks.add(watch);
  final entrySp = cpu.er[7] & 0xFFFFFF;
  final calls = <int, int>{}; // callee -> times entered
  final firstSeen = <int, int>{};
  final order = <int>[];
  var depth = 0, maxDepth = 0, entered = 0;

  for (; i < steps; i++) {
    final pc = cpu.pc;
    final op = cpu.mem.peek(pc);
    // BSR (55, 5C-prefixed), JSR @aa:24 (5E), JSR @aa:16 (5D), JSR @ERn (59),
    // JSR @@aa:8 (5F).
    final isCall =
        op == 0x55 || op == 0x5C || op == 0x5E || op == 0x5D || op == 0x5F;
    final isJsrIndirect = op == 0x59;
    final isReturn = op == 0x54 && cpu.mem.peek(pc + 1) == 0x70;

    cpu.clearBreakHit();
    cpu.step();

    if (isCall || isJsrIndirect) {
      final target = cpu.pc;
      calls[target] = (calls[target] ?? 0) + 1;
      if (!firstSeen.containsKey(target)) {
        firstSeen[target] = order.length;
        order.add(target);
      }
      entered++;
      depth++;
      if (depth > maxDepth) maxDepth = depth;
    } else if (isReturn && depth > 0) {
      depth--;
    }

    if (untilReturn) {
      if ((cpu.er[7] & 0xFFFFFF) > entrySp) {
        print('stopped at step $i: the routine returned');
        break;
      }
    } else if (cpu.breakHit) {
      print('stopped at step $i: ${h6(watch)} touched from '
          '${h6(cpu.breakPc ?? 0)}');
      break;
    }
  }

  print('');
  print('calls made: $entered, maximum depth $maxDepth');
  print('distinct routines entered: ${calls.length}');

  final inApp = order.where((a) => a >= 0x200000 && a < 0x251000).toList();
  final inRom = order.where((a) => a < 0x3000).toList();
  print('  in the application: ${inApp.length}');
  print('  in the boot ROM:    ${inRom.length}');
  print('');
  print('application routines, in the order first entered:');
  for (var n = 0; n < inApp.length; n++) {
    print('  ${(n + 1).toString().padLeft(3)}  ${h6(inApp[n])}'
        '  x${calls[inApp[n]]}');
  }
  if (inRom.isNotEmpty) {
    print('');
    print('boot ROM routines called: ${inRom.map(h6).join(", ")}');
  }
}
