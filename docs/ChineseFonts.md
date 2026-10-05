# Chinese font support

This modified v1.3.0 build includes Chinese glyphs automatically. No font setting,
separate font download, or SD card font installation is needed. Chinese filenames
and internal game titles use the same fallback in the banner list, top screen,
and other text labels. The Game/File display setting still chooses the title
source; it does not control whether Chinese glyphs are available. The menus remain
in English.

The original bundled NotoSansJP files are small subsets without Han characters.
Existing glyphs remain unchanged. Missing CJK glyphs use **PicoPixelCJK**, a bitmap
derivative of **Fusion Pixel (缝合像素字体)**, distributed under the SIL Open Font
License 1.1. This replaces the earlier Source Han Serif fallback with native
pixel outlines for clearer small text on DS/DSi screens. The font copyright,
full license, and upstream notices ship in
[`/_pico/licenses/PicoPixelCJK`](../_pico/licenses/PicoPixelCJK).

Two shared fallback sizes fit the existing text rows: native **10px** for small
labels (11,312 mapped characters) and **12px** for regular labels (21,402 mapped
characters). Glyphs use the simplified-Chinese variant of Fusion Pixel's
monospaced fonts. Common simplified and traditional Chinese are included;
coverage differs between sizes, and some rare characters available at 12px are
absent at 10px. Unsupported characters use the original missing-glyph behavior.
Supplementary-plane Han, emoji and vertical-writing repeat marks are not included. The bitmap is used at its
native size without shrinking, smoothing or changing the existing UI layout.

The two embedded assets total 1,375,482 bytes (about 1.31 MiB), saving 744,913 bytes
of ROM and static ARM9 RAM versus the previous serif fonts. Pixel glyphs use only
transparent or solid coverage, stored in the existing two-bit bitmap format;
existing Noto glyphs retain their original four-bit antialiasing. Font selection
and baseline alignment are shared by measurement, normal drawing, marquee
clipping, and ellipsis drawing.

Each character map uses seven lookup ranges, including zero entries for missing
characters. This avoids walking thousands of sparse ranges for scrolling text.

## Reproducible assets

The source is Fusion Pixel release **2026.09.25**, commit
`6c88c8ec0f16f05e06663890a043ecbc81d448ae`. Release ZIP URLs and SHA-256 values for
both the ZIPs and the selected fonts are pinned in
[`tools/fonts/manifest.json`](../tools/fonts/manifest.json). To regenerate font
assets only, use Python with Pillow 12.3.0, fonttools 4.60.1 and Brotli 1.1.0.
Place the two pinned `10px-monospaced-ttf.woff2` and
`12px-monospaced-ttf.woff2` release ZIPs in a directory, then run:

```sh
python tools/fonts/generate_cjk.py --source-dir path/to/font-archives
python tools/fonts/generate_cjk.py --verify
```

`--preview preview.png` renders sample filenames from the actual packed assets.
These commands do not compile the launcher. Verification checks checksums,
format bounds, binary pixel coverage, representative simplified and traditional
titles at both sizes, and a 1.5 MB font asset budget.

## GitHub compilation

Open **Actions → Build Pico Launcher → Run workflow**, select
`filename-title-v1.3.0`, then download the **Pico_Launcher** artifact after success.
The workflow verifies the assets, runs the actual renderer with address and
undefined-behavior sanitizers, and compiles the NDS program in the BlocksDS
container. The separate **Linker_Map** artifact is for memory inspection.

Replace `LAUNCHER.nds` on the card with the new version. For DSpico, rename it to
`_picoboot.nds` and replace the existing root file. Copy the supplied `_pico`
folder contents alongside the existing loader files and settings; retain those
existing files. Display Settings → **NDS → File**, then **B**, enables and saves
filename titles. Compilation and renderer checks do not replace a real DS test.
