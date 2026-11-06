/* swcc's exports for wasm32 (the web page and tests/wasm_parity.mjs). One call at a time:
     swcc_begin()                     forget the previous call (the heap starts over)
     swcc_alloc(n)                    room for the inputs the host copies in: the file name, the spec, the profile
     swcc_run(spec, len, name, profile, plen, want_bin)
                                      = swcc_compile: the number of errors; then
     swcc_diag()                      the diagnostics, NUL-terminated, one per line (as the CLI writes them)
     swcc_bin(), swcc_blen()          the binary spec when want_bin and no error */
#include "../swcc/swcc.h"
#include "libc/stdlib.h"

#define EXPORT(n) __attribute__((export_name(n)))

void swcc_heap_reset(void);

static char *diag;
static unsigned char *bin;
static size_t blen;

EXPORT("swcc_begin") void swcc_begin(void)
{
    swcc_heap_reset();
    diag = NULL;
    bin = NULL;
    blen = 0;
}

EXPORT("swcc_alloc") void *swcc_alloc(size_t n) { return malloc(n ? n : 1); }

EXPORT("swcc_run") int swcc_run(const char *spec, size_t len, const char *name, const char *profile, size_t plen,
                                int want_bin)
{
    return swcc_compile(spec, len, name, profile, plen, want_bin ? &bin : NULL, &blen, &diag);
}

EXPORT("swcc_diag") const char *swcc_diag(void) { return diag ? diag : ""; }
EXPORT("swcc_bin") const unsigned char *swcc_bin(void) { return bin; }
EXPORT("swcc_blen") size_t swcc_blen(void) { return blen; }
