/* Engine unit tests (S0): PCG32 against the PCG reference vectors, arena, FNV-1a, spec reader rejections. */
#include "../engine/swc.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)

int main(void)
{
    /* pcg32-demo (pcg-c-basic): seed 42, sequence 54 */
    static const uint32_t ref[6] = { 0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e };
    SwcRng r, r2;
    SwcArena a;
    static uint8_t mem[1024];
    uint8_t *p, *q;
    int i, hist[7] = { 0 };
    swc_rng_seed(&r, 42u, 54u);
    for (i = 0; i < 6; i++) CHECK(swc_rng_next(&r) == ref[i]);
    for (i = 0; i < 7000; i++) { uint32_t x = swc_rng_below(&r, 7); CHECK(x < 7); hist[x]++; }
    for (i = 0; i < 7; i++) CHECK(hist[i] > 850 && hist[i] < 1150);
    swc_rng_stream(&r, 1997, 1, swc_fnv1a("eng.fa-cup:ko", 13));
    swc_rng_stream(&r2, 1997, 1, swc_fnv1a("eng.fa-cup:ko", 13));
    CHECK(swc_rng_next(&r) == swc_rng_next(&r2));
    swc_rng_stream(&r2, 1997, 2, swc_fnv1a("eng.fa-cup:ko", 13));
    CHECK(swc_rng_next(&r) != swc_rng_next(&r2));
    /* FNV-1a 32 reference values */
    CHECK(swc_fnv1a("", 0) == 0x811c9dc5u);
    CHECK(swc_fnv1a("a", 1) == 0xe40c292cu);
    CHECK(swc_fnv1a("foobar", 6) == 0xbf9cf968u);
    /* arena */
    CHECK(swc_init(&a, mem, sizeof mem) == SWC_OK);
    p = swc_alloc(&a, 3);
    q = swc_alloc(&a, 8);
    CHECK(p && q && q - p == 4 && ((uintptr_t)q & 3) == 0);
    CHECK(swc_alloc(&a, 2000) == NULL);
    swc_reset(&a, 4);
    CHECK(a.used == 4 && a.peak == 12);
    /* spec reader rejections */
    {
        static uint32_t buf[16];
        SwcSpec s;
        CHECK(swc_spec_open(&s, buf, 8) == SWC_E_TRUNC);
        CHECK(swc_spec_open(&s, buf, sizeof buf) == SWC_E_MAGIC);
        buf[0] = SWC_MAGIC; buf[1] = 2;
        CHECK(swc_spec_open(&s, buf, sizeof buf) == SWC_E_FORMAT);
        buf[1] = SWC_FORMAT | 0x7FFFu << 16;
        CHECK(swc_spec_open(&s, buf, sizeof buf) == SWC_E_ENGINE);
    }
    CHECK(swc_version() == SWC_VERSION);
    printf("test_engine: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
