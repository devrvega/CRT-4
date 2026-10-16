#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""docs/img/crt4.gif: the README's animation, from the simulator. The nine consoles in turn, each playing its
demo for a moment, a frame every 100 ms (each frame its own palette: the consoles' differ).

  tools/readme_gif.py        (builds the simulator first; needs Pillow)"""
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

SRC = Path(__file__).resolve().parents[1]
STEP_MS = 100
# what happens, and for how long: (commands before, frames)
PLAN = [(["tap PLAY", "wait 500"], 12)]                # the NES (the console it starts on)
for d in (-2, 1, 2, 1, 1, 1, 1, 1):                   # the 2600, the C64, then the PC Engine .. the N64
    PLAN.append(([ "tap PLAY", "press GLO", f"turn ALGO {d}", "release GLO", "wait 2400", "tap PLAY", "wait 500"], 12))


def main():
    subprocess.run(["sh", str(SRC / "host" / "build_host.sh")], check=True, capture_output=True)
    with tempfile.TemporaryDirectory() as tmp:
        lines, n = ["wait 2600"], 0
        for cmds, frames in PLAN:
            lines += cmds
            for _ in range(frames):
                lines += [f"wait {STEP_MS}", f"shot f{n:03d}.png"]
                n += 1
        script = Path(tmp) / "gif.crt"
        script.write_text("\n".join(lines) + "\n")
        subprocess.run([str(SRC / "build" / "host" / "crt4_host"), str(script), tmp], check=True, capture_output=True)
        frames = [Image.open(Path(tmp) / f"f{i:03d}.png").convert("RGB") for i in range(n)]
    out = [f.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    dest = SRC / "docs" / "img" / "crt4.gif"
    out[0].save(dest, save_all=True, append_images=out[1:], duration=STEP_MS, loop=0, optimize=True)
    print(f"{dest}: {n} frames, {dest.stat().st_size // 1024} KB")


if __name__ == "__main__":
    sys.exit(main())
