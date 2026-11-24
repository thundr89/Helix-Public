#ifndef DOOM_WAD_H
#define DOOM_WAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[9];
    unsigned int filepos;
    unsigned int size;
} DoomLump;

typedef struct {
    unsigned char* data;
    size_t size;
    DoomLump* lumps;
    unsigned int num_lumps;
} DoomWad;

DoomWad* doom_wad_open(const char* path);
void doom_wad_close(DoomWad* w);
int doom_wad_find_lump(const DoomWad* w, const char* name, int start_idx);
const unsigned char* doom_wad_lump_ptr(const DoomWad* w, int idx, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_WAD_H */
