/*
 * hxfs_cod — modular IAssetPlugin (archive, bsp, xmodel, xanim).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Separate from src/cod2003, which is not developed further. format_name is
 * "cod" and the DLL is hxfs_cod. The game's gameinfo loads this plugin.
 */

#include "iasset_plugin.h"
#include "archive/archive.h"
#include "bsp/names.h"
#include "bsp/read.h"
#include "bsp/helix.h"
#include "xmodel/helix.h"
#include "xanim/helix.h"
#include "sound/helix.h"
#include "ui/helix.h"
#include "gsc/helix.h"
#include "texture/helix.h"
#include "shader/helix.h"
#include "cache.h"
#include "emit.h"
#include "cod_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#endif

#ifdef _WIN32
#include <windows.h>
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif
#else
#include <dirent.h>
#endif

typedef struct CodPlugin {
    hxAssetPlugin api;
    CodArchive* archive;
    unsigned loc_crc;
} CodPlugin;

static int cmp_str(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

/* pak10 is newer than pak9, and pakb (hex 11) is newer than pak9.
 * Same stem, higher version wins because the archive reads the last zip first. */
static void pak_key(const char* path, char* prefix, size_t cap, int* ver) {
    const char* base = path;
    const char* s;
    size_t n, i, p;
    *ver = -1;
    if (cap) prefix[0] = '\0';
    if (!path || !cap) return;
    for (s = path; *s; s++) {
        if (*s == '/' || *s == '\\') base = s + 1;
    }
    n = strlen(base);
    if (n > 4 && cod_strcasecmp(base + n - 4, ".pk3") == 0) n -= 4;
    i = n;
    while (i > 0 && base[i - 1] >= '0' && base[i - 1] <= '9') i--;
    if (i < n) {
        int v = 0;
        size_t k;
        for (k = i; k < n; k++) v = v * 10 + (base[k] - '0');
        *ver = v;
        n = i;
    } else if (n >= 4) {
        char a = base[n - 4], b = base[n - 3], c = base[n - 2], d = base[n - 1];
        int hex = -1;
        if ((a == 'p' || a == 'P') && (b == 'a' || b == 'A') && (c == 'k' || c == 'K')) {
            if (d >= 'a' && d <= 'f') hex = 10 + (d - 'a');
            else if (d >= 'A' && d <= 'F') hex = 10 + (d - 'A');
        }
        if (hex >= 0) {
            *ver = hex;
            n -= 1;
        }
    }
    if (n >= cap) n = cap - 1;
    for (p = 0; p < n; p++) {
        char ch = base[p];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        prefix[p] = ch;
    }
    prefix[p] = '\0';
}

static int cmp_pak(const void* a, const void* b) {
    char pa[260], pb[260];
    int va = -1, vb = -1, c;
    pak_key(*(const char* const*)a, pa, sizeof(pa), &va);
    pak_key(*(const char* const*)b, pb, sizeof(pb), &vb);
    c = strcmp(pa, pb);
    if (c) return c;
    if (va != vb) return (va < vb) ? -1 : 1;
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static int read_file(const char* path, unsigned char** out, unsigned int* out_size) {
    FILE* f;
    long sz;
    unsigned char* data;
    if (!path || !out || !out_size) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    data = (unsigned char*)malloc((size_t)sz ? (size_t)sz : 1);
    if (!data) {
        fclose(f);
        return 0;
    }
    if (sz && fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = data;
    *out_size = (unsigned int)sz;
    return 1;
}

static int add_path(CodPlugin* p, const char* path) {
    size_t len;
    if (!p || !p->archive || !path || !path[0]) return 0;
    len = strlen(path);
    if (len >= 4 && cod_strcasecmp(path + len - 4, ".bsp") == 0) {
        unsigned char* data = NULL;
        unsigned int sz = 0;
        const char* base;
        char vpath[256];
        int ok;
        if (!read_file(path, &data, &sz) || !bsp_read_valid(data, sz)) {
            free(data);
            return 0;
        }
        base = strrchr(path, '/');
        if (!base) base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        /* mp_*.bsp is a multiplayer map. Other loose bsp files stay under maps/ and are not listed. */
        if (strncmp(base, "mp_", 3) == 0)
            snprintf(vpath, sizeof(vpath), "maps/mp/%s", base);
        else
            snprintf(vpath, sizeof(vpath), "maps/%s", base);
        ok = cod_archive_add_blob(p->archive, vpath, data, sz);
        free(data);
        return ok;
    }
    return cod_archive_add_zip(p->archive, path);
}

static int cod_open(hxAssetPlugin* self, const char* path) {
    CodPlugin* p = (CodPlugin*)self;
    if (!p || !p->archive) return 0;
    cod_archive_clear(p->archive);
    return add_path(p, path);
}

static int cod_open_many(hxAssetPlugin* self, const char** paths, int count) {
    CodPlugin* p = (CodPlugin*)self;
    int i, opened = 0;
    if (!p || !p->archive) return 0;
    cod_archive_clear(p->archive);
    if (!paths) return 0;
    for (i = 0; i < count; i++) {
        if (add_path(p, paths[i])) opened++;
    }
    return opened;
}

static int cod_open_dir(hxAssetPlugin* self, const char* dir_path) {
    CodPlugin* p = (CodPlugin*)self;
    char** paths = NULL;
    int pcount = 0, pcap = 0, i, opened = 0;
    if (!p || !p->archive) return 0;
    cod_archive_clear(p->archive);
    if (!dir_path || !dir_path[0]) return 0;

#ifdef _WIN32
    {
        WIN32_FIND_DATAA fd;
        HANDLE hFind;
        char search[1024];
        snprintf(search, sizeof(search), "%s/*.pk3", dir_path);
        hFind = FindFirstFileA(search, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                char* slot;
                char** grown;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (pcount == pcap) {
                    pcap = pcap ? pcap * 2 : 16;
                    grown = (char**)realloc(paths, (size_t)pcap * sizeof(char*));
                    if (!grown) break;
                    paths = grown;
                }
                slot = (char*)malloc(strlen(dir_path) + strlen(fd.cFileName) + 2);
                if (!slot) break;
                sprintf(slot, "%s/%s", dir_path, fd.cFileName);
                paths[pcount++] = slot;
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }
#else
    {
        DIR* dir = opendir(dir_path);
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                size_t nlen = strlen(ent->d_name);
                if (nlen >= 4 && cod_strcasecmp(ent->d_name + nlen - 4, ".pk3") == 0) {
                    char* slot;
                    char** grown;
                    if (pcount == pcap) {
                        pcap = pcap ? pcap * 2 : 16;
                        grown = (char**)realloc(paths, (size_t)pcap * sizeof(char*));
                        if (!grown) break;
                        paths = grown;
                    }
                    slot = (char*)malloc(strlen(dir_path) + nlen + 2);
                    if (!slot) break;
                    sprintf(slot, "%s/%s", dir_path, ent->d_name);
                    paths[pcount++] = slot;
                }
            }
            closedir(dir);
        }
    }
#endif

    if (pcount > 1) qsort(paths, (size_t)pcount, sizeof(char*), cmp_pak);
    for (i = 0; i < pcount; i++) {
        if (add_path(p, paths[i])) opened++;
        free(paths[i]);
    }
    free(paths);
    return opened;
}

static int starts_ci(const char* s, const char* prefix);

static int ends_ci(const char* s, const char* ext) {
    size_t n, e;
    if (!s || !ext) return 0;
    n = strlen(s);
    e = strlen(ext);
    if (n < e) return 0;
    return cod_strcasecmp(s + n - e, ext) == 0;
}

/* Replaces the extension. Returns 0 when `name` already ends with `ext`. */
static int with_ext(const char* name, const char* ext, char* out, size_t cap) {
    const char* slash;
    const char* base;
    const char* dot;
    size_t stem, elen;
    if (!name || !ext || !out || cap == 0) return 0;
    slash = strrchr(name, '/');
    base = slash ? slash + 1 : name;
    dot = strrchr(base, '.');
    stem = dot ? (size_t)(dot - name) : strlen(name);
    elen = strlen(ext);
    if (stem + elen + 1 > cap) return 0;
    memcpy(out, name, stem);
    memcpy(out + stem, ext, elen + 1);
    return strcmp(out, name) != 0;
}

static int texture_alt_exists(const CodArchive* ar, const char* name) {
    char alt[512];
    if (with_ext(name, ".tga", alt, sizeof(alt)) && cod_archive_contains(ar, alt)) return 1;
    if (with_ext(name, ".dds", alt, sizeof(alt)) && cod_archive_contains(ar, alt)) return 1;
    if (with_ext(name, ".jpg", alt, sizeof(alt)) && cod_archive_contains(ar, alt)) return 1;
    return 0;
}

static int texture_source(const CodArchive* ar, const char* name, unsigned char** raw, unsigned int* rsz) {
    char alt[512];
    if (cod_archive_read(ar, name, raw, rsz)) return 1;
    if (with_ext(name, ".tga", alt, sizeof(alt)) && cod_archive_read(ar, alt, raw, rsz)) return 1;
    if (with_ext(name, ".dds", alt, sizeof(alt)) && cod_archive_read(ar, alt, raw, rsz)) return 1;
    if (with_ext(name, ".jpg", alt, sizeof(alt)) && cod_archive_read(ar, alt, raw, rsz)) return 1;
    return 0;
}

static int texture_checksum(const CodArchive* ar, const char* name, unsigned int* crc) {
    char alt[512];
    if (cod_archive_checksum(ar, name, crc)) return 1;
    if (with_ext(name, ".tga", alt, sizeof(alt)) && cod_archive_checksum(ar, alt, crc)) return 1;
    if (with_ext(name, ".dds", alt, sizeof(alt)) && cod_archive_checksum(ar, alt, crc)) return 1;
    if (with_ext(name, ".jpg", alt, sizeof(alt)) && cod_archive_checksum(ar, alt, crc)) return 1;
    return 0;
}

static int is_dds(const unsigned char* p, unsigned n) {
    return n >= 4 && p[0] == 'D' && p[1] == 'D' && p[2] == 'S' && p[3] == ' ';
}

static int cache_dir(char* out, size_t cap) {
    const char* env = getenv("HXFS_CACHE_DIR");
    if (env && env[0]) {
        snprintf(out, cap, "%s", env);
        return 1;
    }
#ifdef HXFS_COD_EXPORTS
#ifdef _WIN32
    {
        HMODULE mod = NULL;
        char path[MAX_PATH];
        char* slash;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)(void*)cache_dir, &mod))
            return 0;
        if (!GetModuleFileNameA(mod, path, MAX_PATH)) return 0;
        slash = strrchr(path, '\\');
        if (!slash) slash = strrchr(path, '/');
        if (!slash) return 0;
        *slash = 0;
        snprintf(out, cap, "%s/cache", path);
        return 1;
    }
#else
    snprintf(out, cap, "cache");
    return 1;
#endif
#else
    (void)out;
    (void)cap;
    return 0;
#endif
}

static int cache_try(const char* key, unsigned crc, unsigned char** out, unsigned* sz) {
    char dir[512];
    if (!key || !out || !sz || !cache_dir(dir, sizeof(dir))) return 0;
    return cod_cache_load(dir, key, crc, out, sz);
}

static void cache_save(const char* key, unsigned crc, const unsigned char* data, unsigned sz) {
    char dir[512];
    if (!key || !data || sz == 0 || !cache_dir(dir, sizeof(dir))) return;
    cod_cache_store(dir, key, crc, data, sz);
}

static int shared_popup_menu(const char* name);

static int menus_txt_to_helix(const CodArchive* ar, const void* data, size_t size,
                              unsigned char** out_buf, unsigned int* out_size) {
    const char* text = (const char*)data;
    CodEmit e;
    size_t i = 0;
    if (!data || !cod_emit_init(&e)) return 0;
    if (!cod_emit_add(&e, "{\n")) {
        cod_emit_free(&e);
        return 0;
    }
    while (i < size) {
        if (text[i] == '"') {
            char path[256];
            size_t k = 0;
            i++;
            while (i < size && text[i] != '"' && k + 1 < sizeof(path)) path[k++] = text[i++];
            path[k] = '\0';
            if (i < size && text[i] == '"') i++;
            if (ends_ci(path, ".menu")) {
                /* MP menus, shared options, and the popups menus.txt names. */
                if (strncmp(path, "ui_mp/", 6) != 0 && strncmp(path, "ui/options_", 11) != 0 &&
                    !shared_popup_menu(path))
                    continue;
                if (ar && !cod_archive_contains(ar, path)) continue;
                if (!cod_emit_fmt(&e, "    loadMenu { \"%s\" }\n", path)) {
                    cod_emit_free(&e);
                    return 0;
                }
            }
        } else {
            i++;
        }
    }
    if (!cod_emit_add(&e, "}\n") || !cod_emit_take(&e, out_buf, out_size)) {
        cod_emit_free(&e);
        return 0;
    }
    return 1;
}

/* maps/mp/<map>.gsc names the allied body. SP maps/<map>.gsc is not a fallback. */
static void note_map_body(const CodArchive* ar, const char* mname) {
    char path[256];
    unsigned char* raw = NULL;
    unsigned int rsz = 0;
    GscFile file;
    char body[160];
    if (!ar || !mname || !mname[0]) {
        cod_set_player_body(NULL);
        return;
    }
    snprintf(path, sizeof(path), "maps/mp/%s.gsc", mname);
    if (!cod_archive_read(ar, path, &raw, &rsz)) {
        cod_set_player_body(NULL);
        return;
    }
    if (!gsc_read(raw, rsz, &file)) {
        free(raw);
        cod_set_player_body(NULL);
        return;
    }
    free(raw);
    body[0] = '\0';
    if (gsc_allied_body(&file, body, sizeof(body)) && cod_archive_contains(ar, body)) {
        gsc_read_free(&file);
        cod_set_player_body(body);
        return;
    }
    gsc_read_free(&file);
    cod_set_player_body(NULL);
}

static int sp_sound_table(const char* name) {
    if (!name || !starts_ci(name, "soundaliases/")) return 0;
    return strstr(name, "campaign") != NULL || strstr(name, "singleplayer") != NULL ||
           strstr(name, "/sp_") != NULL || strstr(name, "\\sp_") != NULL ||
           strstr(name, "soundaliases/sp") != NULL;
}

/* Popups the multiplayer menus.txt loads from ui/. Not campaign menus. */
static int shared_popup_menu(const char* name) {
    return name && (strcmp(name, "ui/quit.menu") == 0 || strcmp(name, "ui/error.menu") == 0 ||
                    strcmp(name, "ui/snd_restart.menu") == 0 ||
                    strcmp(name, "ui/language_restart.menu") == 0 ||
                    strcmp(name, "ui/rec_restart.menu") == 0);
}

static int sp_menu(const char* name) {
    size_t n;
    if (!name) return 0;
    n = strlen(name);
    if (n < 5 || !ends_ci(name, ".menu")) return 0;
    if (strncmp(name, "ui_mp/", 6) == 0) return 0;
    if (strncmp(name, "ui/options_", 11) == 0) return 0;
    if (shared_popup_menu(name)) return 0;
    return 1;
}

static int sp_gsc(const char* name) {
    size_t n;
    if (!name) return 0;
    n = strlen(name);
    if (n < 4 || !ends_ci(name, ".gsc")) return 0;
    if (starts_ci(name, "rawgsc/")) return 0;
    return strncmp(name, "maps/mp/", 8) != 0;
}

static int menu_music_alias(const char* name) {
    return name && (strcmp(name, "music/bgm.mp3") == 0 || strcmp(name, "sound/music/bgm.mp3") == 0 ||
                    strcmp(name, "sound/bgm.mp3") == 0);
}

/* MP menu music. Prefer a sound/music file whose name says menu. SP campaign tracks are skipped. */
static int pick_menu_music(const CodArchive* ar, char* out, size_t cap) {
    char** names = NULL;
    unsigned int count = 0, i;
    const char* fallback = NULL;
    if (!ar || !out || cap < 8) return 0;
    out[0] = '\0';
    cod_archive_list(ar, &names, &count);
    for (i = 0; i < count; i++) {
        const char* n = names[i];
        int music;
        int sample;
        if (!n) continue;
        music = starts_ci(n, "sound/music/");
        sample = ends_ci(n, ".mp3") || ends_ci(n, ".wav");
        if (!music || !sample) continue;
        if (strstr(n, "campaign") || strstr(n, "singleplayer") || strstr(n, "/sp_")) continue;
        if (strstr(n, "menu")) {
            snprintf(out, cap, "%s", n);
            fallback = out;
            break;
        }
        if (!fallback) fallback = n;
    }
    if (out[0] == '\0' && fallback) snprintf(out, cap, "%s", fallback);
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
    return out[0] != '\0';
}

static const char* k_weapon_rows[] = {
    "m1garand_mp xmodel/viewmodel_m1garand xmodel/weapon_m1garand weap_m1garand_fire 40 400 0 1 0 8 2200",
    "thompson_mp xmodel/viewmodel_thompson xmodel/weapon_thompson weap_thompson_fire 20 80 1 1 0.4 30 1800",
    "bar_mp xmodel/viewmodel_BAR xmodel/weapon_bar weap_bar_fire 25 100 1 1 0.2 20 2000",
    "springfield_mp xmodel/viewmodel_springfield xmodel/weapon_springfield weap_springfield_fire 80 1200 0 1 0 5 2500",
    "colt_mp xmodel/viewmodel_colt xmodel/weapon_colt weap_colt_fire 18 300 0 1 0.5 7 1600",
    "kar98k_mp xmodel/viewmodel_kar98k xmodel/weapon_kar98k weap_kar98k_fire 80 1200 0 1 0 5 2500",
    "mp40_mp xmodel/viewmodel_mp40 xmodel/weapon_mp40 weap_mp40_fire 20 90 1 1 0.5 32 1800",
    "mp44_mp xmodel/viewmodel_mp44 xmodel/weapon_mp44 weap_mp44_fire 22 100 1 1 0.3 30 2000",
    "luger_mp xmodel/viewmodel_luger xmodel/weapon_luger weap_luger_fire 18 300 0 1 0.5 8 1600",
};

static int weapons_text(const CodArchive* ar, unsigned char** out_buf, unsigned int* out_size) {
    CodEmit e;
    int i;
    if (!ar || !cod_emit_init(&e)) return 0;
    for (i = 0; i < (int)(sizeof(k_weapon_rows) / sizeof(k_weapon_rows[0])); i++) {
        char view[96];
        char world[96];
        if (sscanf(k_weapon_rows[i], "%*s %95s %95s", view, world) != 2) continue;
        if (!cod_archive_contains(ar, view) && !cod_archive_contains(ar, world)) continue;
        if (!cod_emit_fmt(&e, "%s\n", k_weapon_rows[i])) {
            cod_emit_free(&e);
            return 0;
        }
    }
    if (!cod_emit_take(&e, out_buf, out_size)) {
        cod_emit_free(&e);
        return 0;
    }
    return 1;
}

static int cod_contains(hxAssetPlugin* self, const char* name) {
    CodPlugin* p = (CodPlugin*)self;
    char cands[3][256];
    int n, i;
    if (!p || !p->archive || !name) return 0;
    if (sp_sound_table(name) || sp_menu(name) || sp_gsc(name)) return 0;
    if (strcmp(name, "cod/weapons.txt") == 0) return 1;
    if (strcmp(name, "cod/viewhand.txt") == 0) {
        unsigned char* buf = NULL;
        unsigned sz = 0;
        int ok = cod_viewhand_file(p->archive, &buf, &sz);
        free(buf);
        return ok;
    }
    if (menu_music_alias(name)) {
        char picked[256];
        if (cod_archive_contains(p->archive, name)) return 1;
        return pick_menu_music(p->archive, picked, sizeof(picked));
    }
    if (starts_ci(name, "rawgsc/")) return cod_archive_contains(p->archive, name + 7);
    {
        const char* xm = cod_gameplay_xmodel(name);
        if (xm && cod_archive_contains(p->archive, xm)) return 1;
    }
    n = bsp_candidate_paths(name, cands, 3, NULL, 0);
    for (i = 0; i < n; i++) {
        if (cod_archive_contains(p->archive, cands[i])) return 1;
    }
    if (texture_alt_exists(p->archive, name)) return 1;
    return cod_archive_contains(p->archive, name);
}

static int starts_ci(const char* s, const char* prefix) {
    while (*prefix) {
        unsigned char a = (unsigned char)*s++;
        unsigned char b = (unsigned char)*prefix++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

static int menu_include(void* user, const char* path,
                         unsigned char** data, unsigned int* size) {
    const CodArchive* ar = (const CodArchive*)user;
    if (!ar || !path || !data || !size) return 0;
    return cod_archive_read(ar, path, data, size);
}

static int g_loc_loaded = 0;

static void ensure_menu_loc(CodPlugin* p) {
    char** names = NULL;
    unsigned int count = 0, i;
    const CodArchive* ar;
    if (g_loc_loaded || !p || !p->archive) return;
    ar = p->archive;
    g_loc_loaded = 1;
    cod_archive_list(ar, &names, &count);
    for (i = 0; i < count; i++) {
        const char* n = names[i];
        const char* base;
        const char* dot;
        char pkg[64];
        size_t plen;
        unsigned char* raw = NULL;
        unsigned int rsz = 0;
        unsigned crc = 0;
        if (!n || !strstr(n, "localizedstrings/") || !ends_ci(n, ".str")) continue;
        base = strrchr(n, '/');
        if (!base) base = strrchr(n, '\\');
        base = base ? base + 1 : n;
        dot = strrchr(base, '.');
        plen = dot ? (size_t)(dot - base) : strlen(base);
        if (plen == 0 || plen >= sizeof(pkg)) continue;
        memcpy(pkg, base, plen);
        pkg[plen] = '\0';
        if (cod_archive_checksum(ar, n, &crc)) p->loc_crc ^= crc;
        if (cod_archive_read(ar, n, &raw, &rsz)) ui_loc_add_str(pkg, raw, rsz);
        free(raw);
    }
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}

static int cod_read(hxAssetPlugin* self, const char* name,
                    unsigned char** out_buf, unsigned int* out_size) {
    CodPlugin* p = (CodPlugin*)self;
    size_t len;
    unsigned char* raw = NULL;
    unsigned int rsz = 0;
    if (!p || !p->archive || !name || !out_buf || !out_size) return 0;
    if (sp_sound_table(name) || sp_menu(name) || sp_gsc(name)) return 0;
    if (strcmp(name, "cod/weapons.txt") == 0) return weapons_text(p->archive, out_buf, out_size);
    if (strcmp(name, "cod/viewhand.txt") == 0)
        return cod_viewhand_file(p->archive, out_buf, out_size);
    if (menu_music_alias(name) && !cod_archive_contains(p->archive, name)) {
        char picked[256];
        if (!pick_menu_music(p->archive, picked, sizeof(picked))) return 0;
        return cod_archive_read(p->archive, picked, out_buf, out_size);
    }
    if (starts_ci(name, "rawgsc/")) return cod_archive_read(p->archive, name + 7, out_buf, out_size);
    {
        const char* xm = cod_gameplay_xmodel(name);
        if (xm) name = xm;
    }
    len = strlen(name);

    if (strncmp(name, "xmodel/", 7) == 0 && name[7] &&
        !strchr(name + 7, '/') && !strchr(name + 7, '\\')) {
        unsigned crc = 0;
        int have = cod_archive_checksum(p->archive, name, &crc);
        if (have && cache_try(name, crc, out_buf, out_size)) return 1;
        if (!xmodel_to_helix(p->archive, name, out_buf, out_size)) return 0;
        if (have) cache_save(name, crc, *out_buf, *out_size);
        return 1;
    }
    if (strncmp(name, "xanim/", 6) == 0 && name[6] &&
        !strchr(name + 6, '/') && !strchr(name + 6, '\\')) {
        unsigned crc = 0;
        int have = cod_archive_checksum(p->archive, name, &crc);
        if (have && cache_try(name, crc, out_buf, out_size)) return 1;
        if (!xanim_to_helix(p->archive, name, out_buf, out_size)) return 0;
        if (have) cache_save(name, crc, *out_buf, *out_size);
        return 1;
    }
    if (len >= 6 && cod_strcasecmp(name + len - 6, ".hxmap") == 0) {
        char cands[3][256];
        char mname[128];
        int n, i;
        mname[0] = '\0';
        n = bsp_candidate_paths(name, cands, 3, mname, sizeof(mname));
        for (i = 0; i < n; i++) {
            unsigned crc = 0;
            unsigned char* bsp = NULL;
            unsigned int bsz = 0;
            if (!cod_archive_checksum(p->archive, cands[i], &crc)) continue;
            if (cache_try(name, crc, out_buf, out_size)) {
                note_map_body(p->archive, mname);
                return 1;
            }
            if (!cod_archive_read(p->archive, cands[i], &bsp, &bsz)) continue;
            if (bsp_to_helix(bsp, bsz, mname, out_buf, out_size)) {
                free(bsp);
                cache_save(name, crc, *out_buf, *out_size);
                note_map_body(p->archive, mname);
                return 1;
            }
            free(bsp);
        }
    }
    if (len >= 5 && cod_strcasecmp(name + len - 5, ".menu") == 0) {
        unsigned crc = 0;
        int ok = 0;
        int have;
        ensure_menu_loc(p);
        have = cod_archive_checksum(p->archive, name, &crc);
        if (have) crc ^= p->loc_crc;
        if (have && cache_try(name, crc, out_buf, out_size)) return 1;
        if (!cod_archive_read(p->archive, name, &raw, &rsz)) return 0;
        ui_menu_set_include(menu_include, (void*)p->archive);
        ok = ui_menu_to_helix(raw, rsz, out_buf, out_size);
        free(raw);
        if (ok && have) cache_save(name, crc, *out_buf, *out_size);
        return ok;
    }
    if (starts_ci(name, "soundaliases/") && len >= 4 && cod_strcasecmp(name + len - 4, ".csv") == 0) {
        int ok = 0;
        if (!cod_archive_read(p->archive, name, &raw, &rsz)) return 0;
        ok = sound_csv_to_helix(raw, rsz, out_buf, out_size);
        free(raw);
        if (ok && strstr(name, "iw_sound")) sound_append_gameplay(p->archive, out_buf, out_size);
        return ok;
    }
    if (len >= 4 && cod_strcasecmp(name + len - 4, ".gsc") == 0) {
        int ok = 0;
        if (!cod_archive_read(p->archive, name, &raw, &rsz)) return 0;
        ok = gsc_source_to_helix(raw, rsz, out_buf, out_size);
        free(raw);
        return ok;
    }
    if (cod_strcasecmp(name, "ui_mp/menus.txt") == 0) {
        int ok = 0;
        if (!cod_archive_read(p->archive, name, &raw, &rsz)) return 0;
        ok = menus_txt_to_helix(p->archive, raw, rsz, out_buf, out_size);
        free(raw);
        return ok;
    }
    if (ends_ci(name, ".shader")) {
        unsigned crc = 0;
        int ok = 0;
        int have = cod_archive_checksum(p->archive, name, &crc);
        if (have && cache_try(name, crc, out_buf, out_size)) return 1;
        if (!cod_archive_read(p->archive, name, &raw, &rsz)) return 0;
        ok = shader_source_to_helix(raw, rsz, out_buf, out_size);
        free(raw);
        if (ok && have) cache_save(name, crc, *out_buf, *out_size);
        return ok;
    }
    if (ends_ci(name, ".png") || ends_ci(name, ".tga") || ends_ci(name, ".dds") ||
        texture_alt_exists(p->archive, name)) {
        unsigned crc = 0;
        int ok = 0;
        int have = texture_checksum(p->archive, name, &crc);
        if (!texture_source(p->archive, name, &raw, &rsz)) return 0;
        /* stb_image reads PNG and TGA. DDS still goes through the converter. */
        if (!is_dds(raw, rsz)) {
            *out_buf = raw;
            *out_size = rsz;
            return 1;
        }
        if (have && cache_try(name, crc, out_buf, out_size)) {
            free(raw);
            return 1;
        }
        ok = texture_to_png(raw, rsz, out_buf, out_size);
        free(raw);
        if (ok && have) cache_save(name, crc, *out_buf, *out_size);
        return ok;
    }
    return cod_archive_read(p->archive, name, out_buf, out_size);
}

static void cod_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void cod_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    CodPlugin* p = (CodPlugin*)self;
    char** items = NULL;
    unsigned int total = 0, cap = 0, unique = 0, j;
    char** src = NULL;
    unsigned int scount = 0;

    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p || !p->archive) return;

    cod_archive_list(p->archive, &src, &scount);
    for (j = 0; j < scount; j++) {
        char alias[256];
        char* s;
        if (total + 2 >= cap) {
            unsigned int ncap = cap ? cap * 2 : 32;
            char** grown = (char**)realloc(items, ncap * sizeof(char*));
            if (!grown) goto fail;
            items = grown;
            cap = ncap;
        }
        s = cod_strdup(src[j]);
        if (!s) goto fail;
        items[total++] = s;
        if (bsp_hxmap_alias(src[j], alias, sizeof(alias))) {
            s = cod_strdup(alias);
            if (!s) goto fail;
            items[total++] = s;
        }
    }
    for (j = 0; j < scount; j++) free(src[j]);
    free(src);
    src = NULL;

    if (total == 0) return;
    qsort(items, total, sizeof(char*), cmp_str);
    for (j = 0; j < total; j++) {
        if (j > 0 && strcmp(items[j], items[unique - 1]) == 0) {
            free(items[j]);
        } else {
            items[unique++] = items[j];
        }
    }
    *names = items;
    *count = unique;
    return;

fail:
    for (j = 0; j < total; j++) free(items[j]);
    free(items);
    if (src) {
        for (j = 0; j < scount; j++) free(src[j]);
        free(src);
    }
}

static void cod_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    unsigned int i;
    (void)self;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}

static void cod_close(hxAssetPlugin* self) {
    CodPlugin* p = (CodPlugin*)self;
    if (p && p->archive) cod_archive_clear(p->archive);
}

static void cod_destroy(hxAssetPlugin* self) {
    CodPlugin* p = (CodPlugin*)self;
    if (!p) return;
    cod_archive_destroy(p->archive);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    CodPlugin* p = (CodPlugin*)calloc(1, sizeof(CodPlugin));
    if (!p) return NULL;
    p->archive = cod_archive_create();
    if (!p->archive) {
        free(p);
        return NULL;
    }
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "cod";
    p->api.open = cod_open;
    p->api.contains = cod_contains;
    p->api.read = cod_read;
    p->api.free_buf = cod_free_buf;
    p->api.list = cod_list;
    p->api.free_list = cod_free_list;
    p->api.close = cod_close;
    p->api.open_many = cod_open_many;
    p->api.open_dir = cod_open_dir;
    p->api.destroy = cod_destroy;
    return &p->api;
}
