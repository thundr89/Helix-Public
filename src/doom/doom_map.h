#ifndef DOOM_MAP_H
#define DOOM_MAP_H

#include "doom_wad.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert a DOOM / DOOM II map (e.g. "E1M1" or "MAP01") from a DoomWad
 * into a Helix .hxmap text buffer.
 * Returns 1 on success (caller frees *out_buf with free()), 0 on failure.
 */
int doom_map_to_hxmap(const DoomWad* w, const char* map_name,
                      unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_MAP_H */
