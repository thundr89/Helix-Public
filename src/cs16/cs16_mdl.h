#ifndef CS16_MDL_H
#define CS16_MDL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Converts a Valve GoldSrc StudioModel v10 (.mdl) buffer into Wavefront OBJ format text.
 * 
 * @param mdl_data Pointer to the raw MDL file buffer.
 * @param mdl_size Size of the MDL buffer in bytes.
 * @param out_obj Pointer to store dynamically allocated OBJ text buffer (caller frees with free()).
 * @param out_obj_size Pointer to store the size of the OBJ text in bytes.
 * @return 1 on success, 0 on failure.
 */
int cs16_mdl_to_obj(const unsigned char* mdl_data, size_t mdl_size, unsigned char** out_obj, unsigned int* out_obj_size);
int cs16_mdl_to_obj_ex(const unsigned char* mdl_data, size_t mdl_size, unsigned char** out_obj, unsigned int* out_obj_size, int include_backpack);

/**
 * Extracts a texture from a Valve GoldSrc StudioModel v10 (.mdl) buffer as a 32-bit uncompressed RGBA TGA file.
 * 
 * @param mdl_data Pointer to the raw MDL file buffer.
 * @param mdl_size Size of the MDL buffer in bytes.
 * @param tex_index 0-based index of the texture to extract.
 * @param out_tga Pointer to store dynamically allocated TGA buffer (caller frees with free()).
 * @param out_tga_size Pointer to store the size of the TGA buffer in bytes.
 * @param out_name Optional buffer to receive the texture name (can be NULL).
 * @param max_name Capacity of out_name buffer.
 * @return 1 on success, 0 on failure.
 */
int cs16_mdl_extract_texture(const unsigned char* mdl_data, size_t mdl_size, int tex_index, unsigned char** out_tga, unsigned int* out_tga_size, char* out_name, size_t max_name);

/**
 * Converts a named animation sequence from a StudioModel v10 (.mdl) buffer into Helix HXAC1 binary clip format.
 *
 * @param mdl_data    Raw MDL buffer.
 * @param mdl_size    Size of MDL buffer.
 * @param seq_label   Sequence label in MDL (e.g. "run", "walk", "idle1", "ref_aim_ak47").
 * @param looped      1 if clip loops, 0 for one-shot.
 * @param bone_filter 0 = all bones, 1 = lower body (root/pelvis/legs), 2 = upper body (spine/arms/head).
 * @param out_hxac    Allocated HXAC1 buffer (caller frees with free()).
 * @param out_size    Size of HXAC1 buffer in bytes.
 * @return 1 on success, 0 on failure.
 */
int cs16_mdl_seq_to_hxac(const unsigned char* mdl_data, size_t mdl_size,
                         const char* seq_label, int looped, int bone_filter,
                         unsigned char** out_hxac, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* CS16_MDL_H */
