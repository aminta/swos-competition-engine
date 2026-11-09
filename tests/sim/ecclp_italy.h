/* Eccellenza 2026-27's 28 groups as the library's shape (42-ecclp): size, national play-off urn, regional play-off.
 * Source: the Mod's internal/natcomp2627/eccellenza2627.json (it.wiki group pages, fetched 2026-11-08), the rules as
 * written there. Gaps: "pari o superiore a N" = N, "superiore a N" / "più di N" = N + 1.
 * Not stated on the group page, taken from the regional committee's comunicato ufficiale (fetched 2026-11-09):
 *   Campania  CU n.38 of 16/10/2026, attachment "Play off e play out ECCELLENZA 2026-2027";
 *   Lazio     CU n.26 of 31/08/2026 (both rounds, gap "superiore a 8" = 9);
 *   Sardegna  CU n.35 of 9/10/2026, attachment "Meccanismo promozioni, retrocessioni ... 2026/2027".
 * Not yet published by the committee (Veneto CU 1-41, Molise CU 1-65, Calabria): the standard LND scheme (2v5, 3v4
 * at the better placed's ground, then the final; after extra time the better placed), a PROPOSAL for Davide, no
 * gap. The pack's pool file is the real source; this table is test data. Urns: the LND's draw (10 North, 10 Centre
 * with Emilia-Romagna, 8 South). */
#ifndef ECCLP_ITALY_H
#define ECCLP_ITALY_H
#include "swosrules.h"

enum { IT_GROUPS = 28, IT_URNS = 3, IT_UP = 36 };

typedef struct { const char *name; int size; uint8_t urn; sr_lp_rpo rpo; } it_group;

#define FOUR(gs, gf, dg, db, fn) { SR_LP_RPO_FOUR, 1, gs, gf, dg, db, fn }
static const it_group IT_G[IT_GROUPS] = {
    { "Piemonte-Valle d'Aosta/A", 16, 0, FOUR(8, 8, 0, 0, 0) },
    { "Piemonte-Valle d'Aosta/B", 16, 0, FOUR(8, 8, 0, 0, 0) },
    { "Lombardia/A",              16, 0, FOUR(10, 0, 0, 1, 0) },   /* a draw after 90': the better placed */
    { "Lombardia/B",              16, 0, FOUR(10, 0, 0, 1, 0) },
    { "Lombardia/C",              16, 0, FOUR(10, 0, 0, 1, 0) },
    { "Liguria",                  16, 0, FOUR(7, 0, 7, 0, 0) },
    { "Veneto/A",                 16, 0, FOUR(0, 0, 0, 1, 0) },    /* proposal */
    { "Veneto/B",                 16, 0, FOUR(0, 0, 0, 1, 0) },
    { "Friuli-Venezia Giulia",    18, 0, { SR_LP_RPO_DIRECT, 1, 0, 0, 0, 0, 0 } },
    { "Trentino-Alto Adige",      16, 0, { SR_LP_RPO_DIRECT, 1, 0, 0, 0, 0, 0 } },
    { "Emilia-Romagna/A",         18, 1, FOUR(0, 0, 0, 1, 0) },
    { "Emilia-Romagna/B",         18, 1, FOUR(0, 0, 0, 1, 0) },
    { "Toscana/A",                16, 1, FOUR(10, 0, 10, 0, 1) },  /* the final on neutral ground */
    { "Toscana/B",                16, 1, FOUR(10, 0, 10, 0, 1) },
    { "Umbria",                   16, 1, FOUR(10, 10, 0, 1, 0) },
    { "Marche",                   16, 1, FOUR(11, 11, 0, 0, 0) },
    { "Lazio/A",                  18, 1, { SR_LP_RPO_LADDER, 1, 9, 9, 9, 1, 0 } },   /* 3v4, the winner v the 2nd */
    { "Lazio/B",                  18, 1, { SR_LP_RPO_LADDER, 1, 9, 9, 9, 1, 0 } },
    { "Abruzzo",                  18, 1, FOUR(8, 8, 0, 1, 0) },
    { "Sardegna",                 16, 1, FOUR(10, 0, 10, 1, 0) },
    { "Campania/A",               18, 2, FOUR(10, 0, 10, 1, 0) },
    { "Campania/B",               18, 2, FOUR(10, 0, 10, 1, 0) },
    { "Molise",                   15, 2, FOUR(0, 0, 0, 1, 0) },    /* proposal */
    { "Puglia",                   18, 2, FOUR(7, 0, 8, 0, 0) },
    { "Basilicata",               16, 2, FOUR(10, 10, 0, 0, 0) },
    { "Calabria",                 16, 2, FOUR(0, 0, 0, 1, 0) },    /* proposal */
    { "Sicilia/A",                16, 2, FOUR(10, 10, 10, 0, 0) },
    { "Sicilia/B",                16, 2, FOUR(10, 10, 10, 0, 0) },
};
#undef FOUR

/* round 2: a North club v a Centre club, South v South */
static const uint8_t IT_PAIR[2][2] = { { 0, 1 }, { 2, 2 } };

static inline void it_shape(sr_lp_shape *s, sr_lp_rpo *rpo, uint8_t *urn, const uint16_t *override, int nov)
{
    for (int g = 0; g < IT_GROUPS; g++) { rpo[g] = IT_G[g].rpo; urn[g] = IT_G[g].urn; }
    s->rpo = rpo; s->urn = urn; s->nu = IT_URNS; s->pair = IT_PAIR; s->np = 2;
    s->legs1 = 2; s->legs2 = 2; s->extra = 1; s->override = override; s->nov = nov; s->spread = -1;
}
#endif
