# Chinese font support

This modified v1.3.0 build includes Chinese glyphs automatically. No font setting,
separate font download, or SD card font installation is needed. Chinese filenames
and internal game titles use the same fallback in the banner list, top screen,
and other text labels. The Game/File display setting still chooses the title
source; it does not control whether Chinese glyphs are available. The menus remain
in English.

The original bundled NotoSansJP files are small subsets without Han characters.
Existing glyphs remain unchanged. Missing CJK glyphs use **PicoSerifCJK**, a raster
derivative of Adobe's **Source Han Serif SC Medium (思源宋体)**, distributed under
the SIL Open Font License 1.1. The font copyright and full license ship in
[`/_pico/licenses/PicoSerifCJK-OFL.txt`](../_pico/licenses/PicoSerifCJK-OFL.txt).

Two shared fallback sizes fit the existing 10- and 13/15-pixel text rows. Each
contains 28,273 mapped characters: source-supported BMP Han characters including
Extension A and compatibility ideographs, plus CJK, fullwidth and common
punctuation. This includes common simplified and traditional Chinese. It does
not cover every Unicode character, supplementary-plane Han, or emoji. Small text
is limited by the DS display resolution.

The two embedded assets total 2,120,395 bytes (about 2.02 MiB), which also consume
ARM9 RAM while the launcher is running. Coverage uses four alpha levels packed
into two bits per pixel; existing Noto glyphs retain their original four-bit
coverage. Font selection and baseline alignment are shared by measurement,
normal drawing, marquee clipping, and ellipsis drawing.

## Reproducible assets

The source URL and SHA-256 are pinned in
[`tools/fonts/manifest.json`](../tools/fonts/manifest.json). To regenerate font
assets only, use Python with Pillow 12.3.0 and fonttools 4.60.1:

```sh
python tools/fonts/generate_cjk.py --source SourceHanSerifSC-Medium.otf
python tools/fonts/generate_cjk.py --verify
```

`--preview preview.png` renders sample filenames from the actual packed assets.
These commands do not compile the launcher. Verification checks checksums,
format bounds, equal coverage at both sizes, representative simplified and
traditional titles, and a 2.5 MB font asset budget.

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
