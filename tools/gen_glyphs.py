#!/usr/bin/env python3
"""Pre-renders the UI fonts into app/assets/glyphs.bin (8-bit alpha atlases).

Format (little endian):
  "KGLY" u32 fontCount
  per font: u8 id, u8 pad, u16 size, i16 ascent, i16 lineHeight, u32 glyphCount
    per glyph: u32 codepoint, u16 w, u16 h, i16 xoff, i16 yoff, u16 advance, u16 pad, u32 offset
  then all bitmaps (w*h bytes each), offsets relative to bitmap start.
"""
import struct, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "app/assets/fonts"
sys.path.insert(0, str(ROOT / "tools"))
from gen_kana_data import H_ROWS, K_ROWS  # noqa: E402

LATIN = "".join(chr(c) for c in range(0x20, 0x7f)) + "äöüÄÖÜß·•–—…„“”‘’«»×°%€‹›→←"
KANA = "".join(item[0] for rows in (H_ROWS, K_ROWS) for _, spec in rows for item in spec.split())
EXTRA_JP = "かな道場行小ーゃゅょっ・「」"

# id: (file, chars, sizes)
SPECS = [
    (0, "NotoSans-Regular.ttf", LATIN, [26, 32, 40, 52]),
    (1, "NotoSans-Bold.ttf", LATIN, [32, 40, 52, 72, 110]),
    (2, "KleeOne-SemiBold.ttf", LATIN + KANA + EXTRA_JP, [44, 64, 96, 150]),
]


def render(font, ch):
    l, t, r, b = font.getbbox(ch, anchor="ls")
    w, h = max(1, r - l), max(1, b - t)
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255, anchor="ls")
    return img, l, t, round(font.getlength(ch))


def main(out):
    header, glyph_tables, bitmaps = [], [], bytearray()
    count = 0
    for fid, fname, chars, sizes in SPECS:
        chars = sorted(set(chars))
        for size in sizes:
            font = ImageFont.truetype(str(FONTS / fname), size)
            ascent, descent = font.getmetrics()
            table = bytearray()
            n = 0
            for ch in chars:
                img, xoff, yoff, adv = render(font, ch)
                table += struct.pack("<IHHhhHHI", ord(ch), img.width, img.height,
                                     xoff, yoff, adv, 0, len(bitmaps))
                bitmaps += img.tobytes()
                n += 1
            header.append(struct.pack("<BBHhhI", fid, 0, size, ascent,
                                      ascent + descent, n))
            glyph_tables.append(table)
            count += 1
    data = bytearray(b"KGLY") + struct.pack("<I", count)
    for h, t in zip(header, glyph_tables):
        data += h + t
    data += bitmaps
    Path(out).write_bytes(data)
    print(f"{count} font sizes, {len(data) / 1e6:.1f} MB")


if __name__ == "__main__":
    main(sys.argv[1])
