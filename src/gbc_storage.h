/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#pragma once
#include "gbc.h"

#if (GBC_STORAGE_ENABLE)
/** Build a sidecar path without truncating or modifying its destination on error. */
static inline int gbc_storage_path(char *out, const char *base, const char *suffix)
{
    size_t a = strlen(base), b = strlen(suffix);
    if (a + b >= GBC_SAVE_PATH_MAX) {
        return GBC_ERROR_PATH;
    }
    gbc_memcpy(out, base, a);
    gbc_memcpy(out + a, suffix, b + 1);
    return GBC_OK;
}

/** Finalize an independent file transaction. Never remove the destination first. */
static inline int gbc_storage_commit(FILE *fp, const char *temp, const char *path, int ret)
{
    if (ret == GBC_OK && gbc_file_sync(fp) != 0) {
        ret = GBC_ERROR_IO;
    }
    if (gbc_fclose(fp) != 0) {
        ret = GBC_ERROR_IO;
    }
    if (ret == GBC_OK && gbc_file_replace(temp, path) != 0) {
        ret = GBC_ERROR_IO;
    }
    if (ret != GBC_OK) {
        gbc_file_remove(temp);
    }
    return ret;
}

static inline uint32_t gbc_storage_crc(uint32_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc;
}
#endif
