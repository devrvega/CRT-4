#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The development tools against the engine as it is now: build/host/note, names and dump_sounds, and
# build/tools/spec (what tools/dev/sheet.py runs). The sample bank comes first (tools/bank/gen.sh).
set -e
cd "$(dirname "$0")/../.."
sh tools/bank/gen.sh
mkdir -p build/host build/tools
for t in note names dump_sounds; do
    cc -O2 -ffp-contract=off -std=c99 -Wall -Wno-unused-function -DEN_HOST -Ifirmware/src/dsp -Ibuild/gen \
        -o build/host/$t tools/dev/$t.c firmware/src/dsp/crt4.c -lm
done
cc -O2 -std=c99 -Wall -o build/tools/spec tools/dev/spec.c -lm
