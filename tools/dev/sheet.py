#!/usr/bin/env python3
"""A contact sheet of every sound of a console: each played alone (a note held, let go), its spectrogram,
its name, its peak. For looking at the sounds when they can't be heard.
  tools/dev/sheet.py CON OUT.png [note]"""
import subprocess, sys, os
from PIL import Image, ImageDraw
con = int(sys.argv[1]); out = sys.argv[2]; note = sys.argv[3] if len(sys.argv) > 3 else "60"
W, H = 300, 120
tiles = []
names = subprocess.run(["build/host/names", str(con)], capture_output=True, text=True).stdout.splitlines()
for s in range(16 + 2):
    if s < 16:
        args = ["build/host/note", str(con), str(s), note, "0.7", "1.4", "build/host/t.wav"]
        label = names[s] if s < len(names) else str(s)
    else:
        tiles_kit = []
        continue
    r = subprocess.run(args, capture_output=True, text=True).stdout.strip()
    subprocess.run(["build/tools/spec", "build/host/t.wav", "build/host/t.ppm", str(W), str(H)])
    im = Image.open("build/host/t.ppm").convert("RGB")
    d = ImageDraw.Draw(im)
    d.text((4, 2), f"{s} {label}", fill=(255, 255, 255))
    d.text((4, H - 12), r, fill=(200, 255, 200))
    tiles.append(im)
# the kits: each part in turn
for k in range(2):
    for p in range(11):
        args = ["build/host/note", str(con), str(-(k * 16 + p + 1)), "60", "0.1", "0.9", "build/host/t.wav"]
        r = subprocess.run(args, capture_output=True, text=True).stdout.strip()
        subprocess.run(["build/tools/spec", "build/host/t.wav", "build/host/t.ppm", str(W // 2), str(H)])
        im = Image.open("build/host/t.ppm").convert("RGB")
        d = ImageDraw.Draw(im)
        lab = names[16 + k * 11 + p] if 16 + k * 11 + p < len(names) else f"k{k}p{p}"
        d.text((2, 2), lab, fill=(255, 255, 255))
        d.text((2, H - 12), r.split()[1] if r else "", fill=(200, 255, 200))
        tiles.append(im)
cols = 4
rows16 = 4
sheet = Image.new("RGB", (max(cols * (W + 4), 11 * (W // 2 + 3)), rows16 * (H + 4) + 2 * (H + 4)), (30, 30, 30))
for i in range(16):
    sheet.paste(tiles[i], ((i % cols) * (W + 4), (i // cols) * (H + 4)))
for i in range(22):
    x = (i % 11) * (W // 2 + 3); y = rows16 * (H + 4) + (i // 11) * (H + 4)
    sheet.paste(tiles[16 + i], (x, y))
sheet.save(out)
