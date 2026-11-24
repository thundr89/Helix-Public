/*
 * hxfs_cod4 — stock CoD4 IWD mount for Helix.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Opens the retail main packs only: iw_NN.iwd, then localized_*_iwNN.iwd.
 * A later pack wins. Mods/ and any other .iwd name are ignored. A loose
 * IBSP 22 .d3dbsp under raw/ is a map. A zone fastfile whose name starts
 * with mp_ is a map too: its clipmap brushes are the geometry. Other
 * fastfiles supply raw files (GSC, cfg, menu). IWI bytes stay in the IWD
 * for the host decoder. Models stay on the host xmodel path.
 */

#include "helix/fs/iasset_plugin.h"
#include "cod4_bsp.h"
#include "cod4_ffmap.h"
#include "cod4_iwi_wavelet.h"
#include "ff_file.h"
#include "zip_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#include <windows.h>
#define hxfs_strcasecmp _stricmp
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#define hxfs_strcasecmp strcasecmp
#endif

typedef struct Cod4Pack {
    char* path;
    int group;
    int ver;
    ZipFile* zip;
} Cod4Pack;

typedef struct Cod4Zone {
    char* path;
    char* base;
    FfFile* ff;
    int pin;
    int tick;
} Cod4Zone;

typedef struct Cod4Bsp {
    char* path;
    char* vname;
    int from_ff;
} Cod4Bsp;

typedef struct Cod4Plugin {
    hxAssetPlugin api;
    Cod4Pack* packs;
    int count;
    int cap;
    Cod4Zone* zones;
    int zcount;
    int zcap;
    int tick;
    Cod4Bsp* bsps;
    int bcount;
    int bcap;
} Cod4Plugin;

static void cod4_close_zones(Cod4Plugin* p) {
    int i;
    if (!p) return;
    for (i = 0; i < p->zcount; i++) {
        ff_close(p->zones[i].ff);
        free(p->zones[i].path);
        free(p->zones[i].base);
    }
    free(p->zones);
    p->zones = NULL;
    p->zcount = 0;
    p->zcap = 0;
    p->tick = 0;
}

static void cod4_close_bsps(Cod4Plugin* p) {
    int i;
    if (!p) return;
    for (i = 0; i < p->bcount; i++) {
        free(p->bsps[i].path);
        free(p->bsps[i].vname);
    }
    free(p->bsps);
    p->bsps = NULL;
    p->bcount = 0;
    p->bcap = 0;
}

static void cod4_close_all(Cod4Plugin* p) {
    int i;
    if (!p) return;
    for (i = 0; i < p->count; i++) {
        zip_close(p->packs[i].zip);
        free(p->packs[i].path);
    }
    free(p->packs);
    p->packs = NULL;
    p->count = 0;
    p->cap = 0;
    cod4_close_zones(p);
    cod4_close_bsps(p);
}

/* maps/mp/mp_backlot.gsc and maps/createfx/mp_backlot_fx.gsc -> mp_backlot.ff.
 * Shared scripts (gametypes, ui) return 0 and are resolved from the common zones. */
int cod4_zone_for_name(const char* name, char* out, int cap) {
    const char* base = name;
    const char* dot;
    char stem[128];
    size_t n, i;
    if (!name || !out || cap < 8) return 0;
    for (i = 0; name[i]; i++) if (name[i] == '/' || name[i] == '\\') base = name + i + 1;
    dot = strrchr(base, '.');
    if (!dot || hxfs_strcasecmp(dot, ".gsc") != 0) return 0;
    n = (size_t)(dot - base);
    if (n < 4 || n >= sizeof(stem)) return 0;
    memcpy(stem, base, n);
    stem[n] = 0;
    for (i = 0; i < n; i++) if (stem[i] >= 'A' && stem[i] <= 'Z') stem[i] = (char)(stem[i] - 'A' + 'a');
    if (n > 3 && strcmp(stem + n - 3, "_fx") == 0) {
        n -= 3;
        stem[n] = 0;
    } else if (n > 4 && strcmp(stem + n - 4, "_art") == 0) {
        n -= 4;
        stem[n] = 0;
    }
    if (n < 4 || strncmp(stem, "mp_", 3) != 0) return 0;
    if ((int)n + 4 >= cap) return 0;
    memcpy(out, stem, n);
    memcpy(out + n, ".ff", 4);
    return 1;
}

static const char* base_name(const char* path) {
    const char* s = path;
    const char* b = path;
    if (!path) return "";
    for (s = path; *s; s++) {
        if (*s == '/' || *s == '\\') b = s + 1;
    }
    return b;
}

static int path_has_mods(const char* path) {
    const char* s = path;
    while (s && *s) {
        const char* e = s;
        char tok[16];
        int n = 0;
        while (*e && *e != '/' && *e != '\\') e++;
        while (s < e && n < (int)sizeof(tok) - 1) {
            char c = *s++;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            tok[n++] = c;
        }
        tok[n] = 0;
        if (strcmp(tok, "mods") == 0) return 1;
        s = *e ? e + 1 : e;
    }
    return 0;
}

static int all_digits(const char* s, int n) {
    int i;
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static int parse_stock(const char* base, int* group, int* ver) {
    size_t n;
    char name[260];
    size_t i;
    int v;
    if (!base || !group || !ver) return 0;
    n = strlen(base);
    if (n < 8 || n >= sizeof(name) || hxfs_strcasecmp(base + n - 4, ".iwd") != 0) return 0;
    n -= 4;
    for (i = 0; i < n; i++) {
        char c = base[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        name[i] = c;
    }
    name[n] = 0;
    if (n > 3 && name[0] == 'i' && name[1] == 'w' && name[2] == '_' && all_digits(name + 3, (int)n - 3)) {
        v = 0;
        for (i = 3; i < n; i++) v = v * 10 + (name[i] - '0');
        *group = 0;
        *ver = v;
        return 1;
    }
    if (n > 16 && strncmp(name, "localized_", 10) == 0) {
        const char* iw = NULL;
        const char* p;
        for (p = name; *p; p++) {
            if (p[0] == '_' && p[1] == 'i' && p[2] == 'w') iw = p;
        }
        if (!iw || iw == name + 9) return 0;
        if (!all_digits(iw + 3, (int)strlen(iw + 3))) return 0;
        v = 0;
        for (p = iw + 3; *p; p++) v = v * 10 + (*p - '0');
        *group = 1;
        *ver = v;
        return 1;
    }
    return 0;
}

static int is_dir(const char* path) {
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int add_pack(Cod4Plugin* p, const char* path, int group, int ver) {
    ZipFile* z;
    Cod4Pack* grown;
    if (p->count == p->cap) {
        int ncap = p->cap ? p->cap * 2 : 8;
        grown = (Cod4Pack*)realloc(p->packs, (size_t)ncap * sizeof(Cod4Pack));
        if (!grown) return 0;
        p->packs = grown;
        p->cap = ncap;
    }
    z = zip_open_file(path);
    if (!z) return 0;
    p->packs[p->count].path = (char*)malloc(strlen(path) + 1);
    if (!p->packs[p->count].path) {
        zip_close(z);
        return 0;
    }
    memcpy(p->packs[p->count].path, path, strlen(path) + 1);
    p->packs[p->count].group = group;
    p->packs[p->count].ver = ver;
    p->packs[p->count].zip = z;
    p->count++;
    return 1;
}

static int cmp_pack(const void* a, const void* b) {
    const Cod4Pack* pa = (const Cod4Pack*)a;
    const Cod4Pack* pb = (const Cod4Pack*)b;
    if (pa->group != pb->group) return pa->group < pb->group ? -1 : 1;
    if (pa->ver != pb->ver) return pa->ver < pb->ver ? -1 : 1;
    return strcmp(pa->path, pb->path);
}

static int collect_dir(const char* dir, Cod4Pack** out, int* count) {
    Cod4Pack* packs = NULL;
    int n = 0, cap = 0;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE find;
    char search[512];
    snprintf(search, sizeof(search), "%s/*", dir);
    find = FindFirstFileA(search, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        *out = NULL;
        *count = 0;
        return 1;
    }
    do {
        int group = 0, ver = 0;
        char path[512];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!parse_stock(fd.cFileName, &group, &ver)) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            packs = (Cod4Pack*)realloc(packs, (size_t)cap * sizeof(Cod4Pack));
            if (!packs) {
                FindClose(find);
                return 0;
            }
        }
        packs[n].path = (char*)malloc(strlen(path) + 1);
        if (!packs[n].path) {
            FindClose(find);
            return 0;
        }
        memcpy(packs[n].path, path, strlen(path) + 1);
        packs[n].group = group;
        packs[n].ver = ver;
        packs[n].zip = NULL;
        n++;
    } while (FindNextFileA(find, &fd));
    FindClose(find);
#else
    DIR* d = opendir(dir);
    struct dirent* ent;
    if (!d) {
        *out = NULL;
        *count = 0;
        return 1;
    }
    while ((ent = readdir(d)) != NULL) {
        int group = 0, ver = 0;
        char path[512];
        if (!parse_stock(ent->d_name, &group, &ver)) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            packs = (Cod4Pack*)realloc(packs, (size_t)cap * sizeof(Cod4Pack));
            if (!packs) {
                closedir(d);
                return 0;
            }
        }
        packs[n].path = (char*)malloc(strlen(path) + 1);
        if (!packs[n].path) {
            closedir(d);
            return 0;
        }
        memcpy(packs[n].path, path, strlen(path) + 1);
        packs[n].group = group;
        packs[n].ver = ver;
        packs[n].zip = NULL;
        n++;
    }
    closedir(d);
#endif
    *out = packs;
    *count = n;
    return 1;
}

static int add_zone(Cod4Plugin* p, const char* path) {
    const char* base = base_name(path);
    size_t n = strlen(base);
    Cod4Zone* grown;
    char* path_copy;
    char* base_copy;
    int i;
    if (n < 4 || hxfs_strcasecmp(base + n - 3, ".ff") != 0) return 0;
    if (n > 8 && hxfs_strcasecmp(base + n - 8, "_load.ff") == 0) return 0;
    for (i = 0; i < p->zcount; i++) if (hxfs_strcasecmp(p->zones[i].base, base) == 0) return 0;
    if (p->zcount == p->zcap) {
        int ncap = p->zcap ? p->zcap * 2 : 8;
        grown = (Cod4Zone*)realloc(p->zones, (size_t)ncap * sizeof(Cod4Zone));
        if (!grown) return 0;
        p->zones = grown;
        p->zcap = ncap;
    }
    path_copy = (char*)malloc(strlen(path) + 1);
    base_copy = (char*)malloc(n + 1);
    if (!path_copy || !base_copy) {
        free(path_copy);
        free(base_copy);
        return 0;
    }
    memcpy(path_copy, path, strlen(path) + 1);
    memcpy(base_copy, base, n + 1);
    p->zones[p->zcount].path = path_copy;
    p->zones[p->zcount].base = base_copy;
    p->zones[p->zcount].ff = NULL;
    p->zones[p->zcount].pin = 0;
    p->zones[p->zcount].tick = 0;
    p->zcount++;
    return 1;
}

static void scan_ff_dir(Cod4Plugin* p, const char* dir) {
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE find;
    char search[512];
    snprintf(search, sizeof(search), "%s/*", dir);
    find = FindFirstFileA(search, &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        char path[512];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0] == '.') continue;
            snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
            if (!path_has_mods(path)) scan_ff_dir(p, path);
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
        add_zone(p, path);
    } while (FindNextFileA(find, &fd));
    FindClose(find);
#else
    DIR* d = opendir(dir);
    struct dirent* ent;
    if (!d) return;
    while ((ent = readdir(d)) != NULL) {
        char path[512];
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (path_has_mods(path)) continue;
        if (is_dir(path)) scan_ff_dir(p, path);
        else add_zone(p, path);
    }
    closedir(d);
#endif
}

static void parent_dir(const char* path, char* out, size_t cap) {
    size_t n = path ? strlen(path) : 0;
    while (n && (path[n - 1] == '/' || path[n - 1] == '\\')) n--;
    while (n && path[n - 1] != '/' && path[n - 1] != '\\') n--;
    while (n && (path[n - 1] == '/' || path[n - 1] == '\\')) n--;
    if (n >= cap) n = cap ? cap - 1 : 0;
    if (n && path) memcpy(out, path, n);
    if (cap) out[n] = 0;
}

static void attach_zones(Cod4Plugin* p, const char* game_root) {
    char zone[512];
    if (!game_root || !game_root[0]) return;
    snprintf(zone, sizeof(zone), "%s/zone", game_root);
    if (!is_dir(zone) || path_has_mods(zone)) return;
    scan_ff_dir(p, zone);
}

/* raw/maps/mp/mp_test.d3dbsp -> maps/mp/mp_test.hxmap. A bare file name lands in maps/. */
static int bsp_virtual_name(const char* path, char* out, int cap) {
    const char* s = path;
    const char* hit = NULL;
    int n = 0;
    if (!path || cap < 16) return 0;
    for (; *s; s++) {
        char c0 = s[0], c1 = s[1], c2 = s[2], c3 = s[3];
        if (c0 >= 'A' && c0 <= 'Z') c0 = (char)(c0 - 'A' + 'a');
        if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 - 'A' + 'a');
        if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 - 'A' + 'a');
        if (c3 >= 'A' && c3 <= 'Z') c3 = (char)(c3 - 'A' + 'a');
        if ((s == path || s[-1] == '/' || s[-1] == '\\') && c0 == 'm' && c1 == 'a' && c2 == 'p' && c3 == 's' &&
            (s[4] == '/' || s[4] == '\\'))
            hit = s;
    }
    if (!hit) {
        const char* base = base_name(path);
        size_t bl = strlen(base);
        if (bl < 8 || hxfs_strcasecmp(base + bl - 7, ".d3dbsp") != 0) return 0;
        if ((int)bl + 2 >= cap) return 0;
        memcpy(out, "maps/", 5);
        n = 5;
        while (*base && *base != '.' && n + 1 < cap) {
            char c = *base++;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            out[n++] = c;
        }
        if (n + 7 >= cap) return 0;
        memcpy(out + n, ".hxmap", 7);
        return 1;
    }
    for (s = hit; *s && hxfs_strcasecmp(s, ".d3dbsp") != 0 && n + 8 < cap; s++) {
        char c = *s;
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[n++] = c;
    }
    if (hxfs_strcasecmp(s, ".d3dbsp") != 0 || n + 7 >= cap) return 0;
    memcpy(out + n, ".hxmap", 7);
    return 1;
}

static int add_named(Cod4Plugin* p, const char* path, const char* vname, int from_ff) {
    Cod4Bsp* grown;
    char* path_copy;
    char* name_copy;
    int i;
    size_t n;
    if (!path || !vname || !vname[0]) return 0;
    n = strlen(path);
    for (i = 0; i < p->bcount; i++) if (hxfs_strcasecmp(p->bsps[i].vname, vname) == 0) return 0;
    if (p->bcount == p->bcap) {
        int ncap = p->bcap ? p->bcap * 2 : 8;
        grown = (Cod4Bsp*)realloc(p->bsps, (size_t)ncap * sizeof(Cod4Bsp));
        if (!grown) return 0;
        p->bsps = grown;
        p->bcap = ncap;
    }
    path_copy = (char*)malloc(n + 1);
    name_copy = (char*)malloc(strlen(vname) + 1);
    if (!path_copy || !name_copy) {
        free(path_copy);
        free(name_copy);
        return 0;
    }
    memcpy(path_copy, path, n + 1);
    memcpy(name_copy, vname, strlen(vname) + 1);
    p->bsps[p->bcount].path = path_copy;
    p->bsps[p->bcount].vname = name_copy;
    p->bsps[p->bcount].from_ff = from_ff;
    p->bcount++;
    return 1;
}

static int add_bsp_named(Cod4Plugin* p, const char* path, const char* vname) {
    size_t n = path ? strlen(path) : 0;
    if (n < 8 || hxfs_strcasecmp(path + n - 7, ".d3dbsp") != 0) return 0;
    return add_named(p, path, vname, 0);
}

static int add_bsp(Cod4Plugin* p, const char* path) {
    char vname[256];
    if (!bsp_virtual_name(path, vname, (int)sizeof(vname))) return 0;
    if (!add_bsp_named(p, path, vname)) return 0;
    /* The menu loads "mp_test", which the host resolves as maps/mp_test.hxmap. */
    if (strncmp(vname, "maps/mp/", 8) == 0) {
        char alias[256];
        snprintf(alias, sizeof(alias), "maps/%s", vname + 8);
        add_bsp_named(p, path, alias);
    }
    return 1;
}

static void scan_bsp_dir(Cod4Plugin* p, const char* dir) {
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE find;
    char search[512];
    snprintf(search, sizeof(search), "%s/*", dir);
    find = FindFirstFileA(search, &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        char path[512];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0] == '.') continue;
            snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
            if (!path_has_mods(path)) scan_bsp_dir(p, path);
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
        add_bsp(p, path);
    } while (FindNextFileA(find, &fd));
    FindClose(find);
#else
    DIR* d = opendir(dir);
    struct dirent* ent;
    if (!d) return;
    while ((ent = readdir(d)) != NULL) {
        char path[512];
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (path_has_mods(path)) continue;
        if (is_dir(path)) scan_bsp_dir(p, path);
        else add_bsp(p, path);
    }
    closedir(d);
#endif
}

static void attach_ff_maps(Cod4Plugin* p) {
    int i;
    for (i = 0; i < p->zcount; i++) {
        const char* base = p->zones[i].base;
        size_t n;
        char stem[128];
        char vname[160];
        char alias[160];
        if (!base) continue;
        n = strlen(base);
        if (n < 7 || n >= sizeof(stem) + 3) continue;
        if ((base[0] != 'm' && base[0] != 'M') || (base[1] != 'p' && base[1] != 'P') || base[2] != '_') continue;
        if (hxfs_strcasecmp(base + n - 3, ".ff") != 0) continue;
        memcpy(stem, base, n - 3);
        stem[n - 3] = 0;
        snprintf(vname, sizeof(vname), "maps/mp/%s.hxmap", stem);
        snprintf(alias, sizeof(alias), "maps/%s.hxmap", stem);
        add_named(p, p->zones[i].path, vname, 1);
        add_named(p, p->zones[i].path, alias, 1);
    }
}

static void attach_bsps(Cod4Plugin* p, const char* game_root) {
    char raw[512];
    if (!game_root || !game_root[0]) return;
    snprintf(raw, sizeof(raw), "%s/raw", game_root);
    if (!is_dir(raw) || path_has_mods(raw)) return;
    scan_bsp_dir(p, raw);
}

static Cod4Bsp* find_bsp(Cod4Plugin* p, const char* name) {
    int i;
    if (!name) return NULL;
    for (i = 0; i < p->bcount; i++) if (hxfs_strcasecmp(p->bsps[i].vname, name) == 0) return &p->bsps[i];
    return NULL;
}

static int read_whole(const char* path, unsigned char** out, unsigned int* out_size) {
    FILE* f = fopen(path, "rb");
    long sz;
    unsigned char* buf;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 12 || sz > 256L * 1024L * 1024L) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = buf;
    *out_size = (unsigned int)sz;
    return 1;
}

static int raw_hidden(const char* name) {
    return !name || strcmp(name, "zone.bin") == 0 || strcmp(name, "ff.profile") == 0;
}

static Cod4Zone* zone_by_base(Cod4Plugin* p, const char* base) {
    int i;
    for (i = 0; i < p->zcount; i++) if (hxfs_strcasecmp(p->zones[i].base, base) == 0) return &p->zones[i];
    return NULL;
}

static void zone_evict(Cod4Plugin* p) {
    int i, open_n = 0, oldest = -1;
    for (i = 0; i < p->zcount; i++) if (p->zones[i].ff && !p->zones[i].pin) open_n++;
    while (open_n >= 3) {
        oldest = -1;
        for (i = 0; i < p->zcount; i++) {
            if (!p->zones[i].ff || p->zones[i].pin) continue;
            if (oldest < 0 || p->zones[i].tick < p->zones[oldest].tick) oldest = i;
        }
        if (oldest < 0) return;
        ff_close(p->zones[oldest].ff);
        p->zones[oldest].ff = NULL;
        open_n--;
    }
}

static FfFile* zone_file(Cod4Plugin* p, Cod4Zone* z) {
    if (!z) return NULL;
    if (!z->ff) {
        zone_evict(p);
        z->ff = ff_open_file(z->path);
    }
    if (!z->ff) return NULL;
    z->tick = ++p->tick;
    return z->ff;
}

static FfFile* find_raw(Cod4Plugin* p, const char* name) {
    /* ui_mp.ff contains compiled MenuList assets, not raw .menu text.
       Feeding it to the legacy RawFile scanner can misidentify MenuList headers. */
    static const char* shared[] = {
        "code_post_gfx_mp.ff", "common_mp.ff", "localized_code_post_gfx_mp.ff",
        "localized_common_mp.ff", "code_post_gfx.ff", "common.ff"
    };
    char base[128];
    int i;
    if (raw_hidden(name)) return NULL;
    if (cod4_zone_for_name(name, base, (int)sizeof(base))) {
        FfFile* f = zone_file(p, zone_by_base(p, base));
        if (f && ff_contains(f, name)) return f;
    }
    for (i = 0; i < (int)(sizeof(shared) / sizeof(shared[0])); i++) {
        Cod4Zone* z = zone_by_base(p, shared[i]);
        FfFile* f;
        if (!z) continue;
        if (i < 2) z->pin = 1;
        f = zone_file(p, z);
        if (f && ff_contains(f, name)) return f;
    }
    return NULL;
}

static int cod4_open_dir(hxAssetPlugin* self, const char* dir_path) {
    Cod4Plugin* p = (Cod4Plugin*)self;
    char main_dir[512];
    const char* scan;
    Cod4Pack* found = NULL;
    int n = 0, i, opened = 0;
    if (!p) return 0;
    cod4_close_all(p);
    if (!dir_path || !dir_path[0] || path_has_mods(dir_path)) return 0;
    snprintf(main_dir, sizeof(main_dir), "%s/main", dir_path);
    scan = is_dir(main_dir) ? main_dir : dir_path;
    if (path_has_mods(scan) || !collect_dir(scan, &found, &n)) {
        for (i = 0; i < n; i++) free(found[i].path);
        free(found);
        return 0;
    }
    if (n > 1) qsort(found, (size_t)n, sizeof(Cod4Pack), cmp_pack);
    for (i = 0; i < n; i++) {
        if (add_pack(p, found[i].path, found[i].group, found[i].ver)) opened++;
        free(found[i].path);
    }
    free(found);
    {
        char game_root[512];
        if (scan != dir_path) snprintf(game_root, sizeof(game_root), "%s", dir_path);
        else parent_dir(scan, game_root, sizeof(game_root));
        attach_zones(p, game_root);
        attach_ff_maps(p);
        attach_bsps(p, game_root);
    }
    if (opened == 0 && p->bcount == 0 && p->zcount == 0) return 0;
    return opened > 0 ? opened : 1;
}

static int cod4_open(hxAssetPlugin* self, const char* path) {
    Cod4Plugin* p = (Cod4Plugin*)self;
    int group = 0, ver = 0;
    if (!p) return 0;
    cod4_close_all(p);
    if (!path || !path[0] || path_has_mods(path)) return 0;
    if (is_dir(path)) return cod4_open_dir(self, path);
    {
        size_t n = strlen(path);
        if (n > 3 && hxfs_strcasecmp(path + n - 3, ".ff") == 0) return add_zone(p, path);
        if (n > 7 && hxfs_strcasecmp(path + n - 7, ".d3dbsp") == 0) return add_bsp(p, path);
    }
    if (!parse_stock(base_name(path), &group, &ver)) return 0;
    return add_pack(p, path, group, ver);
}

static int cod4_open_many(hxAssetPlugin* self, const char** paths, int count) {
    Cod4Plugin* p = (Cod4Plugin*)self;
    Cod4Pack* found = NULL;
    int n = 0, i, opened = 0;
    if (!p) return 0;
    cod4_close_all(p);
    if (!paths || count < 0) return 0;
    found = (Cod4Pack*)calloc((size_t)count, sizeof(Cod4Pack));
    if (!found && count) return 0;
    for (i = 0; i < count; i++) {
        int group = 0, ver = 0;
        if (!paths[i] || path_has_mods(paths[i])) continue;
        if (!parse_stock(base_name(paths[i]), &group, &ver)) continue;
        found[n].path = (char*)malloc(strlen(paths[i]) + 1);
        if (!found[n].path) continue;
        memcpy(found[n].path, paths[i], strlen(paths[i]) + 1);
        found[n].group = group;
        found[n].ver = ver;
        n++;
    }
    if (n > 1) qsort(found, (size_t)n, sizeof(Cod4Pack), cmp_pack);
    for (i = 0; i < n; i++) {
        if (add_pack(p, found[i].path, found[i].group, found[i].ver)) opened++;
        free(found[i].path);
    }
    free(found);
    return opened;
}

static int pack_contains(const Cod4Plugin* p, const char* name) {
    int i;
    for (i = p->count - 1; i >= 0; i--) {
        if (zip_contains(p->packs[i].zip, name)) return 1;
    }
    return 0;
}

static int pack_read(Cod4Plugin* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    int i;
    for (i = p->count - 1; i >= 0; i--) {
        if (zip_read(p->packs[i].zip, name, out_buf, out_size)) {
            if(*out_size>=28 && !memcmp(*out_buf,"IWi\6",4) && (*out_buf)[4]>=6 && (*out_buf)[4]<=10) {
                unsigned char* decoded=NULL;unsigned bytes=0;
                int ok=cod4_iwi_wavelet_to_tga(*out_buf,*out_size,&decoded,&bytes);
                free(*out_buf);*out_buf=decoded;*out_size=bytes;return ok;
            }
            return 1;
        }
    }
    return 0;
}

/* World materials ask for images/<name>_col.iwi. Some color maps omit the suffix. */
static int iwi_alias(const char* name, char* out, size_t cap) {
    size_t n, stem;
    if (!name || !out || cap < 16) return 0;
    n = strlen(name);
    if (n < 16 || n + 8 >= cap) return 0;
    if (strncmp(name, "images/", 7) != 0 || strcmp(name + n - 4, ".iwi") != 0) return 0;
    stem = n - 4;
    if (stem >= 5 && name[stem - 4] == '_' && name[stem - 3] == 'c' && name[stem - 2] == 'o' && name[stem - 1] == 'l' &&
        name[stem - 5] != '/') {
        memcpy(out, name, stem - 4);
        memcpy(out + stem - 4, ".iwi", 5);
        return 1;
    }
    memcpy(out, name, stem);
    memcpy(out + stem, "_col.iwi", 9);
    return 1;
}

static int iwi_candidate(const char* name, int which, char* out, size_t cap) {
    size_t n;
    if (which == 0) return iwi_alias(name, out, cap);
    if (!name || !out) return 0;
    n = strlen(name);
    if (n < 5 || n + 3 >= cap || strcmp(name + n - 4, ".iwi") != 0) return 0;
    if (which == 1) {
        memcpy(out, name, n - 4);
        memcpy(out + n - 4, "#0.iwi", 7);
        return 1;
    }
    if (which == 2) {
        char mid[256];
        if (!iwi_alias(name, mid, sizeof(mid))) return 0;
        return iwi_candidate(mid, 1, out, cap);
    }
    return 0;
}

/* Color maps live in the IWDs. A miss must not inflate the shared fastfiles. */
static int cod4_image_name(const char* name) {
    size_t n;
    const char* ext;
    if (!name) return 0;
    if (strncmp(name, "images/", 7) == 0) return 1;
    n = strlen(name);
    if (n < 4) return 0;
    ext = name + n - 4;
    return hxfs_strcasecmp(ext, ".iwi") == 0 || hxfs_strcasecmp(ext, ".dds") == 0 ||
           hxfs_strcasecmp(ext, ".png") == 0 || hxfs_strcasecmp(ext, ".tga") == 0 ||
           hxfs_strcasecmp(ext, ".jpg") == 0;
}

/* Explicit original-UI resource while dynamic IW3 UI support is completed.
 * Never send the compiled MenuList zone through the RawFile scanner. */
static int cod4_original_menu_available(Cod4Plugin* p) {
    return zone_by_base(p,"ui_mp.ff") && zone_by_base(p,"localized_code_post_gfx_mp.ff");
}
static int cod4_original_menu(Cod4Plugin* p,unsigned char** out,unsigned* size) {
    unsigned char *ui=NULL,*loc=NULL,*shared_data[2]={NULL,NULL};unsigned ui_n=0,loc_n=0,i,n=0;int ok=0;
    Cod4UiZone shared[2];
    const char* shared_names[2]={"code_post_gfx_mp.ff","common_mp.ff"};
    Cod4Zone *u=zone_by_base(p,"ui_mp.ff"),*l=zone_by_base(p,"localized_code_post_gfx_mp.ff");
    *out=NULL;*size=0;
    if(!u || !l || !read_whole(u->path,&ui,&ui_n) || !read_whole(l->path,&loc,&loc_n))goto done;
    for(i=0;i<2;i++) {
        Cod4Zone* z=zone_by_base(p,shared_names[i]);unsigned bytes;
        if(!z)continue;
        if(!read_whole(z->path,&shared_data[n],&bytes))goto done;
        shared[n].data=shared_data[n];shared[n].size=bytes;n++;
    }
    ok=cod4_ff_to_menu_bundle(ui,ui_n,loc,loc_n,shared,n,out,size);
done:
    for(i=0;i<2;i++)free(shared_data[i]);free(ui);free(loc);return ok;
}
static int cod4_contains(hxAssetPlugin* self, const char* name) {
    Cod4Plugin* p = (Cod4Plugin*)self;
    char alt[256];
    if (!p || !name) return 0;
    if (!strcmp(name,"ui_mp/cod4_original.hxmenu")) return cod4_original_menu_available(p);
    if (find_bsp(p, name)) return 1;
    if (pack_contains(p, name)) return 1;
    {
        int which;
        for (which = 0; which < 3; which++) {
            if (iwi_candidate(name, which, alt, sizeof(alt)) && pack_contains(p, alt)) return 1;
        }
    }
    if (cod4_image_name(name)) return 0;
    return find_raw(p, name) != NULL;
}

static int cod4_read(hxAssetPlugin* self, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    Cod4Plugin* p = (Cod4Plugin*)self;
    if (!p || !name || !out_buf || !out_size) return 0;
    if (!strcmp(name,"ui_mp/cod4_original.hxmenu")) return cod4_original_menu(p,out_buf,out_size);
    {
        Cod4Bsp* bsp = find_bsp(p, name);
        unsigned char* raw = NULL;
        unsigned int raw_n = 0;
        if (bsp) {
            int ok;
            if (!read_whole(bsp->path, &raw, &raw_n)) return 0;
            ok = bsp->from_ff ? cod4_ff_to_hxmap(raw, raw_n, bsp->vname, out_buf, out_size)
                              : cod4_bsp_to_hxmap(raw, raw_n, bsp->vname, out_buf, out_size);
            free(raw);
            return ok;
        }
    }
    if (pack_read(p, name, out_buf, out_size)) return 1;
    {
        char alt[256];
        int which;
        for (which = 0; which < 3; which++) {
            if (iwi_candidate(name, which, alt, sizeof(alt)) && pack_read(p, alt, out_buf, out_size)) return 1;
        }
    }
    if (!cod4_image_name(name)) {
        FfFile* f = find_raw(p, name);
        if (f && ff_read(f, name, out_buf, out_size)) return 1;
    }
    return 0;
}

static void cod4_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void cod4_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    Cod4Plugin* p = (Cod4Plugin*)self;
    char** all = NULL;
    unsigned int n = 0, cap = 0, j;
    int i;
    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p) return;
    for (i = p->count - 1; i >= 0; i--) {
        char** part = NULL;
        unsigned int pn = 0;
        zip_list(p->packs[i].zip, &part, &pn);
        for (j = 0; j < pn; j++) {
            unsigned int k;
            int found = 0;
            for (k = 0; k < n; k++) {
                if (hxfs_strcasecmp(all[k], part[j]) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                size_t len = strlen(part[j]);
                char* copy;
                if (n == cap) {
                    cap = cap ? cap * 2 : 16;
                    all = (char**)realloc(all, cap * sizeof(char*));
                }
                copy = (char*)malloc(len + 1);
                if (copy) {
                    memcpy(copy, part[j], len + 1);
                    all[n++] = copy;
                }
            }
        }
        zip_free_list(part, pn);
    }
    for (i = 0; i < p->bcount; i++) {
        size_t len = strlen(p->bsps[i].vname);
        char* copy;
        if (n == cap) {
            char** grown;
            cap = cap ? cap * 2 : 16;
            grown = (char**)realloc(all, cap * sizeof(char*));
            if (!grown) break;
            all = grown;
        }
        copy = (char*)malloc(len + 1);
        if (!copy) break;
        memcpy(copy, p->bsps[i].vname, len + 1);
        all[n++] = copy;
    }
    *names = all;
    *count = n;
}

static void cod4_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    unsigned int i;
    (void)self;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}

static void cod4_close(hxAssetPlugin* self) { cod4_close_all((Cod4Plugin*)self); }

static void cod4_destroy(hxAssetPlugin* self) {
    Cod4Plugin* p = (Cod4Plugin*)self;
    if (!p) return;
    cod4_close_all(p);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    Cod4Plugin* p = (Cod4Plugin*)calloc(1, sizeof(Cod4Plugin));
    if (!p) return NULL;
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "cod4";
    p->api.open = cod4_open;
    p->api.contains = cod4_contains;
    p->api.read = cod4_read;
    p->api.free_buf = cod4_free_buf;
    p->api.list = cod4_list;
    p->api.free_list = cod4_free_list;
    p->api.close = cod4_close;
    p->api.open_many = cod4_open_many;
    p->api.open_dir = cod4_open_dir;
    p->api.destroy = cod4_destroy;
    return &p->api;
}
