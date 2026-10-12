/* Compare every channel, PCM buffer and IO byte against the silent-PCM baseline. */
#define main upstream_regression_main
#include "regression_test.c"
#undef main
void reference_apu_step(gbc_t *, uint8_t);
static uint32_t seed = 0x543acdf;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
int main(void)
{
    static gbc_t a, b;
    gbc_init_static(&a);
    for (unsigned i = 0; i < 2000000; i++) {
        if (!(i & 31)) {
            /* Includes NR52 off/on, retriggers, high-frequency noise and sweep. */
            unsigned reg = 0x10 + next() % 0x30;
            gbc_apu_write(&a, (uint8_t)reg, (uint8_t)next());
        }
        memcpy(&b, &a, sizeof(a));
        uint8_t cycles = (uint8_t)(next() % 25);
        gbc_apu_step(&a, cycles); reference_apu_step(&b, cycles);
        if (memcmp(&a, &b, sizeof(a))) { printf("APU differential mismatch at step=%u cycles=%u\n",i,cycles); return 1; }
    }
    puts("APU differential PASS 2000000 steps; channel clocks, all PCM bytes, NR52 and IO identical");
    return 0;
}
