/* The club registry (sr_reg.c; MC 42-s7reg): one league across N team files. Built cases: one file (identity; and
 * Italy 4.1's one-file layout over 1000 random summers: no swap, every word kept), two files (C <-> D across the
 * boundary: the exe's default, only the clubs that change file move; contiguous, every division in its rows), three
 * files, the pool rows and the off-file pool, the limits (a full
 * file, a division in two files, counts that don't add up), and the old-save import on the Mod's italy42 map
 * (tests/rules/italy42_map.h: every word resolved, Serie A / B / C's ordinals of file 102 unchanged). */
#include <stdio.h>
#include <string.h>
#include "swosrules.h"
#include "italy42_map.h"
static sr_registry R;
static sr_reg_work WK;
static int fails;
#define FAIL(...) do { if (fails++ < 20) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define W(f, o) ((uint16_t)((f) | (o) << 8))

static sr_reg_file file(int num, int ndiv, const int *div, const int *size, int pool)
{
    sr_reg_file f;
    memset(&f, 0, sizeof f);
    f.file = (uint8_t)num; f.ndiv = (uint8_t)ndiv; f.pool = (uint16_t)pool;
    for (int k = 0; k < ndiv; k++) f.div[k] = (uint8_t)div[k], f.size[k] = (uint8_t)size[k];
    return f;
}
/* each club where nd says, the registry consistent */
static void placed(const char *what, const uint8_t *nd)
{
    if (sr_reg_check(&R)) FAIL("%s: the registry's invariants", what);
    for (int c = 0; c < R.nclub; c++)
        if (sr_reg_div(&R, c) != nd[c]) FAIL("%s: club %d in division %d, not %d", what, c, sr_reg_div(&R, c), nd[c]);
}
static void kept(const char *what, const uint8_t *od, const uint8_t *nd, const uint16_t *before)
{
    for (int c = 0; c < R.nclub; c++)
        if (od[c] == nd[c] && od[c] != SR_REG_OUT && R.word[c] != before[c])
            FAIL("%s: club %d stayed in division %d but moved %04X -> %04X", what, c, nd[c], before[c], R.word[c]);
}
static void apply(const sr_reg_swap *sw, int n, uint16_t *slot_of, int nclub)   /* the exe's side: records moved */
{
    for (int i = 0; i < n; i++) {
        int ca = -1, cb = -1;
        for (int c = 0; c < nclub; c++) {
            if (slot_of[c] == sw[i].a) ca = c;
            if (sw[i].b != SR_REG_NONE && slot_of[c] == sw[i].b) cb = c;
        }
        if (sw[i].b == SR_REG_NONE) { if (ca >= 0) slot_of[ca] = SR_REG_NONE; slot_of[sw[i].on] = sw[i].a; continue; }
        if (ca >= 0) slot_of[ca] = sw[i].b;
        if (cb >= 0) slot_of[cb] = sw[i].a;
    }
}

int main(void)
{
    sr_reg_swap sw[600];
    uint8_t div[SR_REG_CLUBS], nd[SR_REG_CLUBS];
    uint16_t before[SR_REG_CLUBS], order[SR_REG_CLUBS];
    {   /* one file: identity, nothing moves in a season without moves */
        const int d[3] = { 0, 1, 2 }, s[3] = { 4, 4, 4 };
        sr_reg_file f = file(90, 3, d, s, 0);
        for (int c = 0; c < 12; c++) div[c] = (uint8_t)(c / 4);
        if (sr_reg_build(&R, 1, &f, 12, div)) FAIL("one file: refused");
        for (int c = 0; c < 12; c++) if (R.word[c] != W(90, c) || sr_reg_ident(&R, W(90, c)) != c) FAIL("one file: club %d at %04X", c, R.word[c]);
        if (sr_reg_season(&R, div, NULL, 0, 1, &WK, sw, 600) != 0) FAIL("one file: swaps without a move");
    }
    {   /* two files: A = divisions 0, 1; B = 2, 3 (4 clubs each). 0's 4th <-> 1's 1st, 1's 4th <-> 2's 1st (across),
         * 2's 3rd, 4th <-> 3's 1st, 2nd: the best promoted into the best placed relegated's row */
        const int da[2] = { 0, 1 }, db[2] = { 2, 3 }, s[2] = { 4, 4 };
        sr_reg_file f[2] = { file(102, 2, da, s, 0), file(144, 2, db, s, 0) };
        for (int c = 0; c < 16; c++) div[c] = (uint8_t)(c / 4), order[c] = (uint16_t)c;
        if (sr_reg_build(&R, 2, f, 16, div)) FAIL("two files: refused");
        memcpy(nd, div, 16);
        nd[3] = 1; nd[4] = 0; nd[7] = 2; nd[8] = 1; nd[10] = 3; nd[11] = 3; nd[12] = 2; nd[13] = 2;
        for (int c = 0; c < 16; c++) before[c] = R.word[c];
        const int n = sr_reg_season(&R, nd, order, 16, 1, &WK, sw, 600);
        placed("two files", nd); kept("two files", div, nd, before);
        if (R.word[8] != W(102, 7) || R.word[7] != W(144, 0)) FAIL("two files: across the boundary %04X %04X", R.word[8], R.word[7]);
        if (R.word[12] != W(144, 2) || R.word[13] != W(144, 3)) FAIL("two files: the best promoted not in the best relegated's row");
        int across = 0;
        for (int i = 0; i < n; i++) across += (sw[i].a & 0xFF) != (sw[i].b & 0xFF);
        if (n != 4 || across != 1) FAIL("two files: %d swaps, %d across (4, 1)", n, across);
        uint16_t slot[16];
        for (int c = 0; c < 16; c++) slot[c] = before[c];
        apply(sw, n, slot, 16);
        for (int c = 0; c < 16; c++) if (slot[c] != R.word[c]) FAIL("two files: the swap list puts club %d at %04X, not %04X", c, slot[c], R.word[c]);
        /* counts that don't add up: refused, nothing changed */
        memcpy(nd, div, 16); nd[3] = 1;
        for (int c = 0; c < 16; c++) before[c] = R.word[c];
        if (sr_reg_season(&R, nd, order, 16, 1, &WK, sw, 600) != -1) FAIL("two files: one up and none down not refused");
        for (int c = 0; c < 16; c++) if (R.word[c] != before[c]) FAIL("two files: a refused season changed club %d", c);
    }
    {   /* the exe's default: the same two files and moves -- only 7 and 8 change file (one swap), the others keep their
         * rows with their division changed; one club across and none back: refused */
        const int da[2] = { 0, 1 }, db[2] = { 2, 3 }, s[2] = { 4, 4 };
        sr_reg_file f[2] = { file(102, 2, da, s, 0), file(144, 2, db, s, 0) };
        for (int c = 0; c < 16; c++) div[c] = (uint8_t)(c / 4), order[c] = (uint16_t)c;
        sr_reg_build(&R, 2, f, 16, div);
        memcpy(nd, div, 16);
        nd[3] = 1; nd[4] = 0; nd[7] = 2; nd[8] = 1; nd[10] = 3; nd[11] = 3; nd[12] = 2; nd[13] = 2;
        for (int c = 0; c < 16; c++) before[c] = R.word[c];
        const int n = sr_reg_season(&R, nd, order, 16, 0, &WK, sw, 600);
        placed("default mode", nd);
        if (n != 1 || sw[0].a != W(102, 7) || sw[0].b != W(144, 0)) FAIL("default mode: %d swaps (1: 102/7 <-> 144/0)", n);
        for (int c = 0; c < 16; c++)
            if (c != 7 && c != 8 && R.word[c] != before[c]) FAIL("default mode: club %d moved inside its file", c);
        memcpy(nd, div, 16); nd[3] = 1; nd[4] = 0; nd[7] = 2;
        for (int c = 0; c < 16; c++) before[c] = R.word[c];
        if (sr_reg_season(&R, nd, order, 16, 0, &WK, sw, 600) != -1) FAIL("default mode: one across and none back not refused");
        for (int c = 0; c < 16; c++) if (R.word[c] != before[c]) FAIL("default mode: a refused season changed club %d", c);
    }
    {   /* Italy 4.1's one file (102: A, B 20, C 3 x 20, D 6 x 17 + 3 x 18), 1000 summers of random moves between
         * neighbouring divisions: the registry is the identity, no swap, every word kept */
        static const int sz[14] = { 20, 20, 20, 20, 20, 17, 17, 17, 17, 17, 17, 18, 18, 18 };
        int d14[14], s14[14], n = 0;
        uint32_t h = 12345;
        for (int d = 0; d < 14; d++) { d14[d] = d; s14[d] = sz[d]; for (int k = 0; k < sz[d]; k++) div[n++] = (uint8_t)d; }
        sr_reg_file f = file(102, 14, d14, s14, 0);
        if (sr_reg_build(&R, 1, &f, n, div)) FAIL("Italy 4.1 one file: refused");
        for (int c = 0; c < n; c++) if (R.word[c] != W(102, c)) FAIL("Italy 4.1 one file: club %d at %04X", c, R.word[c]);
        memcpy(nd, div, (size_t)n);
        for (int it = 0; it < 1000; it++) {
            for (int k = 0; k < 20; k++) {      /* (two clubs of neighbouring divisions trade divisions) */
                h = h * 1103515245u + 12345u; const int a = (int)((h >> 8) % (unsigned)n);
                h = h * 1103515245u + 12345u; const int b = (int)((h >> 8) % (unsigned)n);
                if (nd[a] + 1 == nd[b] || nd[b] + 1 == nd[a]) { const uint8_t x = nd[a]; nd[a] = nd[b]; nd[b] = x; }
            }
            const int m = sr_reg_season(&R, nd, NULL, 0, 0, &WK, sw, 600);
            if (m != 0) { FAIL("Italy 4.1 one file: summer %d gave %d swaps", it, m); break; }
            for (int c = 0; c < n; c++) if (R.word[c] != W(102, c) || sr_reg_div(&R, c) != nd[c]) { FAIL("Italy 4.1 one file: club %d", c); break; }
        }
    }
    {   /* three files, one division each (3 clubs): 0 <-> 1 <-> 2, two each way, then the pool rows of file 3 and
         * the off-file pool: 2's last two out (one to the pool rows, one off the files), a pool row's club and an
         * off-file club in */
        const int d0[1] = { 0 }, d1[1] = { 1 }, d2[1] = { 2 }, s[1] = { 3 };
        sr_reg_file f[3] = { file(10, 1, d0, s, 0), file(11, 1, d1, s, 0), file(12, 1, d2, s, 2) };
        for (int c = 0; c < 9; c++) div[c] = (uint8_t)(c / 3), order[c] = (uint16_t)c;
        div[9] = div[10] = SR_REG_POOL; div[11] = SR_REG_OUT; order[9] = 9; order[10] = 10; order[11] = 11;
        if (sr_reg_build(&R, 3, f, 12, div)) FAIL("three files: refused");
        if (R.word[9] != W(12, 3) || R.word[10] != W(12, 4) || R.word[11] != SR_REG_NONE) FAIL("three files: the pool rows");
        memcpy(nd, div, 12);
        nd[2] = 1; nd[3] = 0; nd[5] = 2; nd[6] = 1; nd[7] = SR_REG_POOL; nd[8] = SR_REG_OUT; nd[9] = 2; nd[11] = 2;
        for (int c = 0; c < 12; c++) before[c] = R.word[c];
        const int n = sr_reg_season(&R, nd, order, 12, 1, &WK, sw, 600);
        placed("three files", nd); kept("three files", div, nd, before);
        uint16_t slot[12];
        for (int c = 0; c < 12; c++) slot[c] = before[c];
        apply(sw, n, slot, 12);
        for (int c = 0; c < 12; c++) if (slot[c] != R.word[c]) FAIL("three files: the swap list puts club %d at %04X, not %04X", c, slot[c], R.word[c]);
        if (sr_reg_season(&R, nd, order, 12, 1, &WK, sw, 0) != 0) FAIL("three files: a second summer without moves");
        nd[0] = 1; nd[6] = 0;
        if (sr_reg_season(&R, nd, order, 12, 1, &WK, sw, 0) != -2) FAIL("three files: more swaps than max not refused");
    }
    {   /* the limits */
        const int d[2] = { 0, 1 }, s1[2] = { 200, 56 }, s2[2] = { 200, 57 }, dd[1] = { 1 }, s3[1] = { 4 };
        sr_reg_file f = file(90, 2, d, s1, 0);
        for (int c = 0; c < 257; c++) div[c] = (uint8_t)(c < 200 ? 0 : 1);
        if (sr_reg_build(&R, 1, &f, 256, div)) FAIL("a file of 256: refused");
        f = file(90, 2, d, s2, 0);
        if (sr_reg_build(&R, 1, &f, 257, div) != -1) FAIL("a file of 257: not refused");
        f = file(90, 2, d, s1, 1);
        if (sr_reg_build(&R, 1, &f, 256, div) != -1) FAIL("a file of 256 + a pool row: not refused");
        sr_reg_file two[2] = { file(90, 2, d, s1, 0), file(91, 1, dd, s3, 0) };
        if (sr_reg_build(&R, 2, two, 256, div) != -1) FAIL("a division in two files: not refused");
        sr_reg_file same[2] = { file(90, 1, d, s1, 0), file(90, 1, dd, s3, 0) };
        if (sr_reg_build(&R, 2, same, 256, div) != -1) FAIL("a file number twice: not refused");
        sr_reg_file pools[2] = { file(90, 1, d, s1, 1), file(91, 1, dd, s3, 1) };
        if (sr_reg_build(&R, 2, pools, 256, div) != -1) FAIL("pool rows in two files: not refused");
    }
    {   /* the old-save import: italy42's map */
        const int da[5] = { 0, 1, 2, 3, 4 }, sa[5] = { 20, 20, 20, 20, 20 };
        int db[9], sb[9];
        for (int g = 0; g < 9; g++) db[g] = 5 + g, sb[g] = 18;
        sr_reg_file f[2] = { file(I42_FILE_A, 5, da, sa, 0), file(I42_FILE_B, 9, db, sb, 36) };
        int moved = 0, on = 0;
        for (int i = 0; i < I42_N; i++) div[i] = I42[i].div;
        if (sr_reg_build(&R, 2, f, I42_N, div)) FAIL("italy42: refused");
        for (int i = 0; i < I42_N; i++) {
            if (R.word[i] != I42[i].word) FAIL("italy42: club %d (old %04X) at %04X, not %04X", i, I42[i].old, R.word[i], I42[i].word);
            if (sr_reg_ident(&R, I42[i].word) != i) FAIL("italy42: word %04X not resolved", I42[i].word);
            if (I42[i].old == SR_REG_NONE) on++;
            else if (I42[i].old != R.word[i]) moved++;
            else if ((I42[i].old & 0xFF) != I42_FILE_A || (I42[i].old >> 8) >= 100) FAIL("italy42: %04X kept", I42[i].old);
        }
        for (int o = 0; o < 100; o++) if (sr_reg_ident(&R, W(I42_FILE_A, o)) != o) FAIL("italy42: file %d ordinal %d moved", I42_FILE_A, o);
        if (moved != 156 || on != 42 || sr_reg_check(&R)) FAIL("italy42: %d moved, %d on (156, 42)", moved, on);
    }
    printf("%s: the club registry (one file + Italy 4.1 identity, two files default / contiguous, three files, the pool, "
           "the limits, the italy42 import; sizeof sr_registry %u, sr_reg_work %u), %d case(s) failed\n",
           fails ? "FAIL" : "PASS", (unsigned)sr_reg_size(), (unsigned)sr_reg_work_size(), fails);
    return fails != 0;
}
