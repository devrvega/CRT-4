/* SPDX-License-Identifier: GPL-3.0-only */
/* A console's sounds, then its kits' parts, a name a line (for tools/dev/sheet.py). */
#include <stdio.h>
#include <stdlib.h>
#include "crt4.h"
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
int main(int argc, char **argv)
{
    int c = atoi(argv[1]), s, k, p;
    for (s = 0; s < EN_NSOUND; s++) printf("%s\n", en_sound(c, s)->name);
    for (k = 0; k < EN_NKIT; k++) for (p = 0; p < EN_NPART; p++) printf("%s\n", en_part_name(c, k, p));
    return 0;
}
