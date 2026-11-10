/* bench FILE [v|d]: sr_group_draw on every shape of FILE (gen.py, inst.txt): ok / -2 / other counts, every draw checked
   (sizes, one club of a pot per group, associations apart), the worst time; v = a line per draw, d = its groups.
   BENCH_PAIRS=1: each shape ends with np and np protect pairs (gen.py chain / uefachain): sr_group_draw_p, pairs checked. */
#include "swosrules.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int np_; static uint8_t pr_[2 * SR_PROT_PAIRS];
static int valid(int n, int g, const uint8_t *a, const uint8_t *gr) {
    for (int k = 0; k < np_; k++) for (int i = 0; i < n; i++) for (int j = 0; j < n; j++)
        if (gr[i] == gr[j] && a[i] == pr_[2 * k] && a[j] == pr_[2 * k + 1]) return 0;
    uint8_t sz[64]; if (sr_group_sizes(n, g, sz) < 0) return 0; int c[64] = {0};
    for (int i = 0; i < n; i++) { if (gr[i] >= g) return 0; c[gr[i]]++; int same = 0; for (int j = 0; j < i; j++) same += gr[i] == gr[j] && a[i] == a[j];
        if (!getenv("BENCH_MIN") && same >= (getenv("BENCH_CAP") ? atoi(getenv("BENCH_CAP")) : 1)) return 0; }
    if (getenv("BENCH_MIN"))                                  /* (swc/1.7 BENCH_MIN: floor / ceil m/g a group) */
        for (int i = 0; i < n; i++) { int m = 0, h[64] = {0}; for (int j = 0; j < n; j++) if (a[j] == a[i]) { m++; h[gr[j]]++; }
            for (int x = 0; x < g; x++) if (h[x] < m / g || h[x] > (m + g - 1) / g) return 0; }   /* (BENCH_CAP: that many of an association per group) */
    for (int i = 0; i < g; i++) if (c[i] != sz[i]) return 0;
    for (int i = 0; i < n; i++) for (int j = 0; j < i; j++) if (i / g == j / g && gr[i] == gr[j]) return 0;   /* one per pot */
    return 1; }
int main(int argc, char **argv) {
    double worst = 0, tot = 0; int cnt[5] = {0}, bad = 0, k = 0;
    uint32_t s; int n, g;
    FILE *f = fopen(argv[1], "r");
    while (fscanf(f, "%u %d %d", &s, &n, &g) == 3) {
        uint8_t a[64], gr[64]; for (int i = 0; i < n; i++) { unsigned x; fscanf(f, "%u", &x); a[i] = (uint8_t)x; }
        np_ = 0;
        if (getenv("BENCH_PAIRS")) { fscanf(f, "%d", &np_); for (int i = 0; i < 2 * np_; i++) { unsigned x; fscanf(f, "%u", &x); pr_[i] = (uint8_t)x; } }
        double t = now(); int r = getenv("BENCH_MIN") ? sr_group_draw_m(n, g, a, s, gr) : getenv("BENCH_CAP") ? sr_group_draw_k(n, g, a, 0, 0, atoi(getenv("BENCH_CAP")), s, gr) : np_ ? sr_group_draw_p(n, g, a, pr_, np_, s, gr) : sr_group_draw(n, g, a, s, gr); t = now() - t; tot += t; if (t > worst) worst = t;
        if (r == g) { cnt[0]++; if (!valid(n, g, a, gr)) bad++; } else if (r == -2) cnt[1]++; else if (r == -3) cnt[2]++; else cnt[3]++;
        if (argc > 2 && argv[2][0] == 100) { printf("%d %d", k, r); if (r == g) for (int i = 0; i < n; i++) printf(" %d", gr[i]); printf("\n"); } else if (argc > 2) printf("%d: n %d g %d -> %d %.3fs\n", k, n, g, r, t);
        k++; }
    printf("%d draws: ok %d, -2 %d, -3 %d, other %d, invalid %d; worst %.3fs, total %.2fs\n", k, cnt[0], cnt[1], cnt[2], cnt[3], bad, worst, tot);
}
