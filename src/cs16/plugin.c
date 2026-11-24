#include "helix/fs/iasset_plugin.h"
#include "cs16_bsp.h"
#include "cs16_wad.h"
#include "cs16_mdl.h"
#include "../wad/wad_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CS16_EXPORT __declspec(dllexport)
#else
#include <dirent.h>
#include <sys/stat.h>
#define CS16_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    char* vpath;
    char* disk_path;
} LooseEntry;

typedef struct {
    char clean_name[64];
    int is_masked;
    char* bsp_disk_path;
} EmbeddedTex;

typedef struct {
    char clean_name[64];
    int tex_index;
    int is_masked;
    char* mdl_disk_path;
} ModelTex;

typedef struct {
    hxAssetPlugin base;
    WadFile** wads;
    int wad_count;
    int wad_cap;
    LooseEntry* files;
    int file_count;
    int file_cap;
    EmbeddedTex* embedded_texs;
    int embedded_tex_count;
    int embedded_tex_cap;
    ModelTex* model_texs;
    int model_tex_count;
    int model_tex_cap;
    int is_cstrike;
} Cs16Plugin;

static int cs_strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int cs_ends_with(const char* s, const char* suffix) {
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    if (sl < su) return 0;
    return cs_strcasecmp(s + sl - su, suffix) == 0;
}

static char* cs_strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void add_wad(Cs16Plugin* p, const char* path) {
    WadFile* w = wad_open_file(path);
    if (!w) return;
    if (p->wad_count == p->wad_cap) {
        int nc = p->wad_cap ? p->wad_cap * 2 : 16;
        WadFile** nw = (WadFile**)realloc(p->wads, (size_t)nc * sizeof(WadFile*));
        if (!nw) { wad_close(w); return; }
        p->wads = nw;
        p->wad_cap = nc;
    }
    p->wads[p->wad_count++] = w;
}

static void add_loose_file(Cs16Plugin* p, const char* vpath, const char* disk_path) {
    for (int i = 0; i < p->file_count; ++i) {
        if (cs_strcasecmp(p->files[i].vpath, vpath) == 0) {
            free(p->files[i].disk_path);
            p->files[i].disk_path = cs_strdup(disk_path);
            return;
        }
    }
    if (p->file_count == p->file_cap) {
        int nc = p->file_cap ? p->file_cap * 2 : 256;
        LooseEntry* nf = (LooseEntry*)realloc(p->files, (size_t)nc * sizeof(LooseEntry));
        if (!nf) return;
        p->files = nf;
        p->file_cap = nc;
    }
    p->files[p->file_count].vpath = cs_strdup(vpath);
    p->files[p->file_count].disk_path = cs_strdup(disk_path);
    p->file_count++;
}

static int is_hl_map_name(const char* name) {
    const char* base = strrchr(name, '/');
    if (!base) base = strrchr(name, '\\');
    base = base ? base + 1 : name;

    /* HL singleplayer maps: c0a0, c1a0, c2a1, t0a0, etc. */
    if ((base[0] == 'c' || base[0] == 't') && isdigit((unsigned char)base[1]) && base[2] == 'a') return 1;
    /* HL multiplayer/hazard maps */
    if (cs_strcasecmp(base, "boot_camp.bsp") == 0 || cs_strcasecmp(base, "boot_camp.hxmap") == 0 ||
        cs_strcasecmp(base, "crossfire.bsp") == 0 || cs_strcasecmp(base, "crossfire.hxmap") == 0 ||
        cs_strcasecmp(base, "bounce.bsp") == 0 || cs_strcasecmp(base, "bounce.hxmap") == 0 ||
        cs_strcasecmp(base, "datacore.bsp") == 0 || cs_strcasecmp(base, "datacore.hxmap") == 0 ||
        cs_strcasecmp(base, "frenzy.bsp") == 0 || cs_strcasecmp(base, "frenzy.hxmap") == 0 ||
        cs_strcasecmp(base, "hazard.bsp") == 0 || cs_strcasecmp(base, "hazard.hxmap") == 0 ||
        cs_strcasecmp(base, "lambda_bunker.bsp") == 0 || cs_strcasecmp(base, "lambda_bunker.hxmap") == 0 ||
        cs_strcasecmp(base, "snark_pit.bsp") == 0 || cs_strcasecmp(base, "snark_pit.hxmap") == 0 ||
        cs_strcasecmp(base, "stalkyard.bsp") == 0 || cs_strcasecmp(base, "stalkyard.hxmap") == 0 ||
        cs_strcasecmp(base, "subtransit.bsp") == 0 || cs_strcasecmp(base, "subtransit.hxmap") == 0 ||
        cs_strcasecmp(base, "undertow.bsp") == 0 || cs_strcasecmp(base, "undertow.hxmap") == 0) {
        return 1;
    }
    return 0;
}

static void index_bsp_embedded_textures(Cs16Plugin* p, const char* bsp_disk_path) {
    FILE* f = fopen(bsp_disk_path, "rb");
    if (!f) return;
    int version = 0;
    if (fread(&version, 1, 4, f) != 4 || version != 30) {
        fclose(f);
        return;
    }
    /* LUMP_TEXTURES is index 2 */
    if (fseek(f, 4 + 2 * 8, SEEK_SET) != 0) { fclose(f); return; }
    int lump_ofs = 0, lump_len = 0;
    if (fread(&lump_ofs, 1, 4, f) != 4 || fread(&lump_len, 1, 4, f) != 4) { fclose(f); return; }
    if (lump_ofs <= 0 || lump_len < 4) { fclose(f); return; }

    if (fseek(f, lump_ofs, SEEK_SET) != 0) { fclose(f); return; }
    int count = 0;
    if (fread(&count, 1, 4, f) != 4 || count <= 0 || count > 4096) { fclose(f); return; }

    int* offsets = (int*)malloc((size_t)count * sizeof(int));
    if (!offsets) { fclose(f); return; }
    if (fread(offsets, sizeof(int), (size_t)count, f) != (size_t)count) {
        free(offsets);
        fclose(f);
        return;
    }

    for (int i = 0; i < count; ++i) {
        if (offsets[i] <= 0 || offsets[i] + 40 > lump_len) continue;
        if (fseek(f, lump_ofs + offsets[i], SEEK_SET) != 0) continue;
        char name[16];
        unsigned int width, height, mofs[4];
        if (fread(name, 1, 16, f) != 16) continue;
        if (fread(&width, 1, 4, f) != 4) continue;
        if (fread(&height, 1, 4, f) != 4) continue;
        if (fread(mofs, sizeof(unsigned int), 4, f) != 4) continue;
        if (mofs[0] == 0) continue; /* Not embedded in BSP */

        char raw[17];
        memcpy(raw, name, 16);
        raw[16] = '\0';
        char clean[64];
        size_t j = 0;
        for (size_t k = 0; raw[k] && j + 1 < sizeof(clean); ++k) {
            unsigned char uc = (unsigned char)raw[k];
            char c = (char)tolower(uc);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
            clean[j++] = c;
        }
        clean[j] = '\0';

        /* Check if already indexed */
        int exists = 0;
        for (int e = 0; e < p->embedded_tex_count; ++e) {
            if (strcmp(p->embedded_texs[e].clean_name, clean) == 0) {
                exists = 1;
                break;
            }
        }
        if (exists) continue;

        if (p->embedded_tex_count == p->embedded_tex_cap) {
            int nc = p->embedded_tex_cap ? p->embedded_tex_cap * 2 : 64;
            EmbeddedTex* ne = (EmbeddedTex*)realloc(p->embedded_texs, (size_t)nc * sizeof(EmbeddedTex));
            if (!ne) break;
            p->embedded_texs = ne;
            p->embedded_tex_cap = nc;
        }
        snprintf(p->embedded_texs[p->embedded_tex_count].clean_name,
                 sizeof(p->embedded_texs[p->embedded_tex_count].clean_name), "%s", clean);
        p->embedded_texs[p->embedded_tex_count].is_masked = (raw[0] == '{' || clean[0] == '_') ? 1 : 0;
        p->embedded_texs[p->embedded_tex_count].bsp_disk_path = cs_strdup(bsp_disk_path);
        p->embedded_tex_count++;
    }

    free(offsets);
    fclose(f);
}

static void index_mdl_textures(Cs16Plugin* p, const char* mdl_disk_path) {
    FILE* f = fopen(mdl_disk_path, "rb");
    if (!f) return;
    int id = 0, version = 0;
    if (fread(&id, 1, 4, f) != 4 || fread(&version, 1, 4, f) != 4) {
        fclose(f);
        return;
    }
    if (id != 0x54534449 || version != 10) { /* "IDST", version 10 */
        fclose(f);
        return;
    }
    if (fseek(f, 180, SEEK_SET) != 0) { fclose(f); return; }
    int numtex = 0, texidx = 0;
    if (fread(&numtex, 1, 4, f) != 4 || fread(&texidx, 1, 4, f) != 4) { fclose(f); return; }
    if (numtex <= 0 || numtex > 512 || texidx <= 0) { fclose(f); return; }

    for (int i = 0; i < numtex; ++i) {
        if (fseek(f, texidx + i * 80, SEEK_SET) != 0) break;
        char name[64];
        if (fread(name, 1, 64, f) != 64) break;
        name[63] = '\0';
        int flags = 0, width = 0, height = 0, pix_idx = 0;
        if (fread(&flags, 1, 4, f) != 4) break;
        if (fread(&width, 1, 4, f) != 4) break;
        if (fread(&height, 1, 4, f) != 4) break;
        if (fread(&pix_idx, 1, 4, f) != 4) break;
        if (width <= 0 || height <= 0 || pix_idx <= 0) continue;

        char clean[64];
        size_t j = 0;
        for (size_t k = 0; name[k] && j + 1 < sizeof(clean); ++k) {
            unsigned char uc = (unsigned char)name[k];
            char c = (char)tolower(uc);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
            clean[j++] = c;
        }
        clean[j] = '\0';
        size_t clen = strlen(clean);
        if (clen > 4 && strcmp(clean + clen - 4, "_bmp") == 0) clean[clen - 4] = '\0';
        else if (clen > 4 && strcmp(clean + clen - 4, "_tga") == 0) clean[clen - 4] = '\0';
        if (!clean[0]) continue;

        int exists = 0;
        for (int e = 0; e < p->model_tex_count; ++e) {
            if (strcmp(p->model_texs[e].clean_name, clean) == 0) {
                exists = 1;
                break;
            }
        }
        if (exists) continue;

        if (p->model_tex_count == p->model_tex_cap) {
            int nc = p->model_tex_cap ? p->model_tex_cap * 2 : 64;
            ModelTex* ne = (ModelTex*)realloc(p->model_texs, (size_t)nc * sizeof(ModelTex));
            if (!ne) break;
            p->model_texs = ne;
            p->model_tex_cap = nc;
        }
        snprintf(p->model_texs[p->model_tex_count].clean_name,
                 sizeof(p->model_texs[p->model_tex_count].clean_name), "%s", clean);
        p->model_texs[p->model_tex_count].tex_index = i;
        p->model_texs[p->model_tex_count].is_masked = (name[0] == '{' || (flags & 0x0040) != 0) ? 1 : 0;
        p->model_texs[p->model_tex_count].mdl_disk_path = cs_strdup(mdl_disk_path);
        p->model_tex_count++;
    }
    fclose(f);
}

static void scan_dir_recursive(Cs16Plugin* p, const char* root, const char* rel) {
#ifdef _WIN32
    char pattern[1024];
    if (rel[0]) snprintf(pattern, sizeof(pattern), "%s/%s/*", root, rel);
    else snprintf(pattern, sizeof(pattern), "%s/*", root);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char next_rel[512];
        if (rel[0]) snprintf(next_rel, sizeof(next_rel), "%s/%s", rel, fd.cFileName);
        else snprintf(next_rel, sizeof(next_rel), "%s", fd.cFileName);

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", root, next_rel);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_dir_recursive(p, root, next_rel);
        } else {
            if (!rel[0] && cs_ends_with(fd.cFileName, ".wad")) {
                add_wad(p, full);
            } else {
                for (char* c = next_rel; *c; ++c) {
                    if (*c == '\\') *c = '/';
                    else *c = (char)tolower((unsigned char)*c);
                }
                /* Do not expose Half-Life/CS config.cfg to Helix's own config loader */
                if (cs_strcasecmp(next_rel, "config.cfg") == 0) continue;
                add_loose_file(p, next_rel, full);
                if (cs_ends_with(next_rel, ".bsp")) {
                    index_bsp_embedded_textures(p, full);
                } else if (cs_ends_with(next_rel, ".mdl")) {
                    index_mdl_textures(p, full);
                }
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)p; (void)root; (void)rel;
#endif
}

static int read_disk_file(const char* path, unsigned char** out_buf, unsigned int* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out_buf = buf;
    *out_size = (unsigned int)sz;
    return 1;
}

static const LooseEntry* find_loose(const Cs16Plugin* p, const char* name) {
    for (int i = 0; i < p->file_count; ++i) {
        if (cs_strcasecmp(p->files[i].vpath, name) == 0) {
            return &p->files[i];
        }
    }
    return NULL;
}

static int bsp_path_for_hxmap(const char* name, char* out, size_t cap) {
    size_t len = strlen(name);
    if (len < 6 || !cs_ends_with(name, ".hxmap")) return 0;
    snprintf(out, cap, "%.*s.bsp", (int)(len - 6), name);
    return 1;
}

static int tex_name_for_tga(const char* name, char* out, size_t cap) {
    if (strncmp(name, "textures/", 9) != 0 && strncmp(name, "TEXTURES/", 9) != 0) return 0;
    const char* base = name + 9;
    size_t len = strlen(base);
    if (len == 0) return 0;
    if (cs_ends_with(base, ".tga")) len -= 4;
    else if (strchr(base, '.') != NULL) return 0; /* other extension */
    if (len == 0 || len >= cap) return 0;
    memcpy(out, base, len);
    out[len] = '\0';
    return 1;
}

static void cs16_close(hxAssetPlugin* self) {
    Cs16Plugin* p = (Cs16Plugin*)self;
    for (int i = 0; i < p->wad_count; ++i) {
        wad_close(p->wads[i]);
    }
    free(p->wads);
    p->wads = NULL;
    p->wad_count = 0;
    p->wad_cap = 0;

    for (int i = 0; i < p->file_count; ++i) {
        free(p->files[i].vpath);
        free(p->files[i].disk_path);
    }
    free(p->files);
    p->files = NULL;
    p->file_count = 0;
    p->file_cap = 0;

    for (int i = 0; i < p->embedded_tex_count; ++i) {
        free(p->embedded_texs[i].bsp_disk_path);
    }
    free(p->embedded_texs);
    p->embedded_texs = NULL;
    p->embedded_tex_count = 0;
    p->embedded_tex_cap = 0;

    for (int i = 0; i < p->model_tex_count; ++i) {
        free(p->model_texs[i].mdl_disk_path);
    }
    free(p->model_texs);
    p->model_texs = NULL;
    p->model_tex_count = 0;
    p->model_tex_cap = 0;
    p->is_cstrike = 0;
}

static int cs16_open_dir(hxAssetPlugin* self, const char* dir_path) {
    Cs16Plugin* p = (Cs16Plugin*)self;
    cs16_close(self);
    if (!dir_path || !dir_path[0]) return 0;

    p->is_cstrike = (cs_ends_with(dir_path, "/cstrike") || cs_ends_with(dir_path, "\\cstrike")) ? 1 : 0;

    /* Always scan common valve WAD textures in case valve directory exists */
    char valve_dir[1024];
    if (p->is_cstrike) {
        size_t dlen = strlen(dir_path);
        snprintf(valve_dir, sizeof(valve_dir), "%.*s/valve", (int)(dlen - 8), dir_path);
        /* Scan only WAD files from valve root, do not scan maps/ */
#ifdef _WIN32
        char pattern[1024];
        snprintf(pattern, sizeof(pattern), "%s/*.wad", valve_dir);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char full[1024];
                snprintf(full, sizeof(full), "%s/%s", valve_dir, fd.cFileName);
                add_wad(p, full);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#endif
        scan_dir_recursive(p, dir_path, "");
    } else {
        scan_dir_recursive(p, dir_path, "");
    }

    return (p->file_count > 0 || p->wad_count > 0) ? 1 : 0;
}

static int cs16_open(hxAssetPlugin* self, const char* path) {
    return cs16_open_dir(self, path);
}

static int cs16_open_many(hxAssetPlugin* self, const char** paths, int count) {
    if (!paths || count <= 0) return 0;
    return cs16_open_dir(self, paths[count - 1]);
}

static void sanitize_wad_key(const char* in, char* out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < cap; ++i) {
        unsigned char uc = (unsigned char)in[i];
        char c = (char)tolower(uc);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

static int find_wad_lump_by_sanitized(WadFile* w, const char* clean_name, char* orig_out, size_t orig_cap) {
    if (wad_contains(w, clean_name)) {
        snprintf(orig_out, orig_cap, "%s", clean_name);
        return 1;
    }
    char prefixed[96];
    snprintf(prefixed, sizeof(prefixed), "textures/%s", clean_name);
    if (wad_contains(w, prefixed)) {
        snprintf(orig_out, orig_cap, "%s", prefixed);
        return 1;
    }
    char** names = NULL;
    unsigned int count = 0;
    wad_list(w, &names, &count);
    int found = 0;
    for (unsigned int i = 0; i < count; ++i) {
        char buf[64];
        const char* lump = names[i];
        if (strncmp(lump, "textures/", 9) == 0) lump += 9;
        sanitize_wad_key(lump, buf, sizeof(buf));
        if (strcmp(buf, clean_name) == 0) {
            snprintf(orig_out, orig_cap, "%s", names[i]);
            found = 1;
            break;
        }
    }
    wad_free_list(names, count);
    return found;
}

static const LooseEntry* find_model_loose(const Cs16Plugin* p, const char* name) {
    const LooseEntry* le = find_loose(p, name);
    if (le) return le;
    if (strncmp(name, "models/", 7) != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "models/%s", name);
        le = find_loose(p, buf);
        if (le) return le;
    } else {
        le = find_loose(p, name + 7);
        if (le) return le;
    }
    return NULL;
}

static int convert_mdl_path_to_obj_ex(const Cs16Plugin* p, const char* mdl_vpath, unsigned char** out_buf, unsigned int* out_size, int include_backpack) {
    const LooseEntry* le = find_model_loose(p, mdl_vpath);
    if (!le) return 0;
    unsigned char* raw = NULL;
    unsigned int raw_sz = 0;
    if (!read_disk_file(le->disk_path, &raw, &raw_sz)) return 0;
    int ok = cs16_mdl_to_obj_ex(raw, (size_t)raw_sz, out_buf, out_size, include_backpack);
    free(raw);
    return ok;
}

static int convert_mdl_path_to_obj(const Cs16Plugin* p, const char* mdl_vpath, unsigned char** out_buf, unsigned int* out_size) {
    return convert_mdl_path_to_obj_ex(p, mdl_vpath, out_buf, out_size, 0);
}

static const char* get_player_mdl_path_ex(const char* name, int* out_is_bomb) {
    if (out_is_bomb) *out_is_bomb = 0;
    if (strstr(name, "_bomb") != NULL) {
        if (out_is_bomb) *out_is_bomb = 1;
    }
    if (cs_strcasecmp(name, "player") == 0 || cs_strcasecmp(name, "player_t1") == 0 || cs_strcasecmp(name, "models/player.obj") == 0 || cs_strcasecmp(name, "models/player_t1.obj") == 0 || cs_strcasecmp(name, "models/player_t1_bomb.obj") == 0) return "models/player/terror/terror.mdl";
    if (cs_strcasecmp(name, "player_t2") == 0 || cs_strcasecmp(name, "models/player_t2.obj") == 0 || cs_strcasecmp(name, "models/player_t2_bomb.obj") == 0) return "models/player/leet/leet.mdl";
    if (cs_strcasecmp(name, "player_t3") == 0 || cs_strcasecmp(name, "models/player_t3.obj") == 0 || cs_strcasecmp(name, "models/player_t3_bomb.obj") == 0) return "models/player/arctic/arctic.mdl";
    if (cs_strcasecmp(name, "player_t4") == 0 || cs_strcasecmp(name, "models/player_t4.obj") == 0 || cs_strcasecmp(name, "models/player_t4_bomb.obj") == 0) return "models/player/guerilla/guerilla.mdl";
    if (cs_strcasecmp(name, "player_ct") == 0 || cs_strcasecmp(name, "player_ct1") == 0 || cs_strcasecmp(name, "models/player_ct.obj") == 0 || cs_strcasecmp(name, "models/player_ct1.obj") == 0) return "models/player/urban/urban.mdl";
    if (cs_strcasecmp(name, "player_ct2") == 0 || cs_strcasecmp(name, "models/player_ct2.obj") == 0) return "models/player/gsg9/gsg9.mdl";
    if (cs_strcasecmp(name, "player_ct3") == 0 || cs_strcasecmp(name, "models/player_ct3.obj") == 0) return "models/player/sas/sas.mdl";
    if (cs_strcasecmp(name, "player_ct4") == 0 || cs_strcasecmp(name, "models/player_ct4.obj") == 0) return "models/player/gign/gign.mdl";
    if (cs_strcasecmp(name, "player_vip") == 0 || cs_strcasecmp(name, "models/player_vip.obj") == 0) return "models/player/vip/vip.mdl";
    return NULL;
}

static const char* get_player_mdl_path(const char* name) {
    return get_player_mdl_path_ex(name, NULL);
}

static int resolve_and_convert_model(const Cs16Plugin* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    int is_bomb = 0;
    const char* pmdl = get_player_mdl_path_ex(name, &is_bomb);
    if (pmdl) {
        if (convert_mdl_path_to_obj_ex(p, pmdl, out_buf, out_size, is_bomb)) return 1;
        if (convert_mdl_path_to_obj_ex(p, "models/player/terror/terror.mdl", out_buf, out_size, is_bomb)) return 1;
        if (convert_mdl_path_to_obj_ex(p, "models/player/urban/urban.mdl", out_buf, out_size, 0)) return 1;
        if (convert_mdl_path_to_obj_ex(p, "models/player.mdl", out_buf, out_size, is_bomb)) return 1;
        return 0;
    }
    if (cs_strcasecmp(name, "models/view_mg.obj") == 0) {
        if (convert_mdl_path_to_obj(p, "models/v_ak47.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/v_m4a1.mdl", out_buf, out_size)) return 1;
        return 0;
    }
    if (cs_strcasecmp(name, "models/weapon_mg.obj") == 0) {
        if (convert_mdl_path_to_obj(p, "models/p_ak47.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/w_ak47.mdl", out_buf, out_size)) return 1;
        return 0;
    }
    if (cs_strcasecmp(name, "models/view_sg.obj") == 0) {
        if (convert_mdl_path_to_obj(p, "models/v_glock18.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/v_usp.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/v_m3.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/v_ak47.mdl", out_buf, out_size)) return 1;
        return 0;
    }
    if (cs_strcasecmp(name, "models/weapon_sg.obj") == 0) {
        if (convert_mdl_path_to_obj(p, "models/p_glock18.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/w_glock18.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/p_m3.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/w_m3.mdl", out_buf, out_size)) return 1;
        return 0;
    }
    if (cs_strcasecmp(name, "models/v_glock.obj") == 0) {
        if (convert_mdl_path_to_obj(p, "models/v_glock18.mdl", out_buf, out_size)) return 1;
        return 0;
    }
    if (cs_strcasecmp(name, "models/p_glock.obj") == 0) {
        if (convert_mdl_path_to_obj(p, "models/p_glock18.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/w_glock18.mdl", out_buf, out_size)) return 1;
        return 0;
    }
    if (cs_strcasecmp(name, "models/hostage.obj") == 0) {
        if (convert_mdl_path_to_obj(p, "models/hostage.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/scientist.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/player/vip/vip.mdl", out_buf, out_size)) return 1;
        if (convert_mdl_path_to_obj(p, "models/player/urban/urban.mdl", out_buf, out_size)) return 1;
        return 0;
    }
    if (cs_ends_with(name, ".obj")) {
        char mdl_path[512];
        size_t len = strlen(name);
        if (len >= 4 && len < sizeof(mdl_path)) {
            snprintf(mdl_path, sizeof(mdl_path), "%.*s.mdl", (int)(len - 4), name);
            if (convert_mdl_path_to_obj(p, mdl_path, out_buf, out_size)) return 1;
        }
    }
    return 0;
}

static int can_resolve_model(const Cs16Plugin* p, const char* name) {
    const char* pmdl = get_player_mdl_path(name);
    if (pmdl) {
        return find_model_loose(p, pmdl) != NULL ||
               find_model_loose(p, "models/player/terror/terror.mdl") != NULL ||
               find_model_loose(p, "models/player/urban/urban.mdl") != NULL ||
               find_model_loose(p, "models/player.mdl") != NULL;
    }
    if (cs_strcasecmp(name, "models/view_mg.obj") == 0) {
        return find_model_loose(p, "models/v_ak47.mdl") != NULL;
    }
    if (cs_strcasecmp(name, "models/weapon_mg.obj") == 0) {
        return find_model_loose(p, "models/p_ak47.mdl") != NULL ||
               find_model_loose(p, "models/w_ak47.mdl") != NULL;
    }
    if (cs_strcasecmp(name, "models/view_sg.obj") == 0 || cs_strcasecmp(name, "models/v_glock.obj") == 0) {
        return find_model_loose(p, "models/v_glock18.mdl") != NULL ||
               find_model_loose(p, "models/v_usp.mdl") != NULL ||
               find_model_loose(p, "models/v_m3.mdl") != NULL ||
               find_model_loose(p, "models/v_ak47.mdl") != NULL;
    }
    if (cs_strcasecmp(name, "models/weapon_sg.obj") == 0 || cs_strcasecmp(name, "models/p_glock.obj") == 0) {
        return find_model_loose(p, "models/p_glock18.mdl") != NULL ||
               find_model_loose(p, "models/w_glock18.mdl") != NULL ||
               find_model_loose(p, "models/p_m3.mdl") != NULL ||
               find_model_loose(p, "models/w_m3.mdl") != NULL;
    }
    if (cs_strcasecmp(name, "models/hostage.obj") == 0) {
        return find_model_loose(p, "models/hostage.mdl") != NULL ||
               find_model_loose(p, "models/scientist.mdl") != NULL ||
               find_model_loose(p, "models/player/vip/vip.mdl") != NULL ||
               find_model_loose(p, "models/player/urban/urban.mdl") != NULL;
    }
    if (cs_ends_with(name, ".obj")) {
        char mdl_path[512];
        size_t len = strlen(name);
        if (len >= 4 && len < sizeof(mdl_path)) {
            snprintf(mdl_path, sizeof(mdl_path), "%.*s.mdl", (int)(len - 4), name);
            return find_model_loose(p, mdl_path) != NULL;
        }
    }
    return 0;
}

typedef struct {
    const char* vpath;
    const char* seq_label;
    int looped;
    int bone_filter;
} Cs16AnimMap;

static const Cs16AnimMap s_cs16_anims[] = {
    { "cod/animations/cs16_idle.hxac",          "idle1",                1, 1 },
    { "cod/animations/cs16_walk.hxac",          "walk",                 1, 1 },
    { "cod/animations/cs16_run.hxac",           "run",                  1, 1 },
    { "cod/animations/cs16_crouch_idle.hxac",   "crouch_idle",          1, 1 },
    { "cod/animations/cs16_crouch_walk.hxac",   "crouchrun",            1, 1 },
    { "cod/animations/cs16_jump.hxac",          "jump",                 0, 1 },
    { "cod/animations/cs16_aim_rifle.hxac",     "ref_aim_ak47",         1, 2 },
    { "cod/animations/cs16_shoot_rifle.hxac",   "ref_shoot_ak47",       0, 2 },
    { "cod/animations/cs16_reload_rifle.hxac",  "ref_reload_ak47",      0, 2 },
    { "cod/animations/cs16_aim_pistol.hxac",    "ref_aim_onehanded",    1, 2 },
    { "cod/animations/cs16_shoot_pistol.hxac",  "ref_shoot_onehanded",  0, 2 },
    { "cod/animations/cs16_reload_pistol.hxac", "ref_reload_onehanded", 0, 2 },
    { "cod/animations/cs16_aim_knife.hxac",     "ref_aim_knife",        1, 2 },
    { "cod/animations/cs16_shoot_knife.hxac",   "ref_shoot_knife",      0, 2 }
};

static int can_resolve_animation(const Cs16Plugin* p, const char* name) {
    if (!name || strncmp(name, "cod/animations/cs16_", 20) != 0) return 0;
    const LooseEntry* le = find_model_loose(p, "models/player/terror/terror.mdl");
    if (!le) le = find_model_loose(p, "models/player/urban/urban.mdl");
    if (!le) return 0;
    for (size_t i = 0; i < sizeof(s_cs16_anims) / sizeof(s_cs16_anims[0]); ++i) {
        if (cs_strcasecmp(name, s_cs16_anims[i].vpath) == 0) return 1;
    }
    return 0;
}

static int resolve_and_convert_animation(const Cs16Plugin* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    if (!name || strncmp(name, "cod/animations/cs16_", 20) != 0) return 0;
    const Cs16AnimMap* am = NULL;
    for (size_t i = 0; i < sizeof(s_cs16_anims) / sizeof(s_cs16_anims[0]); ++i) {
        if (cs_strcasecmp(name, s_cs16_anims[i].vpath) == 0) {
            am = &s_cs16_anims[i];
            break;
        }
    }
    if (!am) return 0;
    const LooseEntry* le = find_model_loose(p, "models/player/terror/terror.mdl");
    if (!le) le = find_model_loose(p, "models/player/urban/urban.mdl");
    if (!le) return 0;
    unsigned char* raw = NULL;
    unsigned int raw_sz = 0;
    if (!read_disk_file(le->disk_path, &raw, &raw_sz)) return 0;
    int ok = cs16_mdl_seq_to_hxac(raw, (size_t)raw_sz, am->seq_label, am->looped, am->bone_filter, out_buf, out_size);
    free(raw);
    return ok;
}

static int is_decal_key(const char* clean) {
    if (!clean) return 0;
    return strstr(clean, "site") != NULL || strstr(clean, "target") != NULL ||
           strstr(clean, "arrow") != NULL || strstr(clean, "logo") != NULL ||
           strstr(clean, "decal") != NULL || strstr(clean, "graffiti") != NULL ||
           strstr(clean, "blood") != NULL || strstr(clean, "scorch") != NULL;
}

static int build_cs16_mtr(const Cs16Plugin* p, unsigned char** out_buf, unsigned int* out_size) {
    size_t cap = 65536;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return 0;
    buf[0] = '\0';

    /* 1. WAD3 transparent textures ('{' prefix -> sanitized '_' prefix) */
    for (int w = 0; w < p->wad_count; ++w) {
        char** wnames = NULL;
        unsigned int wcount = 0;
        wad_list(p->wads[w], &wnames, &wcount);
        for (unsigned int j = 0; j < wcount; ++j) {
            if (!wnames[j] || wnames[j][0] != '{') continue;
            char clean[64];
            sanitize_wad_key(wnames[j], clean, sizeof(clean));
            if (!clean[0]) continue;
            int decal = is_decal_key(clean);
            char entry[384];
            int n = snprintf(entry, sizeof(entry),
                "material textures/%s {\n  albedo_map textures/%s.tga\n  alphaTest 0.5\n  twoSided\n%s}\n",
                clean, clean, decal ? "  polygonOffset\n" : "");
            if (n > 0) {
                if (len + (size_t)n + 1 > cap) {
                    cap *= 2;
                    char* nb = (char*)realloc(buf, cap);
                    if (!nb) { free(buf); wad_free_list(wnames, wcount); return 0; }
                    buf = nb;
                }
                memcpy(buf + len, entry, (size_t)n + 1);
                len += (size_t)n;
            }
        }
        wad_free_list(wnames, wcount);
    }

    /* 2. BSP embedded transparent textures */
    for (int e = 0; e < p->embedded_tex_count; ++e) {
        if (!p->embedded_texs[e].is_masked && p->embedded_texs[e].clean_name[0] != '_') continue;
        const char* clean = p->embedded_texs[e].clean_name;
        int decal = is_decal_key(clean);
        char entry[384];
        int n = snprintf(entry, sizeof(entry),
            "material textures/%s {\n  albedo_map textures/%s.tga\n  alphaTest 0.5\n  twoSided\n%s}\n",
            clean, clean, decal ? "  polygonOffset\n" : "");
        if (n > 0) {
            if (len + (size_t)n + 1 > cap) {
                cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); return 0; }
                buf = nb;
            }
            memcpy(buf + len, entry, (size_t)n + 1);
            len += (size_t)n;
        }
    }

    /* 3. StudioMDL masked textures (STUDIO_NF_MASKED or '{' prefix) */
    for (int m = 0; m < p->model_tex_count; ++m) {
        if (!p->model_texs[m].is_masked && p->model_texs[m].clean_name[0] != '_') continue;
        const char* clean = p->model_texs[m].clean_name;
        char entry[512];
        int n = snprintf(entry, sizeof(entry),
            "material textures/models/%s.tga {\n  albedo_map textures/models/%s.tga\n  alphaTest 0.5\n  twoSided\n}\n"
            "material textures/models/%s {\n  albedo_map textures/models/%s.tga\n  alphaTest 0.5\n  twoSided\n}\n",
            clean, clean, clean, clean);
        if (n > 0) {
            if (len + (size_t)n + 1 > cap) {
                cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); return 0; }
                buf = nb;
            }
            memcpy(buf + len, entry, (size_t)n + 1);
            len += (size_t)n;
        }
    }

    if (len == 0) {
        int n = snprintf(buf, cap, "# CS 1.6 Materials\n");
        len = (n > 0) ? (size_t)n : 0;
    }

    *out_buf = (unsigned char*)buf;
    *out_size = (unsigned int)len;
    return 1;
}

static void sanitize_stem(const char* in, char* out, size_t cap) {
    const char* slash = strrchr(in, '/');
    const char* bslash = strrchr(in, '\\');
    const char* base = slash > bslash ? slash : bslash;
    base = base ? base + 1 : in;

    const char* dot = strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - base) : strlen(base);

    size_t j = 0;
    for (size_t i = 0; i < stem_len && j + 1 < cap; ++i) {
        unsigned char uc = (unsigned char)base[i];
        char c = (char)tolower(uc);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

static const char* get_player_texture_mdl(const char* name) {
    if (cs_ends_with(name, ".dds")) return NULL;
    char clean[64];
    sanitize_stem(name, clean, sizeof(clean));
    return get_player_mdl_path(clean);
}

static int extract_model_texture(const Cs16Plugin* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    if (cs_ends_with(name, ".dds")) return 0;

    const char* pmdl = get_player_texture_mdl(name);
    if (pmdl) {
        const LooseEntry* le = find_model_loose(p, pmdl);
        if (!le) le = find_model_loose(p, "models/player/urban/urban.mdl");
        if (!le) le = find_model_loose(p, "models/player/terror/terror.mdl");
        if (le) {
            unsigned char* raw = NULL;
            unsigned int raw_sz = 0;
            if (read_disk_file(le->disk_path, &raw, &raw_sz)) {
                int ok = cs16_mdl_extract_texture(raw, (size_t)raw_sz, 0, out_buf, out_size, NULL, 0);
                free(raw);
                if (ok) return 1;
            }
        }
        return 0;
    }

    char clean[64];
    sanitize_stem(name, clean, sizeof(clean));
    if (!clean[0]) return 0;

    for (int i = 0; i < p->model_tex_count; ++i) {
        if (strcmp(p->model_texs[i].clean_name, clean) == 0) {
            unsigned char* raw = NULL;
            unsigned int raw_sz = 0;
            if (read_disk_file(p->model_texs[i].mdl_disk_path, &raw, &raw_sz)) {
                int ok = cs16_mdl_extract_texture(raw, (size_t)raw_sz, p->model_texs[i].tex_index, out_buf, out_size, NULL, 0);
                free(raw);
                if (ok) return 1;
            }
        }
    }
    return 0;
}

static int can_extract_model_texture(const Cs16Plugin* p, const char* name) {
    if (cs_ends_with(name, ".dds")) return 0;

    const char* pmdl = get_player_texture_mdl(name);
    if (pmdl) {
        return find_model_loose(p, pmdl) != NULL ||
               find_model_loose(p, "models/player/terror/terror.mdl") != NULL ||
               find_model_loose(p, "models/player/urban/urban.mdl") != NULL;
    }

    char clean[64];
    sanitize_stem(name, clean, sizeof(clean));
    if (!clean[0]) return 0;

    for (int i = 0; i < p->model_tex_count; ++i) {
        if (strcmp(p->model_texs[i].clean_name, clean) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cs16_contains(hxAssetPlugin* self, const char* name) {
    Cs16Plugin* p = (Cs16Plugin*)self;
    if (!name || !name[0]) return 0;
    if (find_loose(p, name)) return 1;

    if (cs_strcasecmp(name, "cod/arsenal.txt") == 0) return 1;
    if (cs_strcasecmp(name, "materials/cs16.mtr") == 0) return 1;
    if (can_resolve_animation(p, name)) return 1;

    if (can_resolve_model(p, name)) return 1;
    if (can_extract_model_texture(p, name)) return 1;

    char bsp_path[512];
    if (bsp_path_for_hxmap(name, bsp_path, sizeof(bsp_path)) && find_loose(p, bsp_path)) {
        return 1;
    }

    char tex_name[64];
    if (tex_name_for_tga(name, tex_name, sizeof(tex_name))) {
        char orig[64];
        for (int i = p->wad_count - 1; i >= 0; --i) {
            if (find_wad_lump_by_sanitized(p->wads[i], tex_name, orig, sizeof(orig))) return 1;
        }
        for (int i = 0; i < p->embedded_tex_count; ++i) {
            if (strcmp(p->embedded_texs[i].clean_name, tex_name) == 0) return 1;
        }
    }
    return 0;
}

static int cs16_read(hxAssetPlugin* self, const char* name,
                     unsigned char** out_buf, unsigned int* out_size) {
    Cs16Plugin* p = (Cs16Plugin*)self;
    if (!name || !out_buf || !out_size) return 0;

    if (cs_strcasecmp(name, "materials/cs16.mtr") == 0) {
        return build_cs16_mtr(p, out_buf, out_size);
    }

    if (cs_strcasecmp(name, "cod/arsenal.txt") == 0) {
        static const char s_cs16_arsenal[] =
            "weapon { id 16 name knife display \"Knife\" class pistol slot 1 type 0 semiAuto 1 clipOnly 1 damage 0 meleeDamage 65 meleeTime 0.40 clipSize 0 maxAmmo 0 startAmmo 0 hipSpreadStandMin 0.0 fireTime 0.40 reloadTime 0.5 world models/p_knife.obj }\n"
            "weapon { id 17 name glock display \"Glock-18\" class pistol slot 0 type 0 semiAuto 1 damage 28 meleeDamage 35 meleeTime 0.5 clipSize 20 maxAmmo 120 startAmmo 40 hipSpreadStandMin 0.20 fireTime 0.12 reloadTime 2.2 world models/p_glock.obj }\n"
            "weapon { id 18 name usp display \"USP .45\" class pistol slot 0 type 0 semiAuto 1 damage 34 meleeDamage 35 meleeTime 0.5 clipSize 12 maxAmmo 100 startAmmo 24 hipSpreadStandMin 0.18 fireTime 0.14 reloadTime 2.4 world models/p_usp.obj }\n"
            "weapon { id 19 name deagle display \"Desert Eagle\" class pistol slot 0 type 0 semiAuto 1 damage 54 meleeDamage 35 meleeTime 0.5 clipSize 7 maxAmmo 35 startAmmo 14 hipSpreadStandMin 0.25 fireTime 0.22 reloadTime 2.5 world models/p_deagle.obj }\n"
            "weapon { id 20 name p228 display \"228 Compact (P228)\" class pistol slot 0 type 0 semiAuto 1 damage 32 meleeDamage 35 meleeTime 0.5 clipSize 13 maxAmmo 52 startAmmo 26 hipSpreadStandMin 0.20 fireTime 0.13 reloadTime 2.4 world models/p_p228.obj }\n"
            "weapon { id 21 name fiveseven display \"ES Five-SeveN\" class pistol slot 0 type 0 semiAuto 1 damage 30 meleeDamage 35 meleeTime 0.5 clipSize 20 maxAmmo 100 startAmmo 40 hipSpreadStandMin 0.18 fireTime 0.12 reloadTime 2.4 world models/p_fiveseven.obj }\n"
            "weapon { id 22 name m3 display \"Leone 12 Gauge (M3)\" class shotgun slot 0 type 0 semiAuto 1 damage 26 meleeDamage 40 meleeTime 0.5 clipSize 8 maxAmmo 32 startAmmo 16 hipSpreadStandMin 3.2 rechamberTime 0.85 reloadTime 2.8 world models/p_m3.obj }\n"
            "weapon { id 23 name xm1014 display \"Leone YG1265 (XM1014)\" class shotgun slot 0 type 0 semiAuto 0 damage 22 meleeDamage 40 meleeTime 0.5 clipSize 7 maxAmmo 32 startAmmo 14 hipSpreadStandMin 3.4 fireTime 0.25 reloadTime 2.6 world models/p_xm1014.obj }\n"
            "weapon { id 24 name mp5 display \"KM Sub-Machine Gun (MP5)\" class smg slot 0 type 0 semiAuto 0 damage 26 meleeDamage 35 meleeTime 0.5 clipSize 30 maxAmmo 120 startAmmo 60 hipSpreadStandMin 0.18 fireTime 0.08 reloadTime 2.6 world models/p_mp5.obj }\n"
            "weapon { id 25 name p90 display \"ES C90 (P90)\" class smg slot 0 type 0 semiAuto 0 damage 24 meleeDamage 35 meleeTime 0.5 clipSize 50 maxAmmo 100 startAmmo 100 hipSpreadStandMin 0.20 fireTime 0.07 reloadTime 3.0 world models/p_p90.obj }\n"
            "weapon { id 26 name galil display \"IDF Defender (Galil)\" class rifle slot 0 type 0 semiAuto 0 damage 30 meleeDamage 40 meleeTime 0.5 clipSize 35 maxAmmo 90 startAmmo 70 hipSpreadStandMin 0.16 fireTime 0.09 reloadTime 2.6 world models/p_galil.obj }\n"
            "weapon { id 27 name famas display \"Clarion 5.56 (FAMAS)\" class rifle slot 0 type 0 semiAuto 0 damage 30 meleeDamage 40 meleeTime 0.5 clipSize 25 maxAmmo 90 startAmmo 50 hipSpreadStandMin 0.14 fireTime 0.085 reloadTime 2.6 world models/p_famas.obj }\n"
            "weapon { id 28 name ak47 display \"CV-47 (AK-47)\" class rifle slot 0 type 0 semiAuto 0 damage 36 meleeDamage 40 meleeTime 0.5 clipSize 30 maxAmmo 90 startAmmo 60 hipSpreadStandMin 0.15 fireTime 0.10 reloadTime 2.5 world models/p_ak47.obj }\n"
            "weapon { id 29 name m4a1 display \"Maverick M4A1\" class rifle slot 0 type 0 semiAuto 0 damage 33 meleeDamage 40 meleeTime 0.5 clipSize 30 maxAmmo 90 startAmmo 60 hipSpreadStandMin 0.12 fireTime 0.09 reloadTime 3.1 world models/p_m4a1.obj }\n"
            "weapon { id 30 name scout display \"Schmidt Scout\" class sniper slot 0 type 0 semiAuto 1 ads 1 adsZoomFov 30 adsSpread 0.01 scope textures/ui/scope_sniper reticle cs16 damage 75 meleeDamage 40 meleeTime 0.5 clipSize 10 maxAmmo 90 startAmmo 30 hipSpreadStandMin 0.35 rechamberTime 1.25 reloadTime 2.0 world models/p_scout.obj }\n"
            "weapon { id 31 name awp display \"Magnum Sniper (AWP)\" class sniper slot 0 type 0 semiAuto 1 ads 1 adsZoomFov 20 adsSpread 0.005 scope textures/ui/scope_sniper reticle cs16 damage 115 meleeDamage 40 meleeTime 0.5 clipSize 10 maxAmmo 30 startAmmo 20 hipSpreadStandMin 0.50 rechamberTime 1.45 reloadTime 2.8 world models/p_awp.obj }\n"
            "weapon { id 32 name hegrenade display \"HE Grenade\" class grenade slot 2 type 2 semiAuto 1 disposable 1 damage 98 clipSize 1 maxAmmo 1 startAmmo 1 projectileSpeed 720 fuseTime 2.0 explosionRadius 260 explosionInnerDamage 98 fireTime 0.8 world models/p_hegrenade.obj projectile models/w_hegrenade.obj }\n"
            "weapon { id 33 name c4 display \"C4 Explosive\" class grenade slot 2 type 0 semiAuto 1 clipOnly 1 damage 0 clipSize 1 maxAmmo 1 startAmmo 1 fireTime 0.5 world models/p_c4.obj }\n";
        size_t len = strlen(s_cs16_arsenal);
        unsigned char* buf = (unsigned char*)malloc(len + 1);
        if (!buf) return 0;
        memcpy(buf, s_cs16_arsenal, len + 1);
        *out_buf = buf;
        *out_size = (unsigned int)len;
        return 1;
    }

    if (resolve_and_convert_animation(p, name, out_buf, out_size)) {
        return 1;
    }

    const LooseEntry* le = find_loose(p, name);
    if (le) {
        return read_disk_file(le->disk_path, out_buf, out_size);
    }

    if (resolve_and_convert_model(p, name, out_buf, out_size)) {
        return 1;
    }

    if (extract_model_texture(p, name, out_buf, out_size)) {
        return 1;
    }

    char bsp_path[512];
    if (bsp_path_for_hxmap(name, bsp_path, sizeof(bsp_path))) {
        const LooseEntry* bsp = find_loose(p, bsp_path);
        if (!bsp) return 0;
        unsigned char* raw = NULL;
        unsigned int raw_sz = 0;
        if (!read_disk_file(bsp->disk_path, &raw, &raw_sz)) return 0;
        int ok = cs16_bsp_to_hxmap(raw, raw_sz, bsp_path, out_buf, out_size);
        free(raw);
        return ok;
    }

    char tex_name[64];
    if (tex_name_for_tga(name, tex_name, sizeof(tex_name))) {
        char orig[64];
        for (int i = p->wad_count - 1; i >= 0; --i) {
            if (find_wad_lump_by_sanitized(p->wads[i], tex_name, orig, sizeof(orig))) {
                unsigned char* lump = NULL;
                unsigned int lump_sz = 0;
                if (wad_read(p->wads[i], orig, &lump, &lump_sz)) {
                    int ok = cs16_wad3_miptex_to_tga(lump, lump_sz, orig, out_buf, out_size);
                    free(lump);
                    if (ok) return 1;
                }
            }
        }
        for (int i = 0; i < p->embedded_tex_count; ++i) {
            if (strcmp(p->embedded_texs[i].clean_name, tex_name) == 0) {
                unsigned char* bsp_raw = NULL;
                unsigned int bsp_sz = 0;
                if (read_disk_file(p->embedded_texs[i].bsp_disk_path, &bsp_raw, &bsp_sz)) {
                    int ok = cs16_bsp_extract_embedded_texture(bsp_raw, (size_t)bsp_sz, tex_name, out_buf, out_size);
                    free(bsp_raw);
                    if (ok) return 1;
                }
            }
        }
    }
    return 0;
}

static void cs16_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void cs16_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    Cs16Plugin* p = (Cs16Plugin*)self;
    if (!names || !count) return;
    unsigned int cap = (unsigned int)(p->file_count * 2 + p->embedded_tex_count + p->model_tex_count + 128);
    char** out = (char**)malloc(cap * sizeof(char*));
    unsigned int n = 0;
    if (!out) { *names = NULL; *count = 0; return; }

    for (int i = 0; i < p->file_count; ++i) {
        if (p->is_cstrike && is_hl_map_name(p->files[i].vpath)) continue;
        if (n + 3 > cap) {
            cap *= 2;
            out = (char**)realloc(out, cap * sizeof(char*));
        }
        out[n++] = cs_strdup(p->files[i].vpath);
        if (cs_ends_with(p->files[i].vpath, ".bsp")) {
            char hx[512];
            size_t l = strlen(p->files[i].vpath);
            snprintf(hx, sizeof(hx), "%.*s.hxmap", (int)(l - 4), p->files[i].vpath);
            out[n++] = cs_strdup(hx);
        } else if (cs_ends_with(p->files[i].vpath, ".mdl")) {
            char mobj[512];
            size_t l = strlen(p->files[i].vpath);
            snprintf(mobj, sizeof(mobj), "%.*s.obj", (int)(l - 4), p->files[i].vpath);
            out[n++] = cs_strdup(mobj);
        }
    }

    const char* virtual_assets[] = {
        "materials/cs16.mtr",
        "cod/arsenal.txt",
        "models/player.obj",
        "models/player_t1.obj",
        "models/player_t2.obj",
        "models/player_t3.obj",
        "models/player_t4.obj",
        "models/player_ct.obj",
        "models/player_ct1.obj",
        "models/player_ct2.obj",
        "models/player_ct3.obj",
        "models/player_ct4.obj",
        "models/player_vip.obj",
        "models/hostage.obj",
        "models/v_glock.obj",
        "models/p_glock.obj",
        "models/view_mg.obj",
        "models/weapon_mg.obj",
        "models/view_sg.obj",
        "models/weapon_sg.obj",
        "cod/animations/cs16_idle.hxac",
        "cod/animations/cs16_walk.hxac",
        "cod/animations/cs16_run.hxac",
        "cod/animations/cs16_crouch_idle.hxac",
        "cod/animations/cs16_crouch_walk.hxac",
        "cod/animations/cs16_jump.hxac",
        "cod/animations/cs16_aim_rifle.hxac",
        "cod/animations/cs16_shoot_rifle.hxac",
        "cod/animations/cs16_reload_rifle.hxac",
        "cod/animations/cs16_aim_pistol.hxac",
        "cod/animations/cs16_shoot_pistol.hxac",
        "cod/animations/cs16_reload_pistol.hxac",
        "cod/animations/cs16_aim_knife.hxac",
        "cod/animations/cs16_shoot_knife.hxac",
        "textures/models/player.tga",
        "textures/models/player_t1.tga",
        "textures/models/player_t2.tga",
        "textures/models/player_t3.tga",
        "textures/models/player_t4.tga",
        "textures/models/player_ct.tga",
        "textures/models/player_ct1.tga",
        "textures/models/player_ct2.tga",
        "textures/models/player_ct3.tga",
        "textures/models/player_ct4.tga",
        "textures/models/player_vip.tga"
    };
    for (size_t vi = 0; vi < sizeof(virtual_assets) / sizeof(virtual_assets[0]); ++vi) {
        if (n + 1 > cap) {
            cap *= 2;
            out = (char**)realloc(out, cap * sizeof(char*));
        }
        out[n++] = cs_strdup(virtual_assets[vi]);
    }

    for (int w = 0; w < p->wad_count; ++w) {
        char** wnames = NULL;
        unsigned int wcount = 0;
        wad_list(p->wads[w], &wnames, &wcount);
        for (unsigned int j = 0; j < wcount; ++j) {
            if (n + 2 > cap) {
                cap *= 2;
                out = (char**)realloc(out, cap * sizeof(char*));
            }
            char clean[64];
            sanitize_wad_key(wnames[j], clean, sizeof(clean));
            char tpath[96];
            snprintf(tpath, sizeof(tpath), "textures/%s.tga", clean);
            out[n++] = cs_strdup(tpath);
        }
        wad_free_list(wnames, wcount);
    }

    for (int e = 0; e < p->embedded_tex_count; ++e) {
        if (n + 1 > cap) {
            cap *= 2;
            out = (char**)realloc(out, cap * sizeof(char*));
        }
        char tpath[96];
        snprintf(tpath, sizeof(tpath), "textures/%s.tga", p->embedded_texs[e].clean_name);
        out[n++] = cs_strdup(tpath);
    }

    for (int m = 0; m < p->model_tex_count; ++m) {
        if (n + 1 > cap) {
            cap *= 2;
            out = (char**)realloc(out, cap * sizeof(char*));
        }
        char tpath[96];
        snprintf(tpath, sizeof(tpath), "textures/%s.tga", p->model_texs[m].clean_name);
        out[n++] = cs_strdup(tpath);
    }

    *names = out;
    *count = n;
}

static void cs16_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    (void)self;
    if (!names) return;
    for (unsigned int i = 0; i < count; ++i) free(names[i]);
    free(names);
}

static void cs16_destroy(hxAssetPlugin* self) {
    cs16_close(self);
    free(self);
}

CS16_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    Cs16Plugin* p = (Cs16Plugin*)calloc(1, sizeof(Cs16Plugin));
    if (!p) return NULL;
    p->base.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->base.format_name = "cs16";
    p->base.open = cs16_open;
    p->base.contains = cs16_contains;
    p->base.read = cs16_read;
    p->base.free_buf = cs16_free_buf;
    p->base.list = cs16_list;
    p->base.free_list = cs16_free_list;
    p->base.close = cs16_close;
    p->base.destroy = cs16_destroy;
    p->base.open_many = cs16_open_many;
    p->base.open_dir = cs16_open_dir;
    return &p->base;
}
