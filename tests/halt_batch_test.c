/* Whole-step differential, including pending interrupts and PCM phase. */
#define main upstream_regression_main
#include "regression_test.c"
#undef main
uint8_t reference_gbc_step(gbc_t *);
static uint32_t seed = 0x9d346af;
static uint32_t random32(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
}
int main(void) {
    static gbc_t a, b;
    static uint8_t rom[32768];
    unsigned batched = 0;
    memset(rom, 0, sizeof(rom));
    for (unsigned i = 0; i < 12000; ++i) {
        gbc_init_static(&a);
        if (gbc_cart_load_rom(&a, rom, sizeof(rom), 0) != GBC_OK) return 2;
        a.cpu.halted = 1; a.cpu.ime = (uint8_t)(random32() & 1);
        a.cpu.ime_delay = (uint8_t)(random32() & 1); a.cpu.sp = 0xDFFE;
        a.cpu.cycles = random32();
        a.model = (random32() & 1) ? GBC_MODEL_DMG : GBC_MODEL_CGB;
        a.mmu.double_speed = (uint8_t)(random32() & 1);
        a.mmu.ie = (uint8_t)random32();
        a.mmu.io[15] = (i % 3) ? 0 : (uint8_t)random32();
        a.mmu.io[4] = (uint8_t)random32();
        a.mmu.io[5] = (uint8_t)random32(); a.mmu.io[6] = (uint8_t)random32();
        a.mmu.io[7] = (uint8_t)random32();
        a.timer.div_counter = (uint16_t)random32();
        a.timer.tima_reload_delay = (uint8_t)(random32() % 5);
        a.mmu.io[0x40] = (uint8_t)random32();
        a.mmu.io[0x41] = (uint8_t)random32();
        a.mmu.io[0x44] = (uint8_t)(random32() % 154);
        a.mmu.io[0x45] = (uint8_t)(random32() % 154);
        a.ppu.dot_cycles = (uint16_t)(random32() % 456);
        a.mmu.io[2] = (uint8_t)random32();
        a.mmu.serial_cycles = (uint16_t)(random32() % 4096);
        a.mmu.hdma_busy_cycles = (i % 8) ? 0 : (uint8_t)(random32() & 60);
#if GBC_ENABLE_SOUND
        a.apu.enabled = (uint8_t)(random32() & 1);
        a.apu.sample_acc = random32() % 4194304;
#endif
        memcpy(&b, &a, sizeof(a));
        unsigned got = gbc_step(&a), reference = 0;
        while (reference < got) reference += reference_gbc_step(&b);
        if (got > 4) batched++;
        if (reference != got || memcmp(&a, &b, sizeof(a))) {
            printf("HALT mismatch case=%u cycles=%u reference=%u\n", i, got, reference); return 1;
        }
        gbc_unload_rom_discard(&a);
    }
    if (!batched) return 3;
    printf("HALT differential PASS cases=12000 batch=%u max=%u sound=%u\n", batched, GBC_HALT_BATCH_CYCLES, GBC_ENABLE_SOUND);
    return 0;
}
