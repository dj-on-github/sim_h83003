// ignore_for_file: avoid_print — these are command-line tools; stdout is the UI.
// Static analyser for H8/3003 firmware images, built on the simulator's
// disassembler.
//
// Given a raw dump of the 24-bit address space (or a ROM image), it:
//
//   * seeds from the hardware exception vector table at H'000000 *and* from
//     an application handler table (on the Bernina artista 180 the boot ROM
//     trampolines fetch the real handler from a mirror table at H'200000),
//   * follows calls and branches recursively so it decodes real code rather
//     than linear-sweeping data as if it were instructions,
//   * records statically-known memory references, including addresses built
//     in a register by MOV.L #imm32,ERn and then used through @ERn — marking
//     those that crossed a call or branch as lower confidence, because that
//     inference can be wrong,
//   * classifies on-chip register references by peripheral, and
//   * emits a symbol table the simulator can load, so the disassembly view
//     shows SCR0/P4DR/isr_RXI0 instead of bare addresses.
//
// Usage:
//   dart run tool/analyze_dump.dart <image.bin> [options]
//     --base N              image starts at this address (default 0)
//     --apptable HEX        application handler table (e.g. 200000)
//     --report r            regs | external | funcs | handlers | all
//     --dis HEX [count]     disassembly listing
//     --symbols FILE.json   write a symbol table for the simulator
//     --sym FILE.sym        write a plain-text symbol table (NAME = H'ADDR)
//     --annotate FILE.txt   write an annotated disassembly of known code

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:sim_h83003/h8disasm.dart';

import 'h8_regmap.dart';

String hex6(int v) => v.toRadixString(16).toUpperCase().padLeft(6, '0');
String hex2(int v) => v.toRadixString(16).toUpperCase().padLeft(2, '0');
String hex8(int v) => v.toRadixString(16).toUpperCase().padLeft(8, '0');

/// Names identified by hand during analysis, keyed by the image's reset
/// vector. The artista 180 dumps are different firmware versions whose code
/// does not sit at the same addresses, so a name is only applied to the
/// image it was verified in.
Map<int, String> curatedFor(int resetVector) {
  switch (resetVector) {
    case 0x000448:
      return curatedV448;
    case 0x000400:
      return curatedV400;
    default:
      return const {};
  }
}

/// Verified in the dump whose reset vector is H'000400
/// (MemoryDump-SewingMachine-2026-08-16_16-14-24.bin). These are points
/// confirmed while reading the code, not necessarily function entries.
const Map<int, String> curatedV400 = {
  0x000400: 'boot_reset',
  0x20071E: 'fp_normalise_loop',
  0x20083E: 'adc_start_conversion',
  0x20084A: 'adc_get_result',
  0x200860: 'adc_convert_polled',
  0x2008B2: 'adc_result_jumptable',
  0x200914: 'adc_start_channel',
  0x200934: 'adc_channel_jumptable',
  0x200954: 'adc_start_ch0_ch4',
  0x200960: 'adc_start_ch1_ch5',
  0x20096C: 'adc_start_ch2_ch6',
  0x200978: 'adc_start_ch3_ch7',
  0x2091FE: 'touch_sample_axes',
  0x209C44: 'adc_scan_step',
  0x20651C: 'fill_table_11A6E8',
};

/// RAM locations whose use was established by watching the code run.
Map<int, String> curatedDataFor(int resetVector) =>
    resetVector == 0x000400 ? dataV400 : const {};

/// Variables in the H'000400 image, found by instrumented execution rather
/// than by reading the code, so each has a short note in the emitted file.
const Map<int, String> dataV400 = {
  0x0203D4: 'sci0_rx_payload',
  0x020886: 'sci0_frame_len',
  0x02089E: 'sci0_rx_state',
  0x040000: 'lcd_frame_buffer',
  0x11A251: 'adc_results',
  0x11A6E8: 'table_11A6E8',
  0x11A814: 'touch_debounce',
  0x11A818: 'adc_current_channel',
  0x11A819: 'adc_next_channel',
  0x2155B4: 'sci0_command_table',
  0x216662: 'sci0_check_byte',
  0xFFFEF0: 'touch_x_raw',
  0xFFFEF1: 'touch_y_raw',
  0xFFFEF7: 'touch_axis_drive',
};

/// Notes emitted beside the symbols above.
const Map<int, String> symbolNotes = {
  0x0203D4: 'payload bytes of the frame being received',
  0x020886: 'expected length of the current frame',
  0x02089E: 'receive state machine',
  0x040000: 'LCD frame buffer, 320x240 at 2bpp, MSB first (H\'4B00 bytes)',
  0x11A251: 'latest A/D conversion results',
  0x11A6E8: 'table filled by fill_table_11A6E8',
  0x11A814: 'touch debounce counter, reloaded with H\'C8',
  0x11A818: 'channel being converted',
  0x11A819: 'channel to convert next',
  0x2155B4: '15-entry SCI0 command dispatch table',
  0x216662: 'frame check byte',
  0xFFFEF0: 'X axis sample, from AN4; pressed when >= H\'4C',
  0xFFFEF1: 'Y axis sample, from AN6',
  0xFFFEF7: 'which axis the panel is currently driving',
  0x20071E: 'the loop the machine spends most of its time in',
  0x2091FE: 'drives each axis in turn and samples it',
};

/// Verified in the dump whose reset vector is H'000448
/// (Bernina180-FullMemoryDump.bin).
const Map<int, String> curatedV448 = {
  0x000150: 'isr_dispatch_nmi',
  0x00015C: 'isr_dispatch_common',
  0x00046E: 'delay_loop',
  0x000448: 'boot_reset',
  0x000494: 'sci_getchar',
  0x0004BA: 'sci_putchar',
  0x0004E2: 'sci_status',
  0x000744: 'sci1_init',
  0x000760: 'sci0_init',
  0x000786: 'dma_block_transfer',
  0x0029EC: 'serial_protocol_main',
  0x20D8B0: 'ram_test_DF0000',
  0x216A74: 'gpio_pin_selftest',
};

/// One recorded memory access.
class Ref {
  Ref(this.pc, this.addr, this.text, this.how, this.certain);
  final int pc;
  final int addr;
  final String text;
  final String how; // abs8 | abs16 | abs24 | reg
  final bool certain; // false for register-derived addresses that crossed
  // a branch or call, where the inference may be wrong
}

/// A tracked register value: the constant, and whether control flow has been
/// crossed since it was loaded.
class _RegVal {
  _RegVal(this.value);
  final int value;
  bool crossed = false;
}

class Analyzer {
  Analyzer(this.image, this.base);

  final Uint8List image;
  final int base;

  final Set<int> code = {};
  final Set<int> funcs = {};
  final Map<int, Set<int>> callers = {};
  final List<Ref> refs = [];

  /// Addresses that are the target of a branch — a constant tracked across
  /// one of these is no longer reliable.
  final Set<int> branchTargets = {};

  int peek(int a) {
    final i = a - base;
    return (i >= 0 && i < image.length) ? image[i] : 0;
  }

  bool inImage(int a) => a - base >= 0 && a - base < image.length;

  int word(int a) => (peek(a) << 8) | peek(a + 1);
  int long(int a) => (word(a) << 16) | word(a + 2);

  final List<_RegVal?> _regs = List<_RegVal?>.filled(8, null);

  void _clearRegs() {
    for (var i = 0; i < 8; i++) {
      _regs[i] = null;
    }
  }

  void _markCrossed() {
    for (final r in _regs) {
      r?.crossed = true;
    }
  }

  /// Recursive-descent walk from [entry].
  void walk(int entry) {
    final work = <int>[entry & 0xFFFFFF];
    while (work.isNotEmpty) {
      var pc = work.removeLast();
      _clearRegs(); // a fresh path: nothing known about the registers
      while (true) {
        pc &= 0xFFFFFF;
        if (!inImage(pc) || code.contains(pc)) break;
        code.add(pc);
        final d = disassembleH8(peek, pc);
        _record(pc, d);

        final b0 = peek(pc);
        final b1 = peek(pc + 1);
        final next = pc + d.length;
        final t = d.text;

        // ---- calls ----
        int? call;
        if (b0 == 0x55) {
          call = pc + 2 + (b1 < 0x80 ? b1 : b1 - 0x100);
        } else if (b0 == 0x5C && b1 == 0x00) {
          final w = word(pc + 2);
          call = pc + 4 + (w < 0x8000 ? w : w - 0x10000);
        } else if (b0 == 0x5E) {
          call = ((b1 << 16) | word(pc + 2)) & 0xFFFFFF;
        } else if (b0 == 0x5F) {
          call = long(b1) & 0xFFFFFF; // JSR @@aa:8 (memory indirect)
        }
        if (call != null && inImage(call)) {
          funcs.add(call & 0xFFFFFF);
          callers.putIfAbsent(call & 0xFFFFFF, () => {}).add(pc);
          work.add(call & 0xFFFFFF);
          _markCrossed(); // the callee may clobber anything
        }

        // ---- branches / terminators ----
        if (b0 >= 0x40 && b0 <= 0x4F) {
          final target = (pc + 2 + (b1 < 0x80 ? b1 : b1 - 0x100)) & 0xFFFFFF;
          branchTargets.add(target);
          work.add(target);
          _markCrossed();
          if (b0 == 0x40) break; // BRA
        } else if (b0 == 0x58) {
          final w = word(pc + 2);
          final target = (pc + 4 + (w < 0x8000 ? w : w - 0x10000)) & 0xFFFFFF;
          branchTargets.add(target);
          work.add(target);
          _markCrossed();
          if ((b1 >> 4) == 0) break;
        } else if (b0 == 0x5A) {
          final target = ((b1 << 16) | word(pc + 2)) & 0xFFFFFF;
          branchTargets.add(target);
          work.add(target);
          break;
        } else if (b0 == 0x5B) {
          final target = long(b1) & 0xFFFFFF; // JMP @@aa:8
          if (inImage(target)) {
            branchTargets.add(target);
            work.add(target);
          }
          break;
        } else if (b0 == 0x59) {
          break; // JMP @ERn — target not statically known
        } else if (t == 'RTS' || t == 'RTE' || t.startsWith('.WORD')) {
          break;
        }
        pc = next;
      }
    }
  }

  void _record(int pc, H8Disasm d) {
    final b0 = peek(pc);
    final b1 = peek(pc + 1);
    final t = d.text;

    // MOV.L #imm:32,ERd
    if (b0 == 0x7A && (b1 >> 4) == 0 && (b1 & 8) == 0) {
      _regs[b1 & 7] = _RegVal(long(pc + 2));
      return;
    }

    void add(int addr, String how, {bool certain = true}) =>
        refs.add(Ref(pc, addr & 0xFFFFFF, t, how, certain));

    // MOV.B @aa:8 and bit operations on @aa:8 — the H'FFFFxx page.
    if ((b0 >= 0x20 && b0 <= 0x3F) || b0 == 0x7E || b0 == 0x7F) {
      add(0xFFFF00 | b1, 'abs8');
      return;
    }
    // MOV.B/W @aa:16 or @aa:24.
    if (b0 == 0x6A || b0 == 0x6B) {
      final mode = (b1 >> 4) & 0x7;
      if (mode == 0) {
        final a = word(pc + 2);
        add(a < 0x8000 ? a : 0xFF0000 + a, 'abs16');
      } else if (mode == 2) {
        add(((word(pc + 2) & 0xFF) << 16) | word(pc + 4), 'abs24');
      }
      return;
    }
    // MOV.L @aa:16/24.
    if (b0 == 0x01 && b1 == 0x00 && peek(pc + 2) == 0x6B) {
      final sub = peek(pc + 3);
      final mode = (sub >> 4) & 0x7;
      if (mode == 0) {
        final a = word(pc + 4);
        add(a < 0x8000 ? a : 0xFF0000 + a, 'abs16');
      } else if (mode == 2) {
        add(((word(pc + 4) & 0xFF) << 16) | word(pc + 6), 'abs24');
      }
      return;
    }
    // Register-indirect where the register holds a known constant.
    if (b0 == 0x68 || b0 == 0x69 || b0 == 0x6E || b0 == 0x6F) {
      final ern = (b1 >> 4) & 7;
      final rv = _regs[ern];
      if (rv != null) {
        var a = rv.value;
        if (b0 == 0x6E || b0 == 0x6F) {
          final disp = word(pc + 2);
          a += (disp < 0x8000 ? disp : disp - 0x10000);
        }
        add(a, 'reg', certain: !rv.crossed);
      }
      if ((b1 & 0x80) == 0) _regs[b1 & 0x7] = null; // load clobbers dest
      return;
    }
    if (b0 == 0x6C || b0 == 0x6D) {
      _regs[(b1 >> 4) & 7] = null; // auto-increment/decrement
      return;
    }
  }
}

/// Recovers jump tables that the recursive walk cannot follow.
///
/// The application dispatches through `JMP @ERn` / `JSR @ERn`. The register is
/// almost always loaded from a table whose base appears as a 32-bit immediate
/// a few instructions earlier — the classic switch idiom:
///
///     MOV.W  index,R6
///     SHLL.L ER6            ; scale to longwords
///     ADD.L  #H'00211D38,ER6  ; <- table base
///     MOV.L  @ER6,ER6
///     JMP    @ER6
///
/// In this firmware the base is more often the *displacement* of the load
/// that fetches the pointer, with the index scaled into the register:
///
///     EXTU.L ER6
///     ADD.L  ER6,ER6                    ; ×2
///     ADD.L  ER6,ER6                    ; ×4
///     MOV.L  @(H'00180C:24,ER6),ER6     ; <- table base is the displacement
///     JMP    @ER6
///
/// So: for every indirect jump in known code, look back a short window for a
/// 32-bit immediate *or* an indexed long load, take the immediate or the
/// displacement as a candidate base, and harvest the run of valid code
/// pointers there.
class TableFinder {
  TableFinder(this.an, this.ranges);

  final Analyzer an;

  /// Address ranges that may contain code, as (start, end) pairs. Boot ROM
  /// and application flash are separate islands.
  final List<(int, int)> ranges;

  bool _inCode(int a) {
    for (final (lo, hi) in ranges) {
      if (a >= lo && a < hi) return true;
    }
    return false;
  }

  /// How far back to look for the table base, in instructions.
  static const int lookback = 20;

  /// A target is plausible if it is in range, even, and decodes to a real
  /// instruction rather than a `.WORD` placeholder.
  bool plausible(int a) {
    if (!_inCode(a) || (a & 1) != 0) return false;
    final d = disassembleH8(an.peek, a);
    if (d.text.startsWith('.WORD')) return false;
    final b0 = an.peek(a);
    return b0 != 0x00 && b0 != 0xFF;
  }

  /// Reads the run of pointers starting at [base]; empty if it is not a table.
  List<int> harvest(int base, {int max = 512}) {
    if (base < 0 || !an.inImage(base) || (base & 1) != 0) return const [];
    final out = <int>[];
    for (var i = 0; i < max; i++) {
      final v = an.long(base + i * 4) & 0xFFFFFF;
      if (!plausible(v)) break;
      out.add(v);
    }
    return out.length >= 3 ? out : const [];
  }

  /// Table base addresses this instruction could be supplying: a 32-bit
  /// immediate, or the displacement of an indexed longword load.
  List<int> _candidateBases(int a) {
    final out = <int>[];
    // MOV.L #imm32,ERn (7A 0n) or ADD.L #imm32,ERn (7A 1n)
    if (an.peek(a) == 0x7A) {
      final sub = an.peek(a + 1) >> 4;
      if (sub == 0 || sub == 1) out.add(an.long(a + 2) & 0xFFFFFF);
    }
    if (an.peek(a) == 0x01 && an.peek(a + 1) == 0x00) {
      final b2 = an.peek(a + 2);
      // MOV.L @(d:24,ERn),ERm  ->  01 00 78 n0 6B 2m dd dd dd dd
      if (b2 == 0x78 && an.peek(a + 4) == 0x6B) {
        out.add(an.long(a + 6) & 0xFFFFFF);
      }
      // MOV.L @(d:16,ERn),ERm  ->  01 00 6F nm dd dd
      if (b2 == 0x6F) {
        final d = an.word(a + 4);
        out.add((d < 0x8000 ? d : 0xFF0000 + d) & 0xFFFFFF);
      }
      // MOV.L @aa:24,ERn — a single pointer, but harvest() will reject it
      // unless a run of pointers actually follows.
      if (b2 == 0x6B && (an.peek(a + 3) & 0xF0) == 0x20) {
        out.add(an.long(a + 4) & 0xFFFFFF);
      }
    }
    return out;
  }

  /// All table targets reachable from the indirect jumps in the current code
  /// set, plus the tables themselves for reporting.
  (Set<int> targets, Map<int, int> tables) scan() {
    final sorted = an.code.toList()..sort();
    final index = <int, int>{};
    for (var i = 0; i < sorted.length; i++) {
      index[sorted[i]] = i;
    }
    final targets = <int>{};
    final tables = <int, int>{};

    for (final pc in sorted) {
      final b0 = an.peek(pc);
      final b1 = an.peek(pc + 1);
      // JMP @ERn (59 n0) or JSR @ERn (5D n0)
      if ((b0 != 0x59 && b0 != 0x5D) || (b1 & 0x8F) != 0) continue;
      final i = index[pc]!;
      for (var k = 1; k <= lookback && i - k >= 0; k++) {
        final a = sorted[i - k];
        for (final base in _candidateBases(a)) {
          final entries = harvest(base);
          if (entries.isNotEmpty) {
            tables[base] = entries.length;
            targets.addAll(entries);
          }
        }
      }
    }
    return (targets, tables);
  }
}

/// Reads a 64-entry vector/handler table, skipping unpopulated slots.
Map<int, int> readTable(Analyzer an, int tableBase) {
  final out = <int, int>{};
  for (var v = 0; v < 64; v++) {
    final a = an.long(tableBase + v * 4) & 0xFFFFFF;
    if (a == 0 || a == 0xFFFFFF || !an.inImage(a)) continue;
    out[v] = a;
  }
  return out;
}

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/analyze_dump.dart <image.bin> '
        '[--base N] [--apptable HEX] [--report R] [--dis HEX N] '
        '[--symbols F] [--annotate F]');
    exit(2);
  }
  String? opt(String name) {
    final i = args.indexOf(name);
    return (i >= 0 && i + 1 < args.length) ? args[i + 1] : null;
  }

  final path = args.first;
  final base = int.parse(opt('--base') ?? '0');
  final report = opt('--report') ?? 'all';
  final image = File(path).readAsBytesSync();
  final an = Analyzer(image, base);

  print("image: $path  (${image.length} bytes, base H'${hex6(base)})");

  // --dis: plain listing, no analysis needed.
  final disArg = opt('--dis');
  if (disArg != null) {
    var pc = int.parse(disArg, radix: 16);
    final di = args.indexOf('--dis');
    final n = (di + 2 < args.length) ? int.tryParse(args[di + 2]) ?? 24 : 24;
    for (var i = 0; i < n; i++) {
      final d = disassembleH8(an.peek, pc);
      final bytes = [
        for (var k = 0; k < d.length; k++) hex2(an.peek(pc + k))
      ].join(' ');
      print("  H'${hex6(pc)}  ${bytes.padRight(26)} ${d.text}");
      pc += d.length;
    }
    return;
  }

  // ---- entry points -----------------------------------------------------
  final hwVectors = readTable(an, 0);
  final appTableArg = opt('--apptable');
  final appTable = appTableArg == null
      ? <int, int>{}
      : readTable(an, int.parse(appTableArg, radix: 16));

  print('hardware vectors: ${hwVectors.length} populated');
  if (appTableArg != null) {
    print("application handler table at H'$appTableArg: "
        '${appTable.length} populated');
  }
  print('');

  if (report == 'handlers' || report == 'all') {
    if (appTable.isNotEmpty) {
      print('=== application interrupt handlers ===');
      final vs = appTable.keys.toList()..sort();
      for (final v in vs) {
        final name = h8Vectors[v] ?? 'vector $v';
        print("  ${v.toString().padLeft(2)}  ${name.padRight(16)} "
            "H'${hex6(appTable[v]!)}");
      }
      print('');
    }
  }

  for (final a in hwVectors.values) {
    an.walk(a);
  }
  for (final a in appTable.values) {
    an.walk(a);
  }
  // --seeds: extra entry points, e.g. targets harvested from the jump tables
  // the recursive walk cannot follow through `JMP @ERn`. JSON array of hex
  // strings.
  final seedFile = opt('--seeds');
  if (seedFile != null) {
    final list = jsonDecode(File(seedFile).readAsStringSync()) as List;
    var n = 0;
    for (final s in list) {
      final a = int.tryParse(s.toString(), radix: 16);
      if (a != null && an.inImage(a)) {
        an.funcs.add(a);
        an.walk(a);
        n++;
      }
    }
    print('seeded $n extra entry points from $seedFile');
  }
  // Walking discovered functions again picks up code reached only through
  // paths the first pass cut short.
  for (final f in an.funcs.toList()) {
    an.walk(f);
  }

  // Jump-table recovery, iterated to a fixpoint: each round of newly reached
  // code can contain further indirect jumps, and therefore further tables.
  // Code islands: the boot ROM and the application flash. Jump tables in one
  // point only into that same island.
  final ranges = <(int, int)>[
    for (final r in (opt('--code-range') ?? '000100-030000,200000-230000')
        .split(','))
      (
        int.parse(r.split('-')[0], radix: 16),
        int.parse(r.split('-')[1], radix: 16)
      )
  ];
  final finder = TableFinder(an, ranges);
  final allTables = <int, int>{};
  for (var round = 1; round <= 12; round++) {
    final before = an.code.length;
    final (targets, tables) = finder.scan();
    allTables.addAll(tables);
    var fresh = 0;
    for (final t in targets) {
      if (an.code.contains(t)) continue;
      an.funcs.add(t);
      an.walk(t);
      fresh++;
    }
    if (an.code.length == before) break;
    print('  table round $round: ${tables.length} tables, '
        '$fresh new targets, code now ${an.code.length} instructions');
  }
  if (allTables.isNotEmpty) {
    final total = allTables.values.fold<int>(0, (a, b) => a + b);
    print('jump tables recovered: ${allTables.length} tables, '
        '$total entries');
  }

  print('reachable code: ${an.code.length} instructions, '
      '${an.funcs.length} functions\n');

  // ---- symbol names ------------------------------------------------------
  final symbols = <int, String>{};
  // On-chip peripheral registers.
  h8Registers.forEach((lo, name) => symbols[0xFFFF00 | lo] = name);
  // Boot-ROM trampolines and application handlers.
  hwVectors.forEach((v, a) {
    final n = h8Vectors[v];
    symbols[a] = n == null ? 'tramp_vec$v' : 'tramp_$n';
  });
  appTable.forEach((v, a) {
    final n = h8Vectors[v];
    symbols[a] = n == null ? 'isr_vec$v' : 'isr_$n';
  });
  // Hand-identified routines win over the generated names.
  final resetVector = an.long(0) & 0xFFFFFF;
  final curated = curatedFor(resetVector);
  curated.forEach((a, n) {
    if (an.inImage(a)) symbols[a] = n;
  });
  final curatedData = curatedDataFor(resetVector);
  curatedData.forEach((a, n) => symbols[a] = n);
  // Everything else discovered as a call target.
  for (final f in an.funcs) {
    symbols.putIfAbsent(f, () => 'sub_${hex6(f)}');
  }

  final symFile = opt('--symbols');
  if (symFile != null) {
    // The simulator accepts { "label": "0xADDR" }.
    final map = <String, String>{};
    final used = <String>{};
    final entries = symbols.entries.toList()
      ..sort((a, b) => a.key.compareTo(b.key));
    for (final e in entries) {
      var name = e.value;
      if (!used.add(name)) {
        name = '${e.value}_${hex6(e.key)}'; // keep names unique
        used.add(name);
      }
      map[name] = '0x${hex6(e.key)}';
    }
    File(symFile).writeAsStringSync(
        const JsonEncoder.withIndent('  ').convert(map));
    print('wrote ${map.length} symbols to $symFile\n');
  }

  // ---- plain-text symbol file -------------------------------------------
  // `NAME = H'ADDRESS`, one per line, with `;` comments — readable on its own
  // and accepted by the simulator's symbol loader.
  final symTextFile = opt('--sym');
  if (symTextFile != null) {
    final used = <String>{};
    String unique(String name, int addr) {
      if (used.add(name)) return name;
      final alt = '${name}_${hex6(addr)}';
      used.add(alt);
      return alt;
    }

    final sb = StringBuffer();
    void section(String title, [String? note]) {
      sb.writeln();
      sb.writeln('; ${'=' * 68}');
      sb.writeln('; $title');
      if (note != null) {
        for (final line in note.split('\n')) {
          sb.writeln('; $line');
        }
      }
      sb.writeln('; ${'=' * 68}');
    }

    void emit(int addr, String name, [String? note]) {
      final line = "${unique(name, addr).padRight(24)} = H'${hex6(addr)}";
      sb.writeln(note == null ? line : '${line.padRight(40)}; $note');
    }

    sb.writeln('; Symbol table for ${path.split(Platform.pathSeparator).last}');
    sb.writeln('; Generated by tool/analyze_dump.dart.');
    sb.writeln(';');
    sb.writeln('; The on-chip register and vector names come from the H8/3003');
    sb.writeln('; hardware manual and apply to any image. Everything below');
    sb.writeln("; them was found in this image, whose reset vector is");
    sb.writeln("; H'${hex6(resetVector)} — the artista 180 firmware versions do not");
    sb.writeln('; share addresses, so those names do not transfer to another');
    sb.writeln('; dump.');

    section('On-chip registers (H8/3003 manual appendix B.1)');
    final regLows = h8Registers.keys.toList()..sort();
    String? lastModule;
    for (final lo in regLows) {
      final module = h8Module(lo);
      if (module != lastModule) {
        sb.writeln('; -- $module');
        lastModule = module;
      }
      emit(0xFFFF00 | lo, h8Registers[lo]!);
    }

    section('Exception vector table',
        'Each slot holds a 32-bit handler address. VEC_* names the slot;\n'
        'tramp_* and isr_* name the code it reaches.');
    for (var v = 0; v < 64; v++) {
      final name = h8Vectors[v];
      emit(v * 4, 'VEC_${name ?? 'vec$v'}',
          name == null ? 'vector $v (reserved)' : null);
    }

    // An address gets one name: where a hand-identified routine is also a
    // vector handler, the hand-identified name is the one emitted.
    final done = <int>{};
    section('Vector handlers in this image');
    final hv = hwVectors.keys.toList()..sort();
    for (final v in hv) {
      final a = hwVectors[v]!;
      final n = h8Vectors[v];
      emit(a, curated[a] ?? (n == null ? 'tramp_vec$v' : 'tramp_$n'),
          'from vector $v${n == null ? '' : ' ($n)'}');
      done.add(a);
    }
    if (appTable.isNotEmpty) {
      sb.writeln('; -- application handler table at '
          "H'${hex6(int.parse(appTableArg!, radix: 16))}");
      final av = appTable.keys.toList()..sort();
      for (final v in av) {
        final a = appTable[v]!;
        final n = h8Vectors[v];
        emit(a, curated[a] ?? (n == null ? 'isr_vec$v' : 'isr_$n'),
            'application handler for vector $v');
        done.add(a);
      }
    }

    final code = curated.keys.where((a) => !done.contains(a)).toList()..sort();
    if (code.isNotEmpty) {
      section('Identified code');
      for (final a in code) {
        emit(a, curated[a]!, symbolNotes[a]);
        done.add(a);
      }
    }

    final data = curatedData.keys.where((a) => !done.contains(a)).toList()
      ..sort();
    if (data.isNotEmpty) {
      section('Identified data');
      for (final a in data) {
        emit(a, curatedData[a]!, symbolNotes[a]);
        done.add(a);
      }
    }

    final subs = an.funcs.where((f) => !done.contains(f)).toList()..sort();
    if (subs.isNotEmpty) {
      section('Call targets found by the static walk',
          '${subs.length} subroutines reached from the vectors, the handler'
              '\ntable and the recovered jump tables. These are addresses '
              'that\nwere called — not routines anyone has read.');
      for (final f in subs) {
        emit(f, 'sub_${hex6(f)}');
      }
    }

    File(symTextFile).writeAsStringSync(sb.toString());
    print('wrote ${used.length} symbols to $symTextFile\n');
  }

  // ---- annotated disassembly --------------------------------------------
  final annFile = opt('--annotate');
  if (annFile != null) {
    final sb = StringBuffer();
    sb.writeln('; Annotated disassembly of $path');
    sb.writeln('; ${an.code.length} instructions reached from '
        '${hwVectors.length} hardware vectors and '
        '${appTable.length} application handlers.');
    sb.writeln();
    String sub(String text) {
      return text.replaceAllMapped(
        RegExp(r"(?<!#)H'([0-9A-Fa-f]{6}|[0-9A-Fa-f]{4})(?![0-9A-Fa-f])"),
        (m) {
          final a = int.parse(m.group(1)!, radix: 16);
          final full = m.group(1)!.length == 4 && a >= 0x8000
              ? 0xFF0000 + a
              : a;
          return symbols[full] ?? m.group(0)!;
        },
      );
    }

    final sorted = an.code.toList()..sort();
    int? prev;
    for (final pc in sorted) {
      if (prev != null && pc != prev) sb.writeln('; ----');
      final label = symbols[pc];
      if (label != null) sb.writeln('$label:');
      final d = disassembleH8(an.peek, pc);
      final bytes = [
        for (var k = 0; k < d.length; k++) hex2(an.peek(pc + k))
      ].join(' ');
      sb.writeln("  ${hex6(pc)}  ${bytes.padRight(26)} ${sub(d.text)}");
      prev = pc + d.length;
    }
    File(annFile).writeAsStringSync(sb.toString());
    print('wrote annotated disassembly to $annFile\n');
  }

  // ---- on-chip register usage -------------------------------------------
  if (report == 'all' || report == 'regs') {
    final byModule = <String, Map<int, List<Ref>>>{};
    for (final r in an.refs) {
      if ((r.addr & 0xFFFF00) != 0xFFFF00) continue;
      final lo = r.addr & 0xFF;
      byModule
          .putIfAbsent(h8Module(lo), () => {})
          .putIfAbsent(lo, () => [])
          .add(r);
    }
    print('=== on-chip peripheral registers touched ===');
    final mods = byModule.keys.toList()..sort();
    for (final m in mods) {
      final regs = byModule[m]!;
      final total = regs.values.fold<int>(0, (a, b) => a + b.length);
      print('\n$m  —  $total accesses');
      final los = regs.keys.toList()..sort();
      for (final lo in los) {
        final list = regs[lo]!;
        final sites = list.map((r) => r.pc).toSet().toList()..sort();
        print("  H'FFFF${hex2(lo)} ${(h8Registers[lo] ?? '?').padRight(9)}"
            "${list.length.toString().padLeft(4)} accesses, "
            "${sites.length} site(s): "
            "${sites.take(5).map((p) => hex6(p)).join(' ')}"
            "${sites.length > 5 ? ' …' : ''}");
      }
    }
    print('');
  }

  // ---- off-chip accesses -------------------------------------------------
  if (report == 'all' || report == 'external') {
    final ext = <int, List<Ref>>{};
    for (final r in an.refs) {
      final a = r.addr;
      if (a >= 0xFFFD10) continue; // on-chip RAM + registers
      ext.putIfAbsent(a, () => []).add(r);
    }
    final addrs = ext.keys.toList()..sort();
    print('=== off-chip accesses, grouped into windows ===');
    print('   (certain = address is in the instruction encoding)');
    var i = 0;
    while (i < addrs.length) {
      var j = i;
      while (j + 1 < addrs.length && addrs[j + 1] - addrs[j] <= 0x100) {
        j++;
      }
      final group = addrs.sublist(i, j + 1);
      final all = group.expand((k) => ext[k]!).toList();
      final certain = all.where((r) => r.certain).length;
      if (all.length >= 4) {
        print("  H'${hex6(group.first)}-H'${hex6(group.last)}  "
            "${all.length.toString().padLeft(5)} accesses "
            "($certain certain), "
            "${all.map((r) => r.pc).toSet().length} site(s)");
      }
      i = j + 1;
    }
    print('');
  }

  // ---- which functions touch what ---------------------------------------
  if (report == 'all' || report == 'funcs') {
    void functionsTouching(String label, bool Function(int a) match) {
      final sites = an.refs.where((r) => match(r.addr)).map((r) => r.pc).toSet();
      final fl = an.funcs.toList()..sort();
      final hits = <int, int>{};
      for (final s in sites) {
        var best = -1;
        for (final f in fl) {
          if (f <= s) {
            best = f;
          } else {
            break;
          }
        }
        if (best >= 0) hits[best] = (hits[best] ?? 0) + 1;
      }
      final ordered = hits.entries.toList()
        ..sort((a, b) => b.value.compareTo(a.value));
      print('=== functions touching $label ===');
      for (final e in ordered.take(14)) {
        final name = symbols[e.key] ?? 'sub_${hex6(e.key)}';
        print("  ${hex6(e.key)}  ${name.padRight(22)} "
            "${e.value.toString().padLeft(3)} sites, "
            "called from ${an.callers[e.key]?.length ?? 0}");
      }
      print('');
    }

    bool onChip(int a, int lo, int hi) =>
        (a & 0xFFFF00) == 0xFFFF00 && (a & 0xFF) >= lo && (a & 0xFF) <= hi;
    functionsTouching('SCI (serial)', (a) => onChip(a, 0xB0, 0xBF));
    functionsTouching('I/O ports (GPIO)', (a) => onChip(a, 0xC0, 0xDF));
    functionsTouching('ITU (timers)', (a) => onChip(a, 0x60, 0x9F));
    functionsTouching('DMAC', (a) => onChip(a, 0x20, 0x5F));
  }
}
