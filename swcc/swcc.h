/* swcc library API (ctypes from Python; WASM later). */
#ifndef SWCC_H
#define SWCC_H

#include <stddef.h>

/* Validates spec (JSON text, schema swc/1) against profile (JSON text, a host's limits). Returns the number of
   errors. diag_out: every diagnostic, one per line, NUL-terminated (free with swcc_free). When there is no error and
   bin is not NULL, *bin / *blen receive the binary spec (free with swcc_free). */
int  swcc_compile(const char *spec, size_t len, const char *name, const char *profile, size_t plen,
                  unsigned char **bin, size_t *blen, char **diag_out);
void swcc_free(void *p);

/* v1.3: how swcc reads a file a spec names (a pool file): path as the spec writes it, from = the spec's name; returns
   the text (malloc'd, swcc frees it) and *len, or NULL when it cannot be read. Without a loader (the default, as in
   the WASM editor) a pool file's contents are not checked, only the world's 'pools' list. */
typedef char *(*swcc_loader)(const char *path, const char *from, size_t *len);
void swcc_set_loader(swcc_loader f);

#endif
