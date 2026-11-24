#ifndef COD4_FFMAP_H
#define COD4_FFMAP_H

/* PC fastfile (IWffu100) clipmap brushes -> Helix map text.
 * Draw triangles are the fallback when the brush array is absent.
 * The caller frees *out_buf. */
int cod4_ff_to_hxmap(const unsigned char* data, unsigned int size, const char* map_name,
                     unsigned char** out_buf, unsigned int* out_size);

/* Diagnostic compiled-menu layout export; not a runnable IW3 UI.
 * Reused strings, constant textExp and native action equivalents are translated.
 * Dynamic expressions and IW3 UIScript still require runtime support.
 * Caller frees output. */
int cod4_ff_to_menu_layout(const unsigned char* data, unsigned int size,
                          unsigned char** out_buf, unsigned int* out_size);

/* Typed LocalizeEntry export as quoted key/value pairs, one per line.
 * The complete zone must be walkable; caller frees output. */
int cod4_ff_to_localization(const unsigned char* data, unsigned int size,
                           unsigned char** out_buf, unsigned int* out_size);

/* Layout export with original text from a typed localized FF zone.
 * Unresolved keys are preserved. This does not implement dynamic expressions. */
int cod4_ff_to_localized_menu_layout(const unsigned char* data, unsigned int size,
    const unsigned char* localized_data, unsigned int localized_size,
    unsigned char** out_buf, unsigned int* out_size);

/* Shared-zone material library and menu import bundle. No inputs are retained. */
typedef struct Cod4UiZone { const unsigned char* data;unsigned int size; } Cod4UiZone;
int cod4_ff_to_ui_materials(const unsigned char* data,unsigned int size,
    unsigned char** out_buf,unsigned int* out_size);
int cod4_ff_to_menu_bundle(const unsigned char* data,unsigned int size,
    const unsigned char* localized_data,unsigned int localized_size,
    const Cod4UiZone* shared_zones,unsigned int shared_count,
    unsigned char** out_buf,unsigned int* out_size);

int cod4_ff_to_string_tables(const unsigned char* data,unsigned int size,unsigned char** out_buf,unsigned int* out_size);
#endif
