#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CRT-4's sample bank: build the generator and run it, if build/gen/crt4_bank.h is older than its source.
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
mkdir -p build/gen build/tools
out=build/gen/crt4_bank.h
if [ ! -f "$out" ] || [ -n "$(find tools/bank -newer "$out" -name '*.[ch]' 2>/dev/null)" ]; then
    $CC -O2 -o build/tools/gen_bank tools/bank/gen_bank.c -lm && build/tools/gen_bank "$out" 2>build/tools/gen_bank.log ||
        { cat build/tools/gen_bank.log; exit 1; }
    tail -11 build/tools/gen_bank.log
fi
