#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the CRT-4 site (a static folder: GitHub Pages or any web server serves it as it is):

  index.html                  what CRT-4 is, and the ways in: try, install, manual, download, source
  img/                        screenshots (docs/img)
  emu/                        CRT-4 in the browser (build/emu, from web/emu/build.sh)
  install/index.html          the web installer (web/crt4_installer.html, with fm1pkg.js, fm1ota.js and
                              the package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html, img/     the manual (tools/manual_html.py, from docs/MANUAL.md)
  firmware/crt4-VERSION.fwsc   the package the installer writes; also the download
  source/                     the complete source (GPL-3.0: whoever gets the firmware gets the source),
                              as a tarball of this tree; and CRT4_REPO, if set, the repository's address

  [CRT4_REPO=https://...] tools/make_pages.py build/crt4-0.1.fwsc 0.1 OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_2XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)

REPO = os.environ.get("CRT4_REPO", "")

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CRT-4</title>
<meta name="description" content="A four-track groovebox for the M-VAVE FM-1 that plays nine game consoles' sound chips: the 2600, the C64, the NES, the PC Engine, the Genesis, the Game Boy, the SNES, the PlayStation and the N64, each with its kits, sixteen sounds, its own echo or reverb and a screen of its era.">
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3E%3Crect width='16' height='16' rx='3' fill='%23101014'/%3E%3Crect x='2' y='3' width='12' height='9' rx='2' fill='%2338c4e8'/%3E%3Crect x='5' y='13' width='6' height='2' fill='%2338c4e8'/%3E%3C/svg%3E">
<style>
/* CRT-4's colours: a dark body, a CRT's glow, the four tracks' orange, cyan, yellow and pink */
:root { --paper: #121216; --sheet: #23232a; --ink: #eae8f0; --muted: #9c9ca8; --crt: #0b0b12; --rule: #36363f;
        --t1: #ff7a28; --t2: #38c4e8; --t3: #f6cc28; --t4: #ec54a0; color-scheme: dark; }
body { background: var(--paper); color: var(--ink); margin: 0; padding: 40px 16px 64px; font: 17px/1.6 "Helvetica Neue", Helvetica, Arial, sans-serif; }
main { max-width: 46rem; margin: 0 auto; }
h1 { font: 700 3rem/1.1 ui-monospace, Menlo, monospace; letter-spacing: .06em; margin: 0; display: inline-block; background: var(--crt);
     color: var(--t2); padding: .15em .5em; border-radius: 12px; box-shadow: 0 0 0 4px #2a2a33, 0 0 24px rgba(56, 196, 232, .25);
     text-shadow: 0 0 8px rgba(56, 196, 232, .6); }
.stripes { display: flex; gap: 6px; margin: .9rem 0 0; } .stripes span { width: 44px; height: 7px; border-radius: 2px; }
.sub { color: var(--muted); margin: .8rem 0 1.4rem; font-size: 1.1rem; }
.lede { font-size: 1.12rem; }
.shots { display: flex; flex-wrap: wrap; gap: 12px; margin: 1.6rem 0; }
.shots img { width: calc((100% - 24px) / 3); min-width: 150px; height: auto; image-rendering: pixelated; border-radius: 6px; }
h2 { font-size: 1.15rem; margin: 2.2rem 0 .6rem; padding-top: .6rem; border-top: 1px solid var(--rule); }
.ways { display: grid; grid-template-columns: repeat(auto-fit, minmax(13rem, 1fr)); gap: 12px; margin: 1.6rem 0; }
.ways a { display: block; padding: .8rem 1rem; background: var(--sheet); border: 1px solid var(--rule); border-radius: 8px; text-decoration: none; color: var(--ink); }
.ways a:first-child { background: var(--t2); color: #08080c; }
.ways a strong { display: block; } .ways a span { display: block; font-size: .9rem; opacity: .8; }
.ways a:hover, .ways a:focus-visible { border-color: var(--t2); outline: none; }
.v { font-style: normal; font-weight: 400; opacity: .8; }
table.cons { border-collapse: collapse; width: 100%; font-size: .95rem; }
table.cons td { padding: .35rem .5rem; border-bottom: 1px solid var(--rule); vertical-align: top; }
table.cons td:first-child { font-weight: 700; white-space: nowrap; }
.small { color: var(--muted); font-size: .9rem; }
a { color: var(--ink); text-decoration-color: var(--t2); text-decoration-thickness: 2px; text-underline-offset: 3px; }
code { font-size: .88em; }
</style>
</head>
<body>
<main>
<h1>CRT-4</h1>
<div class="stripes"><span style="background:var(--t1)"></span><span style="background:var(--t2)"></span><span style="background:var(--t3)"></span><span style="background:var(--t4)"></span></div>
<p class="sub">Nine consoles, four tracks, for the M-VAVE FM-1</p>
<p class="lede">A four-track groovebox that plays the sound chips of nine game consoles and home computers. Choose a
console and you play it: its drums and sixteen of its sounds, made the way its chip made them, with its own echo or
reverb, while the whole screen dresses as the console, with a scene of its time that moves with the music. Each
console keeps a song of its own: 16-step patterns, chains, chords and punch-in effects.</p>
<div class="shots">
  <img src="img/screen-nes.png" width="240" height="240" alt="CRT-4's screen on the NES: a side-scroller, Pip jumping on the kick">
  <img src="img/screen-snes.png" width="240" height="240" alt="CRT-4's screen on the SNES: an airship over a turning world map">
  <img src="img/screen-ps1.png" width="240" height="240" alt="CRT-4's screen on the PlayStation: a race through a neon tunnel">
</div>
<nav class="ways" aria-label="Get CRT-4">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, with sound. Mouse, touch or keyboard; no FM-1 needed.</span></a>
  <a href="install/"><strong>Install <em class="v">v__VERSION__</em></strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="manual/"><strong>Manual</strong><span>Getting started in a few minutes, then every console, sound and page.</span></a>
  <a href="firmware/__PKG__"><strong>Download <em class="v">__PKG__</em></strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="source/"><strong>Source</strong><span>GPL-3.0. Built on Felucca by Hügelton Instruments, by way of X0X, AMB-1, IND-1 and LCD-4.</span></a>
</nav>

<h2>The consoles</h2>
<table class="cons">
<tr><td>Atari 2600</td><td>The TIA's dividers and polynomial counters, its notes only near the scale. Buzz, Drone, Siren, Laser, Engine.</td></tr>
<tr><td>C64</td><td>The SID (6581 or 8580): combined waveforms, ring, sync, its filter; 4-bit digis. PWM leads, filter basses, ring bells.</td></tr>
<tr><td>NES</td><td>The 2A03 and the Famicom's VRC6, FDS, N163 and 5B. The triangle bass, DMC bass, arpeggios, an echo lead.</td></tr>
<tr><td>PC Engine</td><td>Six 5-bit wavetables, the LFO, DDA drums; the CD-ROM's ADPCM strings, choir and guitar.</td></tr>
<tr><td>Genesis</td><td>The YM2612's four operators and its gritty DAC, the SN76489. Slap and house basses, E.Piano, a screaming lead, an orchestra hit.</td></tr>
<tr><td>Game Boy</td><td>Two pulses, the 4-bit wave channel, the noise; hard left and right. Wave basses, a wobble, a ping-pong echo, speech.</td></tr>
<tr><td>SNES</td><td>BRR samples at 32 kHz, the Gaussian interpolation, the echo and its FIR. Strings, choir, slap bass, flute, harp.</td></tr>
<tr><td>PlayStation</td><td>SPU ADPCM at 44.1 kHz and its reverb's kinds; a sliced breakbeat. Reese, space pad, gothic organ, Rhodes, sitar.</td></tr>
<tr><td>N64</td><td>ADPCM mixed at 22 kHz, its rooms, echo, chorus and flange. Tuba, marimba, ocarina, accordion, steel drum.</td></tr>
</table>
<p>Two kits and sixteen sounds a console, a song a console; on the 2600, the C64, the NES and the Game Boy a chord
plays as an arpeggio, as their music did. No recordings: every sample is made by the program in the source, in the
console's own format. Sixteen patterns, chains of up to 64; sixteen chords and chains of them; effects held or
written onto the steps; parameter locks, ratchets, slides and swing; writing live or step by step. Nine demo songs.</p>
<p class="small">To go back to the official firmware, use the installer's "Back to the stock firmware", or
M-VAVE's updater, M-UPGRADE, from <a href="https://www.m-vave.com/download">m-vave.com/download</a>.
M-VAVE and FM-1 are trademarks of their owners, as are the consoles' names. CRT-4 is independent: it is not
affiliated with M-VAVE or the makers of the consoles and chips its sounds are modelled on, and its music, samples
and art are its own.</p>
</main>
</body>
</html>
"""

SOURCE = """<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>CRT-4 source</title>
<style>body { background: #161619; color: #eae8e0; font: 17px/1.6 "Helvetica Neue", Helvetica, Arial, sans-serif; margin: 48px 16px; }
main { max-width: 42rem; margin: 0 auto; } a { color: #eae8e0; text-decoration-color: #38c4e8; text-decoration-thickness: 2px; }</style></head>
<body><main><h1>CRT-4 source</h1>
<p>CRT-4 is free software under the GNU General Public License, version 3 only. The complete source of v__VERSION__:
<a href="__TAR__">__TAR__</a>.__REPOLINE__</p>
<p><a href="../">Back to CRT-4</a></p></main></body></html>
"""


def source_tarball(dest, version):
    """the tree as it is (no build output, no caches): what the GPL asks to give with the firmware"""
    name = f"crt-4-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}-src"
    skip = {"build", "__pycache__", ".git", ".DS_Store", ".serena"}

    def keep(ti):
        return None if set(Path(ti.name).parts) & skip else ti
    with tarfile.open(dest / f"{name}.tar.gz", "w:gz") as t:
        t.add(SRC, arcname=name, filter=keep)
    return f"{name}.tar.gz"


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_2\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"crt4-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "manual", "firmware", "source"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    # the installer
    page = (SRC / "web" / "crt4_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"crt4_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    # the manual (a complete page; its images beside it)
    subprocess.run([sys.executable, str(SRC / "tools" / "manual_html.py"), str(SRC / "docs" / "MANUAL.md"),
                    str(out / "manual" / "index.html")], check=True, capture_output=True)
    shutil.copytree(SRC / "docs" / "img", out / "manual" / "img")
    # the landing page and its screenshots
    shutil.copytree(SRC / "docs" / "img", out / "img")
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name),
                                    encoding="utf-8")
    # the source, for the GPL
    tar = source_tarball(out / "source", version)
    repo = f' The repository: <a href="{html.escape(REPO)}">{html.escape(REPO)}</a>.' if REPO else ""
    (out / "source" / "index.html").write_text(SOURCE.replace("__VERSION__", html.escape(version)).replace("__TAR__", tar)
                                               .replace("__REPOLINE__", repo), encoding="utf-8")
    emu = SRC / "build" / "emu"
    if not (emu / "crt4.wasm").exists():
        raise SystemExit("no build/emu/crt4.wasm: run web/emu/build.sh (needs Emscripten)")
    shutil.copytree(emu, out / "emu")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), manual/, emu/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
