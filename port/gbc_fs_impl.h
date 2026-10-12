/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#pragma once
#if (GBC_STORAGE_ENABLE)
#include <errno.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
extern int fileno(FILE *stream);
#endif

GBC_WEAK int gbc_file_sync(FILE *stream)
{
    if (fflush(stream) != 0) {
        return -1;
    }
#if defined(_WIN32)
    return _commit(_fileno(stream));
#else
    return fsync(fileno(stream));
#endif
}

GBC_WEAK int gbc_file_replace(const char *temporary, const char *destination)
{
#if defined(_WIN32)
    return MoveFileExA(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(temporary, destination);
#endif
}

GBC_WEAK int gbc_file_remove(const char *path)
{
    return remove(path);
}

GBC_WEAK int gbc_file_exists(const char *path)
{
    struct stat info;
    if (stat(path, &info) == 0) {
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
}
#endif
