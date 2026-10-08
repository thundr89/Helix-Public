/*
 * CoD1 .menu reader. menuDef / itemDef records, still in CoD vocabulary.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UI_READ_H
#define UI_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CodUiAction {
    char cmd[32];
    char arg1[128];
    char arg2[128];
} CodUiAction;

typedef struct CodUiItem {
    char name[64];
    char type[64];
    char text[160];
    char image[160];
    char textalign[32];
    char feeder[80];
    char ownerdraw[64];
    char cvar[64];
    char cvar_test[64];
    char show_cvar[64];
    char hide_cvar[64];
    char group[64];
    float origin[2];
    int has_origin;
    float rect[4];
    int has_rect;
    float textscale;
    int has_textscale;
    float textalignx;
    float textaligny;
    int has_text_anchor;
    float range_min;
    float range_max;
    int has_range;
    float forecolor[4];
    int has_forecolor;
    float backcolor[4];
    int has_backcolor;
    float focuscolor[4];
    int has_focuscolor;
    int decoration;
    CodUiAction* actions;
    int nactions;
    CodUiAction* on_focus;
    int n_on_focus;
    CodUiAction* on_leave;
    int n_on_leave;
} CodUiItem;

typedef struct CodUiMenu {
    char name[64];
    float rect[4];
    int has_rect;
    int fullscreen;
    int has_fullscreen;
    int visible;
    int has_visible;
    char background[160];
    CodUiAction* on_open;
    int n_on_open;
    CodUiAction* on_esc;
    int n_on_esc;
    CodUiItem* items;
    int nitems;
} CodUiMenu;

typedef struct CodUiFile {
    CodUiMenu* menus;
    int nmenus;
} CodUiFile;

/* Returns 0 when the text has no menuDef. */
int ui_read(const void* data, size_t size, CodUiFile* out);
void ui_read_free(CodUiFile* file);

#ifdef __cplusplus
}
#endif

#endif /* UI_READ_H */
