#ifndef CS16_WAD_H
#define CS16_WAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert a WAD3 miptex lump (40-byte header + 4 mip levels + 2-byte palette count + 256*3 RGB palette)
 * into a 32-bit BGRA TGA image in memory.
 * If name starts with '{', palette index 255 is treated as transparent (alpha = 0).
 * Returns 1 on success (caller frees *out_buf with free()), 0 on failure.
 */
int cs16_wad3_miptex_to_tga(const unsigned char* lump, size_t lump_size,
                            const char* tex_name,
                            unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* CS16_WAD_H */
