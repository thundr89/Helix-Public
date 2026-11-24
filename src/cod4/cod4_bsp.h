#ifndef COD4_BSP_H
#define COD4_BSP_H

/* CoD4 IBSP version 22 -> Helix map text. The caller frees *out_buf. */
int cod4_bsp_to_hxmap(const unsigned char* data, unsigned int size, const char* map_name,
                      unsigned char** out_buf, unsigned int* out_size);

#endif
