/*
 * CoD menuDef → Helix .hxmenu text.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UI_HELIX_H
#define UI_HELIX_H

#include "ui/read.h"
#include "archive/archive.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ui_to_helix(const CodUiFile* menus,
                unsigned char** out_buf, unsigned int* out_size);

int ui_menu_to_helix(const void* data, size_t size,
                     unsigned char** out_buf, unsigned int* out_size);

/* #include "path" is read through this hook while a menu is translated.
 * The buffer is released with free(). NULL clears the hook. */
typedef int (*UiIncludeFn)(void* user, const char* path,
                           unsigned char** data, unsigned int* size);
void ui_menu_set_include(UiIncludeFn fn, void* user);
/* Borrowed only during translation, after all archive layers are mounted. */
void ui_menu_set_gametype_archive(const CodArchive* archive);

/* package is the .str filename without extension (menu -> @MENU_KEY). */
int ui_loc_add_str(const char* package, const void* data, size_t size);
const char* ui_loc_find(const char* key);
int ui_loc_export(unsigned char** out, unsigned int* size);
int ui_cvar_defaults_from_cfg(const void*,size_t,unsigned char**,unsigned int*);

#ifdef __cplusplus
}
#endif

#endif /* UI_HELIX_H */
