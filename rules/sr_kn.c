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
 * field swaps (C.U. 253 3.3: "inversione del campo"). M2: drawn rounds (the previous winners and the round's
 * joiners, drawn openly or within each zone, a joiner hosting) and the zones by proximity (the Coppa Italia Serie
 * C's geographic groups, Lega Pro C.U. n. 36/L 2.1-2.2: four groups of 14, each seed to the nearest group).
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

/* A drawn round's sides into out[first ..): the previous round's winners (their zones) and this round's joiners,
   drawn within each zone (or all as one); under home "joiner" each joiner against a club of the earlier round. The
   ties made; -1 they do not fit. */
static int32_t kn_draw(const sr_kn_round *rd, int32_t r, const sr_kn_entry *join, uint32_t *rng, sr_kn_work *w,
                       sr_kn_tie *out)
{
    int32_t m = 0, made = 0;
    if (r > 0)
        for (int32_t t = 0; t < rd[r - 1].ties; t++) {
            const sr_kn_tie *p = &out[rd[r - 1].first + t];
            if (m >= SR_KN_NUMBERS) return -1;
            w->pool[m] = p->winner; w->pnum[m] = p->nwin; w->pzone[m] = p->zone; w->pjoin[m++] = 0;
        }
    for (int32_t k = 0; join && k < rd[r].njoin; k++) {
        const sr_kn_entry *e = &join[rd[r].jfirst + k];
        if (m >= SR_KN_NUMBERS) return -1;
        w->pool[m] = e->club; w->pnum[m] = 0; w->pzone[m] = e->zone; w->pjoin[m++] = rd[r].home == SR_KN_JOINER;
    }
    for (int32_t z = 0; z < (rd[r].pairing == SR_KN_PAIR_ZONE ? SR_KN_ZONES : 1); z++) {
        u16 *idx = w->cand;                         /* (the zone's members: indexes into the pool) */
        int32_t n = 0, nj = 0, at;
        for (int32_t i = 0; i < m; i++)
            if (rd[r].pairing != SR_KN_PAIR_ZONE || w->pzone[i] == z) idx[n++] = (u16)i;
        for (int32_t i = n - 1; i > 0; i--) {       /* (the draw) */
            const int32_t j = (int32_t)below(rng, (u32)i + 1);
            const u16 x = idx[i]; idx[i] = idx[j]; idx[j] = x;
        }
        for (int32_t i = 0; i < n; i++)             /* (the joiners first, in drawn order) */
            if (w->pjoin[idx[i]]) { const u16 x = idx[i]; idx[i] = idx[nj]; idx[nj++] = x; }
        if (n & 1 || nj * 2 > n) return -1;
        at = nj;                                    /* (joiner i v idx[nj + i]; the rest in pairs) */
        for (int32_t i = 0; i < n / 2; i++) {
            const u16 a = i < nj ? idx[i] : idx[at++], b = idx[at++];
            sr_kn_tie *o;
            if (made >= rd[r].ties) return -1;
            o = &out[rd[r].first + made++];
            o->home = w->pool[a]; o->away = w->pool[b];
            o->nhome = w->pnum[a]; o->naway = w->pnum[b];
            o->zone = w->pzone[a];
            o->flags = w->pjoin[a] ? 4 : 0;         /* (until played: the home side joined here) */
        }
    }
    return made == rd[r].ties ? made : -1;
}

int32_t sr_kn_cup(const sr_kn_round *rd, int32_t nr, const sr_kn_pair *pairs, const uint16_t *numbers, int32_t nn,
                  const sr_kn_entry *join, const uint8_t *swap, uint32_t *rng, sr_kn_play_fn play, void *ctx,
                  sr_kn_work *w, sr_kn_tie *out)
{
    int32_t last = -1;
    for (int32_t r = 0; r < nr; r++) {
        const int32_t drawn = rd[r].pairing != SR_KN_PAIR_FIXED;
        if (drawn && (!w || kn_draw(rd, r, join, rng, w, out) < 0)) return -1;
        for (int32_t t = 0; t < rd[r].ties; t++) {
            const int32_t p = rd[r].first + t;
            u16 club[2], num[2];
            uint8_t zone = 0, joined = 0;
            sr_kn_tie *o = &out[p];
            if (drawn) {
                club[0] = o->home; club[1] = o->away; num[0] = o->nhome; num[1] = o->naway;
                zone = o->zone; joined = o->flags & 4;
            } else
                for (int32_t s = 0; s < 2; s++) {
                    const u16 v = s ? pairs[p].b : pairs[p].a;
                    if (v & 0x8000u) {              /* an earlier tie's winner: its club, its entry number */
                        const int32_t wr = (v >> 9) & 0x3F, wt = v & 0x1FF;
                        if (wr >= r || wt >= rd[wr].ties) return -1;
                        club[s] = out[rd[wr].first + wt].winner;
                        num[s] = out[rd[wr].first + wt].nwin;
                        if (!s) zone = out[rd[wr].first + wt].zone;
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
            case SR_KN_JOINER: h = joined ? 0 : (int32_t)below(rng, 2); break;
            default: break;                         /* (fixed: as listed) */
            }
            if (swap && swap[p]) h ^= 1;
            o->home = club[h]; o->away = club[h ^ 1];
            o->nhome = num[h]; o->naway = num[h ^ 1];
            o->legs = rd[r].legs;
            o->zone = zone;
            const int32_t a = play(ctx, r, t, o);
            if (a < 0) return -1;
            o->winner = a ? o->away : o->home;
            o->nwin = a ? o->naway : o->nhome;
            last = o->winner;
        }
    }
    return last;
}

int32_t sr_kn_bracket(const sr_kn_round *rd, int32_t nr, const sr_kn_pair *pairs, const uint16_t *numbers,
                      int32_t nn, const uint8_t *swap, uint32_t *rng, sr_kn_play_fn play, void *ctx, sr_kn_tie *out)
{
    return sr_kn_cup(rd, nr, pairs, numbers, nn, 0, swap, rng, play, ctx, 0, out);
}

int32_t sr_kn_zones(const sr_kn_geo *geo, const uint16_t *clubs, int32_t n, const sr_kn_geo *centre, int32_t nz,
                    int32_t cap, sr_kn_work *w, uint8_t *zone)
{
    int32_t left[SR_KN_ZONES];
    if (nz < 1 || nz > SR_KN_ZONES || n < 0 || n > nz * cap || n > SR_KN_NUMBERS) return -1;
    for (int32_t z = 0; z < nz; z++) left[z] = cap;
    for (int32_t i = 0; i < n; i++) w->taken[i] = 0;
    for (int32_t k = 0; k < n; k++) {
        int32_t bi = -1, bz = 0;
        u32 breg = 0;
        for (int32_t i = 0; i < n; i++) {
            u32 d1 = 0xFFFFFFFFu, d2 = 0xFFFFFFFFu;
            int32_t z1 = 0;
            if (w->taken[i]) continue;
            for (int32_t z = 0; z < nz; z++) {
                if (!left[z]) continue;
                const int32_t dy = geo[clubs[i]].lat - centre[z].lat;
                const int32_t dx = (geo[clubs[i]].lon - centre[z].lon) * 761 / 1024;   /* (cos 42 = 0.743) */
                const u32 d = (u32)(dy * dy + dx * dx);
                if (d < d1) { d2 = d1; d1 = d; z1 = z; } else if (d < d2) d2 = d;
            }
            const u32 reg = d2 == 0xFFFFFFFFu ? 0xFFFFFFFFu : d2 - d1;
            if (bi < 0 || reg > breg) { bi = i; bz = z1; breg = reg; }
        }
        w->taken[bi] = 1;
        zone[bi] = (uint8_t)bz;
        left[bz]--;
    }
    return 0;
}

uint32_t sr_kn_work_size(void)
{
    return (uint32_t)sizeof(sr_kn_work);
}
