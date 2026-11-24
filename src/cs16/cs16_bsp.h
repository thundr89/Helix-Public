#ifndef CS16_BSP_H
#define CS16_BSP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert a GoldSrc BSP v30 buffer into a Helix .hxmap text buffer.
 * Returns 1 on success (caller frees *out_buf with free()), 0 on failure.
 */
int cs16_bsp_to_hxmap(const unsigned char* bsp_data, size_t bsp_size,
                      const char* map_name,
                      unsigned char** out_buf, unsigned int* out_size);

/*
 * Look up an embedded texture in a GoldSrc BSP v30 buffer by sanitized texture name.
 * If found and offsets[0] > 0, decodes it to 32-bit TGA via cs16_wad3_miptex_to_tga.
 * Returns 1 on success (caller frees *out_buf with free()), 0 on failure.
 */
int cs16_bsp_extract_embedded_texture(const unsigned char* bsp_data, size_t bsp_size,
                                      const char* sanitized_name,
                                      unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* CS16_BSP_H */
