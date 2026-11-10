/* fuzz_swcc: swcc (the spec compiler) and the .SWC reader (format/) on anything (ASan / UBSan).
   Input: one mode byte, then the data.
     even: JSON text for swcc_compile; a NUL in it splits off a pool file's text, which the loader hands back for any
           pool file the spec names. The profile: swos9697 or openswos (mode bit 1). What swcc accepts must open in
           the reader (swc_spec_open SWC_OK), every string reachable.
     odd:  the bytes of a .SWC for swc_spec_open, the header's length and hash set to fit (so the fuzzer reaches the
           sections, not just the hash check).
   T5 (the sr_stage host plan): every groups stage of a spec that opens goes through the ledger item 'ST' as the
   glue would take it (rules/sr_stled.c: its shape, its comp's entrants as the key and CLUB.country apart, a result
   a group); a shape the item takes must begin (or refuse the draw: -2), check 1 and give every club one group.
   libFuzzer: make fuzz-swcc. Without -DFUZZ: a seeded driver (make test) over the repo's specs and their compiled
   bytes, 0 .. 4 changes each; any .fz given (a fuzzer's find) run as it was. A find in swcc becomes a spec of
   tests/specs/bad (run_specs: refused with its message). */
#include "../swcc/swcc.h"
#include "../format/swc_format.h"
#include "swosrules.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *prof[2];
static size_t profn[2];
static const uint8_t *pool;
static size_t pooln;

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long k;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    k = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)k + 1);
    if (!b || fread(b, 1, (size_t)k, f) != (size_t)k) { fclose(f); free(b); return NULL; }
    fclose(f);
    b[k] = 0;
    *n = (size_t)k;
    return b;
}

static char *load_pool(const char *path, const char *from, size_t *n)
{
    char *b;
    (void)path; (void)from;
    if (!pool) return NULL;
    b = malloc(pooln + 1);
    memcpy(b, pool, pooln);
    b[pooln] = 0;
    *n = pooln;
    return b;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static uint64_t compiled, opened, gstages, stled;

/* T5: a groups stage through 'ST' (the glue's inputs: the stage record, the comp's ENTR, CLUB.country) */
static void st_stage(const SwcSpec *s, const SwcStage *st)
{
    static uint8_t blk[16 + SR_STAGE_CLUBS + 4 * SR_STAGE_CLUBS * (SR_STAGE_CLUBS - 1) / 2], assoc[SR_STAGE_CLUBS];
    static uint16_t word[SR_STAGE_CLUBS];
    static sr_match m[4 * SR_STAGE_CLUBS * SR_STAGE_CLUBS / 2];
    uint8_t club[SR_STAGE_CLUBS];
    gstages++;
    const int32_t n = st->teams, g = st->groups, legs = st->legs, size = sr_st_size(n, g, legs);
    if (size < 0) return;
    if (size > (int32_t)sizeof blk) { fprintf(stderr, "fuzz_swcc: st_size %d past the largest block\n", (int)size); abort(); }
    const SwcComp *c = st->comp < s->ncomps ? &s->comps[st->comp] : NULL;
    for (int32_t k = 0; k < n; k++) {
        const uint32_t e = c && (uint32_t)k < c->nentr && c->entr + (uint32_t)k < s->nentr ? s->entr[c->entr + k] : 0xFFFFu;
        word[k] = (uint16_t)(e < s->nclubs ? e : 0xFF00u + (uint32_t)k);
        assoc[k] = (uint8_t)(e < s->nclubs ? s->clubs[e].country : 0xFF - k % 64);
    }
    const int32_t r = sr_st_begin(blk, (int32_t)sizeof blk, n, g, legs, 1, st->purpose, word, assoc, st->purpose ^ 0x5EED);
    if (r == -2) return;                        /* (an association with more clubs than groups: the draw refused) */
    if (r != size || sr_st_check(blk, r, n, g, legs, 1, st->purpose, word) != 1) {
        fprintf(stderr, "fuzz_swcc: stage %u (%d clubs, %d groups, %d legs): st_begin %d / size %d, not checked\n",
                (unsigned)st->id, (int)n, (int)g, (int)legs, (int)r, (int)size);
        abort();
    }
    int32_t total = 0, at = 0;                  /* (at: the group's first result, sr_st_record's index) */
    for (int32_t k = 0; k < g; k++) {
        const int32_t nm = sr_st_group(blk, k, club, m, (int32_t)(sizeof m / sizeof m[0]));
        if (nm < 0) { fprintf(stderr, "fuzz_swcc: st_group %d refused\n", (int)k); abort(); }
        int32_t sz = 0;
        while (sz < SR_STAGE_CLUBS && legs * sz * (sz - 1) / 2 < nm) sz++;
        total += sz;
        const int32_t rx = nm ? sr_st_record(blk, club[m[0].home], club[m[0].away], 2, 1, m, (int32_t)(sizeof m / sizeof m[0])) : at;
        if (rx != at) {
            fprintf(stderr, "fuzz_swcc: st_record, group %d's first match (n %d g %d legs %d, nm %d, home %d away %d)\n", (int)k, (int)n, (int)g, (int)legs, (int)nm, m[0].home, m[0].away);
            abort();
        }
        at += nm;
    }
    if (total != n || sr_st_check(blk, r, n, g, legs, 1, st->purpose, word) != 1) {
        fprintf(stderr, "fuzz_swcc: %d clubs in the groups of %d\n", (int)total, (int)n);
        abort();
    }
    stled++;
}

/* the reader on bin; returns its status. A host walks the strings through swc_str: every one is read to its NUL. */
static SwcStatus reader(const uint8_t *bin, size_t n)
{
    static uint8_t mem[1 << 20];
    static SwcArena a;
    SwcSpec s;
    SwcStatus st;
    uint32_t i;
    volatile size_t sink = 0;
    uint8_t *b = malloc(n ? n : 1);             /* (its own allocation: ASan sees a read past the end) */
    memcpy(b, bin, n);
    swc_init(&a, mem, sizeof mem);
    st = swc_spec_open(&s, b, (uint32_t)n, &a);
    if (st == SWC_OK) {
        opened++;
        for (i = 0; i < s.nclubs; i++) sink += strlen(swc_str(&s, s.clubs[i].id)) + strlen(swc_str(&s, s.clubs[i].name));
        for (i = 0; i < s.ncomps; i++) sink += strlen(swc_str(&s, s.comps[i].id));
        for (i = 0; i < s.nstages; i++) sink += strlen(swc_str(&s, s.stages[i].id));
        for (i = 0; i < s.nstages; i++) if (s.stages[i].type == SWC_ST_GROUPS) st_stage(&s, &s.stages[i]);
        for (i = 0; i < s.nmeta; i++) sink += strlen(swc_str(&s, s.meta[i].id)) + strlen(swc_str(&s, s.meta[i].version));
        for (i = 0; i < s.nnames; i++)
            for (int l = 0; l < 4; l++)
                for (int k = 0; k < 2; k++) sink += strlen(swc_str(&s, s.names[i].str[l][k]));
    }
    (void)sink;
    free(b);
    return st;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    if (n < 1 || n > 65536) return 0;
    const uint8_t mode = d[0];
    d++; n--;
    if (mode & 1) {                             /* the reader on raw bytes, length and hash fitted */
        uint8_t *b = malloc(n ? n : 1);
        memcpy(b, d, n);
        if (n >= 32) {
            uint32_t body = 32u + (uint32_t)((b[14] << 8 | b[15]) * 12u);
            put32(b + 24, (uint32_t)n);
            if (body <= n) put32(b + 16, swc_fnv1a(b + body, (uint32_t)(n - body)));
        }
        reader(b, n);
        free(b);
        return 0;
    }
    const uint8_t *nul = memchr(d, 0, n);
    const size_t sn = nul ? (size_t)(nul - d) : n;
    char *spec = malloc(sn ? sn : 1), *diag = NULL;
    unsigned char *bin = NULL;
    size_t bn = 0;
    memcpy(spec, d, sn);
    pool = nul ? nul + 1 : NULL;
    pooln = nul ? n - sn - 1 : 0;
    const int p = (mode >> 1) & 1;
    const int errors = swcc_compile(spec, sn, "fuzz.json", prof[p], profn[p], &bin, &bn, &diag);
    if (!diag) { fprintf(stderr, "fuzz_swcc: no diagnostics text\n"); abort(); }
    if (!errors) {
        SwcStatus st;
        if (!bin || !bn) { fprintf(stderr, "fuzz_swcc: 0 errors and no bytes\n"); abort(); }
        compiled++;
        if ((st = reader(bin, bn)) != SWC_OK) {
            fprintf(stderr, "fuzz_swcc: swcc accepted a spec the reader refuses (status %d)\n", st);
            abort();
        }
    }
    swcc_free(diag);
    swcc_free(bin);
    free(spec);
    return 0;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    if (!(prof[0] = slurp(FUZZ_PROFILES "/swos9697.json", &profn[0])) ||
        !(prof[1] = slurp(FUZZ_PROFILES "/openswos.json", &profn[1]))) { fprintf(stderr, "fuzz_swcc: no profiles\n"); exit(2); }
    swcc_set_loader(load_pool);
    return 0;
}

#ifndef FUZZ
int main(int argc, char **argv)
{
    static uint8_t *s[512], m[65537];
    static size_t len[512];
    int ns = 0, nreg = 0;
    const int it = argc > 1 ? atoi(argv[1]) : 20000;
    LLVMFuzzerInitialize(&argc, &argv);
    for (int a = 2; a < argc && ns < 510; a++) {
        const size_t al = strlen(argv[a]);
        size_t n;
        char *t = slurp(argv[a], &n);
        if (!t || n > 65536) { free(t); continue; }
        if (al > 3 && !strcmp(argv[a] + al - 3, ".fz")) {  /* (a fuzzer's find, as it was) */
            LLVMFuzzerTestOneInput((const uint8_t *)t, n);
            free(t);
            nreg++;
            continue;
        }
        /* the spec as JSON (mode 0), and its compiled bytes (mode 1) when swcc accepts it */
        s[ns] = malloc(n + 1); s[ns][0] = 0; memcpy(s[ns] + 1, t, n); len[ns++] = n + 1;
        {
            unsigned char *bin = NULL;
            size_t bn = 0;
            char *diag = NULL;
            if (!swcc_compile(t, n, argv[a], prof[0], profn[0], &bin, &bn, &diag) && bn <= 65536) {
                s[ns] = malloc(bn + 1); s[ns][0] = 1; memcpy(s[ns] + 1, bin, bn); len[ns++] = bn + 1;
            }
            swcc_free(diag);
            swcc_free(bin);
        }
        free(t);
    }
    if (!ns) { printf("FAIL: fuzz_swcc: no spec files\n"); return 1; }
    uint32_t r = 2026;
#define R(n) (r = r * 1103515245u + 12345u, (r >> 8) % (n))
    for (int k = 0; k < it; k++) {
        const int i = (int)R((uint32_t)ns);
        size_t n = len[i];
        memcpy(m, s[i], n);
        if (!(m[0] & 1)) m[0] = (uint8_t)(R(2) << 1);    /* (either profile) */
        for (int c = (int)R(5); c > 0; c--) {
            const size_t at = 1 + R((uint32_t)(n > 1 ? n - 1 : 1)), op = R(8);
            if (op < 5) { if (at < n) m[at] = (uint8_t)R(256); }
            else if (op < 7 && n < sizeof m) { memmove(m + at + 1, m + at, n - at); m[at] = (uint8_t)R(256); n++; }
            else n = at;
        }
        LLVMFuzzerTestOneInput(m, n);
    }
    printf("PASS: fuzz_swcc, %d finds of the fuzzer replayed; %d changed specs / .SWC files (%d seeds, 0 .. 4 changes each): %llu compiled (all open in the reader), %llu .SWC opened, %llu groups stages (%llu through 'ST'), no sanitizer report\n",
           nreg, it, ns, (unsigned long long)compiled, (unsigned long long)opened, (unsigned long long)gstages,
           (unsigned long long)stled);
    return 0;
}
#endif
