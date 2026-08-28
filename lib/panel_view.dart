/// The Buttons tab: the machine's front panel, drawn and clickable.
///
/// Pressing a key here does not tell the firmware anything directly. It puts
/// the key down in the [Keypad], which is wired to the CPU as the real one
/// is -- a crossing on a scanned matrix, or a port pin for reverse -- and the
/// firmware finds it for itself the next time its scan comes round. The
/// knobs work the same way: a drag queues detents, and the quadrature pair on
/// port C is walked one step at a time as the CPU reads it.
library;

import 'dart:math' as math;

import 'package:flutter/material.dart';

import 'keypad.dart';

/// How far the pointer must be dragged for one detent of a knob.
const double _pixelsPerDetent = 9.0;

/// Two clicks closer together than this latch a key down.
const Duration _doubleClickWindow = Duration(milliseconds: 350);

class PanelView extends StatefulWidget {
  const PanelView({
    super.key,
    required this.keypad,
    required this.repaint,
    this.askedFor,
  });

  final Keypad keypad;

  /// Ticked by the run loop, so the knobs turn on screen as the firmware
  /// takes their steps.
  final Listenable repaint;

  /// What the firmware is currently asking for, H'11B10E, or null when the
  /// panel is not attached to a running machine.
  final int Function()? askedFor;

  @override
  State<PanelView> createState() => _PanelViewState();
}

class _PanelViewState extends State<PanelView> {
  PanelKey? _held;
  PanelKnob? _turning;
  double _dragRemainder = 0;
  int? _lastDownCode;
  DateTime _lastDownAt = DateTime.fromMillisecondsSinceEpoch(0);

  Keypad get pad => widget.keypad;

  /// Art coordinates for a point in the widget, given the box it is drawn in.
  Offset _toArt(Offset p, Size size) {
    final scale = math.min(
        size.width / Keypad.artWidth, size.height / Keypad.artHeight);
    final dx = (size.width - Keypad.artWidth * scale) / 2;
    final dy = (size.height - Keypad.artHeight * scale) / 2;
    return Offset((p.dx - dx) / scale, (p.dy - dy) / scale);
  }

  PanelKey? _keyAt(Offset art) {
    for (final k in Keypad.panelKeys) {
      if ((art - Offset(k.x, k.y)).distance <= k.radius) return k;
    }
    return null;
  }

  PanelKnob? _knobAt(Offset art) {
    for (final k in pad.knobs) {
      if ((art - Offset(k.x, k.y)).distance <= k.radius) return k;
    }
    return null;
  }

  void _down(Offset art) {
    final knob = _knobAt(art);
    if (knob != null) {
      setState(() {
        _turning = knob;
        _dragRemainder = 0;
      });
      return;
    }

    final key = _keyAt(art);
    if (key == null) return;

    // A latched key is let go by clicking it again.
    if (pad.latched.contains(key.code)) {
      setState(() => pad.setDown(key, false));
      _lastDownCode = null;
      return;
    }

    final now = DateTime.now();
    final isSecond = _lastDownCode == key.code &&
        now.difference(_lastDownAt) < _doubleClickWindow;

    setState(() {
      pad.setDown(key, true);
      if (isSecond) {
        // The second click of a double latches it down.
        pad.latched.add(key.code);
        _held = null;
        _lastDownCode = null;
      } else {
        _held = key;
        _lastDownCode = key.code;
        _lastDownAt = now;
      }
    });
  }

  void _move(Offset art, Offset delta, Size size) {
    final knob = _turning;
    if (knob == null) return;
    final scale = math.min(
        size.width / Keypad.artWidth, size.height / Keypad.artHeight);
    // Down is clockwise. The drag is measured in widget pixels rather than
    // art units so the feel does not change with the window.
    _dragRemainder += delta.dy * scale;
    final detents = (_dragRemainder / _pixelsPerDetent).truncate();
    if (detents == 0) return;
    _dragRemainder -= detents * _pixelsPerDetent;
    setState(() => pad.turn(knob, detents));
  }

  void _up() {
    setState(() {
      _turning = null;
      final k = _held;
      if (k != null && !pad.latched.contains(k.code)) pad.setDown(k, false);
      _held = null;
    });
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Column(
      children: [
        _header(theme),
        Expanded(
          child: LayoutBuilder(
            builder: (context, constraints) {
              final size = Size(constraints.maxWidth, constraints.maxHeight);
              return Listener(
                behavior: HitTestBehavior.opaque,
                onPointerDown: (e) => _down(_toArt(e.localPosition, size)),
                onPointerMove: (e) =>
                    _move(_toArt(e.localPosition, size), e.delta, size),
                onPointerUp: (_) => _up(),
                onPointerCancel: (_) => _up(),
                child: CustomPaint(
                  key: const Key('panelCanvas'),
                  size: size,
                  painter: _PanelPainter(
                    keypad: pad,
                    held: _held,
                    onSurface: theme.colorScheme.onSurface,
                    // The panel's lettering follows the app's typography
                    // rather than hard-coding a face.
                    base: theme.textTheme.bodyMedium ?? const TextStyle(),
                    repaint: widget.repaint,
                  ),
                ),
              );
            },
          ),
        ),
      ],
    );
  }

  Widget _header(ThemeData theme) {
    final mono = theme.textTheme.bodySmall
        ?.copyWith(fontFamily: 'monospace', fontFeatures: const []);
    String hex(int v, [int w = 2]) =>
        v.toRadixString(16).toUpperCase().padLeft(w, '0');

    return ListenableBuilder(
      listenable: widget.repaint,
      builder: (context, _) {
        final asked = widget.askedFor?.call();
        final down = pad.down.toList()..sort();
        return Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          color: theme.colorScheme.surfaceContainerHighest,
          child: Row(
            children: [
              Text(
                down.isEmpty
                    ? 'nothing down'
                    : 'down: ${down.map((c) => "H'${hex(c)}").join(' ')}',
                style: mono,
              ),
              const SizedBox(width: 20),
              if (asked != null)
                Text(
                  "asking for: ${asked == 0xFFFF ? 'nothing' : "H'${hex(asked)}"}"
                  "   (H'11B10E)",
                  style: mono,
                ),
              const Spacer(),
              Text(
                'knobs '
                '${pad.knobs.map((k) => '${k.levels.toRadixString(2).padLeft(2, '0')}'
                    '${k.pending == 0 ? '' : ' ${k.pending > 0 ? '+' : ''}${k.pending}'}').join('  ')}',
                style: mono,
              ),
              const SizedBox(width: 16),
              TextButton(
                onPressed: () => setState(pad.releaseAll),
                child: const Text('Release all'),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _PanelPainter extends CustomPainter {
  _PanelPainter({
    required this.keypad,
    required this.held,
    required this.onSurface,
    required this.base,
    required Listenable repaint,
  }) : super(repaint: repaint);

  final Keypad keypad;
  final PanelKey? held;
  final Color onSurface;
  final TextStyle base;

  static const Color keyFace = Color(0xFFEDEDED);
  static const Color keyHeld = Color(0xFF7FB2FF);
  static const Color keyLatched = Color(0xFFE04A4A);
  static const Color knobFace = Color(0xFF9B30E8);
  static const Color ink = Color(0xFF101010);

  @override
  void paint(Canvas canvas, Size size) {
    final scale = math.min(
        size.width / Keypad.artWidth, size.height / Keypad.artHeight);
    canvas.save();
    canvas.translate((size.width - Keypad.artWidth * scale) / 2,
        (size.height - Keypad.artHeight * scale) / 2);
    canvas.scale(scale);

    _groupBox(canvas);
    for (final k in Keypad.panelKeys) {
      _key(canvas, k);
    }
    for (final k in keypad.knobs) {
      _knob(canvas, k);
    }
    canvas.restore();
  }

  Paint get _outline => Paint()
    ..style = PaintingStyle.stroke
    ..strokeWidth = 5
    ..color = ink;

  /// The moulding the three sewing keys sit in.
  void _groupBox(Canvas canvas) {
    canvas.drawRRect(
      RRect.fromRectAndRadius(
          const Rect.fromLTRB(672, 115, 1190, 455), const Radius.circular(40)),
      _outline..strokeWidth = 4,
    );
  }

  void _key(Canvas canvas, PanelKey k) {
    final centre = Offset(k.x, k.y);
    final latched = keypad.latched.contains(k.code);
    final pressed = keypad.down.contains(k.code);
    canvas.drawCircle(
      centre,
      k.radius,
      Paint()
        ..color = latched
            ? keyLatched
            : (pressed || k.code == held?.code)
                ? keyHeld
                : keyFace,
    );
    canvas.drawCircle(centre, k.radius, _outline..strokeWidth = 5);

    // The code inside the key, as it is written on the drawing.
    _text(canvas, k.code.toRadixString(16).toUpperCase(), centre,
        size: 34,
        color: latched ? Colors.white : ink,
        align: _Align.centre,
        italic: true);

    if (k.label.isNotEmpty) {
      if (k.labelBelow) {
        _text(canvas, k.label, Offset(k.x, k.y + k.radius + 42),
            size: 42, color: onSurface, align: _Align.centre, italic: true);
      } else {
        _text(canvas, k.label, Offset(k.x + k.radius + 22, k.y),
            size: 42, color: onSurface, align: _Align.left, italic: true);
      }
    }
    _glyph(canvas, k);
  }

  /// The markings that are drawn rather than written. They sit to the right
  /// of their key, in the space the label would use.
  void _glyph(Canvas canvas, PanelKey k) {
    final x = k.x + k.radius + 40;
    final y = k.y;
    final p = Paint()
      ..color = onSurface
      ..style = PaintingStyle.stroke
      ..strokeWidth = 9
      ..strokeCap = StrokeCap.round;
    final fill = Paint()..color = onSurface;

    switch (k.glyph) {
      case PanelGlyph.none:
        break;
      case PanelGlyph.straightStitch:
        // A dashed line: the straight stitch.
        for (var i = -3; i <= 3; i++) {
          canvas.drawLine(
              Offset(x + 30, y + i * 22 - 6), Offset(x + 30, y + i * 22 + 6), p);
        }
      case PanelGlyph.buttonhole:
        // A tall bar with a hole in it.
        canvas.drawRect(
            Rect.fromCenter(center: Offset(x + 30, y), width: 34, height: 150),
            p..strokeWidth = 12);
      case PanelGlyph.fancyStitch:
        // The squiggle: an S laid on its side, thick like the panel's.
        final path = Path()..moveTo(x + 8, y - 78);
        path.cubicTo(x + 96, y - 62, x + 4, y - 16, x + 46, y + 4);
        path.cubicTo(x + 92, y + 26, x + 6, y + 44, x + 34, y + 80);
        canvas.drawPath(path, p..strokeWidth = 20);
      case PanelGlyph.frame:
        // An embroidery frame: a square with its bars.
        final r = Rect.fromCenter(
            center: Offset(x + 45, y), width: 110, height: 100);
        canvas.drawRect(r, p..strokeWidth = 6);
        canvas.drawRect(r.deflate(14), p);
        canvas.drawLine(Offset(r.left, r.top + 22),
            Offset(r.right, r.top + 22), p..strokeWidth = 5);
        canvas.drawLine(
            Offset(r.left, r.bottom - 22), Offset(r.right, r.bottom - 22), p);
      case PanelGlyph.letterA:
        _text(canvas, 'A', Offset(x, y),
            size: 46, color: onSurface, align: _Align.left, italic: true);
      case PanelGlyph.module:
        // The machine with two arrows pointing at it.
        final path = Path()
          ..moveTo(x, y - 40)
          ..lineTo(x + 110, y - 40)
          ..lineTo(x + 122, y - 26)
          ..lineTo(x + 122, y + 40)
          ..lineTo(x, y + 40)
          ..close();
        canvas.drawPath(path, p..strokeWidth = 6);
        canvas.drawRect(
            Rect.fromLTRB(x + 20, y - 22, x + 78, y + 8), p..strokeWidth = 5);
        for (final dy in [-14.0, 6.0]) {
          canvas.drawLine(
              Offset(x + 190, y + dy), Offset(x + 138, y + dy), p);
          canvas.drawPath(
              Path()
                ..moveTo(x + 132, y + dy)
                ..lineTo(x + 152, y + dy - 9)
                ..lineTo(x + 152, y + dy + 9)
                ..close(),
              fill);
        }
      case PanelGlyph.leftArrow:
        _arrow(canvas, Offset(k.x, k.y - k.radius - 46), -1, p, fill);
      case PanelGlyph.rightArrow:
        _arrow(canvas, Offset(k.x, k.y - k.radius - 46), 1, p, fill);
    }
  }

  /// The arrows above the two menu keys.
  void _arrow(Canvas canvas, Offset at, int dir, Paint p, Paint fill) {
    final tip = at.dx + dir * 62;
    canvas.drawLine(Offset(at.dx - dir * 55, at.dy), Offset(tip, at.dy),
        p..strokeWidth = 9);
    canvas.drawPath(
        Path()
          ..moveTo(tip + dir * 10, at.dy)
          ..lineTo(tip - dir * 18, at.dy - 16)
          ..lineTo(tip - dir * 18, at.dy + 16)
          ..close(),
        fill);
  }

  void _knob(Canvas canvas, PanelKnob k) {
    final centre = Offset(k.x, k.y);
    canvas.drawCircle(centre, k.radius, Paint()..color = knobFace);
    canvas.drawCircle(centre, k.radius, _outline..strokeWidth = 5);

    // The pointer, at the position the firmware has actually been given --
    // not where the drag has got to, so a queue that has not been paid out
    // yet shows as a knob that has not turned yet.
    final a = k.angle - math.pi / 2;
    canvas.drawLine(
      centre,
      centre + Offset(math.cos(a), math.sin(a)) * (k.radius - 8),
      Paint()
        ..color = ink
        ..strokeWidth = 12
        ..strokeCap = StrokeCap.round,
    );

    for (final (i, line) in k.name.split(' ').indexed) {
      _text(canvas, line, Offset(k.x, k.y - k.radius - 100 + i * 52),
          size: 46, color: onSurface, align: _Align.centre, italic: true);
    }
    if (k.pending != 0) {
      _text(canvas, '${k.pending > 0 ? '+' : ''}${k.pending}',
          Offset(k.x + k.radius + 18, k.y),
          size: 38, color: onSurface, align: _Align.left);
    }
  }

  void _text(Canvas canvas, String s, Offset at,
      {required double size,
      required Color color,
      required _Align align,
      bool italic = false}) {
    final tp = TextPainter(
      text: TextSpan(
        text: s,
        style: base.copyWith(
          color: color,
          fontSize: size,
          fontStyle: italic ? FontStyle.italic : FontStyle.normal,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    final dx = switch (align) {
      _Align.centre => at.dx - tp.width / 2,
      _Align.left => at.dx,
    };
    tp.paint(canvas, Offset(dx, at.dy - tp.height / 2));
  }

  @override
  bool shouldRepaint(_PanelPainter old) =>
      old.held != held || old.onSurface != onSurface || old.base != base;
}

enum _Align { centre, left }
