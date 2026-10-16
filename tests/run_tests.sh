#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CRT-4 host tests: the maths, the platform pieces kept from Felucca / X0X / AMB-1 / IND-1, the instrument,
# and the whole firmware app in the simulator (tests/scenarios/*.crt: the pages, the keys, patterns and
# chains, chords, effects, writing, persistence), with screenshots and audio in build/scenarios/.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FAIL=0
OUT=build/host
mkdir -p "$OUT" build/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run fastmath sh -c "$CC -O2 -ffp-contract=off -Wall -Wextra -Werror -o $OUT/fastmath_test tests/host/fastmath_test.c -lm && $OUT/fastmath_test"
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run uac sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/uac_test tests/host/uac_test.c -lm && $OUT/uac_test"
run trs sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/trs_test tests/host/trs_test.c && $OUT/trs_test"
sh tools/bank/gen.sh >/dev/null || { echo "  FAIL the sample bank (tools/bank)"; FAIL=1; }
run crt4 sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Ifirmware/src/dsp -Ibuild/gen \
    -o $OUT/crt4_test tests/host/crt4_test.c firmware/src/dsp/crt4.c -lm && $OUT/crt4_test"
run storage sh -c "$CC -O2 -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
# the update path, against the firmware package (Felucca's tests; needs ./build.sh)
if [ -f build/crt4.fwsc ]; then
    run ota-entry sh -c "$CC -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test build/crt4.fwsc"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip update-loader (needs AC79_SDK, as the build)"
    else
    run update-loader sh -c "head -c 100000 build/crt4.bin > $OUT/old_app.bin && \
        python3 tools/fm1pkg_make.py $OUT/old_app.bin build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc build/crt4.fwsc"
    fi
    run installer python3 tests/install_test.py
else
    echo "  skip update-path tests (no build/crt4.fwsc: run ./build.sh)"
fi
run host-build sh host/build_host.sh
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "sh web/emu/build.sh >/dev/null && node tests/host/emu_test.mjs build/emu/crt4.wasm"
fi
for s in tests/scenarios/*.crt; do
    n=$(basename "$s" .crt)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/crt4_host "$s" "build/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
