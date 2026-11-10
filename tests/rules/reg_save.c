/* The club registry in a save and the word map (sr_regsave.c; N7, the save side of 42-s7reg). Save / load round
 * trips on one, two and three files (after seasons of the exe's default mode, with the pool rows and off-file clubs);
 * the refusals (room short, a short or truncated block, another version / file count / layout: absent, an ident
 * twice or past nclub: corrupt, the registry unchanged); the word map of 500 random seasons on the italy42 shape
 * against the records moved by the swap list; the old-save import on the Mod's italy42 map (156 words of file 102
 * move, 42 clubs come on, Serie A / B / C keep their ordinals). Controls: a pair dropped from a season's map, two
 * rows swapped in a saved block -- each must be seen. */
#include <stdio.h>
#include <string.h>
#include "swosrules.h"
#include "italy42_map.h"
static sr_registry R, L;
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
static int same(const sr_registry *a, const sr_registry *b)
{
    if (a->nclub != b->nclub || a->nfiles != b->nfiles) return 0;
    for (int c = 0; c < a->nclub; c++) if (a->word[c] != b->word[c]) return 0;
    for (int i = 0; i < a->nfiles; i++)
        for (int o = 0; o < a->rows[i]; o++) if (a->at[i][o] != b->at[i][o]) return 0;
    return 1;
}
/* save R, load it into L (built from the same layout), the same registry, the second save byte-equal */
static void round_trip(const char *what, int nfiles, const sr_reg_file *f, int n, const uint8_t *div)
{
    static uint8_t a[2048], b[2048];
    const uint32_t lay = sr_reg_layout(nfiles, f, n);
    const int size = sr_reg_save(&R, lay, 2027, a, sizeof a);
    if (size != sr_reg_save_size(&R) || size <= SR_REGSAVE_HDR) { FAIL("%s: saved %d bytes", what, size); return; }
    if (sr_reg_build(&L, nfiles, f, n, div)) { FAIL("%s: rebuild refused", what); return; }
    if (sr_reg_load(&L, lay, a, size) != 1 || !same(&R, &L) || sr_reg_check(&L)) FAIL("%s: not the same after a load", what);
    if (sr_reg_save(&L, lay, 2027, b, sizeof b) != size || memcmp(a, b, (size_t)size)) FAIL("%s: save(load(x)) is not x", what);
}

int main(void)
{
    static sr_reg_swap sw[1024];
    static sr_wpair map[1024];
    static uint8_t div[SR_REG_CLUBS], nd[SR_REG_CLUBS], buf[2048];
    static uint16_t slot[SR_REG_CLUBS], start[SR_REG_CLUBS], order[SR_REG_CLUBS];
    uint32_t h = 4242;
    {   /* one file (12 clubs, 3 divisions) */
        const int d[3] = { 0, 1, 2 }, s[3] = { 4, 4, 4 };
        sr_reg_file f = file(90, 3, d, s, 0);
        for (int c = 0; c < 12; c++) div[c] = (uint8_t)(c / 4);
        if (sr_reg_build(&R, 1, &f, 12, div)) FAIL("one file: refused");
        round_trip("one file", 1, &f, 12, div);
        if (sr_reg_save_size(&R) != SR_REGSAVE_HDR + 15) FAIL("one file: %d bytes (12 + 15)", sr_reg_save_size(&R));
    }
    {   /* three files, the pool rows and two off-file clubs, after a season that brings both on */
        const int d0[1] = { 0 }, d1[1] = { 1 }, d2[1] = { 2 }, s[1] = { 3 };
        sr_reg_file f[3] = { file(10, 1, d0, s, 0), file(11, 1, d1, s, 0), file(12, 1, d2, s, 2) };
        for (int c = 0; c < 9; c++) div[c] = (uint8_t)(c / 3), order[c] = (uint16_t)c;
        div[9] = div[10] = SR_REG_POOL; div[11] = div[12] = SR_REG_OUT;
        if (sr_reg_build(&R, 3, f, 13, div)) FAIL("three files: refused");
        memcpy(nd, div, 13);
        nd[2] = 1; nd[3] = 0; nd[5] = 2; nd[6] = 1; nd[7] = SR_REG_POOL; nd[8] = SR_REG_OUT; nd[9] = 2; nd[11] = SR_REG_POOL;
        nd[10] = SR_REG_OUT;
        nd[12] = 2;
        const int m = sr_reg_season(&R, nd, NULL, 0, 0, &WK, sw, 1024);
        if (m < 0 || sr_reg_check(&R)) FAIL("three files: the season refused (%d)", m);
        memcpy(div, (uint8_t[13]){ 0, 0, 0, 1, 1, 1, 2, 2, 2, SR_REG_POOL, SR_REG_POOL, SR_REG_OUT, SR_REG_OUT }, 13);
        round_trip("three files after a season", 3, f, 13, div);
    }
    /* italy42's shape: A = 102 (Serie A, B, C 3 x 20), B = 144 (Serie D 9 x 18 + 36 pool rows), 10 off-file clubs */
    const int da[5] = { 0, 1, 2, 3, 4 }, sa[5] = { 20, 20, 20, 20, 20 };
    int db[9], sb[9];
    for (int g = 0; g < 9; g++) db[g] = 5 + g, sb[g] = 18;
    sr_reg_file fi[2] = { file(I42_FILE_A, 5, da, sa, 0), file(I42_FILE_B, 9, db, sb, 36) };
    const int NC = I42_N + 10;
    for (int i = 0; i < I42_N; i++) div[i] = I42[i].div;
    for (int i = I42_N; i < NC; i++) div[i] = SR_REG_OUT;
    if (sr_reg_build(&R, 2, fi, NC, div)) FAIL("italy42 shape: refused");
    const uint32_t lay = sr_reg_layout(2, fi, NC);
    if (sr_reg_save_size(&R) != SR_REGSAVE_HDR + 373) FAIL("italy42 shape: %d bytes (12 + 373)", sr_reg_save_size(&R));
    round_trip("italy42 shape as built", 2, fi, NC, div);
    {   /* the refusals: L stays as it was */
        const int size = sr_reg_save(&R, lay, 2027, buf, sizeof buf);
        if (sr_reg_save(&R, lay, 2027, buf, size - 1) != -1) FAIL("room short: not refused");
        sr_reg_save(&R, lay, 2027, buf, sizeof buf);
        sr_reg_build(&L, 2, fi, NC, div);
        static sr_registry K;
        K = L;
        struct { const char *what; int len, want; uint32_t lay; } c[] = {
            { "absent (len 0)", 0, 0, lay }, { "another layout", size, 0, lay ^ 1 }, { "a short header", 5, -1, lay },
            { "a truncated map", size - 1, -1, lay } };
        for (unsigned k = 0; k < sizeof c / sizeof c[0]; k++)
            if (sr_reg_load(&L, c[k].lay, buf, c[k].len) != c[k].want || !same(&L, &K)) FAIL("load %s: not %d, or L changed", c[k].what, c[k].want);
        sr_reg_file g[2] = { fi[0], fi[1] };
        g[1].pool = 35;
        if (sr_reg_layout(2, g, NC) == lay || sr_reg_layout(2, fi, NC + 1) == lay) FAIL("the layout hash misses a shape change");
        buf[0] = SR_REGSAVE_VERSION + 1;
        if (sr_reg_load(&L, lay, buf, size) != 0) FAIL("another version: not absent");
        buf[0] = SR_REGSAVE_VERSION; buf[1] = 3;
        if (sr_reg_load(&L, lay, buf, size) != 0) FAIL("another file count: not absent");
        buf[1] = 2;
        /* row 0 := row 1's ident (twice); row 0 := 1023 (past nclub) */
        for (int v = 0; v < 2; v++) {
            sr_reg_save(&R, lay, 2027, buf, sizeof buf);
            const unsigned id = v ? 1023u : R.at[0][1];
            for (int b = 0; b < 10; b++) {
                const int bit = SR_REGSAVE_HDR * 8 + b;
                buf[bit >> 3] = (uint8_t)((buf[bit >> 3] & ~(1 << (bit & 7))) | (id >> b & 1) << (bit & 7));
            }
            if (sr_reg_load(&L, lay, buf, size) != -1 || !same(&L, &K)) FAIL("an ident %s: not corrupt, or L changed", v ? "past nclub" : "twice");
        }
        /* control: rows 0 and 1 swapped -- a valid block, another registry: the round trip's compare sees it */
        sr_reg_save(&R, lay, 2027, buf, sizeof buf);
        const unsigned i0 = R.at[0][0], i1 = R.at[0][1];
        for (int r = 0; r < 2; r++)
            for (int b = 0; b < 10; b++) {
                const int bit = SR_REGSAVE_HDR * 8 + 10 * r + b;
                const unsigned id = r ? i0 : i1;
                buf[bit >> 3] = (uint8_t)((buf[bit >> 3] & ~(1 << (bit & 7))) | (id >> b & 1) << (bit & 7));
            }
        if (sr_reg_load(&L, lay, buf, size) != 1 || same(&L, &R)) FAIL("control (two rows swapped): not seen");
    }
    {   /* 500 seasons of random trades (any two clubs trade divisions, the pool and off the files too): each season's
         * map from its swap list against the records moved; a save / load round trip every 50 */
        int dropped_seen = 0;
        for (int i = 0; i < NC; i++) nd[i] = div[i], order[i] = (uint16_t)i;
        for (int it = 0; it < 500; it++) {
            for (int k = 0; k < 30; k++) {
                h = h * 1103515245u + 12345u; const int a = (int)((h >> 8) % (unsigned)NC);
                h = h * 1103515245u + 12345u; const int b = (int)((h >> 8) % (unsigned)NC);
                const uint8_t x = nd[a]; nd[a] = nd[b]; nd[b] = x;
            }
            for (int c = 0; c < NC; c++) start[c] = slot[c] = R.word[c];
            const int n = sr_reg_season(&R, nd, order, NC, 0, &WK, sw, 1024);
            if (n < 0) { FAIL("season %d refused (%d)", it, n); break; }
            apply(sw, n, slot, NC);
            const int m = sr_wmap_build(sw, n, map, 1024);
            if (m < 0) { FAIL("season %d: the map refused (%d)", it, m); break; }
            for (int c = 0; c < NC; c++) {
                if (slot[c] != R.word[c]) { FAIL("season %d: the swaps put club %d at %04X, not %04X", it, c, slot[c], R.word[c]); break; }
                if (start[c] != SR_REG_NONE && sr_wmap_get(map, m, start[c]) != R.word[c]) {
                    FAIL("season %d: the map sends %04X to %04X, club %d is at %04X", it, start[c], sr_wmap_get(map, m, start[c]), c, R.word[c]);
                    break;
                }
            }
            if (m > 1) {                    /* control: one pair dropped -- some club's word no longer resolves */
                int bad = 0;
                for (int c = 0; c < NC && !bad; c++)
                    bad = start[c] != SR_REG_NONE && sr_wmap_get(map + 1, m - 1, start[c]) != R.word[c];
                dropped_seen += bad;
            }
            if (it % 50 == 49) {
                for (int c = 0; c < NC; c++) div[c] = c < I42_N ? I42[c].div : SR_REG_OUT;
                round_trip("italy42 shape after seasons", 2, fi, NC, div);
            }
        }
        if (dropped_seen < 400) FAIL("control (a pair dropped): seen in %d seasons only", dropped_seen);
    }
    {   /* the old-save import: italy42's map (old word = 4.1's one file 102; 42 Eccellenza stand-ins off the files) */
        static uint16_t old[I42_N];
        for (int i = 0; i < I42_N; i++) div[i] = I42[i].div, old[i] = I42[i].old;
        if (sr_reg_build(&R, 2, fi, I42_N, div)) FAIL("italy42 import: refused");
        const int m = sr_wmap_import(&R, old, I42_N, map, 1024);
        int to_b = 0;
        for (int i = 0; i < m; i++) to_b += (map[i].from & 0xFF) == I42_FILE_A && (map[i].from >> 8) >= 100 && (map[i].to & 0xFF) == I42_FILE_B;
        if (m != 156 || to_b != 156) FAIL("italy42 import: %d pairs, %d from 102/100+ to 144 (156, 156)", m, to_b);
        for (int o = 0; o < 100; o++) if (sr_wmap_get(map, m, W(I42_FILE_A, o)) != W(I42_FILE_A, o)) FAIL("italy42 import: 102/%d moved", o);
        for (int i = 0; i < I42_N; i++)
            if (old[i] != SR_REG_NONE && sr_wmap_get(map, m, old[i]) != R.word[i]) FAIL("italy42 import: club %d's %04X not resolved", i, old[i]);
        for (int o = 100; o < 256; o++)
            if ((sr_wmap_get(map, m, W(I42_FILE_A, o)) & 0xFF) == I42_FILE_A) FAIL("italy42 import: 102/%d left in file 102", o);
        old[0] = old[200];                  /* refused: one old word, two clubs */
        if (sr_wmap_import(&R, old, I42_N, map, 1024) != -1) FAIL("italy42 import: a word twice not refused");
        old[0] = I42[0].old;
        if (sr_wmap_import(&R, old, I42_N, map, 100) != -2) FAIL("italy42 import: room short not refused");
    }
    printf("%s: the registry in a save (round trips: 1 / 3 / 2 files, after seasons; refusals; 500 seasons' word maps; "
           "the italy42 import; controls), %d case(s) failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
