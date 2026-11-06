/* The few <stdlib.h> calls swcc makes, for wasm32 (wasm/libc.c). */
#ifndef SWCC_WASM_STDLIB_H
#define SWCC_WASM_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t k, size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
long strtol(const char *s, char **end, int base);
double strtod(const char *s, char **end);
_Noreturn void exit(int code);
#endif
