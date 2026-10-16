# CRT-4 licensing

CRT-4 is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on LCD-4, IND-1 and AMB-1, which are built on Felucca's
platform layer (through X0X and FoMni), and keeps their licence. If you distribute CRT-4, or firmware derived
from it, you must give your recipients its complete corresponding source under the same licence
(`tools/make_pages.py` puts a tarball of the source on the site it makes, next to the firmware).

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x), FoMni and [AMB-1](https://github.com/charlesvestal/fm1-amb), then IND-1, LCD-4 and CRT-4 | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1,project}.c`, `firmware/src/app/ui.c` (its input, knob and drawing code), `host/crt4_host.c`, `web/emu/`, `web/crt4_installer.html`, `tools/{make_pages,manual_html,gen_manual_figures}.py` | adapted from LCD-4, IND-1 and AMB-1 (themselves from X0X, from Felucca's `panel.c`, `main.c`, `audio.c`) | GPL-3.0-only |
| In `firmware/src/dsp/`: the command queue, the clock and transport, the sequencer (patterns, chains, chords, the punch-in effects), the sine table, the noise, polyBLEP and the state-variable filter | from LCD-4, IND-1 and AMB-1 | GPL-3.0-only |
| The idea of a browser emulator for FM-1 firmware (`web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| Everything else in `firmware/src/{app,dsp}` (the nine consoles' engines, their sounds and kits, their effects and presets, the voices and the music driver, the demo songs, the themes, the scenes and their art, the 3D renderer), `tools/bank/` (the sample bank's generator, its instrument models and the consoles' encoders), `assets/fonts/crt8.txt` (the pixel font), `tests/` | CRT-4 | GPL-3.0-only |

**The samples.** CRT-4's samples (`build/gen/crt4_bank.h`, made at build time) are the output of
`tools/bank/gen_bank.c`, which synthesizes every one of them (no recordings, no sampled instruments, no
material from any game) and encodes it in the consoles' formats. The program is their source.

**The consoles' hardware.** The engines follow the chips' public documentation (how a TIA divides, how a
BRR block decodes, how an SPU reverb's buffer is laid out, and so on); they contain no code, firmware, ROM
contents or data of the consoles' makers or of their development kits. Where a chip's behaviour rests on a
table (the S-DSP's Gaussian interpolation, a reverb's presets), CRT-4 computes or designs its own.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the smooth face (the PlayStation's and the N64's screens) | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
"M-VAVE" and "FM-1" are trademarks of their respective owners, as are the names of the consoles, computers and
chips CRT-4's sounds are modelled on (among them Atari, Commodore, Nintendo, NES, Famicom, Game Boy, Super
Nintendo, SNES, Nintendo 64, NEC, PC Engine, TurboGrafx, Hudson, Sega, Genesis, Mega Drive, Sony,
PlayStation, Yamaha, Konami, Namco, Sunsoft, OKI). CRT-4 is independent firmware; it is not affiliated with,
endorsed by or supported by any of them. The names are used only to say what a sound is modelled on; CRT-4
contains no samples, code, music, logos or artwork of theirs, and its scenes, power-on screens and
characters are its own.

## Radio

CRT-4 never enables the Bluetooth / Wi-Fi radio of the hardware.
