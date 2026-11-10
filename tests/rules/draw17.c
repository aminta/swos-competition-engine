/* swc/1.7 draws (Regia 2026-11-11, AFC ACL Two 2026/27 art. 8.1.3 / 7.1.4.1 "except in cases where it is
 * unavoidable"): sr_group_draw_m, the fewest clubs of one association in a group.
 * - random shapes up to 64 clubs: every draw valid (one club of a pot per group, the sizes), each association with m
 *   clubs in g groups has floor(m/g) or ceil(m/g) in every group (the fewest pairs), never a refusal;
 * - every association within g: sr_group_draw_p's draw, club for club;
 * - an ACL Two West shape (16 clubs in 4 groups, KSA 5): one group holds two KSA clubs, the others one, the pair varies
 *   with the seed (not always the same two clubs);
 * - assoc NULL, n > 64: -1. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "swosrules.h"

static uint32_t rs = 1717;
static uint32_t rnd(uint32_t n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 10) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int balanced(int n, int g, const uint8_t *as, const uint8_t *grp)
{
    uint8_t size[64];
    sr_group_sizes(n, g, size);
    int sz[64] = { 0 }, pot[64][64] = { { 0 } };
    for (int c = 0; c < n; c++) {
        if (grp[c] >= g || pot[c / g][grp[c]]++) return 0;
        sz[grp[c]]++;
    }
    for (int j = 0; j < g; j++) if (sz[j] != size[j]) return 0;
    for (int c = 0; c < n; c++) {
        int m = 0, here[64] = { 0 };
        for (int d = 0; d < n; d++) if (as[d] == as[c]) { m++; here[grp[d]]++; }
        for (int j = 0; j < g; j++) if (here[j] < m / g || here[j] > (m + g - 1) / g) return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 20000, over = 0, same = 0;
    uint8_t as[64], grp[64], grp2[64];
    for (int it = 0; it < iters; it++) {
        int g = 2 + (int)rnd(7), n = 2 * g + (int)rnd(64 - 2 * g + 1), na = 1 + (int)rnd(12);
        for (int c = 0; c < n; c++) as[c] = (uint8_t)(rnd(3) ? rnd(na) : 200 + rnd(3));
        int r = sr_group_draw_m(n, g, as, (uint32_t)it * 2654435761u, grp);
        CHECK(r == g, "it %d n %d g %d: %d", it, n, g, r);
        if (r == g) CHECK(balanced(n, g, as, grp), "it %d n %d g %d: not balanced", it, n, g);
        int cnt[256] = { 0 }, o = 0;
        for (int c = 0; c < n; c++) o |= ++cnt[as[c]] > g;
        if (o) over++;
        else {
            int r2 = sr_group_draw_p(n, g, as, 0, 0, (uint32_t)it * 2654435761u, grp2);
            CHECK(r2 == r && !memcmp(grp, grp2, (size_t)n), "it %d: not sr_group_draw_p's draw", it);
            same++;
        }
    }
    printf("PASS: sr_group_draw_m %d shapes up to 64 clubs (%d with an association over g): every draw valid and "
           "balanced (floor / ceil m/g a group), none refused; %d within g = sr_group_draw_p's draw\n", iters, over, same);
    /* ACL Two West: 16 clubs, 4 groups, KSA (1) 5, UAE (2) 3, QAT (3) 3, IRN (4) 2, others one each */
    uint8_t w[16] = { 1, 2, 3, 1, 4, 1, 2, 5, 3, 1, 6, 2, 7, 3, 1, 4 };
    int pairs[16][16] = { { 0 } }, shapes = 0;
    for (uint32_t sd = 0; sd < 2000; sd++) {
        int r = sr_group_draw_m(16, 4, w, sd * 7919u + 1, grp);
        CHECK(r == 4 && balanced(16, 4, w, grp), "west seed %u", sd);
        for (int a = 0; a < 16; a++)
            for (int b = a + 1; b < 16; b++)
                if (w[a] == 1 && w[b] == 1 && grp[a] == grp[b]) { if (!pairs[a][b]++) shapes++; }
    }
    CHECK(shapes > 3, "the KSA pair is always the same two clubs (%d pairs seen)", shapes);
    printf("PASS: ACL Two West shape (KSA 5 in 4 groups): 2000 draws, one group with two KSA clubs each, %d different "
           "KSA pairs seen\n", shapes);
    CHECK(sr_group_draw_m(16, 4, 0, 1, grp) == -1, "assoc NULL");
    CHECK(sr_group_draw_m(65, 4, as, 1, grp) == -1, "n 65");
    printf(fails ? "FAIL: %d\n" : "PASS: -1 for assoc NULL and n 65\n", fails);
    return fails != 0;
}
