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
static int gbc_persist_load_ram(gbc_t *gbc)
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
static int gbc_persist_save_ram(gbc_t *gbc)
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

#if (GBC_ENABLE_RTC_SAVE == 1)
/** Versioned RTC sidecar, deliberately excluding any wall-clock timestamp. */
static int gbc_persist_rtc(gbc_t *gbc, uint8_t write)
{
    char path[GBC_SAVE_PATH_MAX], temp[GBC_SAVE_PATH_MAX];
    uint8_t bytes[32] = {0};
    gbc_cart_t *cart = &gbc->cart;
    FILE *fp;
    int ret = gbc_storage_path(path, cart->save_path, ".rtc");
    if (ret != GBC_OK || !cart->header.has_timer) {
        return ret;
    }
    if (write) {
        gbc_cart_rtc_sync(gbc);
        gbc_memcpy(bytes, "GRTC", 4);
        bytes[4] = 1;
        gbc_memcpy(bytes + 5, cart->rtc_regs, 5);
        gbc_memcpy(bytes + 10, cart->rtc_latched, 5);
        bytes[15] = cart->rtc_latch_prev;
        bytes[16] = (uint8_t)cart->rtc_ms;
        bytes[17] = (uint8_t)(cart->rtc_ms >> 8);
        uint32_t crc = ~gbc_storage_crc(0xFFFFFFFFUL, bytes, 28);
        for (uint8_t i = 0; i < 4; i++) {
            bytes[28 + i] = (uint8_t)(crc >> (i * 8));
        }
        ret = gbc_storage_path(temp, path, ".tmp");
        if (ret != GBC_OK) {
            return ret;
        }
        fp = gbc_fopen(temp, "wb");
        if (fp == NULL) {
            return GBC_ERROR_IO;
        }
        ret = gbc_fwrite(bytes, 1, sizeof(bytes), fp) == sizeof(bytes) ? GBC_OK : GBC_ERROR_IO;
        return gbc_storage_commit(fp, temp, path, ret);
    }
    int exists = gbc_file_exists(path);
    if (exists == 0) {
        return GBC_OK;
    }
    if (exists < 0 || (fp = gbc_fopen(path, "rb")) == NULL) {
        return GBC_ERROR_IO;
    }
    if (gbc_fseek(fp, 0, SEEK_END) != 0 || gbc_ftell(fp) != sizeof(bytes) ||
        gbc_fseek(fp, 0, SEEK_SET) != 0) {
        ret = GBC_ERROR_FORMAT;
    } else if (gbc_fread(bytes, 1, sizeof(bytes), fp) != sizeof(bytes)) {
        ret = GBC_ERROR_IO;
    }
    if (gbc_fclose(fp) != 0) {
        ret = GBC_ERROR_IO;
    }
    uint32_t stored = 0;
    for (uint8_t i = 0; i < 4; i++) {
        stored |= (uint32_t)bytes[28 + i] << (i * 8);
    }
    if (ret == GBC_OK && (gbc_memcmp(bytes, "GRTC", 4) != 0 || bytes[4] != 1 ||
        stored != ~gbc_storage_crc(0xFFFFFFFFUL, bytes, 28) || bytes[15] > 1 ||
        (uint16_t)(bytes[16] | (bytes[17] << 8)) >= 1000)) {
        ret = GBC_ERROR_FORMAT;
    }
    for (uint8_t offset = 5; offset <= 10 && ret == GBC_OK; offset += 5) {
        if (bytes[offset] > 59 || bytes[offset + 1] > 59 || bytes[offset + 2] > 23 ||
            (bytes[offset + 4] & 0x3E) != 0) {
            ret = GBC_ERROR_FORMAT;
        }
    }
    if (ret == GBC_OK) {
        gbc_memcpy(cart->rtc_regs, bytes + 5, 5);
        gbc_memcpy(cart->rtc_latched, bytes + 10, 5);
        cart->rtc_latch_prev = bytes[15];
        cart->rtc_ms = (uint16_t)(bytes[16] | (bytes[17] << 8));
    } else {
        gbc_memset(cart->rtc_regs, 0, 5);
        gbc_memset(cart->rtc_latched, 0, 5);
        cart->rtc_ms = 0;
        cart->rtc_latch_prev = 0;
    }
    cart->rtc_last_ticks = gbc_get_ticks();
    cart->rtc_started = 1;
    return ret;
}
#endif

int gbc_persist_load(gbc_t *gbc)
{
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    int ret = gbc_persist_load_ram(gbc);
#if (GBC_ENABLE_RTC_SAVE == 1)
    if (gbc->cart.header.has_battery && gbc->cart.save_path[0]) {
        int rtc_ret = gbc_persist_rtc(gbc, 0);
        if (ret == GBC_OK) {
            ret = rtc_ret;
        }
    }
#endif
    gbc->cart.persist_blocked = ret != GBC_OK;
    return ret;
}

int gbc_persist_save(gbc_t *gbc)
{
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    int ret = gbc_persist_save_ram(gbc);
#if (GBC_ENABLE_RTC_SAVE == 1)
    if (!gbc->cart.persist_blocked && gbc->cart.header.has_battery && gbc->cart.save_path[0]) {
        int rtc_ret = gbc_persist_rtc(gbc, 1);
        if (ret == GBC_OK) {
            ret = rtc_ret;
        }
    }
#endif
    return ret;
}
#endif
