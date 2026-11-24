#include "helix/fs/iasset_plugin.h"
#include "doom_wad.h"
#include "doom_map.h"
#include "doom_tex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define DOOM_EXPORT __declspec(dllexport)
#else
#define DOOM_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    hxAssetPlugin base;
    DoomWad* doom1;
    DoomWad* doom2;
} DoomPlugin;

static int dm_strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static char* dm_strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int parse_map_vpath(const char* name, char* out_lump, size_t cap, int* is_doom2) {
    if (!name || strncmp(name, "maps/", 5) != 0) return 0;
    const char* base = name + 5;
    size_t len = strlen(base);
    if (len < 7 || dm_strcasecmp(base + len - 6, ".hxmap") != 0) return 0;
    size_t nlen = len - 6;
    if (nlen == 0 || nlen >= cap) return 0;
    for (size_t i = 0; i < nlen; ++i) {
        out_lump[i] = (char)toupper((unsigned char)base[i]);
    }
    out_lump[nlen] = '\0';

    if (out_lump[0] == 'E' && isdigit((unsigned char)out_lump[1]) &&
        out_lump[2] == 'M' && isdigit((unsigned char)out_lump[3]) && nlen == 4) {
        *is_doom2 = 0;
        return 1;
    }
    if (strncmp(out_lump, "MAP", 3) == 0 && isdigit((unsigned char)out_lump[3]) &&
        isdigit((unsigned char)out_lump[4]) && nlen == 5) {
        *is_doom2 = 1;
        return 1;
    }
    return 0;
}

static void dm_close(hxAssetPlugin* self) {
    DoomPlugin* p = (DoomPlugin*)self;
    doom_wad_close(p->doom1); p->doom1 = NULL;
    doom_wad_close(p->doom2); p->doom2 = NULL;
}

static int dm_open_dir(hxAssetPlugin* self, const char* dir_path) {
    DoomPlugin* p = (DoomPlugin*)self;
    dm_close(self);
    if (!dir_path || !dir_path[0]) return 0;

    char path[1024];
    snprintf(path, sizeof(path), "%s/doom.wad", dir_path);
    p->doom1 = doom_wad_open(path);
    snprintf(path, sizeof(path), "%s/doom2.wad", dir_path);
    p->doom2 = doom_wad_open(path);

    if (!p->doom1) {
        snprintf(path, sizeof(path), "%s/DOOM.WAD", dir_path);
        p->doom1 = doom_wad_open(path);
    }
    if (!p->doom2) {
        snprintf(path, sizeof(path), "%s/doom2/DOOM2.WAD", dir_path);
        p->doom2 = doom_wad_open(path);
    }
    return (p->doom1 || p->doom2) ? 1 : 0;
}

static int dm_open(hxAssetPlugin* self, const char* path) {
    return dm_open_dir(self, path);
}

static int dm_open_many(hxAssetPlugin* self, const char** paths, int count) {
    if (!paths || count <= 0) return 0;
    return dm_open_dir(self, paths[count - 1]);
}

static int dm_ends_with(const char* s, const char* suffix) {
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    if (sl < su) return 0;
    return dm_strcasecmp(s + sl - su, suffix) == 0;
}

static int tex_name_for_tga(const char* name, char* out, size_t cap) {
    if (strncmp(name, "textures/", 9) != 0 && strncmp(name, "TEXTURES/", 9) != 0) return 0;
    const char* base = name + 9;
    size_t len = strlen(base);
    if (len == 0) return 0;
    if (dm_ends_with(base, ".tga")) len -= 4;
    else if (strchr(base, '.') != NULL) return 0;
    if (len == 0 || len >= cap) return 0;
    memcpy(out, base, len);
    out[len] = '\0';
    return 1;
}

static int dm_contains(hxAssetPlugin* self, const char* name) {
    DoomPlugin* p = (DoomPlugin*)self;
    if (!name || !name[0]) return 0;

    char lump[16];
    int is_d2 = 0;
    if (parse_map_vpath(name, lump, sizeof(lump), &is_d2)) {
        DoomWad* w = is_d2 ? p->doom2 : p->doom1;
        return (w && doom_wad_find_lump(w, lump, 0) >= 0) ? 1 : 0;
    }

    char tex_name[64];
    if (tex_name_for_tga(name, tex_name, sizeof(tex_name))) {
        return doom_tex_exists(p->doom1, p->doom2, tex_name);
    }
    return 0;
}

static int dm_read(hxAssetPlugin* self, const char* name,
                   unsigned char** out_buf, unsigned int* out_size) {
    DoomPlugin* p = (DoomPlugin*)self;
    if (!name || !out_buf || !out_size) return 0;

    char lump[16];
    int is_d2 = 0;
    if (parse_map_vpath(name, lump, sizeof(lump), &is_d2)) {
        DoomWad* w = is_d2 ? p->doom2 : p->doom1;
        if (!w) return 0;
        return doom_map_to_hxmap(w, lump, out_buf, out_size);
    }

    char tex_name[64];
    if (tex_name_for_tga(name, tex_name, sizeof(tex_name))) {
        return doom_tex_extract(p->doom1, p->doom2, tex_name, out_buf, out_size);
    }
    return 0;
}

static void dm_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void dm_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    DoomPlugin* p = (DoomPlugin*)self;
    if (!names || !count) return;
    char** out = (char**)malloc(128 * sizeof(char*));
    unsigned int n = 0;
    if (!out) { *names = NULL; *count = 0; return; }

    if (p->doom1) {
        for (int e = 1; e <= 4; ++e) {
            for (int m = 1; m <= 9; ++m) {
                char lump[16], vpath[32];
                snprintf(lump, sizeof(lump), "E%dM%d", e, m);
                if (doom_wad_find_lump(p->doom1, lump, 0) >= 0) {
                    snprintf(vpath, sizeof(vpath), "maps/e%dm%d.hxmap", e, m);
                    out[n++] = dm_strdup(vpath);
                }
            }
        }
    }
    if (p->doom2) {
        for (int m = 1; m <= 32; ++m) {
            char lump[16], vpath[32];
            snprintf(lump, sizeof(lump), "MAP%02d", m);
            if (doom_wad_find_lump(p->doom2, lump, 0) >= 0) {
                snprintf(vpath, sizeof(vpath), "maps/map%02d.hxmap", m);
                out[n++] = dm_strdup(vpath);
            }
        }
    }
    *names = out;
    *count = n;
}

static void dm_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    (void)self;
    if (!names) return;
    for (unsigned int i = 0; i < count; ++i) free(names[i]);
    free(names);
}

static void dm_destroy(hxAssetPlugin* self) {
    dm_close(self);
    free(self);
}

DOOM_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    DoomPlugin* p = (DoomPlugin*)calloc(1, sizeof(DoomPlugin));
    if (!p) return NULL;
    p->base.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->base.format_name = "doom";
    p->base.open = dm_open;
    p->base.contains = dm_contains;
    p->base.read = dm_read;
    p->base.free_buf = dm_free_buf;
    p->base.list = dm_list;
    p->base.free_list = dm_free_list;
    p->base.close = dm_close;
    p->base.destroy = dm_destroy;
    p->base.open_many = dm_open_many;
    p->base.open_dir = dm_open_dir;
    return &p->base;
}
