#!/usr/bin/env python3
"""Rasterize the pinned Adobe font into PicoSerifCJK; never compiles the launcher.

Generation: Pillow 12.3.0 and fonttools 4.60.1.
Verification uses only Python's standard library.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SHA256 = "1d4dc4b757c07034e2412d6edf48f54f94ec7172d4deb3b90a3e4fc9dcb94f5d"
SOURCE_URL = ("https://raw.githubusercontent.com/adobe-fonts/source-han-serif/"
              "7889f11bf31170b5d092a083b357c8c8130f89e0/"
              "OTF/SimplifiedChinese/SourceHanSerifSC-Medium.otf")
SIGNATURE = 0x3254464E
TWO_BIT = 0x800000
RANGES = ((0x2000, 0x206F), (0x3000, 0x303F), (0x3400, 0x4DBF),
          (0x4E00, 0x9FFF), (0xF900, 0xFAFF), (0xFF00, 0xFFEF))
SAMPLES = "马里奥赛车塞尔达传说口袋妖怪精灵宝可梦恶魔城逆转裁判最终幻想汉化版寶可夢薩爾達傳說繁體中文"


def read_font(path):
    data = Path(path).read_bytes()
    signature, info, charmap, bitmap, ascent, descent, count = struct.unpack_from("<IIIIBBH", data)
    assert signature == SIGNATURE and info == 20
    assert info + count * 8 <= charmap < bitmap <= len(data)
    glyphs = [struct.unpack_from("<IbbBb", data, info + i * 8) for i in range(count)]
    mapping = {}
    while True:
        length, start = struct.unpack_from("<HH", data, charmap)
        charmap += 4
        if length == 0:
            break
        assert start + length <= 0x10000 and charmap + 2 * length <= bitmap
        for cp in range(start, start + length):
            index = struct.unpack_from("<H", data, charmap)[0]
            charmap += 2
            assert 0 <= index < count and cp not in mapping
            if index:
                mapping[cp] = index
    for packed, left, right, height, top in glyphs:
        offset, width = packed & 0xFFFFFF, packed >> 24
        row_bytes = (width + (3 if offset & TWO_BIT else 1)) // (4 if offset & TWO_BIT else 2)
        assert bitmap + (offset & (TWO_BIT - 1)) + height * row_bytes <= len(data)
        assert width + left + right >= 0
        if offset & TWO_BIT:
            assert 0 <= top <= ascent + descent and top + height <= ascent + descent
    return data, glyphs, mapping, bitmap, ascent, descent


def pixels(font, cp):
    data, glyphs, mapping, bitmap, _, _ = font
    packed, left, right, height, top = glyphs[mapping.get(cp, 0)]
    width = packed >> 24
    offset = packed & 0xFFFFFF
    bits = 2 if offset & TWO_BIT else 4
    per_byte = 8 // bits
    stride = (width + per_byte - 1) // per_byte
    base = bitmap + (offset & (TWO_BIT - 1))
    values = [((data[base + y * stride + x // per_byte] >> ((x % per_byte) * bits))
               & ((1 << bits) - 1)) * (5 if bits == 2 else 1)
              for y in range(height) for x in range(width)]
    return width, height, left, right, top, values


def generate(source, output, size, ascent, descent):
    from PIL import Image, ImageFont
    from fontTools.ttLib import TTFont
    cmap = TTFont(source).getBestCmap()
    chars = sorted(cp for cp in cmap if any(lo <= cp <= hi for lo, hi in RANGES))
    assert all(ord(c) in chars for c in SAMPLES)
    font = ImageFont.truetype(str(source), size, layout_engine=ImageFont.Layout.BASIC)
    glyphs = [struct.pack("<IbbBb", TWO_BIT, 0, 0, 0, 0)]
    bitmap = bytearray()
    for cp in chars:
        mask, offset = font.getmask2(chr(cp), mode="L", anchor="ls")
        advance = round(font.getlength(chr(cp)))
        image = Image.frombytes("L", mask.size, bytes(mask))
        # A few vertical punctuation marks are two em tall. Fit their entire
        # outline into the existing line height instead of clipping the bottom.
        raw_bbox = image.getbbox()
        if raw_bbox and raw_bbox[3] - raw_bbox[1] > ascent + descent:
            image = image.crop(raw_bbox)
            fitted_width = max(1, round(image.width * (ascent + descent) / image.height))
            image = image.resize((fitted_width, ascent + descent), Image.Resampling.LANCZOS)
            offset = (offset[0] + raw_bbox[0], -ascent)
        image = image.point([round(v * 3 / 255) for v in range(256)])
        bbox = image.getbbox()
        if bbox:
            image = image.crop(bbox)
            width, height = image.size
            left = offset[0] + bbox[0]
            top = ascent + offset[1] + bbox[1]
            assert height <= ascent + descent, (hex(cp), height)
            top = max(0, min(top, ascent + descent - height))
            raw = image.tobytes()
        else:
            width = height = left = top = 0
            raw = []
        right = advance - left - width
        assert len(bitmap) < TWO_BIT and width < 256
        glyphs.append(struct.pack("<IbbBb", (width << 24) | TWO_BIT | len(bitmap),
                                  left, right, height, top))
        for y in range(height):
            for x in range(0, width, 4):
                bitmap.append(sum(raw[y * width + x + n] << (2 * n)
                                  for n in range(min(4, width - x))))
    charmap = bytearray()
    start = 0
    while start < len(chars):
        end = start + 1
        while end < len(chars) and chars[end] == chars[end - 1] + 1:
            end += 1
        charmap.extend(struct.pack("<HH", end - start, chars[start]))
        charmap.extend(struct.pack("<" + "H" * (end - start), *range(start + 1, end + 1)))
        start = end
    charmap.extend(b"\0" * 4)
    while len(charmap) % 4:
        charmap.append(0)
    info_offset = 20
    map_offset = info_offset + len(glyphs) * 8
    bitmap_offset = map_offset + len(charmap)
    data = (struct.pack("<IIIIBBH", SIGNATURE, info_offset, map_offset, bitmap_offset,
                        ascent, descent, len(glyphs)) + b"".join(glyphs) + charmap + bitmap)
    output.write_bytes(data)
    return {"file": output.name, "characters": len(chars), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "pixel_size": size}


def verify():
    manifest = json.loads((ROOT / "tools/fonts/manifest.json").read_text())
    total = 0
    coverage = None
    for entry in manifest["fonts"]:
        path = ROOT / "arm9/data" / entry["file"]
        font = read_font(path)
        data, glyphs, mapping, _, _, _ = font
        assert hashlib.sha256(data).hexdigest() == entry["sha256"]
        assert len(data) == entry["bytes"] and len(mapping) == entry["characters"]
        assert coverage is None or coverage == set(mapping)
        coverage = set(mapping)
        assert all(g[0] & TWO_BIT for g in glyphs)
        for c in SAMPLES:
            assert ord(c) in mapping and any(pixels(font, ord(c))[-1]), c
        total += len(data)
        print(f"Verified {path.name}: {len(mapping)} characters, {len(data)} bytes")
    assert total <= 2_500_000, f"Fallback fonts exceed the 2.5 MB asset budget: {total}"
    print(f"Both fonts verified; total {total} bytes; simplified/traditional sample coverage passed.")


def preview(destination):
    from PIL import Image
    lines = ["马里奥赛车DS.nds", "塞尔达传说 幻影沙漏.nds", "精灵宝可梦 白金 汉化版.nds",
             "逆转裁判3.nds", "薩爾達傳說 大地汽笛.nds", "Pokemon 寶可夢.nds"]
    normal = read_font(ROOT / "arm9/data/NotoSansJP-Medium-10.nft2")
    small = read_font(ROOT / "arm9/data/NotoSansJP-Medium-7_5.nft2")
    image = Image.new("RGB", (256, 192), (238, 238, 238))
    for small_mode, base, size in ((False, normal, 12), (True, small, 9)):
        fallback = read_font(ROOT / f"arm9/data/PicoSerifCJK-{size}.nft2")
        for row, line in enumerate(lines):
            x, y = 8, (8 if not small_mode else 102) + row * 14
            for c in line:
                cp = ord(c)
                selected = base if cp in base[2] else fallback
                width, height, left, right, top, values = pixels(selected, cp)
                x += left
                for gy in range(height):
                    for gx in range(width):
                        if 0 <= x + gx < 256:
                            v = values[gy * width + gx]
                            shade = round(238 + (30 - 238) * v / 15)
                            image.putpixel((x + gx, y + top + base[4] - selected[4] + gy), (shade,) * 3)
                x += width + right
    image.resize((1024, 768), Image.Resampling.NEAREST).save(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    if args.source:
        assert hashlib.sha256(args.source.read_bytes()).hexdigest() == SOURCE_SHA256
        entries = [generate(args.source, ROOT / f"arm9/data/PicoSerifCJK-{size}.nft2", size, asc, desc)
                   for size, asc, desc in ((9, 8, 2), (12, 11, 2))]
        manifest = {"source_url": SOURCE_URL, "source_sha256": SOURCE_SHA256,
                    "license": "SIL OFL 1.1", "family": "PicoSerifCJK", "fonts": entries}
        (ROOT / "tools/fonts/manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.verify or args.source:
        verify()
    if args.preview:
        preview(args.preview)
