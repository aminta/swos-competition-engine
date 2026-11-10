/* swc_format: the SHARED reference of the SWOS Competition Engine project (C99, freestanding, no libc).

   What every implementation must agree on, byte for byte:
   - the compiled spec format (.SWC, big-endian, docs/format.md): record layouts, BE accessors, a reference reader
     that decodes a .SWC into native records (any alignment, any host byte order);
   - the determinism primitives: PCG32 (bit for bit the PCG reference), substreams hash(seed, season, purpose),
     FNV-1a 32 (spec hash, purpose hashes).
   The engine itself is being written inside OpenSWOS (then extracted as a library); this file is what swcc, the
   contract tests and any engine share. */
#ifndef SWC_FORMAT_H
#define SWC_FORMAT_H

#include <stdint.h>
#include <stddef.h>


#define SWC_VERSION        0x0004          /* reader version (engine_min in a spec header must be <= this; 2: swc/1.5's
                                              GDRW / PROT / KENT and SwcRound.avoid -- swcc writes engine_min 2 only
                                              for a spec that uses one, so an older reader refuses it, never draws
                                              it without its constraints; 3: swc/1.6's GDRW separate bit 1, two
                                              clubs of an association per group at most; 4: swc/1.7's bit 2, the
                                              fewest of an association a group, unless unavoidable) */
#define SWC_FORMAT         2               /* binary spec format (2: big-endian; 1 was little-endian, S0 only) */

/* ---- status (stable numbers) ------------------------------------------------------------------------------------ */
typedef enum {
    SWC_OK = 0,
    SWC_E_ARG = 1,          /* a NULL / out-of-range argument */
    SWC_E_NOMEM = 2,        /* the arena is full */
    SWC_E_MAGIC = 3,        /* not a .SWC file */
    SWC_E_FORMAT = 4,       /* format version not supported, or this compiler pads a record struct */
    SWC_E_ENGINE = 5,       /* the spec needs a newer reader */
    SWC_E_TRUNC = 6,        /* a section or record runs past the buffer, or a reference is out of range */
    SWC_E_HASH = 7,         /* the content hash does not match */
    SWC_E_MISSING = 8       /* a required section is absent */
} SwcStatus;

/* ---- arena ----------------------------------------------------------------------------------------------------- */
typedef struct {
    uint8_t *base;
    uint32_t size, used, peak;
} SwcArena;

SwcStatus swc_init(SwcArena *a, void *mem, uint32_t size);   /* zeroes the memory */
void     *swc_alloc(SwcArena *a, uint32_t n);                /* 4-byte aligned, zeroed; NULL when full */
void      swc_reset(SwcArena *a, uint32_t mark);             /* back to a mark (a = a->used taken earlier) */

/* ---- RNG: PCG32 (O'Neill, pcg32_random_r / pcg32_srandom_r), bit for bit ---------------------------------------- */
typedef struct { uint64_t state, inc; } SwcRng;

void     swc_rng_seed(SwcRng *r, uint64_t initstate, uint64_t initseq);
uint32_t swc_rng_next(SwcRng *r);
uint32_t swc_rng_below(SwcRng *r, uint32_t n);               /* uniform in [0, n), rejection, no division */
/* A substream: seed = the world seed, stream = hash(season, purpose). The purpose hash is the FNV-1a 32 of the
   purpose string, computed by swcc and stored in the spec, so the engine never hashes strings at run time. */
void     swc_rng_stream(SwcRng *r, uint64_t seed, uint32_t season, uint32_t purpose);

uint32_t swc_fnv1a(const void *p, uint32_t n);               /* FNV-1a 32 */
uint32_t swc_fnv1a_more(uint32_t h, const void *p, uint32_t n);

/* ---- binary spec format (written by swcc; docs/format.md) --------------------------------------------------------
   BIG-ENDIAN, as OpenSWOS's data (authored on the Amiga): one .SWC file runs on the 1996 DOS exe (i486), on SDL hosts
   and on the m68k Amiga. The reader never casts the buffer to a struct: every field is read through the BE
   accessors below (byte reads: any alignment, any host order) and DECODED into native records in the arena by the
   record layouts (swc_layout). Strings and byte lists stay in place. Offsets are from the file start. */
#define SWC_BE_READ_U16(p)  ((uint16_t)((uint16_t)((const uint8_t *)(p))[0] << 8 | ((const uint8_t *)(p))[1]))
#define SWC_BE_READ_U32(p)  ((uint32_t)((const uint8_t *)(p))[0] << 24 | (uint32_t)((const uint8_t *)(p))[1] << 16 | \
                             (uint32_t)((const uint8_t *)(p))[2] << 8 | (uint32_t)((const uint8_t *)(p))[3])
#define SWC_BE_WRITE_U16(p, v) do { uint8_t *q_ = (uint8_t *)(p); uint16_t v_ = (uint16_t)(v); \
                                    q_[0] = (uint8_t)(v_ >> 8); q_[1] = (uint8_t)v_; } while (0)
#define SWC_BE_WRITE_U32(p, v) do { uint8_t *q_ = (uint8_t *)(p); uint32_t v_ = (uint32_t)(v); \
                                    q_[0] = (uint8_t)(v_ >> 24); q_[1] = (uint8_t)(v_ >> 16); \
                                    q_[2] = (uint8_t)(v_ >> 8); q_[3] = (uint8_t)v_; } while (0)

#define SWC_MAGIC          0x53574342u     /* the bytes 'S' 'W' 'C' 'B' read big-endian */
#define SWC_TAG(a,b,c,d)   ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* Record layouts: the field sizes in order ('1', '2', '4'), the same in the file (big-endian, no padding) and in the
   native struct (natural alignment, no padding: checked at open against sizeof, so a host whose compiler lays a
   struct out differently is refused instead of misread). NULL = a byte section (STRS, TIEB, DECI). */
const char *swc_layout(uint32_t tag);
uint32_t    swc_layout_size(const char *layout);
#define SWC_LAYOUT_HEADER  "4224224444"    /* SwcHeader */
#define SWC_LAYOUT_SECTION "444"           /* SwcSection */

typedef struct {            /* 32 bytes at offset 0 */
    uint32_t magic;         /* SWC_MAGIC */
    uint16_t format;        /* SWC_FORMAT */
    uint16_t engine_min;    /* lowest engine version that runs it */
    uint32_t flags;
    uint16_t world;         /* the host's world number (SWOS 96/97 Mod: the world byte; 0 = 1996-97) */
    uint16_t nsect;         /* directory entries after the header */
    uint32_t hash;          /* FNV-1a 32 of every byte after the directory (sections) */
    uint32_t arena;         /* worst-case engine arena for this spec, bytes */
    uint32_t length;        /* file length */
    uint32_t reserved;
} SwcHeader;

typedef struct { uint32_t tag, off, len; } SwcSection;       /* directory entry, 12 bytes */

/* Sections. Records are fixed size; an index is a 16-bit dense number, 0xFFFF = none; a string is a 32-bit offset
   into STRS (NUL-terminated); a name is an index into NAME. */
#define SWC_S_STRS  SWC_TAG('S','T','R','S')   /* string table (ids, diagnostics) */
#define SWC_S_NAME  SWC_TAG('N','A','M','E')   /* SwcName records: per language long + short, in the host charset */
#define SWC_S_CLUB  SWC_TAG('C','L','U','B')   /* SwcClub records */
#define SWC_S_COMP  SWC_TAG('C','O','M','P')   /* SwcComp records */
#define SWC_S_STAG  SWC_TAG('S','T','A','G')   /* SwcStage records */
#define SWC_S_ENTR  SWC_TAG('E','N','T','R')   /* uint16 club indices: competitions' fixed participant lists */
#define SWC_S_TIEB  SWC_TAG('T','I','E','B')   /* uint8 lists: tie-breaker criteria, each list ends with 0 */
#define SWC_S_DECI  SWC_TAG('D','E','C','I')   /* uint8 lists: knockout deciders, each list ends with 0 */
#define SWC_S_ROUN  SWC_TAG('R','O','U','N')   /* SwcRound records (knockout rounds) */
#define SWC_S_OUTC  SWC_TAG('O','U','T','C')   /* SwcOutcome records */
#define SWC_S_REG   SWC_TAG('R','E','G',' ')   /* SwcReg records: which competitions each game mode offers */
#define SWC_S_META  SWC_TAG('M','E','T','A')   /* SwcMeta: the pack's id and version (saves record them) */
#define SWC_S_FIXT  SWC_TAG('F','I','X','T')   /* SwcFixed records: fixed draws (pairing "fixed") */
#define SWC_S_GRPS  SWC_TAG('G','R','P','S')   /* SwcGroup records: group sizes of a stage with unequal groups */
#define SWC_S_SRCE  SWC_TAG('S','R','C','E')   /* SwcSource records: a linked competition's source competitions */
#define SWC_S_TOLS  SWC_TAG('T','O','L','S')   /* SwcTarget records: every target of an outcome with a 'to' list (v1.2) */
#define SWC_S_PRNT  SWC_TAG('P','R','N','T')   /* SwcParent records: a second team's parent club (v1.3) */
#define SWC_S_SECT  SWC_TAG('S','E','C','T')   /* SwcSecondTeams records: a country's highest level for second teams (v1.3) */
#define SWC_S_SECF  SWC_TAG('S','E','C','F')   /* SwcSecondTeams records: a country's lowest level for second teams, their floor (v1.4) */
#define SWC_S_GDRW  SWC_TAG('G','D','R','W')   /* SwcDraw records: a groups stage's draw constraints (v1.5) */
#define SWC_S_PROT  SWC_TAG('P','R','O','T')   /* SwcProtect records: country pairs whose clubs never meet in a draw (v1.5) */
#define SWC_S_KENT  SWC_TAG('K','E','N','T')   /* SwcKoEntrant records: a stage's entrants from earlier stages, in seeding order (v1.5) */

#define SWC_LANGS   4                          /* it, en, fr, de: the order of SwcName.str */

typedef struct { uint32_t id; uint32_t str[SWC_LANGS][2]; } SwcName;          /* id, [lang][long, short] */
typedef struct {
    uint32_t id;            /* 'club:eng.arsenal' */
    uint32_t name;          /* STRS: the host name */
    uint16_t host_file;     /* SWOS: team file number */
    uint16_t host_ordinal;  /* SWOS: ordinal in the file */
    uint16_t country;       /* index of the country code in STRS order (dense, swcc) */
    uint16_t area;          /* geographic area for parallel groups, 0xFFFF = none */
} SwcClub;

typedef enum { SWC_K_LEAGUE = 1, SWC_K_CUP, SWC_K_LEAGUE_CUP, SWC_K_SUPERCUP, SWC_K_CONTINENTAL, SWC_K_PLAYOFF,
               SWC_K_TOURNAMENT } SwcKind;
typedef struct {
    uint32_t id;
    uint16_t name;          /* NAME index */
    uint8_t  kind, level;   /* SwcKind; pyramid level (leagues), 0 = none */
    uint32_t country;       /* STRS: country code */
    uint16_t first_stage, nstages;
    uint16_t entr, nentr;   /* fixed participants in ENTR; nentr = 0: from links */
    uint16_t first_outc, noutc;
    uint8_t  win, draw, loss, flags;
} SwcComp;

typedef enum { SWC_ST_ROUND_ROBIN = 1, SWC_ST_GROUPS, SWC_ST_LEAGUE_PHASE, SWC_ST_KNOCKOUT, SWC_ST_PLAYOFF,
               SWC_ST_SPLIT, SWC_ST_AGGREGATE, SWC_ST_AVERAGE, SWC_ST_SINGLE } SwcStageType;
typedef struct {
    uint32_t id;
    uint32_t purpose;       /* RNG purpose hash: FNV-1a of the stage id string */
    uint16_t comp;
    uint8_t  type, legs;    /* SwcStageType; meetings per pair (round robin) */
    uint16_t teams;
    uint16_t groups;        /* groups (1 = a single table) */
    uint16_t tieb;          /* offset into TIEB */
    uint16_t first_round, nrounds;  /* ROUN (knockout / playoff) */
    uint16_t flags;
} SwcStage;

typedef struct { uint16_t ties; uint8_t legs, home; uint16_t deci; uint8_t pairing, avoid; } SwcRound;
/* SwcRound.avoid (v1.5; was pad = 0, swcc zeroes every round): the pairs the round's draw never makes */
#define SWC_AV_SAME_GROUP  1                   /* two clubs from one group of the KENT source stage */
#define SWC_AV_ASSOCIATION 2                   /* two clubs of one association, or of a PROT pair of countries */

typedef enum { SWC_TAG_NONE = 0, SWC_TAG_CHAMPION, SWC_TAG_PROMOTED, SWC_TAG_RELEGATED, SWC_TAG_PLAYOFF,
               SWC_TAG_PLAYOUT, SWC_TAG_CONTINENTAL, SWC_TAG_QUALIFIED } SwcTagKind;
typedef enum { SWC_Z_NONE = 0, SWC_Z_CL, SWC_Z_EL, SWC_Z_CONF, SWC_Z_PROM, SWC_Z_POFF, SWC_Z_POUT, SWC_Z_RETR } SwcZone;
typedef struct {
    uint16_t stage;         /* STAG index */
    uint8_t  from, to;      /* positions (1-based, inclusive) */
    uint8_t  tag, zone;
    uint16_t target;        /* COMP index, 0xFFFF = none */
    uint8_t  season;        /* 0 = this season, 1 = next */
    uint8_t  flags;         /* bit 0: 'to' is a list (TOLS; target = its first), the clubs spread by area (v1.2) */
} SwcOutcome;
#define SWC_OF_GEOGRAPHIC 1

typedef enum { SWC_M_CAREER = 1, SWC_M_SEASON = 2, SWC_M_PRESET = 4, SWC_M_FRIENDLY = 8 } SwcMode;
typedef struct { uint16_t comp; uint16_t modes; } SwcReg;
typedef struct { uint32_t id, version; } SwcMeta;                 /* STRS offsets */
typedef struct { uint16_t round, home, away; } SwcFixed;           /* ROUN index; ENTR-style club indices */
typedef struct { uint16_t stage, size; } SwcGroup;                 /* one per group, in order */
typedef struct { uint16_t comp, source; } SwcSource;
typedef struct { uint16_t outcome, target; } SwcTarget;           /* OUTC index, COMP index */
typedef struct { uint16_t club, parent; } SwcParent;               /* CLUB indices: a second team, its first team (v1.3) */
typedef struct { uint16_t country, highest; } SwcSecondTeams;      /* STRS country code, the highest level a second team plays (v1.3) */

/* v1.5: a groups stage's draw (GDRW, at most one per stage). Pots: 1 = access order (the stage's clubs in entrant
   order, pot k = clubs k*g .. k*g+g-1: sr_group_draw's input); 2 = coefficient (game data: refused for a profile
   that does not supply it). separate bit 0: two clubs of one association never in one group (classes: König's
   bound); v1.6 bit 1 (with bit 0): two at most instead of one (CAF CL / CC regulations art. 17; engine_min 3). PROT[protect .. protect+nprotect-1]: country pairs whose clubs never share a group -- kept as PAIRS, never
   merged into classes (UEFA's clashes are chains: KOS-BIH and KOS-SRB do not forbid BIH v SRB; MC 2026-11-10). */
typedef struct { uint16_t stage; uint8_t pots, separate; uint16_t protect, nprotect; } SwcDraw;
#define SWC_POTS_ACCESS_ORDER 1
#define SWC_POTS_COEFFICIENT  2
#define SWC_SEP_ASSOCIATION   1
#define SWC_SEP_CAP2          2           /* v1.6: an association's clubs per group: 2 at most */
#define SWC_SEP_MIN           4           /* v1.7: unless unavoidable - the fewest of an association a group (bit 0 set, no
                                             bit 1, no pairs; sr_group_draw_m) */
typedef struct { uint16_t a, b; } SwcProtect;                      /* CLUB.country's dense indices, a < b */
/* v1.5: one record per item of a stage's `entrants` list, in list order = the seeding order: item by item; within an
   item place by place, then group by group (A..H). kind 1: places from..to of src (1..127, or 128 + N = N-th from
   the bottom, as OUTC); kind 2: best_of (from = the place, to = how many). group 0xFF = every group of src. */
typedef struct { uint16_t stage, src; uint8_t kind, group, from, to; } SwcKoEntrant;
#define SWC_KE_PLACES  1
#define SWC_KE_BEST_OF 2
#define SWC_KE_ALL     0xFF

/* Tie-breaker criteria (TIEB) and deciders (DECI): dense codes shared with swcc. 0 ends a list. */
typedef enum { SWC_TB_END = 0, SWC_TB_POINTS, SWC_TB_GOAL_DIFFERENCE, SWC_TB_GOALS_FOR, SWC_TB_GOALS_AGAINST,
               SWC_TB_WINS, SWC_TB_AWAY_WINS, SWC_TB_AWAY_GOALS_FOR, SWC_TB_H2H_POINTS, SWC_TB_H2H_GOAL_DIFFERENCE,
               SWC_TB_H2H_GOALS_FOR, SWC_TB_H2H_AWAY_GOALS, SWC_TB_FAIR_PLAY, SWC_TB_COEFFICIENT,
               SWC_TB_PREVIOUS_STAGE_RANK, SWC_TB_PLAY_OFF_MATCH, SWC_TB_DRAW_LOTS, SWC_TB_NAME,
               SWC_TB_FAITHFUL_1996, SWC_TB_H2H_REAPPLIED, SWC_TB_AVULSA, SWC_TB_LOSSES_FEWER,
               SWC_TB_HOME_LOSSES_FEWER, SWC_TB_H2H_TABLE_ONCE, SWC_TB_ONLY_IF_TWO,
               SWC_TB_AWAY_GOAL_DIFFERENCE, SWC_TB_COUNT } SwcTieBreak;
typedef enum { SWC_D_END = 0, SWC_D_AGGREGATE, SWC_D_AWAY_GOALS, SWC_D_EXTRA_TIME, SWC_D_PENALTIES,
               SWC_D_BETTER_PLACED, SWC_D_REPLAY, SWC_D_DRAW_LOTS, SWC_D_GOLDEN_GOAL, SWC_D_COUNT } SwcDecider;

/* ---- spec reader ----------------------------------------------------------------------------------------------- */
typedef struct {
    const uint8_t *bin;
    uint32_t len;
    SwcHeader h;            /* decoded */
    const char *strs;   uint32_t nstrs;
    const SwcName *names;   uint32_t nnames;
    const SwcClub *clubs;   uint32_t nclubs;
    const SwcComp *comps;   uint32_t ncomps;
    const SwcStage *stages; uint32_t nstages;
    const uint16_t *entr;   uint32_t nentr;
    const uint8_t *tieb;    uint32_t ntieb;
    const uint8_t *deci;    uint32_t ndeci;
    const SwcRound *rounds; uint32_t nrounds;
    const SwcOutcome *outc; uint32_t noutc;
    const SwcReg *reg;      uint32_t nreg;
    const SwcMeta *meta;    uint32_t nmeta;
    const SwcFixed *fixt;   uint32_t nfixt;
    const SwcGroup *grps;   uint32_t ngrps;
    const SwcSource *srce;  uint32_t nsrce;
    const SwcTarget *tols;  uint32_t ntols;
    const SwcParent *prnt;  uint32_t nprnt;
    const SwcSecondTeams *sect; uint32_t nsect;
    const SwcSecondTeams *secf; uint32_t nsecf;     /* (highest = the floor: below it a second team leaves the world) */
    const SwcDraw *gdrw;    uint32_t ngdrw;         /* (v1.5) */
    const SwcProtect *prot; uint32_t nprot;
    const SwcKoEntrant *kent; uint32_t nkent;
} SwcSpec;

/* Checks the header, the directory, the hash and every section's bounds and record size, and decodes the records
   into native arrays allocated from the arena (strings and byte lists stay in the buffer, which must outlive s). */
SwcStatus   swc_spec_open(SwcSpec *s, const void *bin, uint32_t len, SwcArena *a);
const char *swc_str(const SwcSpec *s, uint32_t off);         /* "" when out of range */

uint32_t swc_version(void);


#endif
