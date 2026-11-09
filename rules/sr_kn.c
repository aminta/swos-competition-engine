/*
 * sr_kn.c -- 42-s2ko: a knockout's bracket numbers and its fixed bracket, as the Coppa Italia plays them (Lega
 * Serie A C.U. n. 253 of 27/05/2024, the regulation 2024-25 .. 2026-27, and its Allegato 1).
 *
 * The numbers: groups of last season's clubs numbered 1, 2, ... in order -- each group takes its clubs from the
 * caller's lists (a league's table, the clubs promoted out of it, a cup's winner ...), skips a club already
 * numbered or not eligible (not in this season's leagues), in list order or by the record (points per match, wins,
 * goals for, then a draw: C.U. 253 3.1 f/g as applied to groups of unequal size), and fills what it lacks from its
 * 'else' lists (place, then points per match: "la meglio classificata ex art. 49 NOIF").
 *
 * The bracket: rounds of fixed pairs, a side being a number (that club enters here) or an earlier tie's winner;
 * the host by the lower entry number (the second leg's, in two legs), neutral, drawn or as listed; a first season's
 * field swaps (C.U. 253 3.3: "inversione del campo").
 *
 * Copyright (c) 2026 Davide Lorigliola. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint16_t u16;
typedef uint32_t u32;

static u32 below(u32 *rng, u32 n)
{
    *rng = *rng * 1103515245u + 12345u;
    return ((*rng >> 16) * n) >> 16;
}

/* a before b: 1 */
static int kn_before(const sr_kn_rec *rec, const u32 *key, u16 a, u16 b, int32_t order)
{
    const sr_kn_rec *x = &rec[a], *y = &rec[b];
    u32 l, r;
    if (order == SR_KN_PLACE_RECORD && x->place != y->place) return x->place < y->place;
    l = (u32)x->pts * (y->pld ? y->pld : 1);
    r = (u32)y->pts * (x->pld ? x->pld : 1);
    if (l != r) return l > r;
    if (order == SR_KN_RECORD) {
        if (x->won != y->won) return x->won > y->won;
        if (x->gf != y->gf) return x->gf > y->gf;
    }
    return key[a] < key[b];
}

/* the candidates of lists ls[0 .. n) not numbered yet and eligible, in order, into c; their count */
static int32_t kn_cands(const sr_kn_list *lists, const uint8_t *ls, int32_t n, const uint8_t *taken,
                        const uint8_t *eligible, u16 *c, int32_t max)
{
    int32_t m = 0;
    for (int32_t i = 0; i < n; i++) {
        const sr_kn_list *l = &lists[ls[i]];
        for (int32_t hop = 0; l && l->n < 0; hop++)  /* (unknown: its fallback, as the spec chains them) */
            l = hop < 16 && l->unknown >= 0 ? &lists[l->unknown] : 0;
        if (!l) continue;
        for (int32_t j = 0; j < l->n; j++) {
            const u16 k = l->club[j];
            int32_t dup = 0;
            if (k >= SR_KN_CLUBS || taken[k] || !eligible[k]) continue;
            for (int32_t q = 0; q < m; q++) dup |= c[q] == k;
            if (!dup && m < max) c[m++] = k;
        }
    }
    return m;
}

static void kn_sort(u16 *c, int32_t m, const sr_kn_rec *rec, const u32 *key, int32_t order)
{
    for (int32_t i = 1; i < m; i++) {               /* (insertion: stable, a handful of clubs) */
        const u16 v = c[i];
        int32_t j = i;
        while (j > 0 && kn_before(rec, key, v, c[j - 1], order)) { c[j] = c[j - 1]; j--; }
        c[j] = v;
    }
}

int32_t sr_kn_number(const sr_kn_group *g, int32_t ng, const sr_kn_list *lists, const sr_kn_rec *rec,
                     const uint8_t *eligible, uint32_t *rng, sr_kn_work *w, uint16_t *out, int32_t max)
{
    int32_t n = 0;
    for (int32_t k = 0; k < SR_KN_CLUBS; k++) { w->taken[k] = 0; w->key[k] = below(rng, 0x10000u); }
    for (int32_t gi = 0; gi < ng; gi++) {
        int32_t m = kn_cands(lists, g[gi].from, g[gi].nfrom, w->taken, eligible, w->cand, SR_KN_NUMBERS), want = g[gi].take;
        if (g[gi].order != SR_KN_LIST) kn_sort(w->cand, m, rec, w->key, g[gi].order);
        for (int32_t i = 0; i < m && want; i++, want--) {
            if (n >= max) return -1;
            out[n++] = w->cand[i];
            w->taken[w->cand[i]] = 1;
        }
        if (want) {                                 /* (short: the group's 'else') */
            m = kn_cands(lists, g[gi].els, g[gi].nelse, w->taken, eligible, w->cand, SR_KN_NUMBERS);
            if (g[gi].else_order != SR_KN_LIST) kn_sort(w->cand, m, rec, w->key, g[gi].else_order);
            for (int32_t i = 0; i < m && want; i++, want--) {
                if (n >= max) return -1;
                out[n++] = w->cand[i];
                w->taken[w->cand[i]] = 1;
            }
        }
        if (want) return -1;                        /* (nobody left to number) */
    }
    return n;
}

int32_t sr_kn_bracket(const sr_kn_round *rd, int32_t nr, const sr_kn_pair *pairs, const uint16_t *numbers,
                      int32_t nn, const uint8_t *swap, uint32_t *rng, sr_kn_play_fn play, void *ctx, sr_kn_tie *out)
{
    int32_t last = -1;
    for (int32_t r = 0; r < nr; r++)
        for (int32_t t = 0; t < rd[r].ties; t++) {
            const int32_t p = rd[r].first + t;
            u16 club[2], num[2];
            sr_kn_tie *o = &out[p];
            for (int32_t s = 0; s < 2; s++) {
                const u16 v = s ? pairs[p].b : pairs[p].a;
                if (v & 0x8000u) {                  /* an earlier tie's winner: its club, its entry number */
                    const int32_t wr = (v >> 9) & 0x3F, wt = v & 0x1FF;
                    if (wr >= r || wt >= rd[wr].ties) return -1;
                    club[s] = out[rd[wr].first + wt].winner;
                    num[s] = out[rd[wr].first + wt].nwin;
                } else {
                    if (v < 1 || v > nn) return -1;
                    club[s] = numbers[v - 1];
                    num[s] = v;
                }
            }
            int32_t h = 0;                          /* (the side at home: in two legs, the first leg's) */
            o->flags = 0;
            switch (rd[r].home) {
            case SR_KN_LOWER: h = num[1] < num[0]; break;
            case SR_KN_LOWER_2ND: h = num[1] > num[0]; o->flags |= SR_KN_F_2ND; break;
            case SR_KN_NEUTRAL: o->flags |= SR_KN_F_NEUTRAL; break;
            case SR_KN_DRAW: h = (int32_t)below(rng, 2); break;
            default: break;                         /* (fixed: as listed) */
            }
            if (swap && swap[p]) h ^= 1;
            o->home = club[h]; o->away = club[h ^ 1];
            o->nhome = num[h]; o->naway = num[h ^ 1];
            o->legs = rd[r].legs;
            const int32_t a = play(ctx, r, t, o);
            if (a < 0) return -1;
            o->winner = a ? o->away : o->home;
            o->nwin = a ? o->naway : o->nhome;
            last = o->winner;
        }
    return last;
}

uint32_t sr_kn_work_size(void)
{
    return (uint32_t)sizeof(sr_kn_work);
}
