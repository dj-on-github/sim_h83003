import 'dart:typed_data';

// A JEDEC-style flash device on the external bus.
//
// The boot ROM talks to its program memory the way every AMD/Atmel-style
// part of the period expects: an unlock pair — H'AA to offset H'5555 and
// H'55 to H'2AAA — followed by a command byte back at H'5555. Plain memory
// ignores all of that, which is why an unmodelled bus answers the identify
// with "nothing here" and every programming command is refused before it
// starts. This class supplies the missing half of the conversation.
//
// The array itself stays in the CPU's memory: an image loaded there is the
// device's contents, and reads that are not part of a command sequence come
// straight back out of it. Only the parts that are not plain reads and
// writes are intercepted.
//
// What is modelled:
//   * autoselect (H'90) — the first two bytes of the bank read back as the
//     manufacturer and device IDs until a reset (H'F0) ends it
//   * page programming (H'A0) — writes are buffered and committed a page at
//     a time, which is how the AT29C-style parts work and why the boot ROM
//     reads a whole page, patches it and writes it back
//   * sector erase (H'80 ... H'30) and chip erase (H'80 ... H'10)
//   * the busy indications the ROM polls for: DQ6 toggling on every read and
//     DQ7 reading back inverted
//
// What is not: programming times in real units, write-cycle endurance, and
// sector geometry other than uniform. [busyReads] stands in for the internal
// timing — it is a count of reads rather than a duration, so tests are
// deterministic.

/// One flash device's place in the address map.
class FlashRegion {
  const FlashRegion(this.name, this.base, this.size, {int? decodes})
      : decodes = decodes ?? size;

  final String name;
  final int base;

  /// How big the device is: how much storage it actually has.
  final int size;

  /// How much address space it answers for.
  ///
  /// Larger than [size] when the board leaves address lines unconnected, and
  /// the device then repeats through the window. The boot flash is 32K
  /// decoded across 128K, so the same storage appears four times.
  final int decodes;

  /// The end of the storage. [decodedEnd] is the end of the window.
  int get end => base + size;
  int get decodedEnd => base + decodes;

  /// Where [addr] really lands in the device.
  int fold(int addr) => base + (addr - base) % size;

  bool get mirrored => decodes > size;
}

/// The artista 180 carries two flash devices, one per external bus area.
///
/// The regions are read off a full memory dump rather than assumed:
///
///  * Area 0 holds a 32K device at H'000000. Its contents repeat every 32K
///    up to H'020000, so only the low fifteen address lines reach it -- the
///    same fifteen the H'5555 and H'2AAA unlock offsets need. Above it in
///    the same area sit the LCD registers at H'020000, the frame buffer at
///    H'040000 and the input latch at H'080000, none of which are flash;
///    taking the device to be larger swallows them and the machine stops
///    drawing.
///
///  * Area 1 holds a 2M device at H'200000 with the application in it. It
///    does not repeat within the area at either 512K or 1M, and nothing
///    writes to it while the machine runs normally.
///
///  * Area 2 holds a 1M device at H'500000: the stitch and icon data the
///    application draws from, and the settings block in its last page at
///    H'57FF80 upwards, which the machine rewrites as it runs. H'400000 to
///    H'4FFFFF reads as zeroes rather than as erased flash, so nothing is
///    decoded in the bottom half of the area and the device sits in the top.
///
///    Leaving this one out is not a quiet loss: the application comes up
///    without its icons, because that is where they are. Thirty thousand of
///    the screen's seventy-six thousand pixels change.
///
/// A flash image file holds the two back to back in this order.
const List<FlashRegion> artista180Flash = [
  // 32K of storage, decoded across 128K: it answers at H'000000, H'008000,
  // H'010000 and H'018000, and all four are the same bytes.
  FlashRegion('boot', 0x000000, 0x008000, decodes: 0x020000),
  FlashRegion('application', 0x200000, 0x200000),
  FlashRegion('data', 0x500000, 0x100000),
];

/// Size of a flash image file holding [regions] back to back.
int flashImageSize([List<FlashRegion> regions = artista180Flash]) =>
    regions.fold(0, (total, r) => total + r.size);

/// True when a file of [length] bytes reaches the top of the highest region,
/// which means it is a picture of the whole address space -- a full memory
/// dump -- rather than the regions concatenated. Both are accepted, because
/// a dump of a working machine is the obvious thing to have to hand.
bool flashImageIsAddressed(int length,
    [List<FlashRegion> regions = artista180Flash]) {
  var top = 0;
  for (final r in regions) {
    if (r.end > top) top = r.end;
  }
  return length >= top;
}

/// Where [region]'s bytes start inside a flash image file.
int flashImageOffset(FlashRegion region, bool addressed,
    [List<FlashRegion> regions = artista180Flash]) {
  if (addressed) return region.base;
  var offset = 0;
  for (final r in regions) {
    if (r.base == region.base && r.size == region.size) return offset;
    offset += r.size;
  }
  return offset;
}

/// Gathers the current contents of [regions] into a flash image, in the
/// plain back-to-back form. [peek] reads the array directly rather than
/// through any device, so this is what is really stored and not what a
/// device part-way through an autoselect would answer with.
Uint8List buildFlashImage(int Function(int) peek,
    [List<FlashRegion> regions = artista180Flash]) {
  final image = Uint8List(flashImageSize(regions));
  for (final region in regions) {
    final offset = flashImageOffset(region, false, regions);
    for (var i = 0; i < region.size; i++) {
      image[offset + i] = peek(region.base + i);
    }
  }
  return image;
}

/// Writes [image] into memory at each region's base. Bytes past the end of
/// a short file are written erased; the count of those is returned so the
/// caller can say how much of the image was missing.
int applyFlashImage(List<int> image, void Function(int, int) poke,
    {List<FlashRegion> regions = artista180Flash, bool? addressed}) {
  final byAddress = addressed ?? flashImageIsAddressed(image.length, regions);
  var padded = 0;
  for (final region in regions) {
    final offset = flashImageOffset(region, byAddress, regions);
    for (var i = 0; i < region.size; i++) {
      final j = offset + i;
      if (j < image.length) {
        poke(region.base + i, image[j]);
      } else {
        poke(region.base + i, 0xFF);
        padded++;
      }
    }
  }
  return padded;
}

/// The command sequence recognised at the unlock offsets.
/// What has happened to one 64K bank of a device since the log was cleared.
///
/// A bank is the unit the boot ROM works in -- its unlock sequence is
/// addressed within `addr & H'FF0000` -- so grouping by it is what makes a
/// burn legible: a 206K application image walks four banks, and the v5 boot
/// update touches one.
class FlashBankWrites {
  FlashBankWrites(this.bank);

  /// Base address of the bank, `addr & H'FF0000`.
  final int bank;

  /// Page programs committed, and bytes programmed by the byte-at-a-time
  /// parts. A page write counts one page and its pageSize bytes.
  int pages = 0;
  int bytes = 0;

  /// Erases that landed in this bank.
  int erases = 0;

  /// The span actually touched, which is narrower than the bank whenever a
  /// burn stops part way -- and that is what shows the progress.
  int lowest = -1;
  int highest = -1;

  void touch(int from, int to) {
    if (lowest < 0 || from < lowest) lowest = from;
    if (to > highest) highest = to;
  }
}

enum _Phase { read, unlock1, unlock2, erasePrefix, eraseUnlock1, eraseUnlock2 }

class JedecFlash {
  JedecFlash({
    required this.base,
    required this.size,
    this.manufacturerId = 0x1F, // Atmel
    this.deviceId = 0xA4,
    this.pageSize = 0x100,
    this.sectorSize = 0x10000,
    this.pageWrite = true,
    this.busyReads = 0,
    int? decodes,
  }) : decodes = decodes ?? size;

  /// The device the artista 180 carries: 512K, Atmel, page-write.
  factory JedecFlash.atmelA4({required int base, int size = 0x80000}) =>
      JedecFlash(base: base, size: size);

  /// Builds the device a [FlashRegion] describes, mirroring and all.
  factory JedecFlash.forRegion(FlashRegion r) =>
      JedecFlash(base: r.base, size: r.size, decodes: r.decodes);

  final int base;
  final int size;

  /// How much address space it answers for. Bigger than [size] when the
  /// board leaves address lines unconnected: the boot flash is 32K wired
  /// across 128K, so the same storage answers at four addresses and a write
  /// through any of them is a write to all four.
  final int decodes;
  final int manufacturerId;
  final int deviceId;
  final int pageSize;
  final int sectorSize;

  /// True for the AT29C-style parts, which take a whole page at a time and
  /// replace everything in it. False for the byte-programmable AMD and
  /// Fujitsu parts, where each write stands on its own and can only clear
  /// bits — an erase is what puts them back.
  final bool pageWrite;

  /// How many reads report "busy" after an operation before it appears
  /// finished. Zero makes programming look instantaneous, which is what the
  /// firmware's own timeout-bounded polls treat as success on the first try.
  ///
  /// Keep it under about 36 for anything that programs through the boot
  /// ROM. The ROM's post-program poll is bounded at FLASH_BUSY_TRIES = 17
  /// iterations of two reads each, and it ignores the failure: past that it
  /// gives up, re-enables interrupts, and the next interrupt fetches its
  /// vector out of a device that is still answering with status instead of
  /// data. The machine then runs off into unmapped space, which looks like a
  /// protocol fault a long way from its cause.
  ///
  /// That is a property of this model, not of the machine: the application
  /// writes flash at runtime through the same poll, so the real part
  /// finishes well inside seventeen tries. Code that does its own waiting --
  /// v5bootrom/updater.c bounds at two million -- is not affected, which is
  /// why tool/boot_update.dart can use 400.
  int busyReads;

  /// Backing store, supplied by the CPU so the device and the debugger see
  /// the same array.
  late int Function(int addr) peek;
  late void Function(int addr, int value) poke;

  _Phase _phase = _Phase.read;
  bool _autoselect = false;

  /// Buffered page, keyed by offset within the page. Null when no program
  /// command is in flight.
  int? _pageBase;
  final Map<int, int> _pageBuffer = {};

  int _busyLeft = 0;
  bool _toggle = false;
  int _busyByte = 0xFF;

  /// Counters, so a test or the UI can see what the device was asked to do.
  int programmedPages = 0;
  int programmedBytes = 0;

  /// Stores the device ignored because no command sequence was in progress.
  /// On real flash these do nothing; a high count on a region means either
  /// software that expects RAM there, or a region that is not really flash.
  int ignoredWrites = 0;
  final Map<int, int> ignoredByBank = {};
  int erasedSectors = 0;

  /// Which banks have been written since [clearWriteLog], in the order they
  /// were first touched. Watching this fill in is how a burn's progress is
  /// visible from the machine's side rather than the host's.
  final Map<int, FlashBankWrites> written = {};

  FlashBankWrites _bankFor(int addr) =>
      written.putIfAbsent(addr & 0xFF0000, () => FlashBankWrites(addr & 0xFF0000));

  /// Forgets what has been written. The contents are untouched: this is the
  /// log, not the device.
  void clearWriteLog() => written.clear();
  int identifyCount = 0;

  bool owns(int addr) => addr >= base && addr < base + decodes;

  /// Where [addr] really lands. Only the address lines the device has are
  /// wired, so anything above them is dropped.
  int fold(int addr) => base + (addr - base) % size;

  void reset() {
    // The write log survives: it is a record of what the host has done, not
    // part of the device's state.
    _phase = _Phase.read;
    _autoselect = false;
    _pageBase = null;
    _pageBuffer.clear();
    _busyLeft = 0;
    _toggle = false;
  }

  // Command addresses decode the low 15 bits only, so the unlock offsets
  // work in whichever 64K bank the driver happens to be addressing.
  bool _isUnlockA(int addr) => (addr & 0x7FFF) == 0x5555;
  bool _isUnlockB(int addr) => (addr & 0x7FFF) == 0x2AAA;

  /// Reads still to go before the device stops reporting busy. Exposed so a
  /// harness can tell "the firmware's poll returned too early" from "the
  /// model is leaking reads the poll never made".
  int get busyLeft => _busyLeft;

  int read(int addr) {
    addr = fold(addr);
    if (_busyLeft > 0) {
      _busyLeft--;
      _toggle = !_toggle;
      // DQ6 toggles on every read while the operation runs; DQ7 reads back
      // as the complement of the bit being programmed.
      return (_toggle ? 0x40 : 0x00) | (~_busyByte & 0x80) | 0x3F;
    }
    if (_autoselect) {
      final offset = addr & 0xFFFF;
      if (offset == 0) return manufacturerId;
      if (offset == 1) return deviceId;
    }
    return peek(addr);
  }

  void write(int addr, int value) {
    addr = fold(addr);
    value &= 0xFF;

    // A program command in flight swallows writes as data. On a byte-write
    // part that is a single store; on a page-write part the data is buffered
    // until the page is full, or until something outside it arrives, which
    // commits what is there and falls through to be read as a command again.
    if (_pendingProgram) {
      if (!pageWrite) {
        _pendingProgram = false;
        poke(addr, peek(addr) & value); // programming can only clear bits
        _busyByte = value;
        _busyLeft = busyReads;
        _toggle = false;
        programmedBytes++;
        final b = _bankFor(addr);
        b.bytes++;
        b.touch(addr, addr);
        return;
      }
      final page = addr & ~(pageSize - 1);
      _pageBase ??= page;
      if (page == _pageBase) {
        _pageBuffer[addr - page] = value;
        if (_pageBuffer.length >= pageSize) _commitPage();
        return;
      }
      _commitPage();
    }

    switch (_phase) {
      case _Phase.read:
        if (_isUnlockA(addr) && value == 0xAA) {
          _phase = _Phase.unlock1;
        } else {
          // Ignored: flash cannot be written by a bare store.
          ignoredWrites++;
          final bank = addr & 0xFF0000;
          ignoredByBank[bank] = (ignoredByBank[bank] ?? 0) + 1;
        }
        return;

      case _Phase.unlock1:
        _phase = (_isUnlockB(addr) && value == 0x55)
            ? _Phase.unlock2
            : _Phase.read;
        return;

      case _Phase.unlock2:
        _phase = _Phase.read;
        if (!_isUnlockA(addr)) return;
        switch (value) {
          case 0x90:
            _autoselect = true;
            identifyCount++;
            break;
          case 0xF0:
            _autoselect = false;
            break;
          case 0xA0:
            _autoselect = false;
            _pageBase = null;
            _pageBuffer.clear();
            _pendingProgram = true;
            break;
          case 0x80:
            _phase = _Phase.erasePrefix;
            break;
          default:
            break;
        }
        return;

      case _Phase.erasePrefix:
        _phase = (_isUnlockA(addr) && value == 0xAA)
            ? _Phase.eraseUnlock1
            : _Phase.read;
        return;

      case _Phase.eraseUnlock1:
        _phase = (_isUnlockB(addr) && value == 0x55)
            ? _Phase.eraseUnlock2
            : _Phase.read;
        return;

      case _Phase.eraseUnlock2:
        _phase = _Phase.read;
        if (value == 0x10 && _isUnlockA(addr)) {
          _eraseRange(base, size);
        } else if (value == 0x30) {
          final sector = addr & ~(sectorSize - 1);
          _eraseRange(sector, sectorSize);
          erasedSectors++;
        }
        return;
    }
  }

  bool _pendingProgram = false;

  void _commitPage() {
    final page = _pageBase;
    _pageBase = null;
    _pendingProgram = false;
    if (page == null) return;

    // A page write replaces the whole page: bytes the host did not supply
    // come back erased, exactly as on an AT29C-style part.
    for (var i = 0; i < pageSize; i++) {
      poke(page + i, _pageBuffer[i] ?? 0xFF);
    }
    _busyByte = _pageBuffer[pageSize - 1] ?? 0xFF;
    _pageBuffer.clear();
    programmedPages++;
    final b = _bankFor(page);
    b.pages++;
    b.bytes += pageSize;
    b.touch(page, page + pageSize - 1);
    _busyLeft = busyReads;
    _toggle = false;
  }

  void _eraseRange(int from, int length) {
    for (var i = 0; i < length; i++) {
      poke(from + i, 0xFF);
    }
    // An erase can span banks; note it against each one it reaches.
    for (var a = from & 0xFF0000; a < from + length; a += 0x10000) {
      final b = _bankFor(a);
      b.erases++;
      b.touch(from < a ? a : from,
          (from + length - 1) > (a + 0xFFFF) ? a + 0xFFFF : from + length - 1);
    }
    _busyByte = 0xFF;
    _busyLeft = busyReads;
    _toggle = false;
  }
}
