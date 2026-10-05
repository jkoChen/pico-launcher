#!/usr/bin/env python3
"""Convert pinned Fusion Pixel glyphs into PicoPixelCJK without resampling.

Generation: Pillow 12.3.0, fonttools 4.60.1 and Brotli 1.1.0.
Verification uses only Python's standard library.
"""
import argparse
import hashlib
import json
from io import BytesIO
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE_VERSION = "2026.09.25"
SOURCE_COMMIT = "6c88c8ec0f16f05e06663890a043ecbc81d448ae"
SOURCE_PROJECT = "https://github.com/TakWolf/fusion-pixel-font"
SOURCES = {
    10: {"archive_sha256": "84da5d4d6f14c49ffbba57a84669f208021a83f4ddcb4b74afa6d92bf1982154",
         "font_sha256": "7232787a01a29aa9b5b199be5a7f362f86311885153287101d77e198ca0bcded"},
    12: {"archive_sha256": "b547511d4e8828e9e04ee4af3519c08221fee3be8467842acb6ed76623b5d624",
         "font_sha256": "6573eb37436b61997f1012b8a8af633f40979edc2ec9c76bcdb42fa23132c428"},
}
for size, source in SOURCES.items():
    source["archive"] = f"fusion-pixel-font-{size}px-monospaced-ttf.woff2-v{SOURCE_VERSION}.zip"
    source["file"] = f"fusion-pixel-{size}px-monospaced-zh_hans.ttf.woff2"
    source["url"] = f"{SOURCE_PROJECT}/releases/download/{SOURCE_VERSION}/{source['archive']}"
SIGNATURE = 0x3254464E
TWO_BIT = 0x800000
# U+3031..3035 are vertical-writing repeat marks, not horizontal UI glyphs.
RANGES = ((0x2000, 0x206F), (0x3000, 0x3030), (0x3036, 0x303F), (0x3400, 0x4DBF),
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
    provenance = SOURCES[size]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == provenance["archive_sha256"]
    with zipfile.ZipFile(source) as archive:
        woff2 = archive.read(provenance["file"])
    assert hashlib.sha256(woff2).hexdigest() == provenance["font_sha256"]
    source_font = TTFont(BytesIO(woff2), recalcTimestamp=False)
    cmap = source_font.getBestCmap()
    chars = sorted(cp for cp in cmap if any(lo <= cp <= hi for lo, hi in RANGES))
    assert all(ord(c) in chars for c in SAMPLES)
    source_font.flavor = None
    ttf = BytesIO()
    source_font.save(ttf)
    ttf.seek(0)
    font = ImageFont.truetype(ttf, size, layout_engine=ImageFont.Layout.BASIC)
    glyphs = [struct.pack("<IbbBb", TWO_BIT, 0, 0, 0, 0)]
    bitmap = bytearray()
    for cp in chars:
        mask, offset = font.getmask2(chr(cp), mode="L", anchor="ls")
        advance = round(font.getlength(chr(cp)))
        # At the font's native pixel size every edge must land on the grid.
        # Never blur or shrink the outlines to force them into a label.
        assert set(bytes(mask)) <= {0, 255}, (hex(cp), "non-pixel coverage")
        image = Image.frombytes("L", mask.size, bytes(mask))
        image = image.point([0] * 255 + [3])
        bbox = image.getbbox()
        if bbox:
            image = image.crop(bbox)
            width, height = image.size
            left = offset[0] + bbox[0]
            top = ascent + offset[1] + bbox[1]
            assert height <= ascent + descent, (hex(cp), height)
            assert 0 <= top and top + height <= ascent + descent, (hex(cp), top, height)
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
    indices = {cp: i + 1 for i, cp in enumerate(chars)}
    # NFT2 searches ranges linearly. Sparse pixel fonts would otherwise create
    # thousands of tiny ranges, slowing Chinese marquee text on the ARM9.
    # Zero indices represent unsupported characters inside these few ranges.
    for lo, hi in RANGES:
        supported = [cp for cp in chars if lo <= cp <= hi]
        if not supported:
            continue
        start, end = supported[0], supported[-1] + 1
        charmap.extend(struct.pack("<HH", end - start, start))
        charmap.extend(struct.pack("<" + "H" * (end - start),
                                   *(indices.get(cp, 0) for cp in range(start, end))))
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
    for entry in manifest["fonts"]:
        path = ROOT / "arm9/data" / entry["file"]
        font = read_font(path)
        data, glyphs, mapping, bitmap, _, _ = font
        assert hashlib.sha256(data).hexdigest() == entry["sha256"]
        assert len(data) == entry["bytes"] and len(mapping) == entry["characters"]
        map_pos = struct.unpack_from("<I", data, 8)[0]
        range_count = 0
        while struct.unpack_from("<H", data, map_pos)[0]:
            count = struct.unpack_from("<H", data, map_pos)[0]
            map_pos += 4 + count * 2
            range_count += 1
        assert range_count <= len(RANGES), "Too many linear lookup ranges for ARM9"
        assert all(g[0] & TWO_BIT for g in glyphs)
        assert all(((byte ^ (byte >> 1)) & 0x55) == 0 for byte in data[bitmap:])
        for c in SAMPLES:
            assert ord(c) in mapping and any(pixels(font, ord(c))[-1]), c
        total += len(data)
        print(f"Verified {path.name}: {len(mapping)} characters, {len(data)} bytes, {range_count} lookup ranges")
    assert total <= 1_500_000, f"Fallback fonts exceed the 1.5 MB asset budget: {total}"
    print(f"Both fonts verified; total {total} bytes; simplified/traditional sample coverage passed.")


def preview(destination):
    from PIL import Image
    lines = ["马里奥赛车DS.nds", "塞尔达传说 幻影沙漏.nds", "精灵宝可梦 白金 汉化版.nds",
             "逆转裁判3.nds", "薩爾達傳說 大地汽笛.nds", "Pokemon 寶可夢.nds"]
    normal = read_font(ROOT / "arm9/data/NotoSansJP-Medium-10.nft2")
    small = read_font(ROOT / "arm9/data/NotoSansJP-Medium-7_5.nft2")
    image = Image.new("RGB", (256, 192), (238, 238, 238))
    for small_mode, base, size in ((False, normal, 12), (True, small, 10)):
        fallback = read_font(ROOT / f"arm9/data/PicoPixelCJK-{size}.nft2")
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
    parser.add_argument("--source-dir", type=Path, help="Directory containing the two pinned release ZIPs")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    if args.source_dir:
        entries = [generate(args.source_dir / SOURCES[size]["archive"],
                            ROOT / f"arm9/data/PicoPixelCJK-{size}.nft2", size, asc, desc)
                   for size, asc, desc in ((10, 8, 2), (12, 11, 2))]
        manifest = {"source_project": SOURCE_PROJECT, "source_version": SOURCE_VERSION,
                    "source_commit": SOURCE_COMMIT, "sources": SOURCES,
                    "license": "SIL OFL 1.1", "family": "PicoPixelCJK", "fonts": entries}
        (ROOT / "tools/fonts/manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.verify or args.source_dir:
        verify()
    if args.preview:
        preview(args.preview)
