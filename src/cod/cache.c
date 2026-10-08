/*
 * Parsed-asset cache beside the plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/* Mixed into the sidecar so a translator change does not reuse an old body.
 * The .cache file itself is the translation, with no binary header. */
/* 23: tcMod transform on the deck flag, and perlight add is not the base blend. */
#define COD_CACHE_REV 23u

static unsigned int cache_key(unsigned int checksum) {
    return checksum ^ (0xC0D00000u + COD_CACHE_REV);
}

static int legacy_prefix(const unsigned char* p, unsigned int n) {
    return n >= 4 && p[0] == 'H' && p[1] == 'X' && p[2] == 'C' && p[3] == 'A';
}

void cod_cache_path(const char* dir, const char* key, char* out, size_t cap) {
    char safe[240];
    size_t i, n = 0;
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!dir || !key) return;
    for (i = 0; key[i] && n + 1 < sizeof(safe); i++) {
        unsigned char c = (unsigned char)key[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        safe[n++] = ok ? (char)c : '_';
    }
    safe[n] = 0;
    snprintf(out, cap, "%s/%s.cache", dir, safe);
}

static void crc_path(const char* cache_path, char* out, size_t cap) {
    snprintf(out, cap, "%s.crc", cache_path);
}

int cod_cache_load(const char* dir, const char* key, unsigned int checksum,
                   unsigned char** out, unsigned int* size) {
    char path[1024];
    char side[1100];
    char line[32];
    unsigned int stored = 0;
    unsigned int sz;
    unsigned char* buf;
    FILE* f;
    if (!dir || !key || !out || !size) return 0;
    *out = NULL;
    *size = 0;
    cod_cache_path(dir, key, path, sizeof(path));
    crc_path(path, side, sizeof(side));
    f = fopen(side, "rb");
    if (!f) return 0;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    stored = (unsigned int)strtoul(line, NULL, 16);
    if (stored != cache_key(checksum)) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = (unsigned int)ftell(f);
    if (sz == 0 || sz > 64u * 1024u * 1024u) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (unsigned char*)malloc(sz);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, sz, f) != sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    if (legacy_prefix(buf, sz)) {
        free(buf);
        return 0;
    }
    *out = buf;
    *size = sz;
    return 1;
}

void cod_cache_store(const char* dir, const char* key, unsigned int checksum,
                     const void* data, unsigned int size) {
    char path[1024];
    char side[1100];
    FILE* f;
    if (!dir || !key || !data || size == 0 || size > 64u * 1024u * 1024u) return;
    if (legacy_prefix((const unsigned char*)data, size)) return;
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
    cod_cache_path(dir, key, path, sizeof(path));
    crc_path(path, side, sizeof(side));
    f = fopen(path, "wb");
    if (!f) return;
    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        remove(path);
        return;
    }
    fclose(f);
    f = fopen(side, "wb");
    if (!f) {
        remove(path);
        return;
    }
    if (fprintf(f, "%08x\n", cache_key(checksum)) < 8) {
        fclose(f);
        remove(path);
        remove(side);
        return;
    }
    fclose(f);
}
