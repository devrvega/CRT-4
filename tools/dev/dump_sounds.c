/* SPDX-License-Identifier: GPL-3.0-only */
/* The consoles' sounds, kits and FX pages as Markdown tables, for the manual (tools/dev/rebuild.sh builds
 * it: build/host/dump_sounds). */
#include <stdio.h>
#include "crt4.h"
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static void knobs(const char *const *k)
{
    int i, n = 0;
    for (i = 0; i < 4; i++)
        if (k[i][0])
            printf("%s%s", n++ ? ", " : "", k[i]);
}
int main(void)
{
    int c, s, k, p;
    static const char *const ROW[4] = {"Bass", "Keys", "Leads", "Its own"};
    for (c = 0; c < EN_NCON; c++) {
        const en_console_t *C = en_console(c);
        printf("<!-- console %d -->\n", c);
        printf("| | Sound | Made of | Knobs (A, B, C, D) |\n|---|---|---|---|\n");
        for (s = 0; s < EN_NSOUND; s++) {
            const en_sound_t *S = en_sound(c, s);
            printf("| %s | **%s** | %s | ", s % 4 == 0 ? ROW[s / 4] : "", S->name, S->after);
            knobs(S->knob);
            printf(" |\n");
        }
        printf("\n| Black key | ");
        for (k = 0; k < EN_NKIT; k++)
            printf("%s%s", en_kit_name(c, k), k + 1 < EN_NKIT ? " | " : " |\n");
        printf("|---|---|---|\n");
        for (p = 0; p < EN_NPART; p++) {
            printf("| %d | ", p + 1);
            for (k = 0; k < EN_NKIT; k++)
                printf("%s%s", en_part_long(c, k, p), k + 1 < EN_NKIT ? " | " : " |\n");
        }
        printf("\nFX page: ");
        knobs(C->fx_knob);
        printf("\n\n");
    }
    return 0;
}
