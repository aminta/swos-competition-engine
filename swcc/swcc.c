/* swcc: the spec compiler and validator. Spec JSON (schema swc/1) + a host profile -> diagnostics, and when there
   is no error, the binary spec (.SWC, big-endian, docs/format.md).

   Library: swcc_compile() (ctypes from Python, later WASM in the patcher page). CLI: swcc_main.c.
   Every diagnostic is "file:line:col: error|warning: <json path>: message". */
#include "swcc.h"
#include "json.h"
#include "../format/swc_format.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(SwcHeader) == 32, "header");
_Static_assert(sizeof(SwcSection) == 12, "section");
_Static_assert(sizeof(SwcName) == 36, "name");
_Static_assert(sizeof(SwcClub) == 16, "club");
_Static_assert(sizeof(SwcComp) == 28, "comp");
_Static_assert(sizeof(SwcStage) == 24, "stage");
_Static_assert(sizeof(SwcRound) == 8, "round");
_Static_assert(sizeof(SwcOutcome) == 10, "outcome");
_Static_assert(sizeof(SwcReg) == 4, "reg");
_Static_assert(sizeof(SwcMeta) == 8, "meta");
_Static_assert(sizeof(SwcFixed) == 6, "fixed");

/* ---- growable buffers -------------------------------------------------------------------------------------------- */
typedef struct { uint8_t *p; size_t n, cap; } Buf;

static void *bput(Buf *b, const void *src, size_t n)
{
    void *at;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->n + n) cap *= 2;
        b->p = realloc(b->p, cap);
        if (!b->p) { fprintf(stderr, "swcc: out of memory\n"); exit(2); }
        b->cap = cap;
    }
    at = b->p + b->n;
    if (src) memcpy(at, src, n); else memset(at, 0, n);
    b->n += n;
    return at;
}

/* ---- compiler state ---------------------------------------------------------------------------------------------- */
typedef struct {
    const char *file;
    Buf diag;
    int errors, warnings;
    /* profile */
    char langs[SWC_LANGS][4];
    int nlangs;
    unsigned char charset[256];
    int latin1;                 /* encoding: 0 = ascii, 1 = latin1 (+ overrides) */
    uint32_t over_cp[32];       /* codepoints above 0xFF mapped to a host byte (OpenSWOS: Œ 0x8C, Ÿ 0x9E, € 0x9D) */
    uint8_t over_b[32];
    int nover;
    int width[256];
    int title_max, title_px, short_px, club_name_max, clubs_per_file, table_rows, long_table_rows, max_teams;
    uint32_t arena_max;
    /* spec */
    JNode *root;
    int minor;                  /* the spec's "schema": "swc/1" = the M1 schema (1.0 + 1.1), "swc/1.1", "swc/1.2", "swc/1.3" */
    const char *schema;         /* its text, for the messages */
    JNode *countries;
    JNode **clubs, **comps;
    int nclubs, ncomps;
    void *stinfo;               /* StageInfo[], all stages in order */
    int nstinfo;
    /* output */
    Buf strs, names, club, comp, stag, entr, tieb, deci, roun, outc, reg, meta, fixt, grps, srce, tols, prnt, sect, secf;
    uint32_t arena;
} C;

static void diag(C *c, JNode *at, int error, const char *path, const char *fmt, ...)
{
    char msg[512], line[1024];
    va_list ap;
    int k;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    k = snprintf(line, sizeof line, "%s:%d:%d: %s: %s: %s\n", c->file, at ? at->line : 1, at ? at->col : 1,
                 error ? "error" : "warning", path && *path ? path : "(root)", msg);
    bput(&c->diag, line, (size_t)(k < (int)sizeof line ? k : (int)sizeof line - 1));
    if (error) c->errors++; else c->warnings++;
}
#define SWC_MINOR 4         /* the schema's minor version: swc/1.4 (1.4 = 1.3 + pool-file members) */
#define ERR(at, path, ...)  diag(c, at, 1, path, __VA_ARGS__)
#define WARN(at, path, ...) diag(c, at, 0, path, __VA_ARGS__)

static const char *pathf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return buf;
}

/* ---- typed member access ----------------------------------------------------------------------------------------- */
static JNode *need(C *c, JNode *obj, const char *key, JType t, const char *path)
{
    JNode *v = json_get(obj, key);
    if (!v) { ERR(obj, path, "missing '%s'", key); return NULL; }
    if (v->type != t) { ERR(v, path, "'%s' must be a %s, not a %s", key, json_type_name(t), json_type_name(v->type)); return NULL; }
    return v;
}

static JNode *opt(C *c, JNode *obj, const char *key, JType t, const char *path)
{
    JNode *v = json_get(obj, key);
    if (v && v->type != t) { ERR(v, path, "'%s' must be a %s, not a %s", key, json_type_name(t), json_type_name(v->type)); return NULL; }
    return v;
}

/* A construct added in swc/1.<m>: refused unless the spec names that minor or a later one ("swc/1" and "swc/1.1" are
   the M1 schema: a v1.2 construct needs "schema": "swc/1.2"). */
static void need_minor(C *c, JNode *at, const char *path, int m, const char *what)
{
    if (c->minor < m) ERR(at, path, "%s is swc/1.%d: this spec says \"%s\"", what, m, c->schema);
}

static int int_in(C *c, JNode *v, const char *key, const char *path, long lo, long hi, long *out)
{
    if (!v) return 0;
    if (v->type != J_NUM || !v->is_int || v->num < lo || v->num > hi) {
        ERR(v, path, "'%s' must be an integer in %ld..%ld", key, lo, hi);
        return 0;
    }
    *out = (long)v->num;
    return 1;
}

static void unknown_keys(C *c, JNode *obj, const char *path)
{
    JNode *m;
    if (!obj || obj->type != J_OBJ) return;
    for (m = obj->child; m; m = m->next)
        if (!m->used && m->key[0] != '_')          /* '_note' style members are comments */
            ERR(m, path, "unknown member '%s'", m->key);
}

static int enum_of(const char *s, const char *const *names, int n)
{
    int i;
    for (i = 0; i < n; i++) if (names[i] && !strcmp(s, names[i])) return i;
    return -1;
}

static void list_names(char *buf, size_t n, const char *const *names, int k)
{
    int i;
    size_t at = 0;
    buf[0] = 0;
    for (i = 0; i < k; i++)
        if (names[i] && at < n) at += (size_t)snprintf(buf + at, n - at, "%s%s", at ? ", " : "", names[i]);
}

/* ---- ids and strings --------------------------------------------------------------------------------------------- */
static int id_ok(const char *s, const char *prefix)
{
    size_t k = prefix ? strlen(prefix) : 0;
    int prev = '.';
    if (prefix && strncmp(s, prefix, k)) return 0;
    s += k;
    if (!*s) return 0;
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) prev = ch;
        else if ((ch == '.' || ch == '-' || ch == '_') && prev != '.' && prev != '-' && prev != '_') prev = ch;
        else return 0;
    }
    return prev != '.' && prev != '-' && prev != '_';
}

static uint32_t str_add(C *c, const char *s)
{
    size_t i = 0, k = strlen(s);
    while (i < c->strs.n) {                                    /* dedupe (linear: specs are small) */
        size_t l = strlen((char *)c->strs.p + i);
        if (l == k && !memcmp(c->strs.p + i, s, k)) return (uint32_t)i;
        i += l + 1;
    }
    i = c->strs.n;
    bput(&c->strs, s, k + 1);
    return (uint32_t)i;
}

/* UTF-8 -> the host's bytes (profile "encoding": ascii, or latin1 + "overrides"). Returns 0 and the first codepoint
   that has no host byte in *bad; out holds at most 255 bytes + NUL. */
static int host_conv(C *c, const char *s, char *out, size_t room, uint32_t *bad)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t k = 0;
    while (*p) {
        uint32_t cp = *p;
        int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0, i, j, b = -1;
        if (extra) {
            cp &= 0x3Fu >> extra;
            for (i = 1; i <= extra; i++) { if ((p[i] & 0xC0) != 0x80) { *bad = 0xFFFD; return 0; } cp = cp << 6 | (p[i] & 63); }
        }
        p += 1 + extra;
        for (j = 0; j < c->nover; j++) if (c->over_cp[j] == cp) b = c->over_b[j];
        if (b < 0 && (cp < 0x80 || (c->latin1 && cp <= 0xFF))) b = (int)cp;
        if (b < 0 || k + 1 >= room) { *bad = cp; return 0; }
        out[k++] = (char)b;
    }
    out[k] = 0;
    return 1;
}

/* A display text: converted to the host's bytes, every byte in the host font. Returns the converted text (valid until
   the next call) or NULL after reporting the error. */
static const char *text_ok(C *c, JNode *at, const char *path, const char *what, const char *s)
{
    static char out[256];
    uint32_t cp = 0;
    const unsigned char *q;
    if (!host_conv(c, s, out, sizeof out, &cp)) {
        if (cp >= 0x20 && cp < 0x7F) ERR(at, path, "%s \"%s\": character '%c' is not in the host font", what, s, (int)cp);
        else ERR(at, path, "%s \"%s\": character U+%04X has no byte in the host's encoding (%s)", what, s, cp,
                 c->latin1 ? "latin1 + overrides" : "ascii: write it without accents, upper case");
        return NULL;
    }
    for (q = (const unsigned char *)out; *q; q++)
        if (!c->charset[*q]) {
            uint32_t u = *q;
            if (u >= 0x20 && u < 0x7F)
                ERR(at, path, "%s \"%s\": character '%c' is not in the host font (allowed: the profile's charset)", what, s, (int)u);
            else
                ERR(at, path, "%s \"%s\": character U+%04X is not in the host font (write it without accents, upper case)", what, s, u);
            return NULL;
        }
    return out;
}

static int text_px(C *c, const char *s)
{
    int w = 0;
    for (; *s; s++) w += c->width[(unsigned char)*s];
    return w;
}

/* ---- profile ----------------------------------------------------------------------------------------------------- */
static int load_profile(C *c, JNode *p)
{
    JNode *v, *m;
    long x;
    int i;
    const char *path = "profile";
    if (!p || p->type != J_OBJ) { ERR(p, path, "the profile must be an object"); return 0; }
    json_get(p, "id");
    if ((v = need(c, p, "languages", J_ARR, path))) {
        for (m = v->child; m; m = m->next) {
            if (m->type != J_STR || strlen(m->str) > 3 || c->nlangs == SWC_LANGS) { ERR(m, path, "bad language"); continue; }
            strcpy(c->langs[c->nlangs++], m->str);
        }
    }
    if ((v = opt(c, p, "encoding", J_STR, path))) {
        if (!strcmp(v->str, "latin1")) c->latin1 = 1;
        else if (strcmp(v->str, "ascii")) ERR(v, path, "encoding \"%s\": ascii or latin1", v->str);
    }
    if ((v = opt(c, p, "overrides", J_OBJ, path)))
        for (m = v->child; m; m = m->next) {
            const unsigned char *k = (const unsigned char *)m->key;     /* the key's one codepoint */
            uint32_t cp = *k;
            int ex = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0, t;
            if (c->nover == 32 || !int_in(c, m, m->key, path, 0x80, 0xFF, &x)) continue;
            cp &= 0x3Fu >> ex;
            for (t = 1; t <= ex; t++) cp = cp << 6 | (k[t] & 63);
            c->over_cp[c->nover] = cp;
            c->over_b[c->nover++] = (uint8_t)x;
        }
    if ((v = need(c, p, "charset", J_STR, path))) {
        char hb[512];
        uint32_t bad = 0;
        if (!host_conv(c, v->str, hb, sizeof hb, &bad)) ERR(v, path, "charset: U+%04X has no byte in the profile's encoding", bad);
        else for (i = 0; hb[i]; i++) c->charset[(unsigned char)hb[i]] = 1;
    }
    if ((v = need(c, p, "widths", J_OBJ, path))) {
        long def = 6;
        int_in(c, json_get(v, "default"), "default", path, 0, 64, &def);
        for (i = 0; i < 256; i++) c->width[i] = (int)def;
        for (m = v->child; m; m = m->next)
            if (strcmp(m->key, "default")) {
                char hb[8];
                uint32_t bad = 0;
                if (!host_conv(c, m->key, hb, sizeof hb, &bad) || strlen(hb) != 1 || !int_in(c, m, m->key, path, 0, 64, &x)) {
                    ERR(m, path, "bad width entry"); continue; }
                c->width[(unsigned char)hb[0]] = (int)x;
            }
    }
#define PI(key, field, lo, hi) x = 0; if (int_in(c, need(c, p, key, J_NUM, path), key, path, lo, hi, &x)) c->field = (int)x;
    PI("title_max", title_max, 1, 255)
    PI("title_px", title_px, 1, 4096)
    PI("short_px", short_px, 1, 4096)
    PI("club_name_max", club_name_max, 1, 255)
    PI("clubs_per_file", clubs_per_file, 1, 65535)
    PI("table_rows", table_rows, 1, 255)
    PI("long_table_rows", long_table_rows, 1, 255)
    PI("max_teams_per_stage", max_teams, 2, 65535)
    x = 0;
    if (int_in(c, need(c, p, "arena_max", J_NUM, path), "arena_max", path, 1024, 0x7FFFFFFF, &x)) c->arena_max = (uint32_t)x;
#undef PI
    unknown_keys(c, p, path);
    return c->errors == 0;
}

/* ---- names: {"it": ["LONG", "SHORT"], ...} ----------------------------------------------------------------------- */
static uint16_t names_rec(C *c, JNode *names, const char *country_code, const char *path, uint32_t id)
{
    SwcName rec;
    int i;
    char p2[256];
    memset(&rec, 0, sizeof rec);
    rec.id = id;
    for (i = 0; i < c->nlangs; i++) {
        JNode *v = names ? json_get(names, c->langs[i]) : NULL, *lg, *sh;
        const char *country = "";
        pathf(p2, sizeof p2, "%s.names.%s", path, c->langs[i]);
        if (!v) { ERR(names, path, "names: missing language '%s' (every name exists in %d languages)", c->langs[i], c->nlangs); continue; }
        if (v->type != J_ARR || v->n != 2 || (lg = json_at(v, 0))->type != J_STR || (sh = json_at(v, 1))->type != J_STR) {
            ERR(v, p2, "a name is [\"LONG NAME\", \"SHORT\"]");
            continue;
        }
        char hc[256] = "", hl[256], hs[256];
        const char *t;
        uint32_t bad;
        if (country_code && c->countries) {
            JNode *cnames = json_get(json_get(c->countries, country_code), "names");
            JNode *cl = cnames ? json_get(cnames, c->langs[i]) : NULL;
            if (cl && cl->type == J_STR && host_conv(c, cl->str, hc, sizeof hc, &bad)) country = hc;
        }
        hl[0] = hs[0] = 0;
        if ((t = text_ok(c, lg, p2, "long name", lg->str))) strcpy(hl, t);
        if (t && (t = text_ok(c, sh, p2, "short name", sh->str))) strcpy(hs, t);
        if (t) {
            size_t tl = strlen(country) + (*country ? 1 : 0) + strlen(hl);
            char title[600];
            snprintf(title, sizeof title, "%s%s%s", country, *country ? " " : "", hl);
            if ((int)tl > c->title_max)
                ERR(lg, p2, "title \"%s\" is %zu characters, the host's title field holds %d", title, tl, c->title_max);
            if (text_px(c, title) > c->title_px)
                ERR(lg, p2, "title \"%s\" is %d px wide, the host's buttons show %d", title, text_px(c, title), c->title_px);
            if (text_px(c, hs) > c->short_px)
                ERR(sh, p2, "short name \"%s\" is %d px wide, the calendar label holds %d", sh->str, text_px(c, hs), c->short_px);
        }
        rec.str[i][0] = str_add(c, hl);
        rec.str[i][1] = str_add(c, hs);
    }
    unknown_keys(c, names, path);
    bput(&c->names, &rec, sizeof rec);
    return (uint16_t)(c->names.n / sizeof rec - 1);
}

/* ---- clubs ------------------------------------------------------------------------------------------------------- */
static int find_club(C *c, const char *id)
{
    int i;
    for (i = 0; i < c->nclubs; i++) {
        JNode *v = json_get(c->clubs[i], "id");
        if (v && v->type == J_STR && !strcmp(v->str, id)) return i;
    }
    return -1;
}

static int find_comp(C *c, const char *id)
{
    int i;
    for (i = 0; i < c->ncomps; i++) {
        JNode *v = json_get(c->comps[i], "id");
        if (v && v->type == J_STR && !strcmp(v->str, id)) return i;
    }
    return -1;
}

static void do_clubs(C *c, JNode *arr)
{
    int i, j;
    char path[128];
    uint16_t *per_file = calloc(65536, sizeof *per_file);
    for (i = 0; i < c->nclubs; i++) {
        JNode *cl = c->clubs[i], *id, *name, *country, *host, *area, *parent, *v;
        SwcClub rec;
        long file = 0, ord = 0;
        memset(&rec, 0xFF, sizeof rec);
        pathf(path, sizeof path, "clubs[%d]", i);
        if (cl->type != J_OBJ) { ERR(cl, path, "a club is an object"); continue; }
        id = need(c, cl, "id", J_STR, path);
        name = need(c, cl, "name", J_STR, path);
        country = need(c, cl, "country", J_STR, path);
        host = need(c, cl, "host", J_OBJ, path);
        area = opt(c, cl, "area", J_STR, path);
        if ((parent = opt(c, cl, "parent", J_STR, path))) need_minor(c, parent, path, 3, "'parent'");
        if ((v = opt(c, cl, "rank", J_NUM, path))) {   /* v1.3: the club's rank for the summer re-allocation */
            long rk = 0;
            need_minor(c, v, path, 3, "'rank'");
            int_in(c, v, "rank", path, 0, 254, &rk);
        }
        if (id) {
            if (!id_ok(id->str, "club:")) ERR(id, path, "club id \"%s\" must be 'club:' + lower-case words joined by '.', '-' or '_'", id->str);
            for (j = 0; j < i; j++) {
                JNode *o = json_get(c->clubs[j], "id");
                if (o && o->type == J_STR && !strcmp(o->str, id->str)) { ERR(id, path, "club id \"%s\" already used by clubs[%d]", id->str, j); break; }
            }
            rec.id = str_add(c, id->str);
        }
        if (name) {
            const char *hn = text_ok(c, name, path, "club name", name->str);
            if (hn && (int)strlen(hn) > c->club_name_max)
                ERR(name, path, "club name \"%s\" is %zu characters, the host holds %d", name->str, strlen(hn), c->club_name_max);
            rec.name = str_add(c, hn ? hn : "");
        }
        if (country) {
            if (!c->countries || !json_get(c->countries, country->str)) ERR(country, path, "country \"%s\" is not in 'countries'", country->str);
            rec.country = (uint16_t)str_add(c, country->str);
        }
        if (host && int_in(c, need(c, host, "file", J_NUM, path), "file", path, 0, 65535, &file) &&
            int_in(c, need(c, host, "ordinal", J_NUM, path), "ordinal", path, 0, 65535, &ord)) {
            rec.host_file = (uint16_t)file;
            rec.host_ordinal = (uint16_t)ord;
            if (ord >= c->clubs_per_file)
                ERR(host, path, "ordinal %ld: a host file holds %d clubs (0..%d)", ord, c->clubs_per_file, c->clubs_per_file - 1);
            if (++per_file[file] == c->clubs_per_file + 1)
                ERR(host, path, "host file %ld has more than %d clubs", file, c->clubs_per_file);
            for (j = 0; j < i; j++) {
                JNode *oh = json_get(c->clubs[j], "host");
                JNode *of = json_get(oh, "file"), *oo = json_get(oh, "ordinal");
                if (of && oo && of->num == file && oo->num == ord) { ERR(host, path, "host file %ld ordinal %ld already used by clubs[%d]", file, ord, j); break; }
            }
            unknown_keys(c, host, path);
        }
        if (area) rec.area = (uint16_t)str_add(c, area->str);
        unknown_keys(c, cl, path);
        bput(&c->club, &rec, sizeof rec);
    }
    free(per_file);
    for (i = 0; i < c->nclubs; i++) {                  /* v1.3: a second team's parent, a first team of its country */
        JNode *cl = c->clubs[i], *p = json_get(cl, "parent"), *cc = json_get(cl, "country"), *pc;
        if (!p || p->type != J_STR) continue;
        pathf(path, sizeof path, "clubs[%d]", i);
        j = find_club(c, p->str);
        if (j < 0) { ERR(p, path, "parent \"%s\" is not in 'clubs'", p->str); continue; }
        if (j == i) { ERR(p, path, "a club is not its own parent"); continue; }
        pc = json_get(c->clubs[j], "country");
        if (cc && pc && cc->type == J_STR && pc->type == J_STR && strcmp(cc->str, pc->str))
            ERR(p, path, "parent \"%s\" is of %s, this club of %s (a second team plays in its parent's country)", p->str, pc->str, cc->str);
        if (json_get(c->clubs[j], "parent"))
            ERR(p, path, "parent \"%s\" is a second team itself (a parent is a first team)", p->str);
        { uint16_t pr[2] = { (uint16_t)i, (uint16_t)j }; bput(&c->prnt, pr, 4); }
    }
    (void)arr;
}

/* ---- positions: "3", "3-6"; references: "stage#3-6", "comp.id:stage#1" ------------------------------------------ */
/* Positions "N", "N-M" (1-based) or counted from the bottom "LN" (L1 = the last): encoded 1..127, or 128 + N for
   "LN" (the binary keeps the encoding: docs/format.md). */
static int pos_one(const char *s, char **e, int *v)
{
    int last = *s == 'L';
    long x = strtol(s + last, e, 10);
    if (*e == s + last || x < 1 || x > 127) return 0;
    *v = (int)x + (last ? 128 : 0);
    return 1;
}

static int positions(const char *s, int *a, int *b)
{
    char *e;
    if (!pos_one(s, &e, a)) return 0;
    *b = *a;
    if (*e == '-' && !pos_one(e + 1, &e, b)) return 0;
    return !*e;
}

static int pos_res(int enc, int size) { return enc > 128 ? size - (enc - 128) + 1 : enc; }

/* ---- competitions ------------------------------------------------------------------------------------------------ */
static const char *const KINDS[] = { NULL, "league", "cup", "league-cup", "supercup", "continental", "playoff", "tournament" };
static const char *const STYPES[] = { NULL, "round_robin", "groups", "league_phase", "knockout", "playoff", "split",
                                      "aggregate_table", "average_table", "single_match" };
static const char *const TIEBS[] = { NULL, "points", "goal_difference", "goals_for", "goals_against", "wins", "away_wins",
                                     "away_goals_for", "head_to_head_points", "head_to_head_goal_difference",
                                     "head_to_head_goals_for", "head_to_head_away_goals", "fair_play", "coefficient",
                                     "previous_stage_rank", "play_off_match", "draw_lots", "name", "faithful_1996",
                                     "head_to_head_reapplied", "avulsa", "losses_fewer",
                                     "home_losses_fewer" };
static const char *const DECIS[] = { NULL, "aggregate", "away_goals", "extra_time", "penalties", "better_placed", "replay",
                                     "draw_lots", "golden_goal",
                                     "better_placed_unless_level_on_points", "seed" };   /* (9, 10: swc/1.4, 42-s3shape) */
static const char *const TAGS[] = { NULL, "champion", "promoted", "relegated", "playoff", "playout", "continental", "qualified" };
static const char *const ZONES[] = { NULL, "CL", "EL", "CONF", "PROM", "P.OFF", "P.OUT", "RETR" };
static const char *const HOMES[] = { "draw", "better_placed", "better_placed_second_leg", "neutral", "fixed",
                                      "lower_number", "lower_number_second_leg" };   /* (v1.4: 42-s2ko) */
static const char *const PAIRS[] = { "draw", "seeded", "bracket", "fixed", "by_place", "urn", "urn_pairs" };
static const char *const MODES[] = { "career", "season", "preset", "friendly" };

#define NE(a) ((int)(sizeof(a) / sizeof(a[0])))

static int enum_member(C *c, JNode *v, const char *key, const char *path, const char *const *names, int n)
{
    int k;
    char all[512];
    if (!v) return -1;
    if (v->type != J_STR) { ERR(v, path, "'%s' must be a string", key); return -1; }
    k = enum_of(v->str, names, n);
    if (k < 0) { list_names(all, sizeof all, names, n); ERR(v, path, "%s \"%s\" is not one of: %s", key, v->str, all); }
    return k;
}

static uint16_t code_list(C *c, Buf *b, JNode *arr, const char *key, const char *path, const char *const *names, int n,
                          int *count)
{
    uint16_t at = (uint16_t)b->n;
    uint8_t zero = 0;
    JNode *m;
    int k = 0;
    for (m = arr ? arr->child : NULL; m; m = m->next) {
        int e = enum_member(c, m, key, path, names, n);
        if (names == TIEBS && e == SWC_TB_H2H_REAPPLIED) need_minor(c, m, path, 3, "the tie-breaker \"head_to_head_reapplied\"");
        if (names == TIEBS && e >= SWC_TB_AVULSA) {                 /* v1.4: the FIGC avulsa, Serie C's losses */
            char what[64];
            snprintf(what, sizeof what, "the tie-breaker \"%s\"", TIEBS[e]);
            need_minor(c, m, path, 4, what);
        }
        if (e > 0) { uint8_t u = (uint8_t)e; bput(b, &u, 1); k++; }
    }
    bput(b, &zero, 1);
    if (count) *count = k;
    return at;
}

/* stage teams, for range checks of outcomes / entrants (0 = unknown yet) */
typedef struct { const char *id; int teams, first, n, type, groups, gmin, gmax, per_group, last_ties; } StageInfo;

static int stage_index(StageInfo *si, int n, const char *id)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(si[i].id, id)) return i;
    return -1;
}

/* v1.4 (42-s3shape): {"cup_place": {...}} -- the play-off place of a cup's winner (the Coppa Italia Serie C's, FIGC
   C.U. 209/A): "cup" the cup, "groups" the leagues whose tables it is read from (default this competition's), "takes"
   the places from which the winner takes it ("table#11-15"), "takes_if_safe" those it takes when their play-out is not
   held ("table#16-17"), "instead" what takes it otherwise, in order: "finalist" (the other finalist, the same
   preclusions), "group#N" (place N of the winner's group), {"place": "winner_group#N", "scroll": true} (that place,
   its group's play-off scrolled up); "unknown" the place when the winner is not known ("best_of:#N"). One club. */
static void gap_obj(C *c, JNode *o, const char *path);
static int cur_kind;                    /* (the competition being read: its kind) */
/* v1.4: a place reference "stage#N" ("stage#N-M" with range) of an earlier stage of this competition -- the stage
   resolved and N within its places, as entr_item does; or (league != NULL) of that league's stage; in a playoff
   competition also "<league>#N", N within the league's clubs. 1, or 0 after an error. */
static int place_ref(C *c, JNode *v, const char *path, const char *what, StageInfo *all_st, int first_stage, int nall,
                     int range, JNode *league)
{
    char *hash = strchr(v->str, '#');
    int a, b, lo = 0, ix;
    if (!hash || !positions(hash + 1, &a, &b) || (!range && a != b)) {
        ERR(v, path, "%s \"%s\": write '%s'", what, v->str, range ? "stage#N-M" : "stage#N");
        return 0;
    }
    *hash = 0;
    if (league) {                               /* (another league's stage: its table's size, its clubs) */
        JNode *sts = json_get(league, "stages"), *q, *cnt = json_get(json_get(league, "participants"), "count");
        ix = -1;
        for (q = sts && sts->type == J_ARR ? sts->child : NULL; q; q = q->next) {
            JNode *id = json_get(q, "id");
            if (id && id->type == J_STR && !strcmp(id->str, v->str)) ix = 0;
        }
        lo = cnt && cnt->type == J_NUM ? (int)cnt->num : 0;
    } else {
        ix = stage_index(all_st + first_stage, nall - first_stage, v->str);
        if (ix >= 0) lo = all_st[first_stage + ix].gmin ? all_st[first_stage + ix].gmin : all_st[first_stage + ix].teams;
        else if (cur_kind == SWC_K_PLAYOFF && (ix = find_comp(c, v->str)) >= 0) {   /* (a playoff competition: "<league>#N") */
            JNode *kd = json_get(c->comps[ix], "kind"), *cnt = json_get(json_get(c->comps[ix], "participants"), "count");
            if (!kd || kd->type != J_STR || strcmp(kd->str, "league")) ix = -1;
            lo = cnt && cnt->type == J_NUM ? (int)cnt->num : 0;
        }
    }
    *hash = '#';
    if (ix < 0) {
        *hash = 0;
        ERR(v, path, "%s \"%s#%s\": no earlier stage \"%s\"%s", what, v->str, hash + 1, v->str, league ? " in that league" : " in this competition");
        *hash = '#';
        return 0;
    }
    if (lo && (pos_res(a, lo) < 1 || pos_res(b, lo) > lo || pos_res(b, lo) < pos_res(a, lo))) {
        ERR(v, path, "%s \"%s\": positions outside the stage (%d clubs per table)", what, v->str, lo);
        return 0;
    }
    return 1;
}

static int cup_place_item(C *c, JNode *it, const char *sp, int *src, int *per, StageInfo *all_st, int first_stage, int nall)
{
    JNode *cp = opt(c, it, "cup_place", J_OBJ, sp), *v, *x;
    int ok = 1;
    unknown_keys(c, it, sp);
    if (!cp) return -1;
    need_minor(c, cp, sp, 4, "'cup_place'");
    if ((v = need(c, cp, "cup", J_STR, sp)) && find_comp(c, v->str) < 0) { ERR(v, sp, "cup_place: no competition \"%s\"", v->str); ok = 0; }
    JNode *grp = opt(c, cp, "groups", J_ARR, sp);
    if (grp)
        for (x = grp->child; x; x = x->next)
            if (x->type != J_STR || find_comp(c, x->str) < 0) { ERR(x, sp, "cup_place.groups: a league id of this file"); ok = 0; }
    for (int k = 0; k < 2; k++) {
        const char *key = k ? "takes_if_safe" : "takes";
        char what[40];
        if (!(v = k ? opt(c, cp, key, J_STR, sp) : need(c, cp, key, J_STR, sp))) { ok &= k; continue; }
        snprintf(what, sizeof what, "cup_place.%s", key);
        if (!grp) ok &= place_ref(c, v, sp, what, all_st, first_stage, nall, 1, NULL);
        else                                    /* (the groups' stage: in each of them) */
            for (x = grp->child; x; x = x->next)
                if (x->type == J_STR && find_comp(c, x->str) >= 0)
                    ok &= place_ref(c, v, sp, what, all_st, first_stage, nall, 1, c->comps[find_comp(c, x->str)]);
    }
    if ((v = need(c, cp, "instead", J_ARR, sp))) {
        if (!v->n) { ERR(v, sp, "cup_place.instead: at least one entry"); ok = 0; }
        for (x = v->child; x; x = x->next) {
            long n = 0;
            if (x->type == J_STR && (!strcmp(x->str, "finalist") ||
                                     (!strncmp(x->str, "group#", 6) && (n = strtol(x->str + 6, NULL, 10)) >= 1 && n <= 64)))
                continue;
            if (x->type == J_OBJ) {
                JNode *pl = need(c, x, "place", J_STR, sp), *sc = opt(c, x, "scroll", J_BOOL, sp);
                unknown_keys(c, x, sp);
                if (sc && !sc->b) { ERR(sc, sp, "cup_place.instead: \"scroll\" is true or absent"); ok = 0; continue; }
                if (pl && !strncmp(pl->str, "winner_group#", 13) && (n = strtol(pl->str + 13, NULL, 10)) >= 1 && n <= 64)
                    continue;
            }
            ERR(x, sp, "cup_place.instead: \"finalist\", \"group#N\" or {\"place\": \"winner_group#N\", \"scroll\": true}");
            ok = 0;
        }
    }
    if ((v = opt(c, cp, "playout_gap", J_NUM, sp))) { long g = 0; if (!int_in(c, v, "playout_gap", sp, 0, 99, &g)) ok = 0; }
    if ((v = opt(c, cp, "unknown", J_STR, sp)) && (strncmp(v->str, "best_of:#", 9) || strtol(v->str + 9, NULL, 10) < 1)) {
        ERR(v, sp, "cup_place.unknown: \"best_of:#N\" (the best placed N-th of the groups)");
        ok = 0;
    }
    unknown_keys(c, cp, sp);
    *src = -1; *per = 0;
    return ok ? 1 : -1;
}

/* v1.4 (42-s2ko): a knockout's bracket numbers (C.U. 253 "posizioni in tabellone"). cur_numbers = the stage's
   numbers (0: none); a "#N" of a 'fixed' pair enters club number N at that round; num_used / win_used count the uses
   (each number once, each earlier tie's winner once). */
static int cur_numbers;
static unsigned char num_used[1025], win_used[256][256];

/* v1.4 (42-s2ko): a club of last season, for a knockout's 'numbers': "<league>#N-M" (its final table), "<league>:up"
   / ":down" (the clubs that left it up / down, by place: the direct ones, then the play-offs'), "<competition>:winner"
   / ":finalist" (its final's winner / loser), "<competition>/<stage>:winner" / ":finalist" (a play-off stage's final;
   a play-out's winner stays up), "<competition>:round<R>" (the clubs that played round R of its first stage). 1, or 0
   after an error. */
static int prev_ref(C *c, JNode *v, const char *path)
{
    char id[128], *hash, *col, *sl;
    int a, b, kx;
    JNode *kd, *cnt;
    if (v->type != J_STR || strlen(v->str) >= sizeof id) { ERR(v, path, "numbers: an item is a string (last season's club)"); return 0; }
    strcpy(id, v->str);
    hash = strchr(id, '#'); col = strchr(id, ':'); sl = strchr(id, '/');
    if (hash) *hash = 0; else if (col) *col = 0; else { ERR(v, path, "numbers \"%s\": write '<league>#N-M', '<id>:up', '<id>:down', '<id>:winner', '<id>:finalist' or '<id>:round<R>'", v->str); return 0; }
    if (sl && !hash) *sl = 0;
    kx = find_comp(c, id);
    if (kx < 0) { ERR(v, path, "numbers \"%s\": no competition \"%s\"", v->str, id); return 0; }
    kd = json_get(c->comps[kx], "kind");
    cnt = json_get(json_get(c->comps[kx], "participants"), "count");
    if (hash || (col && (!strcmp(col + 1, "up") || !strcmp(col + 1, "down")))) {
        if (!kd || kd->type != J_STR || strcmp(kd->str, "league")) { ERR(v, path, "numbers \"%s\": \"%s\" is not a league", v->str, id); return 0; }
        if (sl && hash) { ERR(v, path, "numbers \"%s\": a league's table is '<league>#N-M'", v->str); return 0; }
        if (hash && (!positions(hash + 1, &a, &b) || a < 1 || b < a || (cnt && cnt->type == J_NUM && b > (int)cnt->num))) {
            ERR(v, path, "numbers \"%s\": places within the league (%d clubs)", v->str, cnt && cnt->type == J_NUM ? (int)cnt->num : 0);
            return 0;
        }
        return 1;
    }
    if (sl) {                                   /* (a stage of that competition: a playoff stage) */
        JNode *q;
        int found = 0;
        for (q = json_get(c->comps[kx], "stages") ? json_get(c->comps[kx], "stages")->child : NULL; q; q = q->next) {
            JNode *sid = json_get(q, "id"), *ty = json_get(q, "type");
            if (sid && sid->type == J_STR && !strcmp(sid->str, sl + 1))
                found = ty && ty->type == J_STR && (!strcmp(ty->str, "playoff") || !strcmp(ty->str, "knockout")) ? 1 : -1;
        }
        if (found <= 0) { ERR(v, path, "numbers \"%s\": no playoff stage \"%s\" in \"%s\"", v->str, sl + 1, id); return 0; }
    }
    if (!strcmp(col + 1, "winner") || !strcmp(col + 1, "finalist")) return 1;
    if (!strncmp(col + 1, "round", 5) && !sl) {
        char *e;
        long r = strtol(col + 6, &e, 10);
        if (r >= 1 && !*e) return 1;
    }
    ERR(v, path, "numbers \"%s\": after ':' write up, down, winner, finalist or round<R>", v->str);
    return 0;
}

/* v1.4 (42-s2ko, MC Q5 condition 2): a 'numbers' stage's "unknown" {ref: fallback} -- what each ref of last season
   gives when the save does not know that fact (an old save, a career's first season end, a fact never seen): another
   ref (with its own fallback, unless it is a table), a list of tables (merged) or "none" (nothing: the group's
   'else' fills). A table
   "<league>#N-M" is the season end's own and never unknown. Every other ref of the groups needs one; a key no ref
   reaches, or a chain that loops, is refused. 1, or 0 after an error. */
static int numbers_unknown(C *c, JNode *st, JNode *nu, const char *sp)
{
    JNode *un = opt(c, st, "unknown", J_OBJ, sp), *g, *q, *k;
    const char *used[256];
    int nused = 0, ok = 1;
    char up[360];
    pathf(up, sizeof up, "%s.unknown", sp);
    for (g = nu->child; g; g = g->next) {
        JNode *ls[2] = { json_get(g, "from"), json_get(g, "else") };
        for (int i = 0; i < 2; i++)
            for (q = ls[i] && ls[i]->type == J_ARR ? ls[i]->child : NULL; q; q = q->next) {
                int seen = 0;
                if (q->type != J_STR || strchr(q->str, '#')) continue;
                for (int j = 0; j < nused; j++) seen |= !strcmp(used[j], q->str);
                if (!seen && nused < 256) used[nused++] = q->str;
                if (!seen && !(un && json_get(un, q->str))) {
                    ERR(q, up, "\"%s\": no fallback -- write \"unknown\": {\"%s\": <a ref or \"none\">}", q->str, q->str);
                    ok = 0;
                }
            }
    }
    if (!un) return ok;
    for (k = un->child; k; k = k->next) {       /* (the fallbacks: refs of their own, reached; no loop) */
        JNode *v = k;
        int reached = 0, steps = 0;
        if (k->type == J_ARR) {                 /* (tables, merged: "place_record" picks across them) */
            if (!k->n) { ERR(k, up, "\"%s\": at least one table", k->key); ok = 0; }
            for (q = k->child; q; q = q->next)
                if (q->type != J_STR || !strchr(q->str, '#')) { ERR(q, up, "\"%s\": a list of tables \"<league>#N-M\"", k->key); ok = 0; }
                else ok &= prev_ref(c, q, up);
        } else if (k->type != J_STR) { ERR(k, up, "\"%s\": a ref, a list of tables or \"none\"", k->key); ok = 0; continue; }
        else if (strcmp(k->str, "none") && !prev_ref(c, k, up)) { ok = 0; continue; }
        for (int j = 0; j < nused; j++) reached |= !strcmp(used[j], k->key);
        for (q = un->child; q && !reached; q = q->next) reached |= q->type == J_STR && !strcmp(q->str, k->key);
        if (!reached) { ERR(k, up, "\"%s\": no item of the numbers is that ref", k->key); ok = 0; continue; }
        while (v && v->type == J_STR && strcmp(v->str, "none") && !strchr(v->str, '#') && steps++ < 64) {
            JNode *nx = json_get(un, v->str);
            if (!nx) { ERR(v, up, "\"%s\": the fallback \"%s\" needs a fallback of its own", k->key, v->str); ok = 0; break; }
            v = nx;
        }
        if (steps >= 64) { ERR(k, up, "\"%s\": the fallbacks loop", k->key); ok = 0; }
    }
    return ok;
}

/* v1.4 (42-s2ko): a knockout stage's 'numbers' -- groups {take, from [prev_ref], order "list" | "record", else
   [prev_ref], else_order "list" | "place_record"} numbering last season's clubs 1, 2, ... in order (a club numbered
   once: an earlier group's club is skipped; only clubs of this season's 'source' leagues), and 'first_season'
   {numbers [club ids], swap_home ["R.T"]} (the world's first season, its real numbers and field swaps). Returns
   the numbers, 0 after an error. */
static int numbers_of(C *c, JNode *st, const char *sp, JNode *parts)
{
    JNode *nu = opt(c, st, "numbers", J_ARR, sp), *fs = opt(c, st, "first_season", J_OBJ, sp), *g, *q;
    int n = 0, gi = 0, ok = 1;
    char gp[360];
    if (!nu) { if (fs) ERR(fs, sp, "'first_season' goes with 'numbers'"); return 0; }
    need_minor(c, nu, sp, 4, "a knockout's 'numbers'");
    if (!parts || !json_get(parts, "from") || json_get(parts, "from")->type != J_STR || strcmp(json_get(parts, "from")->str, "numbers"))
        ERR(nu, sp, "'numbers' needs the competition's participants \"from\": \"numbers\"");
    for (g = nu->child; g; g = g->next, gi++) {
        JNode *fr, *el, *o1, *o2;
        long k = 0;
        pathf(gp, sizeof gp, "%s.numbers[%d]", sp, gi);
        if (g->type != J_OBJ) { ERR(g, gp, "numbers: a group is {\"take\": k, \"from\": [...]}"); ok = 0; continue; }
        if (int_in(c, need(c, g, "take", J_NUM, gp), "take", gp, 1, 255, &k)) n += (int)k; else ok = 0;
        fr = need(c, g, "from", J_ARR, gp);
        el = opt(c, g, "else", J_ARR, gp);
        o1 = opt(c, g, "order", J_STR, gp);
        o2 = opt(c, g, "else_order", J_STR, gp);
        if (fr && !fr->n) ERR(fr, gp, "from: at least one item");
        for (q = fr ? fr->child : NULL; q; q = q->next) ok &= prev_ref(c, q, gp);
        for (q = el ? el->child : NULL; q; q = q->next) ok &= prev_ref(c, q, gp);
        if (o1 && strcmp(o1->str, "list") && strcmp(o1->str, "record"))
            ERR(o1, gp, "order: \"list\" (as listed) or \"record\" (points per match, wins, goals for, then a draw)");
        if (o2 && strcmp(o2->str, "list") && strcmp(o2->str, "place_record"))
            ERR(o2, gp, "else_order: \"list\" or \"place_record\" (place, then points per match)");
        if (o2 && !el) ERR(o2, gp, "'else_order' goes with 'else'");
        unknown_keys(c, g, gp);
    }
    ok &= numbers_unknown(c, st, nu, sp);
    if (n > 1024) { ERR(nu, sp, "numbers: %d, at most 1024", n); return 0; }
    if (fs) {
        JNode *fn = need(c, fs, "numbers", J_ARR, sp), *sw = opt(c, fs, "swap_home", J_ARR, sp), *o;
        int i = 0;
        pathf(gp, sizeof gp, "%s.first_season", sp);
        if (fn && fn->n != n) ERR(fn, gp, "numbers: %d clubs listed, the groups take %d", fn->n, n);
        for (q = fn ? fn->child : NULL; q; q = q->next, i++) {
            if (q->type != J_STR || find_club(c, q->str) < 0) { ERR(q, gp, "numbers[%d]: a club id of 'clubs'", i); continue; }
            for (o = fn->child; o != q; o = o->next)
                if (o->type == J_STR && !strcmp(o->str, q->str)) ERR(q, gp, "numbers: club \"%s\" numbered twice", q->str);
        }
        for (q = sw ? sw->child : NULL; q; q = q->next) {
            char *e;
            long r = q->type == J_STR ? strtol(q->str, &e, 10) : 0, t = 0;
            if (r >= 1 && *e == '.') t = strtol(e + 1, &e, 10);
            if (r < 1 || t < 1 || *e) ERR(q, gp, "swap_home: \"R.T\" (round R, tie T, from 1)");
        }
        unknown_keys(c, fs, gp);
    }
    (void)ok;                                   /* (a bad item is reported; the numbers still bound the bracket) */
    return n;
}

/* v1.4 (42-s3shape): a club of a play-off round named by an earlier round -- "winner:R.T" / "loser:R.T" (round R,
   tie T, from 1), {"best_of": "winners:R"} / {"rest_of": "winners:R"} (the best placed of round R's winners, the
   others) -- or a place "stage#N". rties: the ties of the rounds so far. 1, or 0 after an error. */
static int ko_ref(C *c, JNode *it, const char *rp, int ri, const long *rties, StageInfo *all_st, int first_stage, int nall)
{
    long r = 0, t = 0;
    char *e;
    if (it->type == J_OBJ) {
        JNode *v = json_get(it, "best_of") ? opt(c, it, "best_of", J_STR, rp) : opt(c, it, "rest_of", J_STR, rp);
        unknown_keys(c, it, rp);
        need_minor(c, it, rp, 4, "a 'best_of' / 'rest_of' of a round's winners");
        if (v && !strncmp(v->str, "winners:", 8) && (r = strtol(v->str + 8, &e, 10)) >= 1 && r <= ri && !*e) return 1;
        ERR(it, rp, "{\"best_of\" / \"rest_of\": \"winners:R\"}: R an earlier round (from 1)");
        return 0;
    }
    if (it->type == J_STR && !strcmp(it->str, "cup_place")) { need_minor(c, it, rp, 4, "\"cup_place\""); return 1; }
    if (it->type != J_STR) { ERR(it, rp, "a round's club: \"winner:R.T\", \"loser:R.T\", \"stage#N\" or {\"best_of\": \"winners:R\"}"); return 0; }
    if (!strncmp(it->str, "winner:", 7) || !strncmp(it->str, "loser:", 6)) {
        need_minor(c, it, rp, 4, "a round's \"winner:R.T\" / \"loser:R.T\"");
        r = strtol(strchr(it->str, ':') + 1, &e, 10);
        if (*e == '.') t = strtol(e + 1, &e, 10);
        if (*e || r < 1 || r > ri || t < 1 || t > rties[r - 1]) {
            ERR(it, rp, "\"%s\": round R earlier than this one, tie T of it (from 1)", it->str);
            return 0;
        }
        if (cur_numbers && r <= 256 && t <= 256 && it->str[0] == 'w' && win_used[r - 1][t - 1]++)
            ERR(it, rp, "\"%s\": that tie's winner plays twice", it->str);
        return 1;
    }
    if (it->str[0] == '#' && cur_numbers) {    /* v1.4 (42-s2ko): bracket number N enters here */
        r = strtol(it->str + 1, &e, 10);
        if (*e || r < 1 || r > cur_numbers) { ERR(it, rp, "\"%s\": a bracket number 1-%d", it->str, cur_numbers); return 0; }
        if (num_used[r]++) ERR(it, rp, "\"%s\": bracket number %ld enters twice", it->str, r);
        return 1;
    }
    if (strchr(it->str, '#')) return place_ref(c, it, rp, "a round's club", all_st, first_stage, nall, 0, NULL);
    ERR(it, rp, "\"%s\": \"winner:R.T\", \"loser:R.T\" or one place \"stage#N\"", it->str);
    return 0;
}

/* One item of a stage's 'entrants': "stage#N-M", the places N..M of every table of an earlier stage (*per = 1: the
   count is per table), or (v1.2) {"best_of": "stage#P", "count": K}, the K best clubs placed P across the groups of a
   groups stage, ranked by that stage's tie-breakers (*per = 0). Returns the clubs, -1 after an error; *src = the
   source stage (all_st index), *pa..*pb = the places taken (for the overlap check). */
static int entr_item(C *c, JNode *it, const char *sp, StageInfo *all_st, int first_stage, int nall, int *src, int *per,
                     int *pa, int *pb)
{
    JNode *ref = it, *cnt = NULL;
    char *hash;
    int a, b, ix, n = -1;
    long k = 0;
    if (it->type == J_OBJ && json_get(it, "cup_place")) return cup_place_item(c, it, sp, src, per, all_st, first_stage, nall);
    if (it->type == J_STR && (hash = strchr(it->str, '#')) && cur_kind == SWC_K_PLAYOFF) {   /* v1.4: "<league>#N-M" */
        *hash = 0;
        ix = find_comp(c, it->str);
        *hash = '#';
        if (ix >= 0) {
            JNode *kd = json_get(c->comps[ix], "kind");
            need_minor(c, it, sp, 4, "a playoff competition's entrants \"<league>#N-M\"");
            if (!kd || kd->type != J_STR || strcmp(kd->str, "league")) ERR(it, sp, "entrants \"%s\": not a league", it->str);
            if (!positions(hash + 1, &a, &b) || a < 1 || b < a) { ERR(it, sp, "entrants \"%s\": write '<league>#N-M'", it->str); return -1; }
            JNode *pc = json_get(json_get(c->comps[ix], "participants"), "count");
            if (pc && pc->type == J_NUM && b > (int)pc->num) {
                ERR(it, sp, "entrants \"%s\": positions outside the league (%d clubs)", it->str, (int)pc->num);
                return -1;
            }
            *src = -2 - ix; *per = 0; *pa = a; *pb = b;     /* (src -2 - league: the taken-twice check by league) */
            return b - a + 1;
        }
    }
    if (it->type == J_OBJ) {
        ref = need(c, it, "best_of", J_STR, sp);
        cnt = need(c, it, "count", J_NUM, sp);
        unknown_keys(c, it, sp);
        if (!ref || !cnt) return -1;
    } else if (it->type != J_STR) {
        ERR(it, sp, "entrants: an item is 'stage#N-M' or {\"best_of\": \"stage#N\", \"count\": K}");
        return -1;
    }
    hash = strchr(ref->str, '#');
    if (!hash || !positions(hash + 1, &a, &b)) {
        ERR(ref, sp, "entrants \"%s\": write 'stage#N', 'stage#N-M' or 'stage#LN-LM' (from the bottom)", ref->str);
        return -1;
    }
    *hash = 0;
    ix = stage_index(all_st + first_stage, nall - first_stage, ref->str);
    if (ix < 0) ERR(ref, sp, "entrants: no earlier stage \"%s\" in this competition", ref->str);
    *hash = '#';
    if (ix >= 0) {
        StageInfo *s = &all_st[first_stage + ix];
        int lo = s->gmin ? s->gmin : s->teams, hi = s->gmax ? s->gmax : s->teams;
        int n1 = pos_res(b, lo) - pos_res(a, lo) + 1, n2 = pos_res(b, hi) - pos_res(a, hi) + 1;
        *src = first_stage + ix;
        *pa = pos_res(a, lo);
        *pb = pos_res(b, lo);
        if (lo && (pos_res(a, lo) < 1 || pos_res(b, lo) > lo || n1 < 1))
            ERR(ref, sp, "entrants: positions outside stage \"%s\" (%d clubs per table)", s->id, lo);
        else if (n1 != n2)
            ERR(ref, sp, "entrants: the range has %d clubs in a group of %d and %d in a group of %d: count both ends from the same side", n1, lo, n2, hi);
        else if (!cnt) { *per = 1; n = n1; }
        else if (a != b) ERR(ref, sp, "best_of \"%s\": one place ('stage#2': the best runners-up)", ref->str);
        else if (s->groups < 2) ERR(ref, sp, "best_of \"%s\": stage \"%s\" has one table (best_of ranks one place across groups)", ref->str, s->id);
        else if (int_in(c, cnt, "count", sp, 1, s->groups, &k)) { *per = 0; n = (int)k; }
    }
    return n;
}

/* v1.3: one item of a round's 'joins' -- "club:<id>", "<stage>#N-M" (an earlier stage of this competition, this
   season), "<competition>#N-M" (another competition's places), {"from": <competition>, "clubs": n} (by its quota).
   Returns the clubs it brings, -1 after an error. */
static int join_item(C *c, JNode *it, const char *rp, int ci, StageInfo *all_st, int first_stage, int nall, const char *self)
{
    char *hash;
    int a, b, sx, kx, n = -1;
    if (it->type == J_OBJ) {
        JNode *from = need(c, it, "from", J_STR, rp), *cl = need(c, it, "clubs", J_NUM, rp);
        long k = 0;
        unknown_keys(c, it, rp);
        if (!from || !cl) return -1;
        kx = find_comp(c, from->str);
        if (kx < 0) ERR(from, rp, "joins: no competition \"%s\"", from->str);
        else if (kx == ci) ERR(from, rp, "joins: a competition does not feed itself");
        else if (int_in(c, cl, "clubs", rp, 1, 1024, &k)) n = (int)k;
        return n;
    }
    if (it->type != J_STR) { ERR(it, rp, "joins: an item is \"club:<id>\", \"<stage or competition>#N-M\" or {\"from\": <competition>, \"clubs\": n}"); return -1; }
    if (!strncmp(it->str, "club:", 5)) {
        if (find_club(c, it->str) < 0) { ERR(it, rp, "joins: club \"%s\" is not in 'clubs'", it->str); return -1; }
        return 1;
    }
    hash = strchr(it->str, '#');
    if (!hash || !positions(hash + 1, &a, &b)) {
        ERR(it, rp, "joins \"%s\": write 'club:<id>', '<stage>#N-M' or '<competition>#N-M'", it->str);
        return -1;
    }
    *hash = 0;
    sx = stage_index(all_st + first_stage, nall - first_stage, it->str);
    kx = find_comp(c, it->str);
    if (sx >= 0 && kx >= 0) ERR(it, rp, "joins: '%s' is both a stage and a competition: rename one", it->str);
    else if (self && !strcmp(it->str, self)) ERR(it, rp, "joins: stage \"%s\" cannot feed itself (an earlier stage)", it->str);
    else if (sx >= 0) {
        int lo = all_st[first_stage + sx].gmin ? all_st[first_stage + sx].gmin : all_st[first_stage + sx].teams;
        if (lo && (pos_res(a, lo) < 1 || pos_res(b, lo) > lo || pos_res(b, lo) < pos_res(a, lo)))
            ERR(it, rp, "joins: positions outside stage \"%s\" (%d clubs per table)", it->str, lo);
        else n = (pos_res(b, lo) - pos_res(a, lo) + 1) * (all_st[first_stage + sx].per_group ? 1 : all_st[first_stage + sx].groups);
    } else if (kx >= 0) {
        JNode *cn = json_get(json_get(c->comps[kx], "participants"), "count");
        int size = cn && cn->type == J_NUM ? (int)cn->num : 0;
        if (kx == ci) ERR(it, rp, "joins: a competition does not feed itself");
        else if (size && (pos_res(a, size) < 1 || pos_res(b, size) > size || pos_res(b, size) < pos_res(a, size)))
            ERR(it, rp, "joins: positions outside competition \"%s\" (%d clubs)", it->str, size);
        else n = pos_res(b, size) - pos_res(a, size) + 1;
    } else ERR(it, rp, "joins: no earlier stage or competition \"%s\"", it->str);
    *hash = '#';
    return n;
}

static void do_comp(C *c, int ci, StageInfo *all_st, int *nall)
{
    JNode *cp = c->comps[ci], *id, *names, *kind, *country, *parts, *points, *stages, *outcomes, *modes, *level, *v;
    char path[256], p2[300];
    SwcComp rec;
    int k, teams = 0, first_stage = *nall, qbyes = -1;
    long x;
    memset(&rec, 0, sizeof rec);
    rec.entr = (uint16_t)(c->entr.n / 2);
    pathf(path, sizeof path, "competitions[%d]", ci);
    if (cp->type != J_OBJ) { ERR(cp, path, "a competition is an object"); return; }
    id = need(c, cp, "id", J_STR, path);
    if (id) {
        pathf(path, sizeof path, "%s", id->str);
        if (!id_ok(id->str, NULL)) ERR(id, path, "competition id \"%s\": lower-case words joined by '.', '-' or '_'", id->str);
        if (find_comp(c, id->str) != ci) ERR(id, path, "competition id \"%s\" used twice", id->str);
        rec.id = str_add(c, id->str);
    }
    kind = need(c, cp, "kind", J_STR, path);
    if ((k = enum_member(c, kind, "kind", path, KINDS, NE(KINDS))) > 0) rec.kind = (uint8_t)k;
    cur_kind = k;
    country = need(c, cp, "country", J_STR, path);
    if (country) {
        if (!c->countries || !json_get(c->countries, country->str)) ERR(country, path, "country \"%s\" is not in 'countries'", country->str);
        rec.country = str_add(c, country->str);
    }
    if ((level = opt(c, cp, "level", J_NUM, path)) && int_in(c, level, "level", path, 1, 32, &x)) rec.level = (uint8_t)x;
    if ((v = opt(c, cp, "refill", J_STR, path))) {
        if (strcmp(v->str, "pool")) ERR(v, path, "refill must be \"pool\" (clubs that leave the world are replaced from an off-world pool)");
        rec.flags |= 1;
    }
    if ((v = opt(c, cp, "pool", J_STR, path))) {        /* v1.3: which pool refills it (checked with 'pools') */
        need_minor(c, v, path, 3, "'pool'");
        if (!json_get(cp, "refill")) ERR(v, path, "'pool' goes with \"refill\": \"pool\"");
    }
    if ((v = opt(c, cp, "realloc", J_STR, path))) {     /* v1.3: the summer re-allocation of a whole level */
        need_minor(c, v, path, 3, "'realloc'");
        if (strcmp(v->str, "by_rank")) ERR(v, path, "realloc must be \"by_rank\"");
    }
    if ((v = opt(c, cp, "chain", J_BOOL, path))) need_minor(c, v, path, 3, "'chain'");
    if (rec.kind == SWC_K_LEAGUE && !level) ERR(cp, path, "a league needs its pyramid 'level'");
    names = need(c, cp, "names", J_OBJ, path);
    rec.name = names_rec(c, names, country ? country->str : NULL, path, rec.id);

    if ((parts = need(c, cp, "participants", J_OBJ, path))) {
        JNode *cnt = need(c, parts, "count", J_NUM, path), *list = opt(c, parts, "clubs", J_ARR, path),
              *from = opt(c, parts, "from", J_STR, path), *quota = opt(c, parts, "quota", J_ARR, path);
        pathf(p2, sizeof p2, "%s.participants", path);
        if (int_in(c, cnt, "count", p2, 2, c->max_teams, &x)) teams = (int)x;
        if (quota) {                                    /* v1.3: n clubs from each competition (a cup's entry) */
            JNode *q;
            int sum = 0, qi = 0;
            qbyes = 0;
            need_minor(c, quota, p2, 3, "'quota'");
            if (list || from) ERR(quota, p2, "give one of 'clubs', 'from': \"link\" or 'quota'");
            for (q = quota->child; q; q = q->next, qi++) {
                char qp[340];
                JNode *qf;
                long n = 0, by = 0;
                pathf(qp, sizeof qp, "%s.quota[%d]", p2, qi);
                if (q->type != J_OBJ) { ERR(q, qp, "a quota is {\"from\": <competition>, \"clubs\": n, \"byes\": b}"); continue; }
                if ((qf = need(c, q, "from", J_STR, qp))) {
                    int k2 = find_comp(c, qf->str);
                    if (k2 < 0) ERR(qf, qp, "no competition \"%s\"", qf->str);
                    else if (k2 == ci) ERR(qf, qp, "a competition does not feed itself");
                }
                if (int_in(c, need(c, q, "clubs", J_NUM, qp), "clubs", qp, 1, 1024, &n)) sum += (int)n;
                if (int_in(c, opt(c, q, "byes", J_NUM, qp), "byes", qp, 0, 1024, &by)) {
                    if (by > n) ERR(json_get(q, "byes"), qp, "%ld byes for %ld clubs", by, n);
                    qbyes += (int)by;
                }
                unknown_keys(c, q, qp);
            }
            if (teams && sum != teams) ERR(quota, p2, "the quotas add up to %d clubs, 'count' says %d", sum, teams);
        } else if (!list == !from) ERR(parts, p2, "give either 'clubs' (a fixed list) or 'from': \"link\"");
        if (from && !strcmp(from->str, "numbers")) need_minor(c, from, p2, 4, "\"from\": \"numbers\" (a knockout's bracket numbers)");
        else if (from && strcmp(from->str, "link")) ERR(from, p2, "'from' must be \"link\" (entrants come from other competitions' outcomes)");
        {
            JNode *src = opt(c, parts, "source", J_ARR, p2), *q;
            if (src && !from) ERR(src, p2, "'source' goes with \"from\": \"link\"");
            for (q = src ? src->child : NULL; q; q = q->next) {
                int k2 = q->type == J_STR ? find_comp(c, q->str) : -1;
                if (k2 < 0) ERR(q, p2, "source: no competition \"%s\"", q->type == J_STR ? q->str : "?");
                else if (!strcmp(from->str, "numbers")) {   /* (v1.4: the leagues whose clubs may be numbered) */
                    JNode *kd = json_get(c->comps[k2], "kind");
                    if (!kd || kd->type != J_STR || strcmp(kd->str, "league")) ERR(q, p2, "source: \"%s\" is not a league", q->str);
                } else { uint16_t pr2[2] = { (uint16_t)ci, (uint16_t)k2 }; bput(&c->srce, pr2, 4); }
            }
        }
        if (list) {
            JNode *m;
            int i = 0;
            if (teams && list->n != teams) ERR(list, p2, "%d clubs listed, 'count' says %d", list->n, teams);
            for (m = list->child; m; m = m->next, i++) {
                int ix, j;
                JNode *o;
                if (m->type != J_STR) { ERR(m, p2, "clubs[%d] must be a club id", i); continue; }
                ix = find_club(c, m->str);
                if (ix < 0) { ERR(m, p2, "club \"%s\" is not in 'clubs'", m->str); continue; }
                for (j = 0, o = list->child; o != m; o = o->next, j++)
                    if (o->type == J_STR && !strcmp(o->str, m->str)) { ERR(m, p2, "club \"%s\" listed twice (clubs[%d])", m->str, j); break; }
                { uint16_t u = (uint16_t)ix; bput(&c->entr, &u, 2); rec.nentr++; }
            }
        }
        unknown_keys(c, parts, p2);
    }
    if ((points = opt(c, cp, "points", J_OBJ, path))) {
        long w = 3, d = 1, l = 0;
        pathf(p2, sizeof p2, "%s.points", path);
        int_in(c, need(c, points, "win", J_NUM, p2), "win", p2, 0, 10, &w);
        int_in(c, need(c, points, "draw", J_NUM, p2), "draw", p2, 0, 10, &d);
        int_in(c, need(c, points, "loss", J_NUM, p2), "loss", p2, 0, 10, &l);
        rec.win = (uint8_t)w; rec.draw = (uint8_t)d; rec.loss = (uint8_t)l;
        unknown_keys(c, points, p2);
    } else { rec.win = 3; rec.draw = 1; }

    rec.first_stage = (uint16_t)(c->stag.n / sizeof(SwcStage));
    if ((stages = need(c, cp, "stages", J_ARR, path))) {
        JNode *st;
        int si = 0;
        if (!stages->n) ERR(stages, path, "a competition has at least one stage");
        for (st = stages->child; st; st = st->next, si++) {
            SwcStage s;
            JNode *sid, *type, *legs, *tb, *rounds, *groups, *entrants;
            int t, ntb = 0;
            char sp[320];
            memset(&s, 0, sizeof s);
            s.comp = (uint16_t)ci;
            s.groups = 1;
            s.teams = (uint16_t)teams;
            pathf(sp, sizeof sp, "%s.stages[%d]", path, si);
            if (st->type != J_OBJ) { ERR(st, sp, "a stage is an object"); continue; }
            sid = need(c, st, "id", J_STR, sp);
            if (sid) {
                char full[300];
                pathf(sp, sizeof sp, "%s.stages.%s", path, sid->str);
                if (!id_ok(sid->str, NULL) || strchr(sid->str, '.')) ERR(sid, sp, "stage id \"%s\": lower-case words joined by '-' or '_'", sid->str);
                if (stage_index(all_st + first_stage, *nall - first_stage, sid->str) >= 0) ERR(sid, sp, "stage id \"%s\" used twice", sid->str);
                pathf(full, sizeof full, "%s:%s", id ? id->str : "?", sid->str);
                s.id = str_add(c, full);
                s.purpose = swc_fnv1a(full, (uint32_t)strlen(full));
            }
            type = need(c, st, "type", J_STR, sp);
            t = enum_member(c, type, "type", sp, STYPES, NE(STYPES));
            if (t > 0) s.type = (uint8_t)t;
            all_st[*nall].groups = 1;
            entrants = json_get(st, "entrants");                         /* "table#3-6" inside this competition */
            if (entrants && entrants->type != J_STR && entrants->type != J_ARR) {
                ERR(entrants, sp, "'entrants' must be a string ('stage#N-M') or a list of places");
                entrants = NULL;
            }
            if (entrants && entrants->type == J_ARR && !entrants->n) ERR(entrants, sp, "entrants: an empty list");
            if (entrants && entrants->type == J_ARR) need_minor(c, entrants, sp, 2, "'entrants' as a list");
            if (entrants) {                     /* v1.2: a list of places ["table#3", "table#6"] and best_of items */
                JNode *pg = opt(c, st, "per_group", J_BOOL, sp), *it;
                int total = 0, pgn = 0, gsrc = -1, same = 1, best = 0, ok = 1, nr = 0, i2, ncup = 0;
                struct { int src, a, b; } rg[64];
                for (it = entrants->type == J_STR ? entrants : entrants->child; it; it = entrants->type == J_STR ? NULL : it->next) {
                    int src = -1, per = 0, a = 0, b = 0, n = entr_item(c, it, sp, all_st, first_stage, *nall, &src, &per, &a, &b);
                    if (n < 0) { ok = 0; continue; }
                    if (per) { total += n * all_st[src].groups; pgn += n; } else { total += n; best = 1; }
                    if (it->type == J_OBJ && json_get(it, "cup_place") && ncup++)
                        ERR(it, sp, "cup_place: one per stage");
                    if (gsrc < 0) gsrc = src; else if (src != gsrc) same = 0;
                    for (i2 = 0; i2 < nr; i2++)
                        if (rg[i2].src == src && src != -1 && a <= rg[i2].b && rg[i2].a <= b)
                            ERR(it, sp, "entrants: place %d of %s \"%s\" is taken twice", a > rg[i2].a ? a : rg[i2].a,
                                src >= 0 ? "stage" : "league", src >= 0 ? all_st[src].id : json_get(c->comps[-2 - src], "id")->str);
                    if (nr < 64) { rg[nr].src = src; rg[nr].a = a; rg[nr].b = b; nr++; }
                }
                if (!ok) s.teams = 0;              /* (unknown: no round counts) */
                else if (gsrc >= 0) {
                    StageInfo *src = &all_st[gsrc];
                    if (src->groups > 1 && same && !best && (pg ? pg->b : t == SWC_ST_PLAYOFF)) {   /* one play-off per group */
                        all_st[*nall].per_group = 1;
                        all_st[*nall].groups = src->groups;
                        s.groups = (uint16_t)src->groups;
                        s.flags |= 1;
                        s.teams = (uint16_t)pgn;
                    } else {
                        if (pg && pg->b && src->groups > 1)
                            ERR(pg, sp, "per_group: the entrants must all come from one groups stage, with no best_of");
                        s.teams = (uint16_t)total;
                    }
                }
            }
            legs = opt(c, st, "legs", J_NUM, sp);
            if (legs && int_in(c, legs, "legs", sp, 1, 4, &x)) s.legs = (uint8_t)x;
            groups = opt(c, st, "groups", J_NUM, sp);
            if (groups && int_in(c, groups, "groups", sp, 1, 64, &x)) s.groups = (uint16_t)x;
            if (t == SWC_ST_GROUPS || t == SWC_ST_ROUND_ROBIN) {
                JNode *sz = opt(c, st, "sizes", J_ARR, sp), *q;
                int g = s.groups ? s.groups : 1;
                all_st[*nall].groups = g;
                if (sz) {
                    int sum = 0, mn = 999, mx = 0;
                    if (sz->n != g) ERR(sz, sp, "sizes: %d listed, 'groups' says %d", sz->n, g);
                    for (q = sz->child; q; q = q->next) {
                        long k = 0;
                        if (!int_in(c, q, "sizes[]", sp, 2, 255, &k)) continue;
                        sum += (int)k; mn = k < mn ? (int)k : mn; mx = k > mx ? (int)k : mx;
                        { uint16_t gr[2] = { (uint16_t)(c->stag.n / sizeof(SwcStage)), (uint16_t)k }; bput(&c->grps, gr, 4); }
                    }
                    if (s.teams && sum != s.teams) ERR(sz, sp, "sizes add up to %d, the stage has %d clubs", sum, s.teams);
                    all_st[*nall].gmin = mn; all_st[*nall].gmax = mx;
                } else {
                    if (g > 1 && s.teams % g) ERR(st, sp, "%d clubs do not split into %d equal groups: give 'sizes'", s.teams, g);
                    all_st[*nall].gmin = all_st[*nall].gmax = s.teams / g;
                }
            }
            if (t == SWC_ST_ROUND_ROBIN || t == SWC_ST_GROUPS) {
                int per;
                if (!legs) ERR(st, sp, "a %s stage needs 'legs' (meetings per pair)", STYPES[t]);
                tb = need(c, st, "tiebreakers", J_ARR, sp);
                s.tieb = code_list(c, &c->tieb, tb, "tie-breaker", sp, TIEBS, NE(TIEBS), &ntb);
                if (tb && !ntb) ERR(tb, sp, "tiebreakers: at least one criterion");
                per = all_st[*nall].gmax;
                if (per > c->long_table_rows)
                    ERR(st, sp, "%d clubs in one table: the host shows at most %d", per, c->long_table_rows);
                else if (per > c->table_rows && !(json_get(st, "long_table") && json_get(st, "long_table")->b))
                    ERR(st, sp, "%d clubs in one table: over %d needs \"long_table\": true (scrolling table)", per, c->table_rows);
                if (s.teams && s.teams < 2) ERR(st, sp, "a table needs at least 2 clubs");
            } else if (t == SWC_ST_KNOCKOUT || t == SWC_ST_PLAYOFF) {
                JNode *r, *sk;
                int ri = 0, left = s.teams, joined = 0, jbad = 0, jn[256] = { 0 };
                s.first_round = (uint16_t)(c->roun.n / sizeof(SwcRound));
                rounds = need(c, st, "rounds", J_ARR, sp);
                memset(num_used, 0, sizeof num_used);
                memset(win_used, 0, sizeof win_used);
                cur_numbers = numbers_of(c, st, sp, parts);
                if (cur_numbers && s.teams && cur_numbers != s.teams)
                    ERR(json_get(st, "numbers"), sp, "numbers: the groups take %d clubs, the competition has %d", cur_numbers, s.teams);
                {                                       /* v1.4 (42-s3shape): the first entrant straight through when that
                                                           far ahead; a level of groups ranked place, then the record */
                    JNode *di = opt(c, st, "direct_if", J_OBJ, sp), *rk = opt(c, st, "rank", J_STR, sp);
                    if (di) {
                        need_minor(c, di, sp, 4, "a playoff stage's 'direct_if'");
                        if (t != SWC_ST_PLAYOFF) ERR(di, sp, "'direct_if' goes on a playoff stage");
                        gap_obj(c, di, sp);
                    }
                    if (rk) {
                        need_minor(c, rk, sp, 4, "'rank'");
                        if (strcmp(rk->str, "place_then_record")) ERR(rk, sp, "rank: \"place_then_record\"");
                    }
                }
                if ((sk = opt(c, st, "skip_if", J_OBJ, sp))) {             /* v1.3: no play-off when the gap is big */
                    long g2 = 0;
                    need_minor(c, sk, sp, 3, "'skip_if'");
                    if (t != SWC_ST_PLAYOFF) ERR(sk, sp, "'skip_if' goes on a playoff stage");
                    int_in(c, need(c, sk, "points_gap_ge", J_NUM, sp), "points_gap_ge", sp, 1, 99, &g2);
                    unknown_keys(c, sk, sp);
                }
                for (r = rounds ? rounds->child : NULL; r && ri < 256; r = r->next, ri++) {   /* v1.3: clubs joining a round */
                    JNode *jl = json_get(r, "joins"), *it;
                    char rp[360];
                    pathf(rp, sizeof rp, "%s.rounds[%d]", sp, ri);
                    if (!jl) continue;
                    need_minor(c, jl, rp, 3, "'joins'");
                    if (jl->type != J_ARR || !jl->n) { ERR(jl, rp, "'joins' is a list of items"); continue; }
                    for (it = jl->child; it; it = it->next) {
                        int n = join_item(c, it, rp, ci, all_st, first_stage, *nall, sid ? sid->str : NULL);
                        if (n > 0) jn[ri] += n; else jbad = 1;
                    }
                    joined += jn[ri];
                }
                if (jbad) left = 0;                     /* (a bad item: the counts are unknown) */
                if (left && !entrants) {                /* the competition's participants include the clubs that join later */
                    if (joined >= left) ERR(st, sp, "%d clubs join later rounds, the competition has %d", joined, left);
                    left -= joined;
                }
                ri = 0;
                long rties[256];
                for (r = rounds ? rounds->child : NULL; r; r = r->next, ri++) {
                    SwcRound rr;
                    JNode *dec;
                    char rp[360];
                    int nd = 0, h, pr;
                    long ties = 0, rl = 1, byes = 0;
                    memset(&rr, 0, sizeof rr);
                    pathf(rp, sizeof rp, "%s.rounds[%d]", sp, ri);
                    if (r->type != J_OBJ) { ERR(r, rp, "a round is an object"); continue; }
                    int_in(c, need(c, r, "ties", J_NUM, rp), "ties", rp, 1, 1024, &ties);
                    {                                       /* v1.4: a round played in each of several leagues (a playoff
                                                               competition: 'per' the leagues, 'ties' each one's), its
                                                               clubs listed ("#N-M" of that league, "<league>#N",
                                                               "winners:R": an earlier round's -- that league's under 'per') */
                        JNode *pe = opt(c, r, "per", J_ARR, rp), *cl = opt(c, r, "clubs", J_ARR, rp), *q;
                        int np = 0;
                        if (pe) {
                            need_minor(c, pe, rp, 4, "a round's 'per'");
                            if (rec.kind != SWC_K_PLAYOFF) ERR(pe, rp, "'per' goes on a playoff competition's round");
                            for (q = pe->child; q; q = q->next, np++) {
                                int kx = q->type == J_STR ? find_comp(c, q->str) : -1;
                                JNode *kd = kx >= 0 ? json_get(c->comps[kx], "kind") : NULL;
                                if (!kd || kd->type != J_STR || strcmp(kd->str, "league")) ERR(q, rp, "per: a league id of this file");
                            }
                        }
                        if (cl) {
                            need_minor(c, cl, rp, 4, "a round's 'clubs'");
                            for (q = cl->child; q; q = q->next) {
                                long rr2 = 0;
                                char *e2;
                                int a2, b2;
                                const char *h2 = q->type == J_STR ? strchr(q->str, '#') : NULL;
                                if (q->type == J_STR && !strncmp(q->str, "winners:", 8) && (rr2 = strtol(q->str + 8, &e2, 10)) >= 1 && rr2 <= ri && !*e2) continue;
                                if (h2 && positions(h2 + 1, &a2, &b2) && a2 >= 1 && b2 >= a2 && (h2 != q->str || pe)) continue;
                                ERR(q, rp, "clubs: \"#N-M\" (with 'per'), \"<league>#N-M\" or \"winners:R\" (an earlier round)");
                            }
                        }
                        if (np > 1) ties *= np;             /* (the count checks: every league's ties) */
                    }
                    int_in(c, need(c, r, "legs", J_NUM, rp), "legs", rp, 1, 2, &rl);
                    int_in(c, opt(c, r, "byes", J_NUM, rp), "byes", rp, 0, 1024, &byes);
                    h = enum_member(c, need(c, r, "home", J_STR, rp), "home", rp, HOMES, NE(HOMES));
                    pr = enum_member(c, need(c, r, "pairing", J_STR, rp), "pairing", rp, PAIRS, NE(PAIRS));
                    if (pr == 4) need_minor(c, json_get(r, "pairing"), rp, 3, "the pairing \"by_place\"");
                    if (pr == 5 || pr == 6) ERR(r, rp, "the pairing \"%s\" goes on a pool file's 'up.cross' (swc/1.4)", PAIRS[pr]);
                    dec = json_get(r, "decider");
                    if (!dec) ERR(r, rp, "missing 'decider': every round says how a level tie ends (e.g. [\"aggregate\", \"extra_time\", \"penalties\"])");
                    else if (dec->type != J_ARR) ERR(dec, rp, "'decider' must be an array");
                    rr.deci = code_list(c, &c->deci, dec, "decider", rp, DECIS, NE(DECIS), &nd);
                    if (dec && dec->type == J_ARR && !nd) ERR(dec, rp, "decider: at least one entry");
                    for (JNode *d = dec && dec->type == J_ARR ? dec->child : NULL; d; d = d->next)
                        if (d->type == J_STR && (!strcmp(d->str, DECIS[9]) || !strcmp(d->str, DECIS[10])))
                        {
                            need_minor(c, d, rp, 4, d->str[0] == 's' ? "the decider \"seed\"" : "the decider \"better_placed_unless_level_on_points\"");
                            if (d->str[0] == 's' && pr != 1) ERR(d, rp, "the decider \"seed\" goes in a seeded round (\"pairing\": \"seeded\")");
                        }
                    {                                       /* v1.4: a seeded round's seeds, the clubs drawn against them */
                        JNode *se = opt(c, r, "seeds", J_ARR, rp), *dr = opt(c, r, "drawn", J_ARR, rp), *q;
                        if (se) {
                            need_minor(c, se, rp, 4, "'seeds'");
                            if (pr != 1) ERR(se, rp, "'seeds' need \"pairing\": \"seeded\"");
                            if (se->n != ties) ERR(se, rp, "seeds: %d listed, 'ties' says %ld", se->n, ties);
                            int rest = 0;               /* (a 'rest_of' stands for the round's other winners) */
                            for (q = dr ? dr->child : NULL; q; q = q->next) rest |= q->type == J_OBJ && json_get(q, "rest_of");
                            if (dr && !rest && dr->n != ties) ERR(dr, rp, "drawn: %d listed, 'ties' says %ld", dr->n, ties);
                            for (q = se->child; q; q = q->next) ko_ref(c, q, rp, ri, rties, all_st, first_stage, *nall);
                            for (q = dr ? dr->child : NULL; q; q = q->next) ko_ref(c, q, rp, ri, rties, all_st, first_stage, *nall);
                            for (int k2 = 0; k2 < 2; k2++)  /* (one reference once in the round) */
                                for (q = (k2 ? dr : se) ? (k2 ? dr : se)->child : NULL; q; q = q->next)
                                    for (int k3 = k2; k3 < 2; k3++)
                                        for (JNode *q2 = k3 == k2 ? q->next : (dr ? dr->child : NULL); q2; q2 = q2->next) {
                                            const char *x1 = q->type == J_STR ? q->str : NULL, *x2 = q2->type == J_STR ? q2->str : NULL;
                                            JNode *b1 = q->type == J_OBJ ? json_get(q, "best_of") : NULL, *b2 = q2->type == J_OBJ ? json_get(q2, "best_of") : NULL;
                                            JNode *r1 = q->type == J_OBJ ? json_get(q, "rest_of") : NULL, *r2 = q2->type == J_OBJ ? json_get(q2, "rest_of") : NULL;
                                            if ((x1 && x2 && !strcmp(x1, x2)) ||
                                                (b1 && b2 && b1->type == J_STR && b2->type == J_STR && !strcmp(b1->str, b2->str)) ||
                                                (r1 && r2 && r1->type == J_STR && r2->type == J_STR && !strcmp(r1->str, r2->str)))
                                                ERR(q2, rp, "seeds / drawn: a club listed twice in one round");
                                        }
                        } else if (dr) ERR(dr, rp, "'drawn' goes with 'seeds'");
                    }
                    if (pr == 3) {                          /* pairing "fixed": the draw as really made */
                        JNode *fx = need(c, r, "fixed", J_ARR, rp), *pair;
                        int fi = 0;
                        if (fx && fx->n != ties) ERR(fx, rp, "fixed: %d pairs listed, 'ties' says %ld", fx->n, ties);
                        for (pair = fx ? fx->child : NULL; pair; pair = pair->next, fi++) {
                            JNode *h1 = json_at(pair, 0), *a1 = json_at(pair, 1);
                            SwcFixed f;
                            int hx, ax;
                            if (pair->type != J_ARR || pair->n != 2 || h1->type != J_STR || a1->type != J_STR) {
                                ERR(pair, rp, "fixed[%d]: a pair is [\"home club id\", \"away club id\"]", fi);
                                continue;
                            }
                            if (strncmp(h1->str, "club:", 5) || strncmp(a1->str, "club:", 5)) {   /* v1.4: places and earlier
                                                           ties' winners ("table#3", "winner:1.2"): a bracket of the shape */
                                for (int k2 = 0; k2 < 2; k2++) {
                                    JNode *q = k2 ? a1 : h1;
                                    if (strncmp(q->str, "club:", 5)) ko_ref(c, q, rp, ri, rties, all_st, first_stage, *nall);
                                    else ERR(q, rp, "fixed[%d]: clubs and places are not mixed in one pair", fi);
                                }
                                if (!strcmp(h1->str, a1->str)) ERR(a1, rp, "fixed[%d]: a club cannot play itself", fi);
                                continue;
                            }
                            hx = find_club(c, h1->str);
                            ax = find_club(c, a1->str);
                            if (hx < 0) ERR(h1, rp, "fixed[%d]: club \"%s\" is not in 'clubs'", fi, h1->str);
                            if (ax < 0) ERR(a1, rp, "fixed[%d]: club \"%s\" is not in 'clubs'", fi, a1->str);
                            if (hx >= 0 && hx == ax) ERR(a1, rp, "fixed[%d]: a club cannot play itself", fi);
                            if (ri == 0 && hx >= 0 && ax >= 0) {   /* first round: both must be participants, once */
                                const uint16_t *en = (const uint16_t *)c->entr.p + rec.entr;
                                int k2, inh = 0, ina = 0;
                                JNode *q;
                                for (k2 = 0; k2 < rec.nentr; k2++) { inh |= en[k2] == hx; ina |= en[k2] == ax; }
                                if (rec.nentr && (!inh || !ina)) ERR(pair, rp, "fixed[%d]: both clubs must be participants of the competition", fi);
                                for (q = fx->child; q != pair; q = q->next) {
                                    JNode *qh = json_at(q, 0), *qa = json_at(q, 1);
                                    if (q->type == J_ARR && q->n == 2 && qh->type == J_STR && qa->type == J_STR &&
                                        (!strcmp(qh->str, h1->str) || !strcmp(qa->str, h1->str) ||
                                         !strcmp(qh->str, a1->str) || !strcmp(qa->str, a1->str)))
                                        ERR(pair, rp, "fixed[%d]: a club is drawn twice in this round", fi);
                                }
                            }
                            f.round = (uint16_t)(c->roun.n / sizeof(SwcRound));
                            f.home = (uint16_t)(hx < 0 ? 0 : hx);
                            f.away = (uint16_t)(ax < 0 ? 0 : ax);
                            bput(&c->fixt, &f, sizeof f);
                        }
                    } else if (json_get(r, "fixed")) ERR(json_get(r, "fixed"), rp, "'fixed' pairs need \"pairing\": \"fixed\"");
                    if (ri < 256 && left) left += jn[ri];
                    if (ri == 0 && *nall == first_stage && qbyes >= 0 && byes != qbyes)
                        ERR(r, rp, "the first round has %ld byes, the quotas give %d", byes, qbyes);
                    if ((h == 5 || h == 6) && !cur_numbers) ERR(json_get(r, "home"), rp, "home \"%s\" needs the stage's 'numbers'", HOMES[h]);
                    if (h == 5 || h == 6) need_minor(c, json_get(r, "home"), rp, 4, "home \"lower_number\"");
                    if (h == 6 && rl != 2) ERR(json_get(r, "home"), rp, "home \"lower_number_second_leg\" needs 'legs': 2");
                    if (cur_numbers && pr != 3) ERR(json_get(r, "pairing"), rp, "a stage with 'numbers' plays its bracket: \"pairing\": \"fixed\"");
                    if (left && !jbad && !cur_numbers && ties * 2 + byes != left) {
                        if (ri < 256 && jn[ri])
                            ERR(r, rp, "%ld ties + %ld byes take %ld clubs, %d reach this round (%d of them joining)", ties, byes, ties * 2 + byes, left, jn[ri]);
                        else
                            ERR(r, rp, "%ld ties + %ld byes take %ld clubs, %d reach this round", ties, byes, ties * 2 + byes, left);
                    }
                    left = (int)(ties + byes);
                    if (ri < 256) rties[ri] = ties;
                    all_st[*nall].last_ties = (int)ties;
                    rr.ties = (uint16_t)ties;
                    rr.legs = (uint8_t)rl;
                    rr.home = (uint8_t)(h < 0 ? 0 : h);
                    rr.pairing = (uint8_t)(pr < 0 ? 0 : pr);
                    unknown_keys(c, r, rp);
                    bput(&c->roun, &rr, sizeof rr);
                    s.nrounds++;
                }
                if (rounds && !rounds->n) ERR(rounds, sp, "rounds: at least one");
                if (rounds && left != 1 && s.teams && !jbad && t == SWC_ST_KNOCKOUT)    /* (a play-off may qualify several) */
                    ERR(rounds, sp, "the rounds end with %d clubs, not one winner", left);
                if (cur_numbers && rounds) {             /* v1.4 (42-s2ko): every number enters once, every winner plays on */
                    JNode *sw = json_get(json_get(st, "first_season"), "swap_home"), *q;
                    for (int k2 = 1; k2 <= cur_numbers; k2++)
                        if (!num_used[k2]) { ERR(rounds, sp, "bracket number %d never enters ('#%d' in no round)", k2, k2); break; }
                    for (int r2 = 0; r2 + 1 < ri && r2 < 256; r2++)
                        for (int t2 = 0; t2 < rties[r2] && t2 < 256; t2++)
                            if (!win_used[r2][t2]) { ERR(rounds, sp, "the winner of tie %d.%d plays no later round", r2 + 1, t2 + 1); r2 = 256; break; }
                    for (q = sw && sw->type == J_ARR ? sw->child : NULL; q; q = q->next) {
                        char *e;
                        long r3 = q->type == J_STR ? strtol(q->str, &e, 10) : 0, t3 = 0;
                        if (r3 >= 1 && *e == '.') t3 = strtol(e + 1, &e, 10);
                        if (r3 >= 1 && r3 <= ri && r3 <= 256 && (t3 < 1 || t3 > rties[r3 - 1]))
                            ERR(q, sp, "first_season.swap_home \"%s\": round %ld has %ld ties", q->str, r3, rties[r3 - 1]);
                        else if (r3 > ri) ERR(q, sp, "first_season.swap_home \"%s\": the stage has %d rounds", q->str, ri);
                    }
                }
                cur_numbers = 0;
            }
            json_get(st, "long_table");
            json_get(st, "per_group");
            unknown_keys(c, st, sp);
            bput(&c->stag, &s, sizeof s);
            all_st[*nall].id = sid ? sid->str : "";
            all_st[*nall].teams = s.teams;
            all_st[*nall].type = t;
            (*nall)++;
            rec.nstages++;
            c->arena += 64u + (uint32_t)s.teams * 32u + (uint32_t)s.nrounds * 16u;
        }
    }
    rec.first_outc = (uint16_t)(c->outc.n / sizeof(SwcOutcome));
    if ((outcomes = opt(c, cp, "outcomes", J_ARR, path))) {
        JNode *o;
        int oi = 0;
        for (o = outcomes->child; o; o = o->next, oi++) {
            SwcOutcome r;
            JNode *st, *pos, *tag, *zone, *to, *assign, *season;
            int a = 0, b = 0, sx, tg, z;
            char op[320];
            memset(&r, 0, sizeof r);
            r.target = 0xFFFF;
            pathf(op, sizeof op, "%s.outcomes[%d]", path, oi);
            if (o->type != J_OBJ) { ERR(o, op, "an outcome is an object"); continue; }
            st = need(c, o, "stage", J_STR, op);
            pos = need(c, o, "positions", J_STR, op);
            tag = need(c, o, "tag", J_STR, op);
            zone = opt(c, o, "zone", J_STR, op);
            to = json_get(o, "to");
            if (to && to->type != J_STR && to->type != J_ARR) { ERR(to, op, "'to' must be a competition id or a list of them"); to = NULL; }
            assign = opt(c, o, "assign", J_STR, op);
            season = opt(c, o, "season", J_STR, op);
            sx = st ? stage_index(all_st + first_stage, *nall - first_stage, st->str) : -1;
            if (st && sx < 0) ERR(st, op, "stage \"%s\" is not a stage of this competition", st->str);
            if (pos && !positions(pos->str, &a, &b)) ERR(pos, op, "positions \"%s\": write 'N', 'N-M' (1-based) or 'LN' (from the bottom)", pos->str);
            else if (pos && sx >= 0) {
                StageInfo *si = &all_st[first_stage + sx];
                int ko = si->type == SWC_ST_KNOCKOUT || si->type == SWC_ST_PLAYOFF;
                int lo = ko ? 2 : (si->gmin ? si->gmin : si->teams), hi = ko ? 2 : (si->gmax ? si->gmax : si->teams);
                if (ko && (a > 2 || b > 2)) ERR(pos, op, "positions \"%s\": a knockout stage ranks 1 (winners) and 2 (losers of the last round)", pos->str);
                else if (lo && (pos_res(a, lo) < 1 || pos_res(b, hi) > hi || pos_res(b, lo) < pos_res(a, lo)))
                    ERR(pos, op, "position %d, stage \"%s\" ranks %d clubs", pos_res(b, hi), st->str, hi);
            }
            tg = enum_member(c, tag, "tag", op, TAGS, NE(TAGS));
            z = zone ? enum_member(c, zone, "zone", op, ZONES, NE(ZONES)) : 0;
            if (to && to->type == J_STR && !strcmp(to->str, "out")) r.target = 0xFFFE;     /* leaves the world */
            else if (to && to->type == J_STR) {
                int t2 = find_comp(c, to->str);
                if (t2 < 0) ERR(to, op, "'to': no competition \"%s\" in this world", to->str);
                else if (t2 == ci && !season) ERR(to, op, "'to' its own competition needs \"season\": \"next\"");
                r.target = (uint16_t)(t2 < 0 ? 0xFFFF : t2);
            } else if (to) {                    /* v1.2: parallel leagues of one level, each club to its area's */
                JNode *q, *q2;
                int qi = 0, lv = -1;
                need_minor(c, to, op, 2, "a 'to' list");
                if (to->n < 2) ERR(to, op, "a 'to' list names 2 or more leagues (one league: a string)");
                if (!assign) ERR(o, op, "a 'to' list needs \"assign\": \"geographic\" (each club goes to the league of its area)");
                for (q = to->child; q; q = q->next, qi++) {
                    int t2 = q->type == J_STR ? find_comp(c, q->str) : -1, l;
                    JNode *kd, *lvn;
                    if (t2 < 0) { ERR(q, op, "'to'[%d]: no competition \"%s\" in this world", qi, q->type == J_STR ? q->str : "?"); continue; }
                    for (q2 = to->child; q2 != q; q2 = q2->next)
                        if (q2->type == J_STR && !strcmp(q2->str, q->str)) { ERR(q, op, "'to'[%d]: \"%s\" listed twice", qi, q->str); break; }
                    kd = json_get(c->comps[t2], "kind");
                    lvn = json_get(c->comps[t2], "level");
                    l = lvn && lvn->type == J_NUM ? (int)lvn->num : 0;
                    if (!kd || kd->type != J_STR || strcmp(kd->str, "league")) ERR(q, op, "'to' list: \"%s\" is not a league", q->str);
                    else if (t2 == ci) ERR(q, op, "'to' list: \"%s\" is this competition", q->str);
                    else if (lv < 0) lv = l;
                    else if (l != lv) ERR(q, op, "'to' list: \"%s\" is level %d, the first league level %d (parallel leagues share one level)", q->str, l, lv);
                    if (r.target == 0xFFFF) r.target = (uint16_t)t2;
                    { uint16_t tl[2] = { (uint16_t)(c->outc.n / sizeof(SwcOutcome)), (uint16_t)t2 }; bput(&c->tols, tl, 4); }
                }
                r.flags |= SWC_OF_GEOGRAPHIC;
            }
            if (assign) {
                if (strcmp(assign->str, "geographic")) ERR(assign, op, "assign must be \"geographic\" (each club goes to the league of its area, clubs' 'area')");
                else if (!to || to->type != J_ARR) ERR(assign, op, "'assign' goes with a 'to' list");
            }
            if ((tg == SWC_TAG_PROMOTED || tg == SWC_TAG_RELEGATED) && !to) ERR(o, op, "a %s outcome needs 'to'", TAGS[tg]);
            if (season) {
                if (!strcmp(season->str, "next")) r.season = 1;
                else if (strcmp(season->str, "this")) ERR(season, op, "season must be \"this\" or \"next\"");
            }
            r.stage = (uint16_t)(sx < 0 ? 0 : rec.first_stage + sx);
            r.from = (uint8_t)a; r.to = (uint8_t)b;
            r.tag = (uint8_t)(tg < 0 ? 0 : tg);
            r.zone = (uint8_t)(z < 0 ? 0 : z);
            unknown_keys(c, o, op);
            bput(&c->outc, &r, sizeof r);
            rec.noutc++;
        }
    }
    if ((modes = opt(c, cp, "modes", J_ARR, path))) {
        JNode *m;
        SwcReg rg = { (uint16_t)ci, 0 };
        for (m = modes->child; m; m = m->next) {
            int e = enum_member(c, m, "mode", path, MODES, NE(MODES));
            if (e >= 0) rg.modes |= (uint16_t)(1u << e);
        }
        bput(&c->reg, &rg, sizeof rg);
    } else WARN(cp, path, "no 'modes': the competition is offered in no game mode (every world should be playable in all modes)");
    if ((v = json_get(cp, "_note"))) (void)v;
    unknown_keys(c, cp, path);
    bput(&c->comp, &rec, sizeof rec);
}

/* union-find over competitions; the root is the lowest index (errors point at a level's first league) */
static int uf_find(int *p, int i) { while (p[i] != i) i = p[i] = p[p[i]]; return i; }
static void uf_join(int *p, int a, int b)
{
    a = uf_find(p, a);
    b = uf_find(p, b);
    if (a < b) p[b] = a; else p[a] = b;
}

/* ---- link checks across competitions ----------------------------------------------------------------------------- */
/* ---- v1.3: pools (their own files), the summer re-allocation and the chain -------------------------------------- */
static swcc_loader g_loader;
void swcc_set_loader(swcc_loader f) { g_loader = f; }

static const char *jstr(JNode *o, const char *k) { JNode *v = json_get(o, k); return v && v->type == J_STR ? v->str : NULL; }

/* v1.4: a light pool's play-off to its cross play-off (a division's 'playoff', else the pool's 'up.playoff'): the
   library's shapes only (rules sr_lp_regional): {"direct": "table#N"} (that place goes straight, no rounds);
   "table#N-N+3" in two rounds (2 ties by_place, then the final); "table#N+1-N+2" in one tie by_place, then a round of
   one tie that 'joins' "table#N". One match a tie; 'home' and 'decider' on every round (the v1 "better_placed" decider:
   a draw sends the better placed through; all rounds or none). 'skip_if' (the stage, or a round's own) and
   'direct_if' {points_gap_ge}: the first entrant that far ahead of the next goes straight. */
static void gap_obj(C *c, JNode *o, const char *path)
{
    long g = 0;
    int_in(c, need(c, o, "points_gap_ge", J_NUM, path), "points_gap_ge", path, 1, 99, &g);
    unknown_keys(c, o, path);
}

static int deciders_ok(C *c, JNode *r, const char *rp, int *better)
{
    JNode *dec = need(c, r, "decider", J_ARR, rp), *d;
    if (!dec) return 0;
    if (!dec->n) { ERR(dec, rp, "decider: at least one entry"); return 0; }
    for (d = dec->child; d; d = d->next) {
        const int k = d->type == J_STR ? enum_of(d->str, DECIS, NE(DECIS)) : -1;
        if (k < 1) { char l[400]; list_names(l, sizeof l, DECIS + 1, NE(DECIS) - 1); ERR(d, rp, "decider: one of %s", l); }
        else if (!strcmp(DECIS[k], "better_placed")) *better = 1;
    }
    return 1;
}

/* a "table#N" / "table#N-M" reference (places from the 2nd): 1 and a / b, else 0 */
static int table_ref(const char *s, int *a, int *b)
{
    return !strncmp(s, "table#", 6) && positions(s + 6, a, b) && *a >= 2 && *a <= 127 && *b <= 127 && *b >= *a;
}

static void check_pool_playoff(C *c, JNode *po, const char *path, int size)
{
    JNode *rr, *r, *e, *o, *dr = json_get(po, "direct");
    int a = 0, b = 0, ri = 0, shape_ok = 0, nb = -1;
    long ties[2] = { 0, 0 };
    int bypl[2] = { 0, 0 }, join[2] = { 0, 0 };
    if (dr) {                                       /* the place goes straight */
        if (dr->type != J_STR || !table_ref(dr->str, &a, &b) || a != b)
            ERR(dr, path, "direct: one place, \"table#N\" (from the 2nd)");
        else if (size && a > size) ERR(dr, path, "direct \"%s\": the division has %d clubs", dr->str, size);
        if (json_get(po, "rounds")) ERR(po, path, "'direct' and 'rounds' together: a place goes straight or plays");
        if (json_get(po, "entrants")) ERR(po, path, "'direct' and 'entrants' together: 'direct' names the place");
        unknown_keys(c, po, path);
        return;
    }
    const char *ty = jstr(po, "type");
    if (!ty || strcmp(ty, "playoff")) ERR(po, path, "a pool play-off is a stage with \"type\": \"playoff\" (or {\"direct\": \"table#N\"})");
    e = need(c, po, "entrants", J_STR, path);
    if (e && !table_ref(e->str, &a, &b)) {
        ERR(e, path, "entrants \"%s\": \"table#N-M\" (places from the 2nd)", e->str);
        a = 0;
    }
    if (a && size && b > size) ERR(e, path, "entrants \"%s\": the division has %d clubs", e->str, size), a = 0;
    if ((o = opt(c, po, "skip_if", J_OBJ, path))) gap_obj(c, o, path);
    if ((o = opt(c, po, "direct_if", J_OBJ, path))) gap_obj(c, o, path);
    rr = need(c, po, "rounds", J_ARR, path);
    if (rr && !rr->n) ERR(rr, path, "no rounds: a place that goes straight is {\"direct\": \"table#N\"}");
    for (r = rr ? rr->child : NULL; r; r = r->next, ri++) {
        char rp[300];
        long legs = 1;
        int better = 0;
        JNode *jl;
        pathf(rp, sizeof rp, "%s.rounds[%d]", path, ri);
        if (r->type != J_OBJ) { ERR(r, rp, "a round is an object"); continue; }
        if (ri < 2) int_in(c, need(c, r, "ties", J_NUM, rp), "ties", rp, 1, 2, &ties[ri]);
        else json_get(r, "ties");
        int_in(c, need(c, r, "legs", J_NUM, rp), "legs", rp, 1, 1, &legs);
        enum_member(c, need(c, r, "home", J_STR, rp), "home", rp, HOMES, NE(HOMES));
        if (ri < 2) bypl[ri] = enum_member(c, need(c, r, "pairing", J_STR, rp), "pairing", rp, PAIRS, NE(PAIRS)) == 4;
        else json_get(r, "pairing");
        deciders_ok(c, r, rp, &better);
        if (nb < 0) nb = better;
        else if (nb != better) ERR(r, rp, "a light play-off has one decider rule for all its rounds (\"better_placed\" in all or none)");
        if ((o = opt(c, r, "skip_if", J_OBJ, rp))) gap_obj(c, o, rp);
        if ((jl = opt(c, r, "joins", J_ARR, rp))) {
            int ja = 0, jb = 0;
            JNode *it = jl->child;
            if (jl->n != 1 || it->type != J_STR || strncmp(it->str, "table#", 6) || !positions(it->str + 6, &ja, &jb) || ja != jb
                || ja != a - 1)
                ERR(jl, rp, "joins: one place, \"table#%d\" (the place above the entrants)", a > 1 ? a - 1 : 1);
            else if (ri < 2) join[ri] = 1;
        }
        unknown_keys(c, r, rp);
    }
    if (a && rr && rr->n) {
        const int n = b - a + 1;
        if (n == 4) shape_ok = ri == 2 && ties[0] == 2 && bypl[0] && !join[0] && ties[1] == 1 && !join[1];
        else if (n == 2) shape_ok = ri == 2 && ties[0] == 1 && bypl[0] && !join[0] && ties[1] == 1 && join[1];   /* the ladder */
        if (!shape_ok)
            ERR(po, path, "a light pool's play-off: {\"direct\": \"table#N\"}, \"table#N-N+3\" in two rounds (2 ties "
                "by_place, the final), or \"table#N+1-N+2\" (1 tie by_place) then one tie that joins \"table#N\"");
    }
    unknown_keys(c, po, path);
}

/* v1.4: a play-off across the pool's divisions ('up.cross'; Italy's national play-off): urns of divisions, one entrant
   each; round 1 pairing "urn" (drawn within each urn), round 2 pairing "urn_pairs" (the winners by the named urn
   pairs, each kind of pair as long as it can be used, in order: rules sr_lp_cross); 'up' = round 2's winners. */
static void check_cross(C *c, JNode *na, const char *const *dname, int nd)
{
    JNode *ur = need(c, na, "urns", J_ARR, "up.cross"), *m, *rr, *r;
    const char *uid[64];
    int nu = 0, cnt[64] = { 0 }, seen[256] = { 0 }, ri = 0;
    long ties[2] = { 0, 0 }, nup = 0;
    for (m = ur ? ur->child : NULL; m && nu < 64; m = m->next, nu++) {
        char up_[120];
        JNode *id, *dv, *d;
        pathf(up_, sizeof up_, "up.cross.urns[%d]", nu);
        uid[nu] = "";
        if (m->type != J_OBJ) { ERR(m, up_, "an urn is {\"id\", \"divisions\"}"); continue; }
        if ((id = need(c, m, "id", J_STR, up_))) {
            for (int j = 0; j < nu; j++) if (!strcmp(uid[j], id->str)) ERR(id, up_, "urn id \"%s\" used twice", id->str);
            uid[nu] = id->str;
        }
        if ((dv = need(c, m, "divisions", J_ARR, up_)))
            for (d = dv->child; d; d = d->next) {
                int j = 0;
                if (d->type == J_STR) while (j < nd && strcmp(dname[j], d->str)) j++;
                if (d->type != J_STR || j == nd) { ERR(d, up_, "no division \"%s\" in this pool", d->type == J_STR ? d->str : "?"); continue; }
                if (seen[j]++) ERR(d, up_, "division \"%s\" in two urns", d->str);
                cnt[nu]++;
            }
        if (cnt[nu] % 2) ERR(m, up_, "urn \"%s\": %d divisions, the pairing \"urn\" draws them in pairs", uid[nu], cnt[nu]);
        unknown_keys(c, m, up_);
    }
    for (int j = 0; j < nd; j++) if (!seen[j]) ERR(ur, "up.cross", "division \"%s\" is in no urn", dname[j]);
    rr = need(c, na, "rounds", J_ARR, "up.cross");
    if (rr && rr->n != 2) ERR(rr, "up.cross", "the cross play-off has 2 rounds: pairing \"urn\", then \"urn_pairs\"");
    for (r = rr ? rr->child : NULL; r && ri < 2; r = r->next, ri++) {
        char rp[120];
        long legs = 1;
        int better = 0, pr;
        JNode *ps;
        pathf(rp, sizeof rp, "up.cross.rounds[%d]", ri);
        if (r->type != J_OBJ) { ERR(r, rp, "a round is an object"); continue; }
        int_in(c, need(c, r, "ties", J_NUM, rp), "ties", rp, 1, 1024, &ties[ri]);
        int_in(c, need(c, r, "legs", J_NUM, rp), "legs", rp, 1, 2, &legs);
        enum_member(c, need(c, r, "home", J_STR, rp), "home", rp, HOMES, NE(HOMES));
        pr = enum_member(c, need(c, r, "pairing", J_STR, rp), "pairing", rp, PAIRS, NE(PAIRS));
        if (pr >= 0 && pr != 5 + ri) ERR(r, rp, "round %d of 'up.cross' has the pairing \"%s\"", ri + 1, PAIRS[5 + ri]);
        deciders_ok(c, r, rp, &better);
        ps = ri == 1 ? need(c, r, "urn_pairs", J_ARR, rp) : NULL;
        if (ri == 1 && ps) {                    /* the winners per urn used as the library uses them */
            int left[64], made = 0;
            JNode *pp;
            for (int u = 0; u < nu; u++) left[u] = cnt[u] / 2;
            for (pp = ps->child; pp; pp = pp->next) {
                JNode *x = json_at(pp, 0), *y = json_at(pp, 1);
                int u = -1, v = -1;
                if (pp->type != J_ARR || pp->n != 2 || x->type != J_STR || y->type != J_STR) { ERR(pp, rp, "urn_pairs: a pair is [\"urn\", \"urn\"]"); continue; }
                for (int j = 0; j < nu; j++) { if (!strcmp(uid[j], x->str)) u = j; if (!strcmp(uid[j], y->str)) v = j; }
                if (u < 0 || v < 0) { ERR(pp, rp, "urn_pairs: no urn \"%s\"", u < 0 ? x->str : y->str); continue; }
                while (u == v ? left[u] >= 2 : left[u] >= 1 && left[v] >= 1) { left[u]--; left[v]--; made++; }
            }
            for (int u = 0; u < nu; u++)
                if (left[u]) ERR(ps, rp, "urn_pairs: %d round-1 winners of urn \"%s\" left without a pair", left[u], uid[u]);
            if (ties[1] && made != ties[1]) ERR(ps, rp, "urn_pairs make %d ties, 'ties' says %ld", made, ties[1]);
        }
        unknown_keys(c, r, rp);
    }
    if (ties[0] && ties[0] * 2 != nd) ERR(rr, "up.cross", "round 1: %ld ties x 2 != %d divisions (one entrant each)", ties[0], nd);
    if (ties[0] && ties[1] && ties[1] * 2 != ties[0]) ERR(rr, "up.cross", "round 2: %ld ties x 2 != round 1's %ld winners", ties[1], ties[0]);
    if (int_in(c, need(c, na, "up", J_NUM, "up.cross"), "up", "up.cross", 1, 128, &nup) && ties[1] && nup != ties[1])
        ERR(json_get(na, "up"), "up.cross", "up %ld: the last round's winners are %ld", nup, ties[1]);
    unknown_keys(c, na, "up.cross");
}

/* one pool file (root p) of the world's pool `pid`; ids: the pool clubs' ids seen so far (all pools) */
static void check_pool_file(C *c, JNode *p, const char *pid, const char ***ids, int *nids)
{
    JNode *v, *divs, *cls, *up, *m, *dpo[256] = { 0 };
    int pmin = 3, nd = 0, i, j, cnt[256] = { 0 }, size[256] = { 0 };
    const char *dname[256];
    char path[160];
    if (p->type != J_OBJ) { ERR(p, "", "a pool file is a JSON object"); return; }
    if ((v = need(c, p, "schema", J_STR, ""))) {
        if (!strcmp(v->str, "swc/1.4")) pmin = 4;
        else if (strcmp(v->str, "swc/1.3")) ERR(v, "", "a pool file says \"schema\": \"swc/1.3\" or \"swc/1.4\"");
    }
    if ((v = need(c, p, "id", J_STR, "")) && strcmp(v->str, pid)) ERR(v, "", "pool file id \"%s\", the world names it \"%s\"", v->str, pid);
    if ((v = need(c, p, "country", J_STR, "")) && (!c->countries || !json_get(c->countries, v->str)))
        ERR(v, "", "country \"%s\" is not in the world's 'countries'", v->str);
    if ((divs = need(c, p, "divisions", J_ARR, ""))) {
        if (!divs->n) ERR(divs, "", "divisions: at least one");
        for (m = divs->child; m; m = m->next, nd++) {
            JNode *d, *nm;
            long sz = 0;
            pathf(path, sizeof path, "divisions[%d]", nd);
            if (m->type != J_OBJ || nd >= 256) { ERR(m, path, "a division is an object"); continue; }
            dname[nd] = "";
            if ((d = need(c, m, "id", J_STR, path))) {
                if (!id_ok(d->str, NULL)) ERR(d, path, "division id \"%s\": lower-case words joined by '.', '-' or '_'", d->str);
                for (j = 0; j < nd; j++) if (!strcmp(dname[j], d->str)) ERR(d, path, "division id \"%s\" used twice", d->str);
                dname[nd] = d->str;
            }
            if ((nm = need(c, m, "names", J_OBJ, path))) {
                for (j = 0; j < c->nlangs; j++) {
                    JNode *l = json_get(nm, c->langs[j]);
                    if (!l || l->type != J_STR) ERR(nm, path, "names: missing language '%s'", c->langs[j]);
                    else text_ok(c, l, path, "division name", l->str);
                }
                unknown_keys(c, nm, path);
            }
            if (int_in(c, need(c, m, "size", J_NUM, path), "size", path, 2, c->max_teams, &sz)) size[nd] = (int)sz;
            if ((dpo[nd] = opt(c, m, "playoff", J_OBJ, path))) {    /* v1.4: its own play-off (or 'direct') */
                char pp[200];
                if (pmin < 4) ERR(dpo[nd], path, "a division's 'playoff' is swc/1.4: this pool file says \"swc/1.3\"");
                pathf(pp, sizeof pp, "%s.playoff", path);
                check_pool_playoff(c, dpo[nd], pp, size[nd]);
            }
            unknown_keys(c, m, path);
        }
    }
    if ((cls = need(c, p, "clubs", J_ARR, ""))) {
        for (i = 0, m = cls->child; m; m = m->next, i++) {
            JNode *id, *nm, *dv, *sg, *rk, *ar, *ho;
            pathf(path, sizeof path, "clubs[%d]", i);
            if (m->type != J_OBJ) { ERR(m, path, "a club is an object"); continue; }
            if ((id = need(c, m, "id", J_STR, path))) {
                if (!id_ok(id->str, "club:")) ERR(id, path, "club id \"%s\" must be 'club:' + lower-case words joined by '.', '-' or '_'", id->str);
                if (find_club(c, id->str) >= 0) ERR(id, path, "club id \"%s\" is a club of the world too", id->str);
                for (j = 0; j < *nids; j++) if (!strcmp((*ids)[j], id->str)) { ERR(id, path, "club id \"%s\" used twice in the pools", id->str); break; }
                *ids = realloc((void *)*ids, (size_t)(*nids + 1) * sizeof **ids);
                {                                           /* (a copy, kept past this file's tree: unique across the pools) */
                    const size_t ln = strlen(id->str) + 1;
                    char *cp2 = malloc(ln);
                    memcpy(cp2, id->str, ln);
                    (*ids)[(*nids)++] = cp2;
                }
            }
            if ((nm = need(c, m, "name", J_STR, path))) {
                const char *hn = text_ok(c, nm, path, "club name", nm->str);
                if (hn && (int)strlen(hn) > c->club_name_max)
                    ERR(nm, path, "club name \"%s\" is %zu characters, the host holds %d", nm->str, strlen(hn), c->club_name_max);
            }
            if ((dv = need(c, m, "division", J_STR, path))) {
                for (j = 0; j < nd && strcmp(dname[j], dv->str); j++) ;
                if (j == nd) ERR(dv, path, "no division \"%s\" in this pool", dv->str); else cnt[j]++;
            }
            if ((sg = need(c, m, "strength", J_NUM, path))) {     /* hundredths, 0.00..2.50 */
                const double h = sg->num * 100;
                const long r = (long)(h + 0.5);
                if (sg->num < 0 || sg->num > 2.5 || h - r > 1e-6 || r - h > 1e-6) ERR(sg, path, "strength %g: 0.00..2.50, in hundredths", sg->num);
            }
            if ((rk = opt(c, m, "rank", J_NUM, path))) { long r = 0; int_in(c, rk, "rank", path, 0, 254, &r); }
            ar = opt(c, m, "area", J_STR, path);
            (void)ar;
            if ((ho = opt(c, m, "host", J_OBJ, path))) {
                long f = 0, o = 0;
                int_in(c, need(c, ho, "file", J_NUM, path), "file", path, 0, 65535, &f);
                if (int_in(c, need(c, ho, "ordinal", J_NUM, path), "ordinal", path, 0, 65535, &o) && o >= c->clubs_per_file)
                    ERR(ho, path, "ordinal %ld: a host file holds %d clubs (0..%d)", o, c->clubs_per_file, c->clubs_per_file - 1);
                unknown_keys(c, ho, path);
            }
            unknown_keys(c, m, path);
        }
        for (j = 0; j < nd; j++)
            if (size[j] && cnt[j] != size[j]) ERR(cls, "clubs", "division \"%s\" has %d clubs, its size is %d", dname[j], cnt[j], size[j]);
    }
    if ((up = need(c, p, "up", J_OBJ, ""))) {
        long ch = 0;
        JNode *po = opt(c, up, "playoff", J_OBJ, "up"), *na = opt(c, up, "cross", J_OBJ, "up"), *ex = opt(c, up, "extra", J_OBJ, "up");
        int_in(c, need(c, up, "champion", J_NUM, "up"), "champion", "up", 0, 8, &ch);
        if (na && pmin < 4) ERR(na, "up", "'up.cross' is swc/1.4: this pool file says \"swc/1.3\"");
        if (ex && pmin < 4) ERR(ex, "up", "'up.extra' is swc/1.4: this pool file says \"swc/1.3\"");
        if (po && !na) {                                /* a playoff stage's shape: the engine reads it (S3) */
            const char *ty = jstr(po, "type");
            JNode *rr = json_get(po, "rounds");
            if (!ty || strcmp(ty, "playoff")) ERR(po, "up.playoff", "up.playoff is a stage with \"type\": \"playoff\"");
            if (!rr || rr->type != J_ARR || !rr->n) ERR(po, "up.playoff", "up.playoff needs its 'rounds'");
        }
        if (!na)
            for (j = 0; j < nd; j++) if (dpo[j]) ERR(dpo[j], dname[j], "a division's own 'playoff' goes with 'up.cross'");
        if (na) {                                       /* v1.4: a play-off across the divisions (Italy's national one) */
            int small = 0;
            for (j = 0; j < nd; j++) if (size[j] && (!small || size[j] < small)) small = size[j];
            if (po) check_pool_playoff(c, po, "up.playoff", small);
            for (j = 0; j < nd; j++)
                if (!dpo[j] && !po) ERR(na, "up.cross", "division \"%s\": no 'playoff' of its own and no 'up.playoff'", dname[j]);
            if (ch != 1) ERR(up, "up", "with 'cross': champion 1 (each division's champion up, its play-off to the cross one)");
            check_cross(c, na, dname, nd);
        }
        if (ex) {                                       /* v1.4: places by a declared rule (the cup's: italy42 5d) */
            long k = 0;
            JNode *ru = need(c, ex, "rule", J_STR, "up.extra"), *ov = opt(c, ex, "override", J_ARR, "up.extra"), *o;
            if (ru && strcmp(ru->str, "strongest_not_up")) ERR(ru, "up.extra", "rule \"%s\": the only rule is \"strongest_not_up\"", ru->str);
            int_in(c, need(c, ex, "count", J_NUM, "up.extra"), "count", "up.extra", 1, 8, &k);
            for (o = ov ? ov->child : NULL; o; o = o->next) {
                JNode *q, *o2;
                int found = 0;
                if (o->type != J_STR) { ERR(o, "up.extra", "override: a list of club ids"); continue; }
                for (q = cls ? cls->child : NULL; q; q = q->next) { const char *qi = jstr(q, "id"); found |= qi && !strcmp(qi, o->str); }
                if (!found) ERR(o, "up.extra", "override \"%s\" is not a club of this pool", o->str);
                for (o2 = ov->child; o2 != o; o2 = o2->next) if (o2->type == J_STR && !strcmp(o2->str, o->str)) ERR(o, "up.extra", "override \"%s\" listed twice", o->str);
            }
            if (!na) ERR(ex, "up.extra", "'extra' goes with 'cross'");
            unknown_keys(c, ex, "up.extra");
        }
        unknown_keys(c, up, "up");
    }
    if ((v = opt(c, p, "feeds", J_ARR, ""))) {
        for (m = v->child; m; m = m->next) {
            int k = m->type == J_STR ? find_comp(c, m->str) : -1;
            const char *pp;
            if (k < 0) { ERR(m, "feeds", "no competition \"%s\" in the world", m->type == J_STR ? m->str : "?"); continue; }
            if (!jstr(c->comps[k], "refill")) ERR(m, "feeds", "\"%s\" is fed by a pool: it needs \"refill\": \"pool\"", m->str);
            if ((pp = jstr(c->comps[k], "pool")) && strcmp(pp, pid)) ERR(m, "feeds", "\"%s\" names pool \"%s\"", m->str, pp);
        }
    }
    unknown_keys(c, p, "");
}

static void check_pools(C *c)
{
    JNode *pl = json_get(c->root, "pools"), *m;
    const char **ids = NULL, *pid[64];
    int np = 0, nids = 0, i, j;
    if (pl) {
        need_minor(c, pl, "pools", 3, "'pools'");
        if (pl->type != J_ARR) { ERR(pl, "pools", "'pools' is a list of {\"id\", \"file\"}"); pl = NULL; }
    }
    for (m = pl ? pl->child : NULL; m && np < 64; m = m->next) {
        JNode *id, *f;
        char path[64];
        pathf(path, sizeof path, "pools[%d]", np);
        if (m->type != J_OBJ) { ERR(m, path, "a pool is {\"id\", \"file\"}"); continue; }
        id = need(c, m, "id", J_STR, path);
        f = need(c, m, "file", J_STR, path);
        unknown_keys(c, m, path);
        if (!id) continue;
        if (!id_ok(id->str, NULL)) ERR(id, path, "pool id \"%s\": lower-case words joined by '.', '-' or '_'", id->str);
        for (j = 0; j < np; j++) if (!strcmp(pid[j], id->str)) ERR(id, path, "pool id \"%s\" used twice", id->str);
        pid[np++] = id->str;
        if (f && !g_loader) WARN(f, path, "pool file \"%s\" not checked: no loader", f->str);
        if (f && g_loader) {
            size_t n = 0;
            char *text = g_loader(f->str, c->file, &n), err[256];
            const char *spec_file = c->file;
            JNode *root;
            if (!text) { ERR(f, path, "pool file \"%s\" cannot be read", f->str); continue; }
            root = json_parse(text, n, err, sizeof err);
            c->file = f->str;
            if (!root) ERR(NULL, "", "(syntax): %s", err);
            else check_pool_file(c, root, id->str, &ids, &nids);
            c->file = spec_file;
            if (root) json_free(root);
            free(text);
        }
    }
    for (i = 0; i < nids; i++) free((void *)ids[i]);
    free((void *)ids);
    for (i = 0; i < c->ncomps; i++) {
        const char *pp = jstr(c->comps[i], "pool");
        JNode *at = json_get(c->comps[i], "pool");
        if (!pp) continue;
        for (j = 0; j < np && strcmp(pid[j], pp); j++) ;
        if (j == np) ERR(at, jstr(c->comps[i], "id") ? jstr(c->comps[i], "id") : "?", "pool \"%s\" is not in the world's 'pools'", pp);
    }
    for (i = 0; i < c->ncomps; i++) {                   /* realloc / chain: every league of a level agrees */
        JNode *a = c->comps[i], *la = json_get(a, "level");
        const char *ca = jstr(a, "country"), *ra = jstr(a, "realloc");
        JNode *cha = json_get(a, "chain");
        for (j = 0; j < i; j++) {
            JNode *b = c->comps[j], *lb = json_get(b, "level"), *chb = json_get(b, "chain");
            const char *cb = jstr(b, "country"), *rb = jstr(b, "realloc");
            if (!la || !lb || la->type != J_NUM || lb->type != J_NUM || la->num != lb->num || !ca || !cb || strcmp(ca, cb)) continue;
            if ((ra == NULL) != (rb == NULL) || (ra && strcmp(ra, rb)))
                ERR(a, jstr(a, "id") ? jstr(a, "id") : "?", "realloc: \"%s\" and \"%s\" are one level of %s: every league of a level says the same", jstr(a, "id"), jstr(b, "id"), ca);
            if ((cha && cha->type == J_BOOL && cha->b) != (chb && chb->type == J_BOOL && chb->b))
                ERR(a, jstr(a, "id") ? jstr(a, "id") : "?", "chain: \"%s\" and \"%s\" are one level of %s: every league of a level says the same", jstr(a, "id"), jstr(b, "id"), ca);
        }
    }
}

/* v1.3: a second team (a club with a 'parent') never in its parent's league, never above its country's
   second_teams.highest level */
static void check_second_teams(C *c)
{
    int i;
    for (i = 0; i < c->ncomps; i++) {
        JNode *cp = c->comps[i], *kind = json_get(cp, "kind"), *lv = json_get(cp, "level"),
              *list = json_get(json_get(cp, "participants"), "clubs"), *m, *o;
        char path[128];
        if (!kind || kind->type != J_STR || strcmp(kind->str, "league") || !list || list->type != J_ARR) continue;
        pathf(path, sizeof path, "competitions[%d].participants", i);
        for (m = list->child; m; m = m->next) {
            int ix = m->type == J_STR ? find_club(c, m->str) : -1;
            JNode *p, *cc, *hi;
            if (ix < 0 || !(p = json_get(c->clubs[ix], "parent")) || p->type != J_STR) continue;
            for (o = list->child; o; o = o->next)
                if (o->type == J_STR && !strcmp(o->str, p->str))
                    ERR(m, path, "\"%s\" plays in the league of its parent \"%s\"", m->str, p->str);
            cc = json_get(c->clubs[ix], "country");
            hi = cc && cc->type == J_STR ? json_get(json_get(json_get(c->countries, cc->str), "second_teams"), "highest") : NULL;
            if (hi && hi->type == J_NUM && lv && lv->type == J_NUM && lv->num < hi->num)
                ERR(m, path, "second team \"%s\" at level %d: %s's second teams play level %d or lower", m->str,
                    (int)lv->num, cc->str, (int)hi->num);
        }
    }
}

/* v1.4: second_teams.lowest is a level some league of that country plays, with a way out below it -- a league of that
   level refilled from a pool ("refill": "pool": the pool sends one more up for each second team that leaves), or a
   league of that country below it (where the second team goes) */
static void check_second_floor(C *c)
{
    JNode *m;
    if (!c->countries || c->countries->type != J_OBJ) return;
    for (m = c->countries->child; m; m = m->next) {
        JNode *lo = json_get(json_get(m, "second_teams"), "lowest");
        int i, found = 0, out = 0;
        char path[128];
        if (!lo || lo->type != J_NUM) continue;
        for (i = 0; i < c->ncomps; i++) {
            JNode *cp = c->comps[i], *kind = json_get(cp, "kind"), *lv = json_get(cp, "level"), *cc = json_get(cp, "country");
            if (!kind || kind->type != J_STR || strcmp(kind->str, "league") || !lv || lv->type != J_NUM || !cc ||
                cc->type != J_STR || strcmp(cc->str, m->key))
                continue;
            if (lv->num == lo->num) { found = 1; out |= json_get(cp, "refill") != NULL; }
            if (lv->num > lo->num) out = 1;
        }
        pathf(path, sizeof path, "countries.%s.second_teams", m->key);
        if (!found) ERR(lo, path, "second_teams.lowest %d: no league of %s plays level %d", (int)lo->num, m->key, (int)lo->num);
        else if (!out)
            ERR(lo, path, "second_teams.lowest needs a pool below level %d (where the B team goes): a league of that level "
                "with \"refill\": \"pool\", or a league of %s below it", (int)lo->num, m->key);
    }
}

/* v1.4: countries.<c>.files -- every league of that country in exactly one file, each a league of that country; a
   file's rows (its leagues' clubs + its pool rows) <= 256; file numbers unique, no file shared with another country;
   pool rows in one file at most (the engine's sr_reg_build) */
static void check_files(C *c)
{
    JNode *m, *o;
    if (!c->countries || c->countries->type != J_OBJ) return;
    for (m = c->countries->child; m; m = m->next) {
        JNode *fl = json_get(m, "files"), *e, *d;
        int i, fi, pools = -1;
        char path[96];
        if (!fl || fl->type != J_ARR) continue;
        for (e = fl->child, fi = 0; e; e = e->next, fi++) {
            JNode *fn = json_get(e, "file"), *dv = json_get(e, "divisions"), *pr = json_get(e, "pool_rows"), *e2;
            int rows = pr && pr->type == J_NUM ? (int)pr->num : 0, fj;
            pathf(path, sizeof path, "countries.%s.files[%d]", m->key, fi);
            if (!fn || fn->type != J_NUM || !dv || dv->type != J_ARR) continue;
            for (e2 = fl->child, fj = 0; e2 != e; e2 = e2->next, fj++) {
                JNode *f2 = json_get(e2, "file");
                if (f2 && f2->type == J_NUM && f2->num == fn->num) ERR(fn, path, "file %d is in 'files' twice", (int)fn->num);
            }
            for (o = c->countries->child; o; o = o->next) {
                JNode *f3 = o == m ? NULL : json_get(o, "files"), *e3;
                for (e3 = f3 && f3->type == J_ARR ? f3->child : NULL; e3; e3 = e3->next) {
                    JNode *n3 = json_get(e3, "file");
                    if (n3 && n3->type == J_NUM && n3->num == fn->num)
                        ERR(fn, path, "file %d is %s's too: a team file belongs to one country", (int)fn->num, o->key);
                }
            }
            if (rows) {
                if (pools >= 0) ERR(pr, path, "pool rows in two files (%d, %d): the pool rows live in one file", pools, (int)fn->num);
                else pools = (int)fn->num;
            }
            for (d = dv->child; d; d = d->next) {
                int k = d->type == J_STR ? find_comp(c, d->str) : -1;
                JNode *kind = k >= 0 ? json_get(c->comps[k], "kind") : NULL, *cc = k >= 0 ? json_get(c->comps[k], "country") : NULL,
                      *cnt = k >= 0 ? json_get(json_get(c->comps[k], "participants"), "count") : NULL;
                if (k < 0 || !kind || kind->type != J_STR || strcmp(kind->str, "league") || !cc || cc->type != J_STR ||
                    strcmp(cc->str, m->key)) {
                    ERR(d, path, "division '%s' is not a league of %s", d->type == J_STR ? d->str : "?", m->key);
                    continue;
                }
                rows += cnt && cnt->type == J_NUM ? (int)cnt->num : 0;
                for (e2 = fl->child; e2 != e; e2 = e2->next) {
                    JNode *d2v = json_get(e2, "divisions"), *d2, *f2 = json_get(e2, "file");
                    for (d2 = d2v && d2v->type == J_ARR ? d2v->child : NULL; d2; d2 = d2->next)
                        if (d2->type == J_STR && !strcmp(d2->str, d->str))
                            ERR(d, path, "division '%s' is in two files (%d, %d)", d->str,
                                f2 && f2->type == J_NUM ? (int)f2->num : 0, (int)fn->num);
                }
                for (JNode *d2 = dv->child; d2 != d; d2 = d2->next)
                    if (d2->type == J_STR && !strcmp(d2->str, d->str)) ERR(d, path, "division '%s' twice in file %d", d->str, (int)fn->num);
            }
            if (rows > 256) ERR(fn, path, "file %d holds %d rows (its divisions' clubs + pool rows; at most 256)", (int)fn->num, rows);
        }
        for (i = 0; i < c->ncomps; i++) {          /* every league of the country in a file */
            JNode *cp = c->comps[i], *kind = json_get(cp, "kind"), *cc = json_get(cp, "country"), *id = json_get(cp, "id");
            int found = 0;
            if (!kind || kind->type != J_STR || strcmp(kind->str, "league") || !cc || cc->type != J_STR ||
                strcmp(cc->str, m->key) || !id || id->type != J_STR)
                continue;
            for (e = fl->child; e && !found; e = e->next) {
                JNode *dv = json_get(e, "divisions");
                for (d = dv && dv->type == J_ARR ? dv->child : NULL; d && !found; d = d->next)
                    found = d->type == J_STR && !strcmp(d->str, id->str);
            }
            pathf(path, sizeof path, "countries.%s.files", m->key);
            if (!found) ERR(fl, path, "league '%s' of %s is in no file", id->str, m->key);
        }
    }
}

static void check_links(C *c)
{
    /* every league keeps its size: the clubs that leave it (promoted, relegated, out of the world) = the clubs that
       come in; a league with "refill": "pool" is refilled from outside, so only its out-side is counted. A play-off
       whose participants come "from": "link" with a "source" counts its moves for its source leagues. */
    const SwcOutcome *o = (const SwcOutcome *)c->outc.p;
    const SwcComp *cm = (const SwcComp *)c->comp.p;
    const SwcStage *sg = (const SwcStage *)c->stag.p;
    const StageInfo *si = (const StageInfo *)c->stinfo;
    /* v1.2: parallel leagues (the targets of one 'to' list; the league sources of one linked competition, when they
       share a level) are one level: the sizes are counted for the level, the clubs spread by area. */
    const uint16_t *src = (const uint16_t *)c->srce.p, *tl = (const uint16_t *)c->tols.p;
    int no = (int)(c->outc.n / sizeof *o), nsrc = (int)(c->srce.n / 4), ntl = (int)(c->tols.n / 4), i, j, k;
    int *out = calloc((size_t)c->ncomps + 1, sizeof *out), *in = calloc((size_t)c->ncomps + 1, sizeof *in);
    int *par = calloc((size_t)c->ncomps + 1, sizeof *par), *one = calloc((size_t)c->ncomps + 1, sizeof *one);
    SwcSpec sv = { .strs = (const char *)c->strs.p, .nstrs = (uint32_t)c->strs.n };
    for (i = 0; i < c->ncomps; i++) { par[i] = i; one[i] = -1; }
    for (k = 0; k < ntl; k++) uf_join(par, o[tl[2 * k]].target, tl[2 * k + 1]);
    for (k = 0; k < nsrc; k++) {                    /* one[comp] = its first league source, when they share a level */
        int a = src[2 * k], b = src[2 * k + 1];
        if (cm[b].kind != SWC_K_LEAGUE || one[a] == -2) continue;
        if (one[a] < 0) one[a] = b;
        else if (cm[one[a]].level == cm[b].level) uf_join(par, one[a], b);
        else one[a] = -2;
    }
    for (j = 0; j < no; j++) {
        const StageInfo *x = &si[o[j].stage];
        int ko = x->type == SWC_ST_KNOCKOUT || x->type == SWC_ST_PLAYOFF, n, from = sg[o[j].stage].comp, any = 0;
        if (o[j].tag != SWC_TAG_PROMOTED && o[j].tag != SWC_TAG_RELEGATED) continue;
        if (ko) n = ((o[j].from <= 1 && o[j].to >= 1) + (o[j].from <= 2 && o[j].to >= 2)) * x->last_ties * (x->per_group ? x->groups : 1);
        else n = (pos_res(o[j].to, x->gmin) - pos_res(o[j].from, x->gmin) + 1) * x->groups;
        if (one[from] >= 0) { out[one[from]] += n; any = 1; }
        else for (k = 0; k < nsrc; k++)
            if (src[2 * k] == from && cm[src[2 * k + 1]].kind == SWC_K_LEAGUE) { out[src[2 * k + 1]] += n; any = 1; }
        if (!any) out[from] += n;
        if (o[j].target < c->ncomps) in[o[j].target] += n;
    }
    for (i = 0; i < c->ncomps; i++) {               /* sums per level: at its first league */
        int r = uf_find(par, i);
        if (r != i) { in[r] += in[i]; out[r] += out[i]; }
    }
    for (i = 0; i < c->ncomps; i++) {
        int n = 1, refill = 1, l;
        char lst[300];
        size_t at = 0;
        if (uf_find(par, i) != i || cm[i].kind != SWC_K_LEAGUE || (!in[i] && !out[i])) continue;
        lst[0] = 0;
        for (l = 0; l < c->ncomps; l++)
            if (uf_find(par, l) == i) {
                refill &= cm[l].flags & 1;
                if (l != i) n++;
                if (at < sizeof lst - 40) at += (size_t)snprintf(lst + at, sizeof lst - at, "%s%s", at ? ", " : "", swc_str(&sv, cm[l].id));
            }
        if (refill ? in[i] <= out[i] : in[i] == out[i]) continue;
        if (n == 1)
            ERR(c->comps[i], swc_str(&sv, cm[i].id), "the league loses %d clubs and gains %d: its size would change%s", out[i], in[i],
                refill ? " (more clubs come in than leave, even with its refill)" : "");
        else
            ERR(c->comps[i], swc_str(&sv, cm[i].id), "the %d parallel leagues of level %d (%s) lose %d clubs and gain %d: their sizes would change%s",
                n, cm[i].level, lst, out[i], in[i], refill ? " (more clubs come in than leave, even with their refill)" : "");
    }
    free(out);
    free(in);
    free(par);
    free(one);
}

/* ---- writer ------------------------------------------------------------------------------------------------------ */
static void pad4(Buf *b) { while (b->n & 3) bput(b, NULL, 1); }

/* Encodes n native records of layout l at src into big-endian bytes at dst (docs/format.md). */
static void encode(uint8_t *dst, const uint8_t *src, size_t n, const char *l)
{
    uint32_t rec = swc_layout_size(l);
    size_t i;
    for (i = 0; i < n; i++) {
        const char *f;
        uint32_t off = 0;
        for (f = l; *f; f++) {
            const uint8_t *q = src + i * rec + off;
            uint8_t *d = dst + i * rec + off;
            if (*f == '1') *d = *q;
            else if (*f == '2') { uint16_t v; memcpy(&v, q, 2); SWC_BE_WRITE_U16(d, v); }
            else { uint32_t v; memcpy(&v, q, 4); SWC_BE_WRITE_U32(d, v); }
            off += (uint32_t)(*f - '0');
        }
    }
}

static uint8_t *write_bin(C *c, int world, size_t *len)
{
    struct { uint32_t tag; Buf *b; } sec[] = {
        { SWC_S_STRS, &c->strs }, { SWC_S_NAME, &c->names }, { SWC_S_CLUB, &c->club }, { SWC_S_COMP, &c->comp },
        { SWC_S_STAG, &c->stag }, { SWC_S_ENTR, &c->entr }, { SWC_S_TIEB, &c->tieb }, { SWC_S_DECI, &c->deci },
        { SWC_S_ROUN, &c->roun }, { SWC_S_OUTC, &c->outc }, { SWC_S_REG, &c->reg }, { SWC_S_META, &c->meta },
        { SWC_S_FIXT, &c->fixt }, { SWC_S_GRPS, &c->grps }, { SWC_S_SRCE, &c->srce }, { SWC_S_TOLS, &c->tols },
        { SWC_S_PRNT, &c->prnt }, { SWC_S_SECT, &c->sect }, { SWC_S_SECF, &c->secf } };
    int ns = (int)(sizeof sec / sizeof sec[0]), i;
    while (ns > 13 && !sec[ns - 1].b->n) ns--;           /* sections added after the first release: only when used */
    Buf out = { 0 };
    uint8_t *h;
    size_t body;
    bput(&out, NULL, 32 + (size_t)ns * 12);
    body = out.n;
    for (i = 0; i < ns; i++) {
        const char *l = swc_layout(sec[i].tag);
        uint8_t *e;
        size_t at;
        pad4(&out);
        at = out.n;
        bput(&out, NULL, sec[i].b->n);
        if (sec[i].b->n) {
            if (l) encode(out.p + at, sec[i].b->p, sec[i].b->n / swc_layout_size(l), l);
            else memcpy(out.p + at, sec[i].b->p, sec[i].b->n);
        }
        e = out.p + 32 + (size_t)i * 12;
        SWC_BE_WRITE_U32(e, sec[i].tag);
        SWC_BE_WRITE_U32(e + 4, at);
        SWC_BE_WRITE_U32(e + 8, sec[i].b->n);
    }
    pad4(&out);
    h = out.p;
    SWC_BE_WRITE_U32(h, SWC_MAGIC);
    SWC_BE_WRITE_U16(h + 4, SWC_FORMAT);
    SWC_BE_WRITE_U16(h + 6, SWC_VERSION);
    SWC_BE_WRITE_U32(h + 8, 0);
    SWC_BE_WRITE_U16(h + 12, world);
    SWC_BE_WRITE_U16(h + 14, ns);
    SWC_BE_WRITE_U32(h + 20, c->arena);
    SWC_BE_WRITE_U32(h + 24, out.n);
    SWC_BE_WRITE_U32(h + 28, 0);
    SWC_BE_WRITE_U32(h + 16, swc_fnv1a(out.p + body, (uint32_t)(out.n - body)));
    *len = out.n;
    return out.p;
}

/* ---- entry point ------------------------------------------------------------------------------------------------- */
int swcc_compile(const char *spec, size_t len, const char *name, const char *profile, size_t plen,
                 unsigned char **bin, size_t *blen, char **diag_out)
{
    C cc, *c = &cc;
    char err[256];
    JNode *proot, *v, *m;
    StageInfo *st = NULL;
    int nst = 0, i;
    long world = 0;
    memset(c, 0, sizeof *c);
    c->file = name ? name : "spec";
    if (bin) *bin = NULL;
    if (blen) *blen = 0;
    proot = json_parse(profile, plen, err, sizeof err);
    if (!proot) {
        const char *f = c->file;
        c->file = "profile";
        ERR(NULL, "profile", "%s", err);
        c->file = f;
    } else load_profile(c, proot);
    c->root = json_parse(spec, len, err, sizeof err);
    if (!c->root) {
        int l = 1, col = 1;
        char tmp[300];
        sscanf(err, "%d:%d:", &l, &col);
        snprintf(tmp, sizeof tmp, "%s:%d:%d: error: (syntax): %s\n", c->file, l, col, strchr(strchr(err, ':') + 1, ':') + 2);
        bput(&c->diag, tmp, strlen(tmp));
        c->errors++;
    }
    if (c->root && c->errors == 0) {
        if (c->root->type != J_OBJ) ERR(c->root, "", "the spec is a JSON object");
        else {
            c->minor = SWC_MINOR;
            c->schema = "swc/1.4";
            if ((v = need(c, c->root, "schema", J_STR, ""))) {
                c->schema = v->str;
                if (!strcmp(v->str, "swc/1") || !strcmp(v->str, "swc/1.1")) c->minor = 1;
                else if (!strcmp(v->str, "swc/1.2")) c->minor = 2;
                else if (!strcmp(v->str, "swc/1.3")) c->minor = 3;
                else if (!strcmp(v->str, "swc/1.4")) c->minor = 4;
                else
                    ERR(v, "", "schema \"%s\": this compiler reads \"swc/1\" (= 1.1, the M1 schema), \"swc/1.1\", \"swc/1.2\", \"swc/1.3\" and \"swc/1.4\"", v->str);
            }
            if ((v = need(c, c->root, "id", J_STR, "")) && !id_ok(v->str, NULL)) ERR(v, "", "world id \"%s\": lower-case words joined by '.', '-' or '_'", v->str);
            int_in(c, opt(c, c->root, "host_world", J_NUM, ""), "host_world", "", 0, 65535, &world);
            {
                JNode *sq = opt(c, c->root, "squad", J_NUM, "");    /* v1.3: the host world's squad size */
                if (sq) {
                    need_minor(c, sq, "squad", 3, "'squad'");
                    if (!sq->is_int || (sq->num != 16 && sq->num != 20)) ERR(sq, "squad", "squad must be 16 or 20 (players per club)");
                }
            }
            {
                SwcMeta me;
                JNode *ver = opt(c, c->root, "version", J_STR, "");
                me.id = str_add(c, v && v->type == J_STR ? v->str : "");
                me.version = str_add(c, ver ? ver->str : "0");
                bput(&c->meta, &me, sizeof me);
            }
            if ((v = opt(c, c->root, "languages", J_ARR, ""))) {
                for (i = 0; i < c->nlangs; i++) {
                    int found = 0;
                    for (m = v->child; m; m = m->next) if (m->type == J_STR && !strcmp(m->str, c->langs[i])) found = 1;
                    if (!found) ERR(v, "languages", "the host needs language '%s'", c->langs[i]);
                }
            }
            c->countries = need(c, c->root, "countries", J_OBJ, "");
            for (m = c->countries ? c->countries->child : NULL; m; m = m->next) {
                char cpth[64];
                JNode *cn;
                pathf(cpth, sizeof cpth, "countries.%s", m->key);
                if (strlen(m->key) != 3 || m->key[0] < 'A' || m->key[0] > 'Z') ERR(m, cpth, "a country code is 3 upper-case letters (FIFA)");
                if (m->type != J_OBJ) { ERR(m, cpth, "a country is an object"); continue; }
                if ((cn = need(c, m, "names", J_OBJ, cpth))) {
                    for (i = 0; i < c->nlangs; i++) {
                        JNode *l = json_get(cn, c->langs[i]);
                        if (!l || l->type != J_STR) ERR(cn, cpth, "names: missing language '%s'", c->langs[i]);
                        else text_ok(c, l, cpth, "country name", l->str);
                    }
                    unknown_keys(c, cn, cpth);
                }
                if ((cn = opt(c, m, "second_teams", J_OBJ, cpth))) {      /* v1.3 */
                    long hi = 0;
                    need_minor(c, cn, cpth, 3, "'second_teams'");
                    if (int_in(c, need(c, cn, "highest", J_NUM, cpth), "highest", cpth, 1, 32, &hi)) {
                        uint16_t st2[2] = { (uint16_t)str_add(c, m->key), (uint16_t)hi };
                        bput(&c->sect, st2, 4);
                    }
                    JNode *lo = opt(c, cn, "lowest", J_NUM, cpth);                 /* v1.4: the floor */
                    long lw = 0;
                    if (lo) need_minor(c, lo, cpth, 4, "'second_teams.lowest'");
                    if (lo && int_in(c, lo, "lowest", cpth, 1, 32, &lw)) {
                        if (hi && lw < hi) ERR(lo, cpth, "second_teams: 'lowest' %ld above 'highest' %ld", lw, hi);
                        uint16_t sf[2] = { (uint16_t)str_add(c, m->key), (uint16_t)lw };
                        bput(&c->secf, sf, 4);
                    }
                    unknown_keys(c, cn, cpth);
                }
                if ((cn = opt(c, m, "files", J_ARR, cpth))) {             /* v1.4: one league across N team files */
                    JNode *e;
                    int fi = 0;
                    need_minor(c, cn, cpth, 4, "'files'");
                    for (e = cn->child; e; e = e->next, fi++) {
                        char fp[96];
                        long x;
                        pathf(fp, sizeof fp, "%s.files[%d]", cpth, fi);
                        if (e->type != J_OBJ) { ERR(e, fp, "a file is {\"file\", \"divisions\": [league ids], \"pool_rows\"}"); continue; }
                        int_in(c, need(c, e, "file", J_NUM, fp), "file", fp, 1, 255, &x);
                        need(c, e, "divisions", J_ARR, fp);
                        int_in(c, opt(c, e, "pool_rows", J_NUM, fp), "pool_rows", fp, 0, 256, &x);
                        unknown_keys(c, e, fp);
                    }
                }
                unknown_keys(c, m, cpth);
            }
            if ((v = need(c, c->root, "clubs", J_ARR, ""))) {
                c->nclubs = v->n;
                c->clubs = calloc((size_t)v->n + 1, sizeof *c->clubs);
                for (i = 0, m = v->child; m; m = m->next) c->clubs[i++] = m;
                do_clubs(c, v);
            }
            if ((v = need(c, c->root, "competitions", J_ARR, ""))) {
                int total = 0;
                JNode *s;
                c->ncomps = v->n;
                c->comps = calloc((size_t)v->n + 1, sizeof *c->comps);
                for (i = 0, m = v->child; m; m = m->next) {
                    c->comps[i++] = m;
                    s = json_get(m, "stages");
                    total += s && s->type == J_ARR ? s->n : 0;
                }
                st = calloc((size_t)total + 1, sizeof *st);
                c->stinfo = st;
                for (i = 0; i < c->ncomps; i++) do_comp(c, i, st, &nst);
                if (c->errors == 0) check_links(c);
                if (c->errors == 0) check_second_teams(c);
                if (c->errors == 0) check_second_floor(c);
                if (c->errors == 0) check_files(c);
                check_pools(c);
            }
            unknown_keys(c, c->root, "");
        }
    }
    c->arena += 4096u + (uint32_t)c->nclubs * 8u;
    if (c->errors == 0 && c->arena_max && c->arena > c->arena_max)
        ERR(c->root, "", "the engine would need %u bytes of arena, the host gives %u", c->arena, c->arena_max);
    if (c->errors == 0 && bin) {
        size_t n;
        *bin = write_bin(c, (int)world, &n);
        if (blen) *blen = n;
    }
    bput(&c->diag, "", 1);
    if (diag_out) *diag_out = (char *)c->diag.p; else free(c->diag.p);
    json_free(c->root);
    json_free(proot);
    free(c->clubs); free(c->comps); free(st);
    free(c->strs.p); free(c->names.p); free(c->club.p); free(c->comp.p); free(c->stag.p); free(c->entr.p);
    free(c->tieb.p); free(c->deci.p); free(c->roun.p); free(c->outc.p); free(c->reg.p); free(c->meta.p); free(c->fixt.p); free(c->grps.p); free(c->srce.p); free(c->tols.p); free(c->prnt.p); free(c->sect.p); free(c->secf.p);
    return c->errors;
}

void swcc_free(void *p) { free(p); }
