/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "gbc.h"

static int gbc_has_rom_ext(const char *path)
{
    size_t len;

    if (path == NULL) {
        return 0;
    }
    len = strlen(path);
    if (len < 3) {
        return 0;
    }
    if (len >= 4 && (gbc_memcmp(path + len - 4, ".gbc", 4) == 0 || gbc_memcmp(path + len - 4, ".GBC", 4) == 0)) {
        return 1;
    }
    return gbc_memcmp(path + len - 3, ".gb", 3) == 0 || gbc_memcmp(path + len - 3, ".GB", 3) == 0;
}

#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
int gbc_sdl_set_path(const char *base);
#endif

int main(int argc, char **argv)
{
    gbc_t *gbc = gbc_init();
    int ret;
    const char *rom_path = NULL;
#if (GBC_ENABLE_BOOT_ROM == 1)
    const char *boot_path = NULL;
    uint8_t *boot_image = NULL;
#endif

    if (gbc == NULL) {
        GBC_LOG_ERROR("gbc init failed\n");
        return -1;
    }

    for (int i = 1; i < argc; i++) {
#if (GBC_ENABLE_BOOT_ROM == 1)
        if (strcmp(argv[i], "--boot-rom") == 0 && i + 1 < argc && !boot_path) { boot_path = argv[++i]; continue; }
#endif
        if (!rom_path && gbc_has_rom_ext(argv[i])) { rom_path = argv[i]; }
        else { rom_path = NULL; break; }
    }
    if (!rom_path) {
        GBC_LOG_ERROR("Please enter the .gb or .gbc file path\n");
        gbc_deinit(gbc);
        return -1;
    }

#if (GBC_ENABLE_BOOT_ROM == 1)
    if (boot_path) {
        FILE *fp = gbc_fopen(boot_path, "rb");
        long size = -1;
        if (fp && gbc_fseek(fp, 0, SEEK_END) == 0) { size = gbc_ftell(fp); }
        ret = GBC_ERROR_FORMAT;
        if (size == 256 || size == 2048 || size == 2304) {
            boot_image = gbc_malloc((int)size);
            if (boot_image && gbc_fseek(fp, 0, SEEK_SET) == 0 && gbc_fread(boot_image, 1, (size_t)size, fp) == (size_t)size) {
                ret = gbc_boot_set_rom(gbc, boot_image, (uint32_t)size, size == 256 ? GBC_MODEL_DMG : GBC_MODEL_CGB);
            }
        }
        if (fp && gbc_fclose(fp) != 0) { ret = GBC_ERROR_IO; }
        if (ret != GBC_OK) {
            GBC_LOG_ERROR("invalid/unreadable boot ROM: %d\n", ret);
            gbc_deinit(gbc); gbc_free(boot_image); return -1;
        }
    }
#endif
    ret = gbc_load_file(gbc, rom_path);
    if (ret != GBC_OK) {
        GBC_LOG_ERROR("gbc load file fail: %d\n", ret);
        gbc_unload_rom_discard(gbc);
        gbc_deinit(gbc);
#if (GBC_ENABLE_BOOT_ROM == 1)
        gbc_free(boot_image);
#endif
        return -1;
    }

#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
    if (gbc_sdl_set_path(rom_path) != GBC_OK) {
        GBC_LOG_WARN("state path too long; state shortcuts disabled\n");
    }
#endif
    gbc_run(gbc);
    gbc_unload_file(gbc);
    gbc_deinit(gbc);
#if (GBC_ENABLE_BOOT_ROM == 1)
    gbc_free(boot_image);
#endif
    return 0;
}

