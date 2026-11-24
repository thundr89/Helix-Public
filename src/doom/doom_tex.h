#ifndef DOOM_TEX_H
#define DOOM_TEX_H

#include "doom_wad.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode a Doom flat or composite wall texture from wads (doom1 or doom2) into a 32-bit TGA buffer.
 * tex_name is the sanitized texture name (lowercase, e.g. "floor4_8", "startan3", etc.).
 * Returns 1 on success (caller frees *out_tga with free()), 0 if not found.
 */
int doom_tex_extract(const DoomWad* doom1, const DoomWad* doom2,
                     const char* tex_name,
                     unsigned char** out_tga, unsigned int* out_size);

int doom_tex_exists(const DoomWad* doom1, const DoomWad* doom2,
                    const char* tex_name);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_TEX_H */
