#include "doom_wad.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    char identification[4];
    unsigned int numlumps;
    unsigned int infotableofs;
} WadHeader;

typedef struct {
    unsigned int filepos;
    unsigned int size;
    char name[8];
} WadDirEntry;
#pragma pack(pop)

static int dw_strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

DoomWad* doom_wad_open(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < (long)sizeof(WadHeader)) { fclose(f); return NULL; }

    unsigned char* data = (unsigned char*)malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);

    const WadHeader* hdr = (const WadHeader*)data;
    if (memcmp(hdr->identification, "IWAD", 4) != 0 &&
        memcmp(hdr->identification, "PWAD", 4) != 0) {
        free(data);
        return NULL;
    }
    if ((size_t)hdr->infotableofs + (size_t)hdr->numlumps * sizeof(WadDirEntry) > (size_t)sz) {
        free(data);
        return NULL;
    }

    DoomWad* w = (DoomWad*)calloc(1, sizeof(DoomWad));
    if (!w) { free(data); return NULL; }
    w->data = data;
    w->size = (size_t)sz;
    w->num_lumps = hdr->numlumps;
    w->lumps = (DoomLump*)calloc(hdr->numlumps, sizeof(DoomLump));
    if (!w->lumps) { free(data); free(w); return NULL; }

    const WadDirEntry* dir = (const WadDirEntry*)(data + hdr->infotableofs);
    for (unsigned int i = 0; i < hdr->numlumps; ++i) {
        memcpy(w->lumps[i].name, dir[i].name, 8);
        w->lumps[i].name[8] = '\0';
        w->lumps[i].filepos = dir[i].filepos;
        w->lumps[i].size = dir[i].size;
    }
    return w;
}

void doom_wad_close(DoomWad* w) {
    if (!w) return;
    free(w->lumps);
    free(w->data);
    free(w);
}

int doom_wad_find_lump(const DoomWad* w, const char* name, int start_idx) {
    if (!w || !name) return -1;
    if (start_idx < 0) start_idx = 0;
    for (unsigned int i = (unsigned int)start_idx; i < w->num_lumps; ++i) {
        if (dw_strcasecmp(w->lumps[i].name, name) == 0) return (int)i;
    }
    return -1;
}

const unsigned char* doom_wad_lump_ptr(const DoomWad* w, int idx, unsigned int* out_size) {
    if (!w || idx < 0 || (unsigned int)idx >= w->num_lumps) return NULL;
    const DoomLump* l = &w->lumps[idx];
    if ((size_t)l->filepos + (size_t)l->size > w->size) return NULL;
    if (out_size) *out_size = l->size;
    return w->data + l->filepos;
}
