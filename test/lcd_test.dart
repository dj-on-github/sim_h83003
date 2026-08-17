// Tests for the LCD frame buffer decoding: 320x240, 2 bits per pixel,
// four grey levels, most significant bits first, 80 bytes per line.

import 'package:flutter_test/flutter_test.dart';
import 'package:sim_h83003/lcd.dart';
import 'package:sim_h83003/sparse_memory.dart';

/// Red channel of the pixel at (x, y) — the image is greyscale, so one
/// channel is the whole story.
int pixel(List<int> rgba, int x, int y) =>
    rgba[(y * LcdFormat.width + x) * 4];

void main() {
  test('geometry matches the panel', () {
    expect(LcdFormat.width, 320);
    expect(LcdFormat.height, 240);
    expect(LcdFormat.stride, 80); // 320 pixels x 2 bits / 8
    expect(LcdFormat.bytes, 19200); // H'4B00
    expect(LcdFormat.bytes.toRadixString(16).toUpperCase(), '4B00');
  });

  test('pixels within a byte run most significant bits first', () {
    final mem = SparseMemory();
    const base = 0x040000;
    // H'1B = 00 01 10 11 -> stored levels 0, 1, 2, 3 left to right, which on
    // this positive-mode panel render white -> black.
    mem.poke(base, 0x1B);
    final rgba = lcdToRgba(mem.peek, base);
    expect(pixel(rgba, 0, 0), LcdFormat.levels[3]); // white
    expect(pixel(rgba, 1, 0), LcdFormat.levels[2]);
    expect(pixel(rgba, 2, 0), LcdFormat.levels[1]);
    expect(pixel(rgba, 3, 0), LcdFormat.levels[0]); // black
  });

  test('stored 0 is the light background and 3 is black', () {
    final mem = SparseMemory();
    const base = 0x040000;
    mem.poke(base, 0x00); // four pixels of level 0
    mem.poke(base + 1, 0xFF); // four pixels of level 3
    final rgba = lcdToRgba(mem.peek, base);
    expect(pixel(rgba, 0, 0), 0xFF); // white
    expect(pixel(rgba, 4, 0), 0x00); // black
  });

  test('invert renders the panel the other way up', () {
    final mem = SparseMemory();
    const base = 0x040000;
    mem.poke(base, 0x1B); // stored levels 0,1,2,3
    final rgba = lcdToRgba(mem.peek, base, invert: true);
    expect(pixel(rgba, 0, 0), LcdFormat.levels[0]); // now black
    expect(pixel(rgba, 1, 0), LcdFormat.levels[1]);
    expect(pixel(rgba, 2, 0), LcdFormat.levels[2]);
    expect(pixel(rgba, 3, 0), LcdFormat.levels[3]); // now white
  });

  test('a blank (all-zero) buffer renders as a light screen', () {
    final mem = SparseMemory();
    final rgba = lcdToRgba(mem.peek, 0x040000);
    expect(pixel(rgba, 0, 0), 0xFF);
    expect(pixel(rgba, LcdFormat.width - 1, LcdFormat.height - 1), 0xFF);
  });

  test('each scan line is 80 bytes further on', () {
    final mem = SparseMemory();
    const base = 0x040000;
    // Ink (stored level 3) at the first pixel of row 1 and the last of row 0.
    mem.poke(base + LcdFormat.stride, 0xC0);
    mem.poke(base + LcdFormat.stride - 1, 0x03);
    final rgba = lcdToRgba(mem.peek, base);
    expect(pixel(rgba, 0, 1), LcdFormat.levels[0]); // black
    expect(pixel(rgba, LcdFormat.width - 1, 0), LcdFormat.levels[0]);
    expect(pixel(rgba, 0, 0), LcdFormat.levels[3]); // untouched: background
  });

  test('the buffer is read from the given base and is the right size', () {
    final mem = SparseMemory();
    const base = 0x040000;
    // Ink in the very last pixel of the last line.
    mem.poke(base + LcdFormat.bytes - 1, 0x03);
    final rgba = lcdToRgba(mem.peek, base);
    expect(rgba.length, LcdFormat.width * LcdFormat.height * 4);
    expect(pixel(rgba, LcdFormat.width - 1, LcdFormat.height - 1),
        LcdFormat.levels[0]);
  });

  test('reads follow a relocated base', () {
    final mem = SparseMemory();
    mem.poke(0x050000, 0xC0); // ink at the first pixel
    final rgba = lcdToRgba(mem.peek, 0x050000);
    expect(pixel(rgba, 0, 0), LcdFormat.levels[0]); // black
    // The old base holds nothing, so it renders as blank background.
    final other = lcdToRgba(mem.peek, 0x040000);
    expect(pixel(other, 0, 0), LcdFormat.levels[3]);
  });
}
