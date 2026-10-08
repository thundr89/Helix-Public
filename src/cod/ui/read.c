/*
 * CoD1 .menu reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "ui/read.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Lex {
    const char* p;
    const char* end;
} Lex;

static void skip(Lex* l) {
    for (;;) {
        while (l->p < l->end && isspace((unsigned char)*l->p)) l->p++;
        if (l->p < l->end && *l->p == '#') {
            while (l->p < l->end && *l->p != '\n') l->p++;
            continue;
        }
        /* CoD menus use a backslash as a line comment (`\\ note`). */
        if (l->p < l->end && *l->p == '\\') {
            while (l->p < l->end && *l->p != '\n') l->p++;
            continue;
        }
        if (l->p + 1 < l->end && l->p[0] == '/' && l->p[1] == '/') {
            while (l->p < l->end && *l->p != '\n') l->p++;
            continue;
        }
        if (l->p + 1 < l->end && l->p[0] == '/' && l->p[1] == '*') {
            l->p += 2;
            while (l->p + 1 < l->end && !(l->p[0] == '*' && l->p[1] == '/')) l->p++;
            if (l->p + 1 < l->end) l->p += 2;
            continue;
        }
        break;
    }
}

static void lower_inplace(char* s) {
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
    }
}

static int key_eq(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int is_known_key(const char* key) {
    static const char* keys[] = {
        "name", "type", "text", "rect", "visible", "decoration", "action",
        "onopen", "onesc", "onfocus", "onleave", "fullscreen", "forecolor",
        "backcolor", "focuscolor", "disablecolor", "bordercolor", "style",
        "textstyle", "textscale", "textalign", "textalignx", "textaligny",
        "textfont", "cvar", "cvartest", "showcvar", "hidecvar", "mouseenter",
        "mouseexit", "border", "bordersize", "background", "group", "image",
        "asset_shader", "ownerdraw", "feeder", "elementwidth", "elementheight",
        "elementtype", "special", "origin", "notselectable", "wrapped",
        "autowrapped", "horizontalscroll", "doubleclick", "popup", "soundloop",
        "itemdef", "menudef", "assetglobaldef", "leavefocus", NULL
    };
    int i;
    for (i = 0; keys[i]; i++) {
        if (key_eq(key, keys[i])) return 1;
    }
    return 0;
}

static int take_ident(Lex* l, char* out, size_t cap) {
    size_t n = 0;
    skip(l);
    if (l->p >= l->end || !(isalpha((unsigned char)*l->p) || *l->p == '_')) return 0;
    while (l->p < l->end && (isalnum((unsigned char)*l->p) || *l->p == '_')) {
        if (n + 1 < cap) out[n++] = *l->p;
        l->p++;
    }
    out[n] = '\0';
    return 1;
}

static int take_number(Lex* l, float* out) {
    char buf[64];
    size_t n = 0;
    skip(l);
    if (l->p < l->end && (*l->p == '+' || *l->p == '-')) {
        buf[n++] = *l->p++;
    }
    if (l->p >= l->end || !(isdigit((unsigned char)*l->p) || *l->p == '.')) return 0;
    while (l->p < l->end && (isdigit((unsigned char)*l->p) || *l->p == '.') && n + 1 < sizeof(buf)) {
        buf[n++] = *l->p++;
    }
    buf[n] = '\0';
    *out = (float)atof(buf);
    return 1;
}

static int take_string(Lex* l, char* out, size_t cap) {
    size_t n = 0;
    skip(l);
    if (l->p >= l->end || *l->p != '"') return 0;
    l->p++;
    while (l->p < l->end && *l->p != '"') {
        char c = *l->p++;
        if (c == '\\' && l->p < l->end) c = *l->p++;
        if (n + 1 < cap) out[n++] = c;
    }
    if (l->p < l->end && *l->p == '"') l->p++;
    out[n] = '\0';
    return 1;
}

/* One menu token: a quoted string, or a bare word such as 0x04 or FEEDER_ALLMAPS. */
static int take_raw_token(Lex* l, char* out, size_t cap) {
    size_t n = 0;
    skip(l);
    if (l->p < l->end && *l->p == '"') return take_string(l, out, cap);
    while (l->p < l->end && !isspace((unsigned char)*l->p) && *l->p != '{' && *l->p != '}') {
        if (n + 1 < cap) out[n++] = *l->p;
        l->p++;
    }
    out[n] = '\0';
    return n > 0;
}

static int take_punct(Lex* l, char c) {
    skip(l);
    if (l->p < l->end && *l->p == c) {
        l->p++;
        return 1;
    }
    return 0;
}

static void skip_braced(Lex* l) {
    int depth = 1;
    if (!take_punct(l, '{')) return;
    while (l->p < l->end && depth > 0) {
        if (*l->p == '"') {
            char dump[4];
            take_string(l, dump, sizeof(dump));
            continue;
        }
        if (*l->p == '{') depth++;
        else if (*l->p == '}') depth--;
        if (depth > 0) l->p++;
        else l->p++;
    }
}

static int peek_ident(Lex* l, char* out, size_t cap) {
    const char* save = l->p;
    int ok = take_ident(l, out, cap);
    l->p = save;
    return ok;
}

/* Unknown keys must not desync the file. A flag (decoration) has no value when
 * the next token is another key. Colors are a run of numbers, not one token. */
static void skip_value(Lex* l) {
    char ident[64];
    float n;
    char s[8];
    skip(l);
    /* A bare flag (decoration) ends at the next key or at the item's brace. */
    if (l->p >= l->end || *l->p == '}' || *l->p == ';') return;
    if (*l->p == '{') {
        skip_braced(l);
        return;
    }
    if (*l->p == '"') {
        take_string(l, s, sizeof(s));
        /* cvarFloat "name" min default max leaves the numbers after the string. */
        while (take_number(l, &n)) {}
        return;
    }
    if (take_number(l, &n)) {
        while (take_number(l, &n)) {}
        return;
    }
    if (peek_ident(l, ident, sizeof(ident))) {
        const char* save = l->p;
        char next;
        if (is_known_key(ident)) return;
        take_ident(l, ident, sizeof(ident));
        skip(l);
        next = l->p < l->end ? *l->p : 0;
        /* `popup` then `onClose {` — the ident is the next key, not a value. */
        if (next == '{' || next == '"' || next == '.' || next == '-' || next == '+' ||
            (next >= '0' && next <= '9')) {
            l->p = save;
            return;
        }
        return;
    }
    if (l->p < l->end) l->p++;
}

static int take_string_or_ident(Lex* l, char* out, size_t cap) {
    const char* save = l->p;
    if (take_string(l, out, cap)) return 1;
    l->p = save;
    return take_ident(l, out, cap);
}

static int take_cvar_list(Lex* l, char* out, size_t cap) {
    size_t n = 0;
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (take_punct(l, '{')) {
        for (;;) {
            char tok[64];
            skip(l);
            if (l->p >= l->end) return 0;
            if (*l->p == '}') {
                l->p++;
                return 1;
            }
            if (*l->p == ';') {
                l->p++;
                continue;
            }
            tok[0] = 0;
            if (!take_string_or_ident(l, tok, sizeof(tok))) return 0;
            if (!tok[0]) continue;
            if (n && n + 1 < cap) out[n++] = ' ';
            {
                size_t i;
                for (i = 0; tok[i] && n + 1 < cap; i++) out[n++] = tok[i];
            }
            out[n] = 0;
        }
    }
    return take_string_or_ident(l, out, cap);
}

static int take_boolish(Lex* l, int* out) {
    float n;
    char ident[64];
    const char* save = l->p;
    if (take_number(l, &n)) {
        *out = n != 0.f;
        return 1;
    }
    l->p = save;
    if (!take_ident(l, ident, sizeof(ident))) return 0;
    *out = !(key_eq(ident, "false") || key_eq(ident, "menu_false") || key_eq(ident, "0"));
    return 1;
}

static int push_action(CodUiAction** list, int* count, const CodUiAction* a) {
    CodUiAction* grown = (CodUiAction*)realloc(*list, (size_t)(*count + 1) * sizeof(CodUiAction));
    if (!grown) return 0;
    *list = grown;
    (*list)[(*count)++] = *a;
    return 1;
}

static int parse_actions(Lex* l, CodUiAction** list, int* count) {
    if (!take_punct(l, '{')) return 0;
    for (;;) {
        char cmd[32];
        CodUiAction a;
        skip(l);
        if (l->p >= l->end) return 0;
        if (take_punct(l, '}')) return 1;
        while (take_punct(l, ';')) {}
        skip(l);
        if (l->p < l->end && *l->p == '}') {
            l->p++;
            return 1;
        }
        if (!take_ident(l, cmd, sizeof(cmd))) {
            skip_value(l);
            continue;
        }
        memset(&a, 0, sizeof(a));
        snprintf(a.cmd, sizeof(a.cmd), "%s", cmd);
        if (strcmp(cmd, "open") == 0 || strcmp(cmd, "close") == 0 ||
            strcmp(cmd, "ingameopen") == 0 || strcmp(cmd, "ingameclose") == 0 ||
            strcmp(cmd, "play") == 0 || strcmp(cmd, "exec") == 0 ||
            strcmp(cmd, "toggle") == 0 || strcmp(cmd, "togglecvar") == 0 ||
            key_eq(cmd, "show") || key_eq(cmd, "hide") ||
            key_eq(cmd, "openforgametype") || key_eq(cmd, "closeforgametype") ||
            key_eq(cmd, "uiscript") || key_eq(cmd, "scriptMenuResponse") ||
            key_eq(cmd, "scriptmenuresponse")) {
            if (!take_string_or_ident(l, a.arg1, sizeof(a.arg1))) {
                skip_value(l);
                continue;
            }
            if (!push_action(list, count, &a)) return 0;
        } else if (strcmp(cmd, "setcvar") == 0 || strcmp(cmd, "set") == 0) {
            float num;
            char ident[64];
            if (!take_string_or_ident(l, a.arg1, sizeof(a.arg1))) {
                skip_value(l);
                continue;
            }
            skip(l);
            if (l->p < l->end && *l->p == '"') {
                if (!take_string(l, a.arg2, sizeof(a.arg2))) {
                    skip_value(l);
                    continue;
                }
            } else if (take_number(l, &num)) {
                snprintf(a.arg2, sizeof(a.arg2), "%g", num);
            } else if (take_ident(l, ident, sizeof(ident))) {
                snprintf(a.arg2, sizeof(a.arg2), "%s", ident);
            }
            if (!push_action(list, count, &a)) return 0;
        } else {
            while (l->p < l->end) {
                if (*l->p == ';' || *l->p == '}' || *l->p == '\n') break;
                if (*l->p == '"') {
                    char dump[4];
                    take_string(l, dump, sizeof(dump));
                    continue;
                }
                l->p++;
            }
        }
        take_punct(l, ';');
    }
}

static int parse_rect(Lex* l, float rect[4]) {
    int i;
    for (i = 0; i < 4; i++) {
        if (!take_number(l, &rect[i])) return 0;
    }
    for (;;) {
        float extra;
        const char* save = l->p;
        if (!take_number(l, &extra)) {
            l->p = save;
            break;
        }
    }
    return 1;
}

static int parse_item(Lex* l, CodUiItem* item) {
    memset(item, 0, sizeof(*item));
    if (!take_punct(l, '{')) return 0;
    for (;;) {
        char key[64];
        skip(l);
        if (take_punct(l, '}')) {
            if (item->has_origin && item->has_rect) {
                item->rect[0] += item->origin[0];
                item->rect[1] += item->origin[1];
            }
            return 1;
        }
        if (!take_ident(l, key, sizeof(key))) return 0;
        lower_inplace(key);
        if (strcmp(key, "name") == 0) {
            if (!take_string_or_ident(l, item->name, sizeof(item->name))) return 0;
        } else if (strcmp(key, "type") == 0) {
            float num;
            const char* save = l->p;
            if (take_ident(l, item->type, sizeof(item->type))) {
            } else {
                l->p = save;
                if (!take_number(l, &num)) return 0;
                snprintf(item->type, sizeof(item->type), "%g", num);
            }
        } else if (strcmp(key, "text") == 0) {
            if (!take_string_or_ident(l, item->text, sizeof(item->text))) return 0;
        } else if (strcmp(key, "image") == 0 || strcmp(key, "asset_shader") == 0) {
            if (!take_string_or_ident(l, item->image, sizeof(item->image))) return 0;
        } else if (strcmp(key, "textalign") == 0) {
            float num;
            const char* save = l->p;
            if (!take_ident(l, item->textalign, sizeof(item->textalign))) {
                l->p = save;
                if (!take_number(l, &num)) return 0;
                snprintf(item->textalign, sizeof(item->textalign), "%g", num);
            }
        } else if (strcmp(key, "cvar") == 0) {
            if (!take_string_or_ident(l, item->cvar, sizeof(item->cvar))) return 0;
        } else if (strcmp(key, "cvartest") == 0) {
            if (!take_string_or_ident(l, item->cvar_test, sizeof(item->cvar_test))) return 0;
        } else if (strcmp(key, "showcvar") == 0) {
            if (!take_cvar_list(l, item->show_cvar, sizeof(item->show_cvar))) return 0;
        } else if (strcmp(key, "hidecvar") == 0) {
            if (!take_cvar_list(l, item->hide_cvar, sizeof(item->hide_cvar))) return 0;
        } else if (strcmp(key, "group") == 0) {
            if (!take_string_or_ident(l, item->group, sizeof(item->group))) return 0;
        } else if (strcmp(key, "origin") == 0) {
            float ox = 0, oy = 0;
            if (take_number(l, &ox) && take_number(l, &oy)) {
                item->origin[0] = ox;
                item->origin[1] = oy;
                item->has_origin = 1;
            } else {
                skip_value(l);
            }
        } else if (strcmp(key, "background") == 0) {
            if (item->image[0]) skip_value(l);
            else if (!take_string_or_ident(l, item->image, sizeof(item->image))) return 0;
        } else if (strcmp(key, "rect") == 0) {
            if (!parse_rect(l, item->rect)) return 0;
            item->has_rect = 1;
        } else if (strcmp(key, "textscale") == 0) {
            if (take_number(l, &item->textscale)) item->has_textscale = 1;
            else skip_value(l);
        } else if (strcmp(key, "textalignx") == 0) {
            if (take_number(l, &item->textalignx)) item->has_text_anchor = 1;
            else skip_value(l);
        } else if (strcmp(key, "textaligny") == 0) {
            if (take_number(l, &item->textaligny)) item->has_text_anchor = 1;
            else skip_value(l);
        } else if (strcmp(key, "feeder") == 0) {
            if (!take_raw_token(l, item->feeder, sizeof(item->feeder))) return 0;
        } else if (strcmp(key, "ownerdraw") == 0) {
            if (!take_raw_token(l, item->ownerdraw, sizeof(item->ownerdraw))) return 0;
        } else if (strcmp(key, "cvarfloat") == 0) {
            float def = 0, mn = 0, mx = 0;
            if (!take_string_or_ident(l, item->cvar, sizeof(item->cvar))) return 0;
            if (take_number(l, &def) && take_number(l, &mn) && take_number(l, &mx)) {
                item->range_min = mn;
                item->range_max = mx;
                item->has_range = 1;
            }
        } else if (strcmp(key, "forecolor") == 0) {
            int i;
            const char* save = l->p;
            for (i = 0; i < 4; i++) {
                if (!take_number(l, &item->forecolor[i])) {
                    l->p = save;
                    skip_value(l);
                    break;
                }
            }
            if (i == 4) item->has_forecolor = 1;
        } else if (strcmp(key, "backcolor") == 0) {
            int i;
            const char* save = l->p;
            for (i = 0; i < 4; i++) {
                if (!take_number(l, &item->backcolor[i])) {
                    l->p = save;
                    skip_value(l);
                    break;
                }
            }
            if (i == 4) item->has_backcolor = 1;
        } else if (strcmp(key, "focuscolor") == 0) {
            int i;
            const char* save = l->p;
            for (i = 0; i < 4; i++) {
                if (!take_number(l, &item->focuscolor[i])) {
                    l->p = save;
                    skip_value(l);
                    break;
                }
            }
            if (i == 4) item->has_focuscolor = 1;
        } else if (strcmp(key, "decoration") == 0) {
            item->decoration = 1;
        } else if (strcmp(key, "onfocus") == 0 || strcmp(key, "mouseenter") == 0) {
            if (!parse_actions(l, &item->on_focus, &item->n_on_focus)) return 0;
        } else if (strcmp(key, "leavefocus") == 0 || strcmp(key, "mouseexit") == 0) {
            if (!parse_actions(l, &item->on_leave, &item->n_on_leave)) return 0;
        } else if (strcmp(key, "action") == 0) {
            if (!parse_actions(l, &item->actions, &item->nactions)) return 0;
        } else {
            skip_value(l);
        }
    }
}

static int parse_menu(Lex* l, CodUiMenu* menu) {
    memset(menu, 0, sizeof(*menu));
    if (!take_punct(l, '{')) return 0;
    for (;;) {
        char key[64];
        skip(l);
        if (take_punct(l, '}')) return 1;
        if (!take_ident(l, key, sizeof(key))) return 0;
        lower_inplace(key);
        if (strcmp(key, "name") == 0) {
            if (!take_string_or_ident(l, menu->name, sizeof(menu->name))) return 0;
        } else if (strcmp(key, "rect") == 0) {
            if (!parse_rect(l, menu->rect)) return 0;
            menu->has_rect = 1;
        } else if (strcmp(key, "fullscreen") == 0) {
            int on = 0;
            if (!take_boolish(l, &on)) return 0;
            menu->fullscreen = on;
            menu->has_fullscreen = 1;
        } else if (strcmp(key, "visible") == 0) {
            int on = 0;
            if (!take_boolish(l, &on)) return 0;
            menu->visible = on;
            menu->has_visible = 1;
        } else if (strcmp(key, "itemdef") == 0) {
            CodUiItem item;
            CodUiItem* grown;
            if (!parse_item(l, &item)) return 0;
            grown = (CodUiItem*)realloc(menu->items, (size_t)(menu->nitems + 1) * sizeof(CodUiItem));
            if (!grown) return 0;
            menu->items = grown;
            menu->items[menu->nitems++] = item;
        } else if (strcmp(key, "onopen") == 0) {
            if (!parse_actions(l, &menu->on_open, &menu->n_on_open)) return 0;
        } else if (strcmp(key, "onesc") == 0) {
            if (!parse_actions(l, &menu->on_esc, &menu->n_on_esc)) return 0;
        } else if (strcmp(key, "background") == 0) {
            if (!take_string_or_ident(l, menu->background, sizeof(menu->background))) return 0;
        } else {
            skip_value(l);
        }
    }
}

int ui_read(const void* data, size_t size, CodUiFile* out) {
    Lex lex;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return 0;
    lex.p = (const char*)data;
    lex.end = lex.p + size;
    for (;;) {
        char word[64];
        skip(&lex);
        if (lex.p >= lex.end) break;
        if (!take_ident(&lex, word, sizeof(word))) {
            lex.p++;
            continue;
        }
        if (strcmp(word, "menuDef") == 0) {
            CodUiMenu menu;
            CodUiMenu* grown;
            if (!parse_menu(&lex, &menu)) {
                ui_read_free(out);
                return 0;
            }
            grown = (CodUiMenu*)realloc(out->menus, (size_t)(out->nmenus + 1) * sizeof(CodUiMenu));
            if (!grown) {
                ui_read_free(out);
                return 0;
            }
            out->menus = grown;
            out->menus[out->nmenus++] = menu;
        }
    }
    if (out->nmenus == 0) return 0;
    return 1;
}

void ui_read_free(CodUiFile* file) {
    int i, j;
    if (!file) return;
    for (i = 0; i < file->nmenus; i++) {
        CodUiMenu* m = &file->menus[i];
        free(m->on_open);
        free(m->on_esc);
        for (j = 0; j < m->nitems; j++) {
            free(m->items[j].actions);
            free(m->items[j].on_focus);
            free(m->items[j].on_leave);
        }
        free(m->items);
    }
    free(file->menus);
    file->menus = NULL;
    file->nmenus = 0;
}
