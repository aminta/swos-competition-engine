/* A minimal libc for swcc on wasm32 (freestanding, no emscripten): exactly the calls swcc.c, json.c and
   format/swc_format.c make. Memory is a bump heap that the host resets before every call (swcc_wasm.c: swcc_begin),
   so free() only gives back the last block; one validation of the largest pack stays far below a few MB. */
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"

#include <stdint.h>

/* ---- heap -------------------------------------------------------------------------------------------------------- */
extern unsigned char __heap_base;
static uintptr_t heap_top, heap_last;       /* next free byte; the last block handed out (0: none) */

#define HDR 8u                              /* a block = its size (8 bytes, keeps 8-byte alignment) + the bytes */

void swcc_heap_reset(void)
{
    heap_top = ((uintptr_t)&__heap_base + 7u) & ~(uintptr_t)7u;
    heap_last = 0;
}

static int heap_room(uintptr_t end)
{
    uintptr_t have = (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
    if (end <= have) return 1;
    return __builtin_wasm_memory_grow(0, (end - have + 65535u) / 65536u) != (size_t)-1;
}

void *malloc(size_t n)
{
    uintptr_t at, end;
    if (!heap_top) swcc_heap_reset();
    n = (n + 7u) & ~(size_t)7u;
    at = heap_top + HDR;
    end = at + n;
    if (end < at || !heap_room(end)) return NULL;
    *(size_t *)(at - HDR) = n;
    heap_top = end;
    heap_last = at;
    return (void *)at;
}

void *calloc(size_t k, size_t n)
{
    void *p;
    if (n && k > (size_t)-1 / n) return NULL;
    p = malloc(k * n);
    if (p) memset(p, 0, k * n);
    return p;
}

void free(void *p)
{
    if (p && (uintptr_t)p == heap_last) { heap_top = heap_last - HDR; heap_last = 0; }
}

void *realloc(void *p, size_t n)
{
    size_t old;
    void *q;
    if (!p) return malloc(n);
    old = *(size_t *)((uintptr_t)p - HDR);
    if ((uintptr_t)p == heap_last) {                    /* the last block grows in place */
        uintptr_t end = (uintptr_t)p + ((n + 7u) & ~(size_t)7u);
        if (end < (uintptr_t)p || !heap_room(end)) return NULL;
        *(size_t *)((uintptr_t)p - HDR) = (n + 7u) & ~(size_t)7u;
        heap_top = end;
        return p;
    }
    if (n <= old) return p;
    if (!(q = malloc(n))) return NULL;
    memcpy(q, p, old);
    return q;
}

_Noreturn void exit(int code)
{
    (void)code;
    __builtin_trap();                                   /* the host reports it (only out of memory gets here) */
}

/* ---- memory and strings ------------------------------------------------------------------------------------------ */
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    const char *e = s;
    while (*e) e++;
    return (size_t)(e - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

/* ---- numbers ----------------------------------------------------------------------------------------------------- */
#define LONG_MAX_ 0x7fffffffL                    /* long is 32 bits on wasm32 */

static int isspace_(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

long strtol(const char *s, char **end, int base)
{
    const char *p = s;
    unsigned long v = 0;
    int neg = 0, any = 0;
    (void)base;                                         /* swcc asks base 10 only */
    while (isspace_(*p)) p++;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    for (; *p >= '0' && *p <= '9'; p++, any = 1) {
        unsigned long d = (unsigned long)(*p - '0');
        if (v > ((unsigned long)LONG_MAX_ + neg - d) / 10) v = (unsigned long)LONG_MAX_ + neg;   /* saturate */
        else v = v * 10 + d;
    }
    if (end) *end = (char *)(any ? p : s);
    return neg ? (long)(0 - v) : (long)v;
}

/* JSON numbers only (json.c has checked the syntax). Exact when the digits fit in 2^53 and the power of ten is at
   most 22 (both exact doubles: one correctly rounded operation), which every spec number is; beyond that within an
   ulp or two. */
double strtod(const char *s, char **end)
{
    static const double p10[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14,
                                  1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
    const char *p = s;
    uint64_t m = 0;
    int neg = 0, e = 0, ex = 0, eneg = 0;
    double v;
    while (isspace_(*p)) p++;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    for (; *p >= '0' && *p <= '9'; p++) {
        if (m < 1000000000000000000ull) m = m * 10 + (uint64_t)(*p - '0');
        else e++;
    }
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++)
            if (m < 1000000000000000000ull) m = m * 10 + (uint64_t)(*p - '0'), e--;
    if ((*p == 'e' || *p == 'E') && (p[1] == '+' || p[1] == '-' || (p[1] >= '0' && p[1] <= '9'))) {
        p++;
        if (*p == '+' || *p == '-') eneg = *p++ == '-';
        for (; *p >= '0' && *p <= '9'; p++) if (ex < 10000) ex = ex * 10 + (*p - '0');
        e += eneg ? -ex : ex;
    }
    if (end) *end = (char *)p;
    v = (double)m;
    if (e > 0) { while (e > 22) { v *= 1e22; e -= 22; } v *= p10[e]; }
    else if (e < 0) { e = -e; while (e > 22) { v /= 1e22; e -= 22; } v /= p10[e]; }
    return neg ? -v : v;
}

/* ---- formatting -------------------------------------------------------------------------------------------------- */
/* The conversions swcc uses: %d %i %ld %u %zu %x %X %s %c %%, with the flags '-' '0', a width and a precision for %s. */
typedef struct { char *b; size_t n, k; } Out;

static void put(Out *o, char c)
{
    if (o->k + 1 < o->n) o->b[o->k] = c;
    o->k++;
}

static void pad(Out *o, int w, char c) { while (w-- > 0) put(o, c); }

int vsnprintf(char *b, size_t n, const char *fmt, va_list ap)
{
    Out o = { b, n, 0 };
    for (; *fmt; fmt++) {
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0, neg = 0, len, base = 10, upper = 0;
        char num[24], *q;
        unsigned long u = 0;
        const char *str;
        if (*fmt != '%') { put(&o, *fmt); continue; }
        for (fmt++;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        if (*fmt == 'l' || *fmt == 'z') { lng = 1; fmt++; }
        switch (*fmt) {
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : va_arg(ap, int);
            neg = v < 0;
            u = neg ? 0ul - (unsigned long)v : (unsigned long)v;
            goto number;
        }
        case 'X': upper = 1; /* fall through */
        case 'x': base = 16; /* fall through */
        case 'u':
            u = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
        number:
            q = num + sizeof num;
            do { int d = (int)(u % (unsigned)base); *--q = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10); u /= (unsigned)base; } while (u);
            len = (int)(num + sizeof num - q) + neg;
            if (left) { if (neg) put(&o, '-'); while (q < num + sizeof num) put(&o, *q++); pad(&o, width - len, ' '); }
            else if (zero) { if (neg) put(&o, '-'); pad(&o, width - len, '0'); while (q < num + sizeof num) put(&o, *q++); }
            else { pad(&o, width - len, ' '); if (neg) put(&o, '-'); while (q < num + sizeof num) put(&o, *q++); }
            break;
        case 's':
            str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            for (len = 0; str[len] && (prec < 0 || len < prec); len++) ;
            if (!left) pad(&o, width - len, ' ');
            for (int i = 0; i < len; i++) put(&o, str[i]);
            if (left) pad(&o, width - len, ' ');
            break;
        case 'c':
            if (!left) pad(&o, width - 1, ' ');
            put(&o, (char)va_arg(ap, int));
            if (left) pad(&o, width - 1, ' ');
            break;
        case '%': put(&o, '%'); break;
        default: __builtin_trap();                     /* a conversion this libc lacks: fail loudly, never silently */
        }
    }
    if (n) b[o.k < n ? o.k : n - 1] = 0;
    return (int)o.k;
}

int snprintf(char *b, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(b, n, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    (void)f;
    (void)fmt;
    return 0;
}

/* Literal characters and %d only (swcc reads "line:col:" back from json.c's message). */
int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int got = 0;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (fmt[0] == '%' && fmt[1] == 'd') {
            char *e;
            long v = strtol(s, &e, 10);
            if (e == s) break;
            *va_arg(ap, int *) = (int)v;
            got++;
            s = e;
            fmt++;
        } else if (*s == *fmt) s++;
        else break;
    }
    va_end(ap);
    return got;
}
