// Decoding of the machine's LCD frame buffer into pixels.
//
// The Bernina artista 180 drives a 320x240 monochrome panel through an Epson
// SED1351F controller, with the frame buffer in memory the CPU addresses
// directly. Each pixel is 2 bits, giving four grey levels, so one byte holds
// four pixels and a scan line is 80 bytes.
//
// Within a byte the pixels run most significant bits first: bits 7-6 are the
// leftmost pixel of the group, then 5-4, 3-2, and 1-0.
//
// Kept separate from the UI so the bit layout can be unit-tested.

import 'dart:typed_data';

class LcdFormat {
  static const int width = 320;
  static const int height = 240;

  /// Bits per pixel — four grey levels.
  static const int bpp = 2;

  /// Bytes per scan line: four pixels to a byte.
  static const int stride = width * bpp ~/ 8; // 80

  /// Total frame buffer size: 19200 bytes (H'4B00).
  static const int bytes = stride * height;

  /// The four grey levels as 8-bit intensities, black through white.
  static const List<int> levels = [0x00, 0x55, 0xAA, 0xFF];
}

/// Reads the frame buffer at [base] through [peek] and returns it as RGBA
/// bytes, ready for `ui.decodeImageFromPixels`.
///
/// The panel is positive-mode, which is how the artista 180's screen renders:
/// a pixel value of 0 leaves the cell undriven and reads as the light
/// background, and 3 is full black. Confirmed against a dump taken with the
/// machine's stitch-selection screen displayed — white tile backgrounds are
/// stored as 0.
///
/// [invert] renders the other way up (0 black, 3 white).
Uint8List lcdToRgba(
  int Function(int address) peek,
  int base, {
  bool invert = false,
}) {
  final rgba = Uint8List(LcdFormat.width * LcdFormat.height * 4);
  var p = 0;
  for (var y = 0; y < LcdFormat.height; y++) {
    final row = base + y * LcdFormat.stride;
    for (var b = 0; b < LcdFormat.stride; b++) {
      final byte = peek(row + b);
      // Most significant bits first.
      for (var shift = 6; shift >= 0; shift -= 2) {
        final level = (byte >> shift) & 3;
        // Positive-mode panel: 0 is the light background, 3 is black.
        final v = LcdFormat.levels[invert ? level : 3 - level];
        rgba[p++] = v;
        rgba[p++] = v;
        rgba[p++] = v;
        rgba[p++] = 0xFF;
      }
    }
  }
  return rgba;
}
