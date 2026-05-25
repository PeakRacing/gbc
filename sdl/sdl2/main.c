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

int main(int argc, char **argv)
{
    gbc_t *gbc = gbc_init();
    int ret;

    if (gbc == NULL) {
        GBC_LOG_ERROR("gbc init failed\n");
        return -1;
    }

    if (argc != 2 || !gbc_has_rom_ext(argv[1])) {
        GBC_LOG_ERROR("Please enter the .gb or .gbc file path\n");
        gbc_deinit(gbc);
        return -1;
    }

    ret = gbc_load_file(gbc, argv[1]);
    if (ret != GBC_OK) {
        GBC_LOG_ERROR("gbc load file fail: %d\n", ret);
        gbc_deinit(gbc);
        return -1;
    }

    gbc_run(gbc);
    gbc_unload_file(gbc);
    gbc_deinit(gbc);
    return 0;
}

