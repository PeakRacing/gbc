/* Differential checks against the pinned, unoptimized timer and PPU. */
#define main upstream_regression_main
#include "regression_test.c"
#undef main
void reference_timer_step(gbc_t *, uint8_t);
void reference_ppu_step(gbc_t *, uint8_t);
void reference_ppu_render_scanline(gbc_t *, uint8_t);
static uint32_t random_state = 0x193ab45;
static uint32_t next_random(void)
{
    random_state ^= random_state << 13; random_state ^= random_state >> 17;
    random_state ^= random_state << 5; return random_state;
}
int main(void)
{
    static gbc_t a, b;
    static const uint8_t clocks[] = {0, 1, 4, 8, 12, 16, 20, 24, 255};
    unsigned cases = 0;
    gbc_init_static(&a); gbc_init_static(&b);
    for (unsigned tac = 0; tac < 8; tac++)
        for (unsigned div = 0; div < 65536; div++)
            for (unsigned c = 0; c < sizeof(clocks); c++) {
                a.timer.div_counter = (uint16_t)div;
                a.timer.tima_reload_delay = (uint8_t)(next_random() % 5);
                a.mmu.io[4] = (uint8_t)next_random(); a.mmu.io[5] = (uint8_t)next_random();
                a.mmu.io[6] = (uint8_t)next_random(); a.mmu.io[7] = (uint8_t)tac;
                a.mmu.io[15] = (uint8_t)next_random();
                b.timer = a.timer; memcpy(b.mmu.io, a.mmu.io, sizeof(a.mmu.io));
                gbc_timer_step(&a, clocks[c]); reference_timer_step(&b, clocks[c]);
                if (memcmp(&a.timer, &b.timer, sizeof(a.timer)) || memcmp(a.mmu.io, b.mmu.io, sizeof(a.mmu.io))) {
                    printf("timer mismatch DIV=%u TAC=%u cycles=%u\n", div,tac,clocks[c]); return 1;
                }
                cases++;
            }
    printf("Timer differential PASS cases=%u (all DIV values, wrap, TAC, pending reload)\n", cases);
    cases = 0;
    gbc_init_static(&a);
    for (unsigned ly = 0; ly < 154; ly++)
        for (unsigned dot = 0; dot < 456; dot++)
            for (unsigned c = 0; c < sizeof(clocks)-1; c++) {
                a.model = (next_random() & 1) ? GBC_MODEL_DMG : GBC_MODEL_CGB;
                a.ppu.dot_cycles = (uint16_t)dot;
                a.mmu.io[0x40] = (uint8_t)next_random();
                a.mmu.io[0x41] = (uint8_t)next_random();
                a.mmu.io[0x43] = (uint8_t)next_random(); a.mmu.io[0x44] = (uint8_t)ly;
                a.mmu.io[0x45] = (uint8_t)(next_random() % 154);
                for (unsigned i = 0; i < sizeof(a.mmu.oam); i++) a.mmu.oam[i] = (uint8_t)next_random();
                memcpy(&b, &a, sizeof(a));
                gbc_ppu_step(&a, clocks[c]); reference_ppu_step(&b, clocks[c]);
                if (memcmp(&a, &b, sizeof(a))) {
                    printf("PPU mismatch LY=%u dot=%u cycles=%u\n",ly,dot,clocks[c]); return 1;
                }
                cases++;
            }
    printf("PPU differential PASS cases=%u (all LY/dots, dynamic OAM/SCX/LCDC/STAT)\n", cases);
    for (unsigned i = 0; i < 20000; i++) {
        a.model = (next_random() & 1) ? GBC_MODEL_DMG : GBC_MODEL_CGB;
        for (unsigned bank = 0; bank < GBC_VRAM_BANKS; bank++)
            for (unsigned j = 0; j < 0x2000; j++) a.mmu.vram[bank][j] = (uint8_t)next_random();
        for (unsigned j = 0; j < sizeof(a.mmu.oam); j++) a.mmu.oam[j] = (uint8_t)next_random();
        for (unsigned j = 0x40; j <= 0x4B; j++) a.mmu.io[j] = (uint8_t)next_random();
        for (unsigned j = 0; j < 64; j++) {
            a.ppu.bg_palette[j] = (uint8_t)next_random(); a.ppu.obj_palette[j] = (uint8_t)next_random();
        }
        a.ppu.win_line = (uint8_t)next_random();
#if GBC_ENABLE_BOOT_ROM
        a.boot_compat = next_random() & 1; a.hardware_model = GBC_MODEL_CGB;
        a.boot_opri = next_random() & 1;
#endif
        /* Cycle through every WX value, including 0..6, 166 and off-screen. */
        a.mmu.io[0x4B] = (uint8_t)i;
        memcpy(&b, &a, sizeof(a));
        uint8_t ly = (uint8_t)(next_random() % 144);
        gbc_ppu_render_scanline(&a, ly); reference_ppu_render_scanline(&b, ly);
        if (memcmp(&a, &b, sizeof(a))) {
            printf("render mismatch case=%u WX=%u LY=%u model=%u\n",i,a.mmu.io[0x4B],ly,a.model); return 1;
        }
    }
    puts("PPU render differential PASS 20000 random VRAM/OAM/palette/scroll/window scenes");
    return 0;
}
