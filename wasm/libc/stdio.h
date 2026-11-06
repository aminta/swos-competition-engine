/* The few <stdio.h> calls swcc's core makes, for wasm32 (wasm/libc.c): formatting into memory only. There is no
   file: stderr is a null stream and fprintf to it is dropped (swcc writes there only before exit(2) on out of memory,
   which traps). */
#ifndef SWCC_WASM_STDIO_H
#define SWCC_WASM_STDIO_H
#include <stdarg.h>
#include <stddef.h>
typedef struct FILE FILE;
#define stderr ((FILE *)0)
int vsnprintf(char *b, size_t n, const char *fmt, va_list ap);
int snprintf(char *b, size_t n, const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
#endif
