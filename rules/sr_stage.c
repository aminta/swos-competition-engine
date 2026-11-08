/*
 * sr_stage.c -- the generic stages of a spec's competitions (swc/1.2), as the competition editor writes
 * them: a round robin's calendar, a table ranked by the spec's tie-breakers, a knockout's round sizes and
 * one tie decided by the spec's deciders, a groups stage's group sizes. Nothing in either game plays them
 * yet (they are the new engine's first pieces); the codes are swcc's (SR_TB_* / SR_D_*).
 *
 * Every draw comes from a hash of the seed and the club (or the tie), never a running generator: the same
 * clubs give the same answer whatever the order they come in.
 *
 * Copyright (c) 2026 Davide Lorigliola. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  i32;

enum { N = SR_STAGE_CLUBS };

static u32 mix(u32 h, u32 v)
{
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    return h ^ (h >> 16);
}

/* ---- the round robin's calendar ---- */

/* One leg by the circle method over m = n or n + 1 slots (slot m - 1 fixed, the others turning; with n odd
 * the slot n is the rest). The fixed slot's match swaps home every matchday; the others take the home side
 * by their place on the circle, which keeps each club's home matches within one of each other. */
static i32 leg(i32 n, i32 md0, int swap, sr_match *out)
{
    const i32 m = n + (n & 1);
    i32 k = 0;
    for (i32 r = 0; r < m - 1; r++) {
        for (i32 i = 0; i < m / 2; i++) {
            const i32 a = i == 0 ? m - 1 : (r + i) % (m - 1), b = (r + m - 1 - i) % (m - 1);
            if (a >= n || b >= n) continue;                     /* the rest */
            int a_home = i == 0 ? (r & 1) : (i & 1);
            if (swap) a_home = !a_home;
            out[k].md = (u8)(md0 + r);
            out[k].home = (u8)(a_home ? a : b);
            out[k].away = (u8)(a_home ? b : a);
            out[k].played = out[k].hg = out[k].ag = 0;
            k++;
        }
    }
    return k;
}

i32 sr_rr_calendar(i32 n, i32 legs, sr_match *out, i32 room)
{
    if (n < 2 || n > N || legs < 1 || legs > 4) return -1;
    const i32 per = n * (n - 1) / 2, mds = n - 1 + (n & 1);
    if (room < legs * per) return -1;
    i32 k = 0;
    for (i32 l = 0; l < legs; l++) k += leg(n, l * mds, l & 1, out + k);
    return k;
}

/* ---- the table ---- */

static int usable(u8 c)
{
    return c == SR_TB_H2H_REAPPLIED || (c >= SR_TB_POINTS && c <= SR_TB_NAME && c != SR_TB_FAIR_PLAY &&
                                        c != SR_TB_COEFFICIENT && c != SR_TB_PREVIOUS_STAGE_RANK && c != SR_TB_PLAY_OFF_MATCH);
}

/* the key of club c under criterion t (higher is better); the head-to-head ones over the clubs in `in` */
static i32 key(u8 t, const sr_srow *row, i32 c, const sr_match *m, i32 nm, const u8 *in,
               i32 win, i32 draw, i32 loss, u32 seed)
{
    const sr_srow *r = &row[c];
    switch (t) {
    case SR_TB_POINTS:          return r->pts;
    case SR_TB_GOAL_DIFFERENCE: return (i32)r->gf - (i32)r->ga;
    case SR_TB_GOALS_FOR:       return r->gf;
    case SR_TB_GOALS_AGAINST:   return -(i32)r->ga;
    case SR_TB_WINS:            return r->w;
    case SR_TB_AWAY_WINS:       return r->aw;
    case SR_TB_AWAY_GOALS_FOR:  return r->agf;
    case SR_TB_DRAW_LOTS:       return (i32)(mix(seed, 0x51A6E000u + (u32)c) >> 1);
    case SR_TB_NAME:            return -c;
    default: break;
    }
    i32 pts = 0, gd = 0, gf = 0, ag = 0;                        /* head to head */
    for (i32 k = 0; k < nm; k++) {
        const sr_match *x = &m[k];
        if (!x->played || !in[x->home] || !in[x->away] || (x->home != c && x->away != c)) continue;
        const int home = x->home == c;
        const i32 f = home ? x->hg : x->ag, a = home ? x->ag : x->hg;
        pts += f > a ? win : f == a ? draw : loss;
        gd += f - a; gf += f;
        if (!home) ag += f;
    }
    return t == SR_TB_H2H_POINTS ? pts : t == SR_TB_H2H_GOAL_DIFFERENCE ? gd : t == SR_TB_H2H_GOALS_FOR ? gf : ag;
}

typedef struct { const sr_srow *row; const sr_match *m; i32 nm, n, win, draw, loss; u32 seed; } sr_tctx;

static void block_in(i32 n, const u8 *ord, i32 b, i32 e, u8 *in)
{
    for (i32 k = 0; k < n; k++) in[k] = 0;
    for (i32 k = b; k < e; k++) in[ord[k]] = 1;
}

/* criterion t inside ord[b..e) (one tied block): a stable sort, best first, then a cut between unequal keys */
static void split(const sr_tctx *x, u8 t, const u8 *in, u8 *ord, u8 *cut, i32 b, i32 e)
{
    i32 kv[N];
    for (i32 k = b; k < e; k++) kv[ord[k]] = key(t, x->row, ord[k], x->m, x->nm, in, x->win, x->draw, x->loss, x->seed);
    for (i32 k = b + 1; k < e; k++) {
        const u8 c = ord[k];
        i32 j = k;
        while (j > b && kv[ord[j - 1]] < kv[c]) { ord[j] = ord[j - 1]; j--; }
        ord[j] = c;
    }
    for (i32 k = b + 1; k < e; k++) if (kv[ord[k]] != kv[ord[k - 1]]) cut[k] = 1;
}

/* head_to_head_reapplied (UEFA): head-to-head points, goal difference, goals for among the tied clubs; a smaller
 * block still tied after them takes the three again over its own matches only, until nothing splits */
static void reapply(const sr_tctx *x, u8 *ord, u8 *cut, i32 b, i32 e)
{
    u8 in[N];
    block_in(x->n, ord, b, e, in);
    for (u8 t = SR_TB_H2H_POINTS; t <= SR_TB_H2H_GOALS_FOR; t++)
        for (i32 s = b; s < e;) {
            i32 f = s + 1;
            while (f < e && !cut[f]) f++;
            if (f - s > 1) split(x, t, in, ord, cut, s, f);
            s = f;
        }
    for (i32 s = b; s < e;) {
        i32 f = s + 1;
        while (f < e && !cut[f]) f++;
        if (f - s > 1 && f - s < e - b) reapply(x, ord, cut, s, f);
        s = f;
    }
}

i32 sr_rr_table(i32 n, const sr_match *m, i32 nm, i32 win, i32 draw, i32 loss, const u8 *tieb, u32 seed,
                sr_srow *out)
{
    if (n < 1 || n > N) return -1;
    for (const u8 *t = tieb; t && *t; t++) if (!usable(*t)) return -1;
    sr_srow row[N];
    for (i32 c = 0; c < n; c++) {
        row[c].club = (u8)c; row[c].pad = 0; row[c].pts = 0;
        row[c].pl = row[c].w = row[c].d = row[c].l = row[c].gf = row[c].ga = row[c].aw = row[c].agf = 0;
    }
    for (i32 k = 0; k < nm; k++) {
        const sr_match *x = &m[k];
        if (x->home >= n || x->away >= n || x->home == x->away) return -1;
        if (!x->played) continue;
        sr_srow *h = &row[x->home], *a = &row[x->away];
        h->pl++; a->pl++;
        h->gf = (u16)(h->gf + x->hg); h->ga = (u16)(h->ga + x->ag);
        a->gf = (u16)(a->gf + x->ag); a->ga = (u16)(a->ga + x->hg);
        a->agf = (u16)(a->agf + x->ag);
        if (x->hg > x->ag)      { h->w++; a->l++; h->pts = (int16_t)(h->pts + win); a->pts = (int16_t)(a->pts + loss); }
        else if (x->hg < x->ag) { a->w++; a->aw++; h->l++; a->pts = (int16_t)(a->pts + win); h->pts = (int16_t)(h->pts + loss); }
        else                    { h->d++; a->d++; h->pts = (int16_t)(h->pts + draw); a->pts = (int16_t)(a->pts + draw); }
    }
    /* each criterion once, in order, inside the blocks the ones before left tied (a stable sort there) */
    u8 ord[N], cut[N + 1];                                      /* cut[k]: a block starts at k */
    for (i32 k = 0; k < n; k++) { ord[k] = (u8)k; cut[k] = k == 0; }
    cut[n] = 1;
    const sr_tctx x = { row, m, nm, n, win, draw, loss, seed };
    for (const u8 *t = tieb; t && *t; t++) {
        for (i32 b = 0; b < n;) {
            i32 e = b + 1;
            while (!cut[e]) e++;
            if (e - b > 1) {
                if (*t == SR_TB_H2H_REAPPLIED) reapply(&x, ord, cut, b, e);
                else {
                    u8 in[N];
                    block_in(n, ord, b, e, in);
                    split(&x, *t, in, ord, cut, b, e);
                }
            }
            b = e;
        }
    }
    for (i32 k = 0; k < n; k++) {                               /* (field by field: a struct copy is a memcpy call) */
        const sr_srow *r = &row[ord[k]];
        sr_srow *o = &out[k];
        o->club = r->club; o->pad = 0; o->pts = r->pts; o->pl = r->pl; o->w = r->w; o->d = r->d; o->l = r->l;
        o->gf = r->gf; o->ga = r->ga; o->aw = r->aw; o->agf = r->agf;
    }
    return n;
}

/* ---- knockouts ---- */

i32 sr_ko_left(i32 entrants, const sr_round_size *r, i32 nr)
{
    i32 left = entrants;
    for (i32 k = 0; k < nr; k++) {
        if (2 * (i32)r[k].ties + (i32)r[k].byes != left) return -(k + 1);
        left = r[k].ties + r[k].byes;
    }
    return left;
}

i32 sr_ko_tie(const sr_leg *leg_, i32 nlegs, const sr_tie_extra *x, const u8 *deci, u32 seed)
{
    i32 tot[2] = { 0, 0 }, away[2] = { 0, 0 };
    for (i32 k = 0; k < nlegs; k++) {
        tot[0] += leg_[k].g[0]; tot[1] += leg_[k].g[1];
        if (leg_[k].home < 2) away[!leg_[k].home] += leg_[k].g[!leg_[k].home];
    }
    if (tot[0] != tot[1]) return tot[1] > tot[0];
    for (const u8 *d = deci; d && *d; d++) {
        switch (*d) {
        case SR_D_AGGREGATE: break;                             /* (level already) */
        case SR_D_AWAY_GOALS:
            if (away[0] != away[1]) return away[1] > away[0];
            break;
        case SR_D_EXTRA_TIME: case SR_D_GOLDEN_GOAL:
            if (!x || !x->et_played) return SR_TIE_OPEN;
            tot[0] += x->et[0]; tot[1] += x->et[1];
            if (nlegs && leg_[nlegs - 1].home < 2) {            /* extra time's away goals: the last leg's visitors' */
                const i32 v = !leg_[nlegs - 1].home;
                away[v] += x->et[v];
            }
            if (tot[0] != tot[1]) return tot[1] > tot[0];
            break;
        case SR_D_PENALTIES:
            if (!x || !x->pens_played || x->pens[0] == x->pens[1]) return SR_TIE_OPEN;
            return x->pens[1] > x->pens[0];
        case SR_D_BETTER_PLACED:
            if (x && x->place[0] != x->place[1]) return x->place[1] < x->place[0];
            break;
        case SR_D_REPLAY: return SR_TIE_REPLAY;
        case SR_D_DRAW_LOTS: return (i32)(mix(seed, 0x7E1Eu) & 1);
        default: return SR_TIE_OPEN;
        }
    }
    return SR_TIE_OPEN;
}

/* ---- groups ---- */

i32 sr_group_sizes(i32 n, i32 g, u8 *size)
{
    if (g < 1 || n / g < 2 || (n + g - 1) / g > 255) return -1;
    for (i32 k = 0; k < g; k++) size[k] = (u8)(n / g + (k < n % g));
    return g;
}
