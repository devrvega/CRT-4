# Changelog

Each release's notes come from its section here (`tools/release_notes.py`). The newest is first.

## 0.1.2 — 2026-10-16

- **Fixed:** safe mode can be reached over USB again after a crash. If the FM-1 crashed while it was making sound,
  safe mode came up with USB off, so neither the web installer nor M-UPGRADE could find it until it was switched
  off and on. (The same fix as SEQ-4 0.1.0's; tried on an FM-1: two crashes on purpose, safe mode, then an install
  from it.) Nothing else changes.

## 0.1.1 — 2026-10-16

- **The site:** the browser emulator, the web installer and the manual are published with this release.
  0.1.0's site builder still looked for LCD-4's identity and stopped. The firmware is 0.1.0's, apart from its
  version.

## 0.1.0 — 2026-10-16

The first release.

- **Nine consoles:** the Atari 2600 (TIA), the Commodore 64 (SID), the NES (2A03 and the Famicom's VRC6, FDS,
  N163 and 5B), the PC Engine (HuC6280, the CD-ROM's ADPCM), the Genesis (YM2612 and SN76489), the Game Boy,
  the SNES (S-DSP), the PlayStation (SPU) and the N64, each made the way its chip made sound: its channels,
  its sample format and rate, its interpolation, its envelopes, its rounding.
- **Two kits and sixteen sounds a console**, in rows of basses, keys, leads and the console's own; samples
  made by a program in the source and stored in each console's format (no recordings).
- **A song a console:** sixteen 16-step patterns holding all four tracks, chains of up to 64, chords, swing,
  the console's sounds and knobs; switching consoles saves one song and brings back the other's.
- **The consoles' own effects:** the SNES's echo and its FIR, the PlayStation's reverb presets, the N64's
  rooms, echo, chorus and flange; on the chip consoles the trackers' echo, their region (NTSC, PAL), their
  model (the 6581 or the 8580, the Genesis's Model 1 or 2, the Game Boy's three) and the TV's speaker.
- **Era-true chords:** a note at a time on the 2600, the C64, the NES and the Game Boy, chords played as
  arpeggios a frame a note; real chords on the others.
- **Writing as on LCD-4:** live, step by step, on the steps (locks, slides, ratchets, accents); sixteen
  punch-in effects, the last four the consoles' own (fade, pitch dive, vibrato, echo).
- **The screen dresses as the console:** its palette and lettering, a scene of its time with Pip in it
  (a shooter, a scroller, a side-scroller, a city at night, a route, a Mode 7 world map, a neon tunnel, an
  island), its own power-on, and a shelf to choose consoles from (GLO + ALGORITHM).
- **Nine demo songs**, one a console. MIDI in and out, MIDI clock.
