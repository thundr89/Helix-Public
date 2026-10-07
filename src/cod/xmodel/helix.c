/*
 * Assemble an xmodel OBJ from archive entries.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "xmodel/helix.h"
#include "xmodel/read.h"
#include "cod_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_player_body[160];

void cod_set_player_body(const char* xmodel) {
    g_player_body[0] = '\0';
    if (!xmodel || !xmodel[0]) return;
    snprintf(g_player_body, sizeof(g_player_body), "%s", xmodel);
}

/* CoD viewmodels point along +X. Helix view space is +Z forward, +Y up. */
static int rewrite_view_basis(unsigned char** buf, unsigned int* size) {
    const char* p;
    const char* end;
    char* out;
    size_t cap, n = 0;
    if (!buf || !*buf || !size) return 0;
    cap = (size_t)(*size) + 64;
    out = (char*)malloc(cap);
    if (!out) return 0;
    p = (const char*)*buf;
    end = p + *size;
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        char line[256];
        int vn = 0;
        float x, y, z;
        const char* src = p;
        size_t slen = len;
        if (len < sizeof(line)) {
            memcpy(line, p, len);
            line[len] = '\0';
            if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') vn = 1;
            if ((vn || (line[0] == 'v' && line[1] == ' ')) &&
                sscanf(line + (vn ? 3 : 2), "%f %f %f", &x, &y, &z) == 3) {
                snprintf(line, sizeof(line), vn ? "vn %.6g %.6g %.6g" : "v %.6g %.6g %.6g", y, z, x);
                src = line;
                slen = strlen(line);
            }
        }
        if (n + slen + 2 > cap) {
            size_t ncap = cap * 2 + slen + 2;
            char* grown = (char*)realloc(out, ncap);
            if (!grown) {
                free(out);
                return 0;
            }
            out = grown;
            cap = ncap;
        }
        memcpy(out + n, src, slen);
        n += slen;
        if (nl) out[n++] = '\n';
        p += len;
        if (nl) p++;
    }
    out[n] = '\0';
    free(*buf);
    *buf = (unsigned char*)out;
    *size = (unsigned int)n;
    return 1;
}

const char* cod_gameplay_xmodel(const char* name) {
    if (!name || !name[0]) return NULL;
    if (cod_strcasecmp(name, "models/player.obj") == 0) {
        if (g_player_body[0]) return g_player_body;
        return "xmodel/playerbody_american_airborne";
    }
    /* Weapon view and world models come from cod/weapons.txt, not a fixed BAR pair. */
    return NULL;
}

int xmodel_to_helix(const CodArchive* ar, const char* name,
                unsigned char** out_buf, unsigned int* out_size) {
    unsigned char* xm = NULL;
    unsigned char* sf = NULL;
    unsigned char* pt = NULL;
    unsigned int xsz = 0, ssz = 0, psz = 0;
    CodXmodelLod slots[4];
    char surf[300];
    char parts[300];
    int nslots, i, ok;
    unsigned char* best = NULL;
    unsigned best_sz = 0;

    if (!ar || !name || !out_buf || !out_size) return 0;
    if (strncmp(name, "xmodel/", 7) != 0 || !name[7]) return 0;
    if (strchr(name + 7, '/') || strchr(name + 7, '\\')) return 0;

    if (!cod_archive_read(ar, name, &xm, &xsz)) return 0;
    if (cod_xmodel_is_text(xm, xsz)) {
        ok = cod_xmodel_to_obj(xm, xsz, NULL, 0, NULL, 0, out_buf, out_size);
        free(xm);
        return ok;
    }
    nslots = cod_xmodel_lod_slots(xm, xsz, slots, 4);
    if (nslots <= 0) {
        free(xm);
        return 0;
    }
    cod_xmodel_set_keep_hands(strstr(name, "viewmodel") != NULL);
    for (i = 0; i < nslots; i++) {
        unsigned char* obj = NULL;
        unsigned osz = 0;
        if (!slots[i].name[0]) continue;
        snprintf(surf, sizeof(surf), "xmodelsurfs/%s", slots[i].name);
        if (!cod_archive_read(ar, surf, &sf, &ssz)) continue;
        snprintf(parts, sizeof(parts), "xmodelparts/%s", slots[i].name);
        if (!cod_archive_read(ar, parts, &pt, &psz)) {
            pt = NULL;
            psz = 0;
        }
        ok = cod_xmodel_to_obj(xm, xsz, sf, ssz, pt, psz, &obj, &osz);
        free(pt);
        free(sf);
        pt = NULL;
        sf = NULL;
        if (!ok || !obj) {
            free(obj);
            continue;
        }
        best = obj;
        best_sz = osz;
        break;
    }
    cod_xmodel_set_keep_hands(0);
    free(xm);
    if (!best) return 0;
    if (strstr(name, "viewmodel")) rewrite_view_basis(&best, &best_sz);
    *out_buf = best;
    *out_size = best_sz;
    return 1;
}
