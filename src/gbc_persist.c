/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "gbc_storage.h"

#if (GBC_PERSIST_ENABLE)
static uint8_t *gbc_persist_ram(gbc_t *gbc, uint32_t *size)
{
    if (gbc->cart.header.mbc == GBC_MBC2) {
        *size = sizeof(gbc->cart.mbc2_ram);
        return gbc->cart.mbc2_ram;
    }
    *size = gbc->cart.ram_size;
    return gbc->cart.ram_data;
}

int gbc_persist_set_path(gbc_t *gbc, const char *base_path)
{
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    if (base_path == NULL) {
        gbc->cart.save_path[0] = 0;
        gbc->cart.persist_blocked = 0;
        return GBC_OK;
    }
    if (strlen(base_path) + 16 >= sizeof(gbc->cart.save_path)) {
        return GBC_ERROR_PATH;
    }
    gbc_memcpy(gbc->cart.save_path, base_path, strlen(base_path) + 1);
    /* Binding a different path must not accidentally authorize overwriting a
     * sidecar rejected by a previous load. Explicit detach/discard is required. */
    return GBC_OK;
}

/** Read the legacy raw RAM layout only after validating its exact length. */
int gbc_persist_load(gbc_t *gbc)
{
    uint32_t size;
    uint8_t *ram;
    char path[GBC_SAVE_PATH_MAX];
    FILE *fp;
    int ret = GBC_OK;
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    if (!gbc->cart.header.has_battery || !gbc->cart.save_path[0]) {
        return GBC_OK;
    }
#if (GBC_ENABLE_SRAM_SAVE == 1)
    ram = gbc_persist_ram(gbc, &size);
    if (ram != NULL && size != 0) {
        ret = gbc_storage_path(path, gbc->cart.save_path, ".sav");
        if (ret != GBC_OK) {
            return ret;
        }
        int exists = gbc_file_exists(path);
        if (exists < 0) {
            ret = GBC_ERROR_IO;
        } else if (exists) {
            fp = gbc_fopen(path, "rb");
            if (fp == NULL) {
                ret = GBC_ERROR_IO;
            } else {
                if (gbc_fseek(fp, 0, SEEK_END) != 0 || gbc_ftell(fp) != (long)size ||
                    gbc_fseek(fp, 0, SEEK_SET) != 0) {
                    ret = GBC_ERROR_FORMAT;
                } else if (gbc_fread(ram, 1, size, fp) != size) {
                    /* A failed read may have modified a prefix; discard it. */
                    gbc_memset(ram, gbc->cart.header.mbc == GBC_MBC2 ? 0x0F : 0xFF, size);
                    ret = GBC_ERROR_IO;
                }
                if (gbc_fclose(fp) != 0) {
                    ret = GBC_ERROR_IO;
                }
            }
        }
    }
#else
    (void)size; (void)ram; (void)path; (void)fp;
#endif
    if (ret == GBC_OK) {
        gbc->cart.ram_dirty = 0;
        gbc->cart.persist_blocked = 0;
    } else {
        gbc->cart.persist_blocked = 1;
    }
    return ret;
}

/** Save RAM in bounded writes; release the dirty flag only after replacement. */
int gbc_persist_save(gbc_t *gbc)
{
    uint32_t size;
    uint8_t *ram;
    char path[GBC_SAVE_PATH_MAX], temp[GBC_SAVE_PATH_MAX];
    FILE *fp;
    int ret;
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    if (gbc->cart.persist_blocked) {
        return GBC_ERROR_FORMAT;
    }
    if (!gbc->cart.header.has_battery || !gbc->cart.save_path[0]) {
        return GBC_OK;
    }
#if (GBC_ENABLE_SRAM_SAVE == 1)
    ram = gbc_persist_ram(gbc, &size);
    if (ram == NULL || size == 0 || !gbc->cart.ram_dirty) {
        return GBC_OK;
    }
    ret = gbc_storage_path(path, gbc->cart.save_path, ".sav");
    if (ret == GBC_OK) {
        ret = gbc_storage_path(temp, path, ".tmp");
    }
    if (ret != GBC_OK) {
        return ret;
    }
    fp = gbc_fopen(temp, "wb");
    if (fp == NULL) {
        return GBC_ERROR_IO;
    }
    ret = GBC_OK;
    for (uint32_t offset = 0; offset < size; offset += 256) {
        size_t count = size - offset < 256 ? size - offset : 256;
        if (gbc_fwrite(ram + offset, 1, count, fp) != count) {
            ret = GBC_ERROR_IO;
            break;
        }
    }
    ret = gbc_storage_commit(fp, temp, path, ret);
    if (ret == GBC_OK) {
        gbc->cart.ram_dirty = 0;
    }
    return ret;
#else
    (void)size; (void)ram; (void)path; (void)temp; (void)fp; (void)ret;
    return GBC_OK;
#endif
}
#endif
