/* The mass-simulation driver (4.2 host first, Davide 08/11; the Mod's notes/CONVENTIONS.md 17b).
 * v0: England's non-league chain through the library, many seasons in a row, many chains in parallel:
 *   National League (24)  champion + play-off winner up (to a League Two bucket), 4 down to N / S
 *   N / S (2 x 24)        each: champion + play-off winner up, 4 down into the step-3 pool by area
 *   step 3 (4 x 22)       sr_lp_season: champion + play-off winner up (8), placed in N / S by area
 *   summer                sr_pg_realloc of N / S (24 / 24 by ground)
 * The National League's and N / S's tables are light tables here (the game plays them for real); the
 * rules under test are the moves, the pool and the re-allocation. Each summer checks the invariant
 * catalogue below; the totals compare the distributions with the real ones.
 *   invariants  every identity in exactly one place; sizes 24 / 24 / 24 / 4 x 22; 8 distinct up from
 *               step 3 with each division's champion; N / S strict clubs in their group (or a crossing,
 *               counted); flexible clubs split by rank north first; a second re-allocation pass
 *               changes nothing; strengths within 0 .. 375
 *   real        a club relegated from N / S straight back up: 4 of 55 = 7.3% (2011-12 .. 2023-24,
 *               the Mod's internal/nonleague2627/bounce)
 * Usage: masssim [CHAINS [SEASONS [WORKERS]]] (default 1000 100 8). Exit 1 on any broken invariant. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "swosrules.h"

enum { NL = 24, NS = 24, PD = 22, PDIV = 4, L2B = 16, MAXID = 4096 };

typedef struct {
    long summers, bad, crossed, bounce_n, bounce_up, po_place[8], idem_bad, why_n[8];
    long str_sum[3], str_cnt[3], dec_sum[3][10], dec_cnt[10];
} stats;

static const char *why_txt[8] = { "an identity lost or doubled", "a level's size", "step 3: not 8 distinct up",
                                  "step 3: a champion not up", "N / S: a strict club across", "N / S: flexible not by rank",
                                  "re-allocation refused", "a strength out of range" };

/* an identity's fixed attributes: the ground's rank (0 north .. 254) and its area byte at N / S */
static uint8_t rank_of[MAXID], area_of[MAXID];
static uint16_t str_of[MAXID];
static unsigned char dead[MAXID];   /* an identity drawn for a full feed division: never used */
static int next_id;

static uint32_t h32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; return x ^ (x >> 16); }

static int new_club(uint32_t chain, int lo, int hi)
{
    const int i = next_id++;
    const uint32_t h = h32(chain * 7919u + (uint32_t)i * 104729u);
    const int lat = (int)(h % 241u), k = (int)(h >> 8) % 10;
    rank_of[i] = (uint8_t)lat;
    area_of[i] = k < 4 ? (lat < 120 ? 0x00 : 0x01)      /* strict, by its side of the line */
               : k < 9 ? (lat < 120 ? 0x20 : 0x11)      /* flexible: the nearer group first */
               : 0xE0;                                  /* any group (a second team) */
    if ((h >> 20) % 25 == 0) rank_of[i] = SR_RANK_NONE, area_of[i] = (uint8_t)(lat < 120 ? 0x00 : 0x01);
    str_of[i] = (uint16_t)(lo + (int)((h >> 12) % (uint32_t)(hi - lo + 1)));
    return i;
}

static int pool_div(int id) { const int north = (area_of[id] & 15) == 0; return north ? (rank_of[id] < 70 ? 0 : 1) : (rank_of[id] < 190 ? 2 : 3); }

static int cmp_str_desc(const void *a, const void *b) { return (int)*(const uint16_t *)b - (int)*(const uint16_t *)a; }

/* a light table of ids[0 .. n): order of ids best first into ord */
static void table(uint32_t seed, int d, const int *ids, int n, int *ord, sr_row *t)
{
    sr_lp_club c[SR_LP_CLUBS];
    for (int i = 0; i < n; i++) c[i].ident = (uint16_t)ids[i], c[i].strength = str_of[ids[i]];
    sr_lp_table(seed, d, c, n, t);
    for (int k = 0; k < n; k++) ord[k] = ids[t[k].team];
}

static int playoff_id(uint32_t seed, int d, const int *ids, int n, int *place)
{
    sr_lp_club c[SR_LP_CLUBS];
    sr_row t[SR_LP_CLUBS];
    for (int i = 0; i < n; i++) c[i].ident = (uint16_t)ids[i], c[i].strength = str_of[ids[i]];
    sr_lp_table(seed, d, c, n, t);
    const int p = sr_lp_playoff(seed, d, t, n, c);
    *place = p;
    return ids[t[p].team];
}

static void chain_run(uint32_t chain, int seasons, stats *st)
{
    static int nl[NL], ns[2][NS], pool[PDIV][PD], l2[L2B], down_season[MAXID];
    next_id = 0;
    memset(dead, 0, sizeof dead);
    for (int i = 0; i < NL; i++) nl[i] = new_club(chain, 180, 300);
    for (int g = 0; g < 2; g++) for (int i = 0; i < NS; i++) ns[g][i] = new_club(chain, 120, 250);
    for (int i = 0; i < L2B; i++) l2[i] = new_club(chain, 260, 340);
    {   /* the pool: 88 clubs, each in its feed division */
        int cnt[PDIV] = { 0 };
        while (cnt[0] < PD || cnt[1] < PD || cnt[2] < PD || cnt[3] < PD) {
            const int id = new_club(chain, 40, 200), d = pool_div(id);
            if (cnt[d] < PD) pool[d][cnt[d]++] = id; else dead[id] = 1;
        }
    }
    for (int i = 0; i < MAXID; i++) down_season[i] = -100;
    int l2_head = 0;
    for (int s = 0; s < seasons; s++) {
        const uint32_t seed = h32(chain * 1000003u + (uint32_t)s);
        int ord[SR_LP_CLUBS], place;
        sr_row t[SR_LP_CLUBS];
        unsigned bad = 0;
        /* the National League */
        table(seed, 20, nl, NL, ord, t);
        int nl_up[2] = { ord[0], playoff_id(seed, 20, nl, NL, &place) }, nl_down[4];
        for (int k = 0; k < 4; k++) nl_down[k] = ord[NL - 1 - k];
        /* N / S */
        int ns_up[4], ns_down[2][4];
        for (int g = 0; g < 2; g++) {
            table(seed, 21 + g, ns[g], NS, ord, t);
            ns_up[2 * g] = ord[0];
            ns_up[2 * g + 1] = playoff_id(seed, 21 + g, ns[g], NS, &place);
            for (int k = 0; k < 4; k++) ns_down[g][k] = ord[NS - 1 - k];
        }
        /* step 3 */
        sr_lp_club pc[PDIV][SR_LP_CLUBS];
        const sr_lp_club *pcp[PDIV];
        int32_t pn[PDIV], up[2 * PDIV];
        for (int d = 0; d < PDIV; d++) {
            for (int i = 0; i < PD; i++) pc[d][i].ident = (uint16_t)pool[d][i], pc[d][i].strength = str_of[pool[d][i]];
            pcp[d] = pc[d]; pn[d] = PD;
        }
        sr_lp_season(seed, PDIV, pcp, pn, up);
        int p_up[8];
        for (int d = 0; d < PDIV; d++) {
            p_up[2 * d] = pool[d][up[2 * d]]; p_up[2 * d + 1] = pool[d][up[2 * d + 1]];
            sr_row tt[SR_LP_CLUBS];
            sr_lp_table(seed, d, pc[d], PD, tt);
            if (pool[d][tt[0].team] != p_up[2 * d]) bad |= 1u << 3;
            for (int k = 0; k < PD; k++) if (pool[d][tt[k].team] == p_up[2 * d + 1]) st->po_place[k < 7 ? k : 7]++;
        }
        for (int a = 0; a < 8; a++) for (int b = a + 1; b < 8; b++) if (p_up[a] == p_up[b]) bad |= 1u << 2;
        for (int k = 0; k < 8; k++)             /* bounce-back: down last summer, up now */
            if (down_season[p_up[k]] == s - 1) st->bounce_up++;
        /* the moves: L2 <-> NL */
        for (int k = 0; k < 2; k++) {
            int *slot = NULL;
            for (int i = 0; i < NL; i++) if (nl[i] == nl_up[k]) slot = &nl[i];
            const int back = l2[l2_head];
            l2[l2_head] = *slot; l2_head = (l2_head + 1) % L2B;
            *slot = back;
        }
        /* NL down 4 <-> N / S up 4 (their slots: N / S's level, placed below) */
        static int level[NL + 2 * NS];          /* N / S after the moves, 48 */
        int nlv = 0;
        for (int g = 0; g < 2; g++)
            for (int i = 0; i < NS; i++) {
                const int id = ns[g][i];
                int out = 0;
                for (int k = 0; k < 4; k++) out |= id == ns_up[k] || id == ns_down[g][k];
                if (!out) level[nlv++] = id;
            }
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < NL; i++) if (nl[i] == nl_down[k]) nl[i] = ns_up[k];
            level[nlv++] = nl_down[k];
        }
        /* N / S down 8 <-> step 3 up 8: a promoted club takes a relegated slot's record (its strength);
           the relegated one the pool's strength at its N / S position (D) */
        int ri = 0;
        for (int g = 0; g < 2; g++) {
            uint16_t above[NS], pl[PDIV * PD];
            int np = 0;
            for (int i = 0; i < NS; i++) above[i] = str_of[ns[g][i]];
            qsort(above, NS, sizeof above[0], cmp_str_desc);
            for (int d = 0; d < PDIV; d++) for (int i = 0; i < PD; i++) pl[np++] = str_of[pool[d][i]];
            qsort(pl, (size_t)np, sizeof pl[0], cmp_str_desc);
            for (int k = 0; k < 4; k++) {
                const int down = ns_down[g][k], upc = p_up[ri++];
                const uint16_t slot_str = str_of[down];
                str_of[down] = (uint16_t)sr_lp_rank_strength(slot_str, above, NS, pl, np);
                str_of[upc] = slot_str;
                down_season[down] = s;
                st->bounce_n++;
                level[nlv++] = upc;
                const int fd = pool_div(upc), td = pool_div(down);   /* pool: upc's slot out, down in */
                int *src = NULL;
                for (int d = 0; d < PDIV; d++) for (int i = 0; i < PD; i++) if (pool[d][i] == upc) src = &pool[d][i];
                if (fd == td || 1) *src = down;  /* (step 4 not modelled: the relegated club takes the slot) */
            }
        }
        if (nlv != 2 * NS) bad |= 1u << 1;
        /* the summer re-allocation */
        static sr_pg_club cl[NL + 2 * NS];
        static uint8_t rk[NL + 2 * NS];
        static uint16_t id[NL + 2 * NS];
        static int8_t off[NL + 2 * NS], off2[NL + 2 * NS];
        static const sr_pg_level lv[2] = { { 0, 1, { NL }, 0 }, { 1, 2, { NS, NS }, 0 } };
        for (int i = 0; i < NL; i++) {
            const int c = nl[i];
            cl[i].base = 0; memset(cl[i].area, 0xFF, sizeof cl[i].area);
            rk[i] = rank_of[c]; id[i] = (uint16_t)c; off[i] = 0;
        }
        for (int i = 0; i < 2 * NS; i++) {
            const int c = level[i], j = NL + i;
            cl[j].base = 0; memset(cl[j].area, 0xFF, sizeof cl[j].area);
            cl[j].area[0] = area_of[c];
            rk[j] = rank_of[c]; id[j] = (uint16_t)c; off[j] = (int8_t)(1 + (i & 1));
        }
        const sr_pg_league pg = { 2, lv, NL + 2 * NS, cl };
        const int rc = sr_pg_realloc(&pg, 1, rk, id, off);
        if (rc < 0) bad |= 1u << 6;
        else {
            st->crossed += rc;
            memcpy(off2, off, sizeof off);
            sr_pg_realloc(&pg, 1, rk, id, off2);
            if (memcmp(off, off2, sizeof off)) st->idem_bad++;
            int n[2] = { 0, 0 }, maxn = -1, mins = 256, k[2] = { 0, 0 };
            for (int i = NL; i < NL + 2 * NS; i++) {
                const int g = off[i] - 1, a = cl[i].area[0], hi = a >> 4;
                if (g < 0 || g > 1) { bad |= 1u << 1; continue; }
                n[g]++;
                const int flex = rk[i] != SR_RANK_NONE && (hi == 14 || hi == 1 || hi == 2);
                if (!flex && (a & 15) != g && rc == 0) bad |= 1u << 4;
                if (flex) { if (g == 0 && rk[i] > maxn) maxn = rk[i]; if (g == 1 && rk[i] < mins) mins = rk[i]; }
            }
            if (maxn > mins) bad |= 1u << 5;
            if (n[0] != NS || n[1] != NS) bad |= 1u << 1;
            for (int i = NL; i < NL + 2 * NS; i++) { const int g = off[i] - 1; if (g == 0 || g == 1) ns[g][k[g]++] = id[i]; }
        }
        /* every identity once */
        static unsigned char seen[MAXID];
        memset(seen, 0, (size_t)next_id);
        for (int i = 0; i < NL; i++) seen[nl[i]]++;
        for (int g = 0; g < 2; g++) for (int i = 0; i < NS; i++) seen[ns[g][i]]++;
        for (int d = 0; d < PDIV; d++) for (int i = 0; i < PD; i++) seen[pool[d][i]]++;
        for (int i = 0; i < L2B; i++) seen[l2[i]]++;
        for (int i = 0; i < next_id; i++) if (seen[i] != !dead[i]) bad |= 1u;
        for (int i = 0; i < next_id; i++) if (str_of[i] > 375) bad |= 1u << 7;
        for (int i = 0; i < NL; i++) st->str_sum[0] += str_of[nl[i]], st->str_cnt[0]++;
        for (int g = 0; g < 2; g++) for (int i = 0; i < NS; i++) st->str_sum[1] += str_of[ns[g][i]], st->str_cnt[1]++;
        for (int d = 0; d < PDIV; d++) for (int i = 0; i < PD; i++) st->str_sum[2] += str_of[pool[d][i]], st->str_cnt[2]++;
        {   /* the levels' mean strength by tenth of the chain (drift) */
            const int dz = s * 10 / seasons;
            long a[3] = { 0, 0, 0 };
            for (int i = 0; i < NL; i++) a[0] += str_of[nl[i]];
            for (int g = 0; g < 2; g++) for (int i = 0; i < NS; i++) a[1] += str_of[ns[g][i]];
            for (int d = 0; d < PDIV; d++) for (int i = 0; i < PD; i++) a[2] += str_of[pool[d][i]];
            st->dec_sum[0][dz] += a[0] / NL; st->dec_sum[1][dz] += a[1] / (2 * NS); st->dec_sum[2][dz] += a[2] / (PDIV * PD);
            st->dec_cnt[dz]++;
        }
        st->summers++;
        if (bad) { st->bad++; for (int b = 0; b < 8; b++) if (bad >> b & 1) st->why_n[b]++; }
    }
}

int main(int argc, char **argv)
{
    const int chains = argc > 1 ? atoi(argv[1]) : 1000, seasons = argc > 2 ? atoi(argv[2]) : 100,
              workers = argc > 3 ? atoi(argv[3]) : 8;
    int fd[64][2];
    for (int w = 0; w < workers && w < 64; w++) {
        if (pipe(fd[w])) return 2;
        if (fork() == 0) {
            stats st;
            memset(&st, 0, sizeof st);
            for (int c = w; c < chains; c += workers) chain_run((uint32_t)c + 1u, seasons, &st);
            if (write(fd[w][1], &st, sizeof st) != (ssize_t)sizeof st) _exit(2);
            _exit(0);
        }
        close(fd[w][1]);
    }
    stats all;
    memset(&all, 0, sizeof all);
    for (int w = 0; w < workers && w < 64; w++) {
        stats st;
        if (read(fd[w][0], &st, sizeof st) != (ssize_t)sizeof st) { fprintf(stderr, "worker %d lost\n", w); return 2; }
        long *a = (long *)&all, *b = (long *)&st;
        for (size_t k = 0; k < sizeof st / sizeof(long); k++) a[k] += b[k];
        wait(NULL);
    }
    printf("masssim: %d chains x %d seasons = %ld summers (%d workers)\n", chains, seasons, all.summers, workers);
    for (int b = 0; b < 8; b++) if (all.why_n[b]) printf("  FAIL %ld summers: %s\n", all.why_n[b], why_txt[b]);
    printf("  re-allocation: %ld strict crossings (%.4f a summer), second pass changed %ld\n",
           all.crossed, (double)all.crossed / (double)all.summers, all.idem_bad);
    printf("  bounce-back N/S -> step 3 -> N/S next season: %.2f%% (real 7.3%%, 4/55)\n",
           100.0 * (double)all.bounce_up / (double)(all.bounce_n ? all.bounce_n : 1));
    long po = 0;
    for (int k = 0; k < 8; k++) po += all.po_place[k];
    printf("  step-3 play-off winners by place:");
    for (int k = 1; k < 7; k++) printf(" %d:%.1f%%", k + 1, 100.0 * (double)all.po_place[k] / (double)(po ? po : 1));
    printf("\n  mean strength NL %.1f  N/S %.1f  step 3 %.1f\n",
           (double)all.str_sum[0] / (double)all.str_cnt[0], (double)all.str_sum[1] / (double)all.str_cnt[1],
           (double)all.str_sum[2] / (double)all.str_cnt[2]);
    printf("  drift (mean strength by tenth of the chain) NL / N-S / step 3:\n   ");
    for (int z = 0; z < 10; z++) if (all.dec_cnt[z])
        printf(" %ld/%ld/%ld", all.dec_sum[0][z] / all.dec_cnt[z], all.dec_sum[1][z] / all.dec_cnt[z], all.dec_sum[2][z] / all.dec_cnt[z]);
    printf("\n");
    const int ok = all.bad == 0 && all.idem_bad == 0 && all.po_place[0] == 0 && all.po_place[7] == 0;
    printf("%s masssim\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
