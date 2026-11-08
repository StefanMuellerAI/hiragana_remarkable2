#!/usr/bin/env python3
"""Pre-renders the UI fonts into app/assets/glyphs.bin (8-bit alpha atlases).

  gen_glyphs.py <out>                  render everything from the fonts
  gen_glyphs.py <out> --extend <base>  keep every glyph of <base> byte for
                                       byte and render only the missing ones
                                       (e.g. new kanji), so existing text looks
                                       exactly as before even with another
                                       Pillow version

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
from gen_kanji_data import load_list  # noqa: E402

LATIN = "".join(chr(c) for c in range(0x20, 0x7f)) + "äöüÄÖÜß·•–—…„“”‘’«»×°%€‹›→←"
KANA = "".join(item[0] for rows in (H_ROWS, K_ROWS) for _, spec in rows for item in spec.split())
EXTRA_JP = "かな道場行小ーゃゅょっ・「」"
KANJI = "".join(glyph for glyph, _, _ in load_list()) + "漢字"

# id: (file, chars, sizes)
SPECS = [
    (0, "NotoSans-Regular.ttf", LATIN, [26, 32, 40, 52]),
    (1, "NotoSans-Bold.ttf", LATIN, [32, 40, 52, 72, 110]),
    (2, "KleeOne-SemiBold.ttf", LATIN + KANA + EXTRA_JP + KANJI, [44, 64, 96, 150]),
]


def load(path):
    """{(id, size): (ascent, lineHeight, {cp: (w, h, xoff, yoff, adv, bitmap)})}"""
    data = Path(path).read_bytes()
    if data[:4] != b"KGLY":
        raise SystemExit(f"{path}: not a glyphs.bin")
    (count,) = struct.unpack_from("<I", data, 4)
    pos, faces = 8, []
    for _ in range(count):
        fid, _, size, ascent, lh, n = struct.unpack_from("<BBHhhI", data, pos)
        pos += 12
        glyphs = []
        for _ in range(n):
            glyphs.append(struct.unpack_from("<IHHhhHHI", data, pos))
            pos += 20
        faces.append((fid, size, ascent, lh, glyphs))
    bitmaps = data[pos:]
    out = {}
    for fid, size, ascent, lh, glyphs in faces:
        out[(fid, size)] = (ascent, lh, {
            cp: (w, h, xo, yo, adv, bitmaps[off:off + w * h])
            for cp, w, h, xo, yo, adv, _, off in glyphs})
    return out


def render(font, ch):
    l, t, r, b = font.getbbox(ch, anchor="ls")
    w, h = max(1, r - l), max(1, b - t)
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255, anchor="ls")
    return img, l, t, round(font.getlength(ch))


def main(out, base=None):
    kept = load(base) if base else {}
    header, glyph_tables, bitmaps = [], [], bytearray()
    count = added = 0
    for fid, fname, chars, sizes in SPECS:
        chars = sorted(set(chars))
        for size in sizes:
            font = ImageFont.truetype(str(FONTS / fname), size)
            ascent, descent = font.getmetrics()
            lh = ascent + descent
            old = {}
            if (fid, size) in kept:
                ascent, lh, old = kept[(fid, size)]
            glyphs = dict(old)
            for ch in chars:
                if ord(ch) not in glyphs:
                    img, xoff, yoff, adv = render(font, ch)
                    glyphs[ord(ch)] = (img.width, img.height, xoff, yoff, adv, img.tobytes())
                    added += 1
            table = bytearray()
            for cp in sorted(glyphs):
                w, h, xoff, yoff, adv, bmp = glyphs[cp]
                table += struct.pack("<IHHhhHHI", cp, w, h, xoff, yoff, adv, 0, len(bitmaps))
                bitmaps += bmp
            header.append(struct.pack("<BBHhhI", fid, 0, size, ascent, lh, len(glyphs)))
            glyph_tables.append(table)
            count += 1
    data = bytearray(b"KGLY") + struct.pack("<I", count)
    for h, t in zip(header, glyph_tables):
        data += h + t
    data += bitmaps
    Path(out).write_bytes(data)
    note = f", {added} glyphs added to {base}" if base else ""
    print(f"{count} font sizes, {len(data) / 1e6:.1f} MB{note}")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[2] == "--extend":
        main(sys.argv[1], sys.argv[3])
    else:
        main(sys.argv[1])
