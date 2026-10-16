# Building CRT-4

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `crt4.bin` | the firmware app (the sample bank in it) |
| `loader/ota.bin` | the update loader |
| `crt4.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- A C compiler for the computer itself (`cc`: Xcode's command line tools on a Mac; gcc on Linux): it builds
  the sample bank's generator (`tools/bank`), which makes every sample when the firmware is built.
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- Three files of the JieLi AC79 SDK (Apache-2.0): `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin` and
  `cfg/eq_cfg_hw.bin`. They are not part of this tree; this fetches just them, from JieLi's repository or a
  GitHub mirror of it, and keeps them only if their SHA-256 is the one the build expects:

  ```
  tools/get_sdk_files.sh            # into ~/fw-AC79_AIoT_SDK
  ```

  (or the whole SDK: `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13
  https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK`)

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.1` makes a release build; the package is `build/crt4-0.1.fwsc`.

`./build.sh --bench` makes a development build (`build/crt4-bench.fwsc`) that also takes heavy test songs
over SysEx, to measure the audio load on the FM-1 itself. Every build answers the load query: send
`F0 7D 43 34 01 F7` and the FM-1 replies `F0 7D 43 34 01` with these, each as five 7-bit groups:
- the load (%)
- the slowest render (µs, of the 5805 µs a 256-frame block has)
- late renders and renders
- the bench flag
- the time each part of the engine took (sequencer, drums, tracks 2–4, the console's effect, the output, the
  rest)
- the core's clock, the transport, the steps and the MIDI messages in
- the screen's frames, the time they took and the slowest

`F0 7D 43 34 03 F7` clears them. A bench build also takes:

| SysEx | What it does |
|---|---|
| `F0 7D 43 34 02 k F7` | Loads test song k on the console in use. 0: three tracks of four-note chords of its heaviest sounds, and every drum on every step. 1: its demo. |
| `06 p` | Plays (1) or stops (0). |
| `08` | Saves the console's demo. |
| `09 c` | Switches to console c, as the shelf does. |
| `0A` | Times each part of a screen frame (ticks of 24 MHz), and the wait for the LCD's transfers. |
| `0B` | The slowest render since `03`: its parts, then its whole. |

On macOS with podman instead of Docker, put a `docker` script that runs `exec podman "$@"` first
on your PATH. `EN_JOBS` (default 4) limits parallel compiles: a podman machine drops
connections when many containers start at once.

Build option: `EN_CDC=1` adds Felucca's USB serial function (off by default: one plain
MIDI interface).

The build also generates `build/gen/`: the font, and `crt4_bank.h`, the sample bank (`tools/bank/gen.sh`
builds `tools/bank/gen_bank.c` with the computer's `cc` and runs it: about a second; it prints each console's
bank size against its budget). It fails if a soft-double routine is linked
(a `double` crept in) and checks the image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh
```

Runs, on the build machine: the maths library against libm; the instrument (`tests/host/crt4_test.c`:
every console's settings and demo (their levels), validation, tempo and length, pattern switching, chains
and chords and following them, ratchets and swing, every punch-in effect, live writing (notes, chords,
lengths, slides, drum hits), MIDI clock in and out, mutes; on every console, every kit's every part and
every sound at its knobs' extremes, bounded, and quiet again after); flash storage; Felucca's update-path
tests against `build/crt4.fwsc`; and the whole app in the simulator (`tests/scenarios/*.crt`: the first
steps, the pages, the consoles and their songs), with its screenshots and audio in `build/scenarios/`.

`host/build_host.sh` builds the simulator alone; `build/host/crt4_host SCRIPT OUTDIR` runs one
script (the command list is at the top of `host/crt4_host.c`).

- `build/host/crt4_host tests/demo.crt build/demo`: the nine demo songs, as WAV files (not part of the
  tests).
- `tools/dev/`: looking at sounds without hearing them: `note.c` (one sound as a WAV), `spec.c` (a WAV's
  spectrogram), `sheet.py` (every sound and drum of a console in one picture), `dump_sounds.c` (the manual's
  tables). `tools/dev/rebuild.sh` builds them.
- `tools/manual_shots.sh`: the manual's screenshots (`tests/manual.crt`) into `docs/img`, and the panel
  drawing (`tools/gen_manual_figures.py`).

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/crt4.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for Felucca's update
mode. If the FM-1 no longer starts but reaches the chip's update mode (4C4A:8057 on USB),
`bash tools/fm1_rescue.sh` puts stock firmware back from a Mac; otherwise recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## On GitHub: builds, releases, the site

Three workflows in `.github/workflows/` (they work the same in a fork):

- **Build and test** (every push and pull request): the toolchain and the SDK files fetched (and cached), the
  firmware built, `tests/run_tests.sh` run (with the browser build), the package kept as an artifact.
- **Release** (a tag `vX.Y.Z`): the tests, then `./build.sh --release X.Y.Z` (identity `FM-1_2XXYYZZ`, the
  version on the FM-1's screen), and a GitHub release with `crt4-X.Y.Z.fwsc`, its notes taken from
  `CHANGELOG.md`.
- **Pages** (after a release, or by hand): the site for the latest release on GitHub Pages: the landing page,
  the browser emulator, the web installer with the release's package, the manual, and the complete source as
  a tarball (as the GPL asks). In a fork, set **Settings → Pages → Source** to **GitHub Actions** first.

To release: add the version's section to `CHANGELOG.md`, then `git tag vX.Y.Z && git push origin vX.Y.Z`.

The same by hand:

1. `./build.sh --release X.Y.Z` (the identity, FM-1_2XXYYZZ, is what the installer checks; Polly's is
   FM-1_3..., LCD-4's FM-1_4..., IND-1's FM-1_5..., CTL-1's FM-1_6..., AMB-1's FM-1_7..., FiMba's FM-1_8...,
   X0X's FM-1_9...).
2. `web/emu/build.sh` (needs Emscripten) for the browser emulator.
3. `[CRT4_REPO=https://...] python3 tools/make_pages.py build/crt4-X.Y.Z.fwsc X.Y.Z OUT_DIR`: the site in a
   static folder, for any web server. The web installer needs it served over HTTPS (Web MIDI). The
   manual's source is `docs/MANUAL.md` (`tools/manual_html.py` turns it into the page).
4. `tools/readme_gif.py` remakes the README's animation, `tools/manual_shots.sh` the manual's screenshots.

## On the FM-1: how it's doing

`python3 tools/fm1_status.py load` asks a connected FM-1 running CRT-4 for its audio load and where the
time goes; `play` for what it's playing, `store` for what's saved, `clock` to play it from the computer by
MIDI clock while measuring. It only asks; it never changes anything on the FM-1.
