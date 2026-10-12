/* Copyright PeakRacing; Apache-2.0. */
#include "gbc.h"
#include <SDL.h>

int gbc_sdl_set_path(const char *base);

static int shortcut(gbc_t *gbc, SDL_Keycode key)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    if (SDL_PushEvent(&event) != 1) { return 0; }
    gbc_frame(gbc);
    return 1;
}

int main(void)
{
    static gbc_t gbc;
    static uint8_t rom[32768];
    int failed = 0;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    rom[0x147] = 3; rom[0x149] = 2;
    if (gbc_init_static(&gbc) != GBC_OK) { return 1; }
    if (gbc_load_rom(&gbc, rom, sizeof(rom)) != GBC_OK ||
        gbc_persist_set_path(&gbc, "sdl_feature") != GBC_OK ||
        gbc_sdl_set_path("sdl_feature") != GBC_OK) { failed = 1; goto cleanup; }
    gbc_cart_write(&gbc, 0, 10);
    gbc_cart_write_ram(&gbc, 0xA123, 0x5A);
    if (!shortcut(&gbc, SDLK_F6) || gbc.cart.ram_dirty) { failed = 1; goto cleanup; }
    FILE *fp = fopen("sdl_feature.sav", "rb");
    if (!fp) { failed = 1; goto cleanup; }
    fseek(fp, 0x123, SEEK_SET);
    if (fgetc(fp) != 0x5A) { failed = 1; }
    fclose(fp);
    if (!shortcut(&gbc, SDLK_F5)) { failed = 1; goto cleanup; }
    gbc.cpu.pc = 0x1234;
    gbc_cart_write_ram(&gbc, 0xA123, 0x6B);
    if (!shortcut(&gbc, SDLK_F9) || gbc.cpu.pc != 0x100 ||
        gbc_cart_read_ram(&gbc, 0xA123) != 0x5A) { failed = 1; goto cleanup; }
    /* SDL restore hook must discard pre-load queued audio. */
#if (GBC_ENABLE_SOUND == 1)
    gbc.apu.sample_buffer[0] = 0;
    gbc_sound_output(gbc.apu.sample_buffer, 4);
    if (gbc_state_load_file(&gbc, "sdl_feature.state") != GBC_OK) { failed = 1; }
#endif
    SDL_Event event;
    SDL_zero(event); event.type = SDL_QUIT;
    SDL_PushEvent(&event); gbc_frame(&gbc);
    if (!gbc.gbc_quit || gbc.cart.rom_data) { failed = 1; }
cleanup:
    gbc_unload_rom_discard(&gbc);
    gbc_deinit_static(&gbc);
    remove("sdl_feature.sav"); remove("sdl_feature.state");
    printf("SDL storage event test: %s\n", failed ? "FAILED" : "passed");
    return failed;
}
