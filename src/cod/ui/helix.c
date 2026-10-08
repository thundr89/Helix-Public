/*
 * CoD menuDef → Helix .hxmenu text.
 * ITEM_TYPE_* names become Helix item types. setcvar becomes set.
 * Unknown item types become label so the caption is still drawn.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "ui/helix.h"
#include "emit.h"
#include "cod_util.h"

#include <ctype.h>
#include <stdio.h>

static int type_blocked(const char* cod) {
    (void)cod;
    /* Ownerdraw, multi and bind stay. They are the server, team and weapon panels. */
    return 0;
}

/* NULL means Helix cannot draw this item, so it is left out.
 * Numeric values are the menudef.h ITEM_TYPE_* constants after #define expansion. */
static const char* item_type(const char* cod) {
    if (type_blocked(cod)) return NULL;
    if (!cod || !cod[0] || cod_strcasecmp(cod, "ITEM_TYPE_TEXT") == 0 || strcmp(cod, "0") == 0 ||
        cod_strcasecmp(cod, "label") == 0)
        return "label";
    if (cod_strcasecmp(cod, "ITEM_TYPE_BUTTON") == 0 || cod_strcasecmp(cod, "button") == 0 || strcmp(cod, "1") == 0)
        return "button";
    if (cod_strcasecmp(cod, "ITEM_TYPE_CHECKBOX") == 0 || cod_strcasecmp(cod, "ITEM_TYPE_YESNO") == 0 ||
        cod_strcasecmp(cod, "checkbox") == 0 || strcmp(cod, "2") == 0 || strcmp(cod, "3") == 0 ||
        strcmp(cod, "11") == 0)
        return "checkbox";
    if (cod_strcasecmp(cod, "ITEM_TYPE_LISTBOX") == 0 || cod_strcasecmp(cod, "listbox") == 0 || strcmp(cod, "6") == 0)
        return "listbox";
    if (cod_strcasecmp(cod, "ITEM_TYPE_SLIDER") == 0 || cod_strcasecmp(cod, "slider") == 0 || strcmp(cod, "10") == 0)
        return "slider";
    if (cod_strcasecmp(cod, "ITEM_TYPE_IMAGE") == 0 || cod_strcasecmp(cod, "image") == 0)
        return "image";
    if (cod_strcasecmp(cod, "ITEM_TYPE_OWNERDRAW") == 0 || cod_strcasecmp(cod, "ownerdraw") == 0 || strcmp(cod, "8") == 0)
        return "button";
    if (cod_strcasecmp(cod, "ITEM_TYPE_MULTI") == 0 || cod_strcasecmp(cod, "multi") == 0 || strcmp(cod, "12") == 0)
        return "listbox";
    if (cod_strcasecmp(cod, "ITEM_TYPE_BIND") == 0 || cod_strcasecmp(cod, "bind") == 0 || strcmp(cod, "13") == 0)
        return "button";
    return NULL;
}

static const char* align_name(const char* cod) {
    if (!cod || !cod[0]) return NULL;
    if (cod_strcasecmp(cod, "ITEM_ALIGN_CENTER") == 0 || cod_strcasecmp(cod, "center") == 0 || strcmp(cod, "1") == 0)
        return "center";
    if (cod_strcasecmp(cod, "ITEM_ALIGN_RIGHT") == 0 || cod_strcasecmp(cod, "right") == 0 || strcmp(cod, "2") == 0)
        return "right";
    if (cod_strcasecmp(cod, "ITEM_ALIGN_LEFT") == 0 || cod_strcasecmp(cod, "left") == 0 || strcmp(cod, "0") == 0)
        return "left";
    return NULL;
}

/* Host commands the original uiScript names map onto. Anything else is dropped. */
static const char* uiscript_exec(const char* name) {
    if (!name || !name[0]) return NULL;
    if (cod_strcasecmp(name, "quit") == 0) return "quit";
    if (cod_strcasecmp(name, "runmod") == 0) return "load_mod";
    return NULL;
}

static int is_uiscript(const char* cmd) {
    return cmd && cod_strcasecmp(cmd, "uiscript") == 0;
}

static int is_script_response(const char* cmd) {
    return cmd && (cod_strcasecmp(cmd, "scriptMenuResponse") == 0 ||
                   cod_strcasecmp(cmd, "scriptmenuresponse") == 0);
}

static const char* action_cmd(const char* cod) {
    if (!cod) return NULL;
    if (cod_strcasecmp(cod, "setcvar") == 0 || cod_strcasecmp(cod, "set") == 0) return "set";
    if (cod_strcasecmp(cod, "open") == 0) return "open";
    if (cod_strcasecmp(cod, "close") == 0) return "close";
    if (cod_strcasecmp(cod, "play") == 0) return "play";
    if (cod_strcasecmp(cod, "exec") == 0) return "exec";
    if (cod_strcasecmp(cod, "toggle") == 0 || cod_strcasecmp(cod, "togglecvar") == 0) return "toggle";
    if (cod_strcasecmp(cod, "stopmusic") == 0) return "stopmusic";
    if (cod_strcasecmp(cod, "ingameopen") == 0) return "ingameopen";
    if (cod_strcasecmp(cod, "ingameclose") == 0) return "ingameclose";
    if (cod_strcasecmp(cod, "show") == 0) return "show";
    if (cod_strcasecmp(cod, "hide") == 0) return "hide";
    if (cod_strcasecmp(cod, "openforgametype") == 0) return "openforgametype";
    if (cod_strcasecmp(cod, "closeforgametype") == 0) return "closeforgametype";
    return NULL;
}

/* set only reaches cvars the host actually stores. volume is the host's s_volume. */
static const char* set_cvar(const char* name) {
    if (!name || !name[0]) return NULL;
    if (cod_strcasecmp(name, "volume") == 0 || cod_strcasecmp(name, "s_volume") == 0 ||
        cod_strcasecmp(name, "mss_volume") == 0)
        return "s_volume";
    if (cod_strcasecmp(name, "s_musicvolume") == 0 || cod_strcasecmp(name, "mss_musicvolume") == 0)
        return "s_musicvolume";
    if (cod_strcasecmp(name, "s_musicloop") == 0) return "s_musicloop";
    if (cod_strcasecmp(name, "sensitivity") == 0) return "sensitivity";
    if (cod_strcasecmp(name, "m_invert") == 0 || cod_strcasecmp(name, "ui_mousePitch") == 0) return "m_invert";
    if (cod_strcasecmp(name, "g_gametype") == 0) return "g_gametype";
    if (cod_strcasecmp(name, "g_team") == 0) return "g_team";
    if (cod_strcasecmp(name, "g_timelimit") == 0) return "g_timelimit";
    if (cod_strcasecmp(name, "g_fraglimit") == 0) return "g_fraglimit";
    if (cod_strcasecmp(name, "name") == 0) return "name";
    if (cod_strcasecmp(name, "r_width") == 0) return "r_width";
    if (cod_strcasecmp(name, "r_height") == 0) return "r_height";
    if (cod_strcasecmp(name, "r_fullscreen") == 0 || cod_strcasecmp(name, "ui_r_fullscreen") == 0) return "r_fullscreen";
    if (cod_strcasecmp(name, "r_vsync") == 0 || cod_strcasecmp(name, "r_swapinterval") == 0) return "r_vsync";
    if (cod_strcasecmp(name, "r_borderless") == 0) return "r_borderless";
    if (cod_strcasecmp(name, "r_aces") == 0) return "r_aces";
    if (cod_strcasecmp(name, "r_physical_falloff") == 0) return "r_physical_falloff";
    if (cod_strcasecmp(name, "ui_map") == 0) return "ui_map";
    if (cod_strcasecmp(name, "r_gamma") == 0) return "r_gamma";
    if (cod_strcasecmp(name, "r_mode") == 0) return "r_mode";
    if (cod_strcasecmp(name, "fov") == 0 || cod_strcasecmp(name, "cg_fov") == 0) return "fov";
    if (cod_strcasecmp(name, "sv_hostname") == 0) return "sv_hostname";
    if (cod_strcasecmp(name, "sv_maxclients") == 0) return "sv_maxclients";
    return NULL;
}

static int starts_ci(const char* s, const char* prefix) {
    while (*prefix) {
        unsigned char a = (unsigned char)*s++;
        unsigned char b = (unsigned char)*prefix++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

/* SP campaign starts (spmap, map credits) are not multiplayer launches. */
static int exec_blocked(const char* arg) {
    const char* s = arg ? arg : "";
    const char* name;
    while (*s == ' ' || *s == '\t') s++;
    if (starts_ci(s, "spmap")) return 1;
    if (!starts_ci(s, "map ") && !starts_ci(s, "devmap ")) return 0;
    name = s;
    while (*name && *name != ' ' && *name != '\t') name++;
    while (*name == ' ' || *name == '\t') name++;
    return starts_ci(name, "credits") && (name[7] == '\0' || name[7] == ' ' || name[7] == ';' || name[7] == '\t');
}

typedef struct LocEntry {
    char* key;
    char* value;
} LocEntry;

static LocEntry* g_loc = NULL;
static int g_loc_n = 0;
static int g_loc_cap = 0;

static char* loc_dup(const char* s) {
    size_t n = strlen(s);
    char* d = (char*)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static void loc_upper(char* s) {
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
    }
}

const char* ui_loc_find(const char* key) {
    char tmp[192];
    int i;
    if (!key || !key[0]) return NULL;
    snprintf(tmp, sizeof(tmp), "%s", key[0] == '@' ? key + 1 : key);
    loc_upper(tmp);
    for (i = 0; i < g_loc_n; i++) {
        if (strcmp(g_loc[i].key, tmp) == 0) return g_loc[i].value;
    }
    return NULL;
}

static int loc_store(const char* key, const char* value) {
    char k[192];
    int i;
    LocEntry* grown;
    if (!key || !key[0] || !value) return 0;
    snprintf(k, sizeof(k), "%s", key);
    loc_upper(k);
    for (i = 0; i < g_loc_n; i++) {
        if (strcmp(g_loc[i].key, k) == 0) {
            free(g_loc[i].value);
            g_loc[i].value = loc_dup(value);
            return g_loc[i].value != NULL;
        }
    }
    if (g_loc_n + 1 > g_loc_cap) {
        int ncap = g_loc_cap ? g_loc_cap * 2 : 32;
        grown = (LocEntry*)realloc(g_loc, (size_t)ncap * sizeof(LocEntry));
        if (!grown) return 0;
        g_loc = grown;
        g_loc_cap = ncap;
    }
    g_loc[g_loc_n].key = loc_dup(k);
    g_loc[g_loc_n].value = loc_dup(value);
    if (!g_loc[g_loc_n].key || !g_loc[g_loc_n].value) return 0;
    g_loc_n++;
    return 1;
}

static const char* line_word(const char* s, const char* end, char* out, size_t cap) {
    size_t n = 0;
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r')) s++;
    while (s < end && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && n + 1 < cap) out[n++] = *s++;
    out[n] = '\0';
    return s;
}

static int line_quoted(const char* s, const char* end, char* out, size_t cap) {
    size_t n = 0;
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    if (s >= end || *s != '"') return 0;
    s++;
    while (s < end && *s != '"' && *s != '\n' && *s != '\r') {
        char c = *s++;
        if (c == '\\' && s < end) c = *s++;
        if (n + 1 < cap) out[n++] = c;
    }
    out[n] = '\0';
    return n > 0 || (s < end && *s == '"');
}

int ui_loc_add_str(const char* package, const void* data, size_t size) {
    const char* p;
    const char* end;
    char pkg[64];
    char ref[128];
    if (!package || !package[0] || !data || size == 0) return 0;
    snprintf(pkg, sizeof(pkg), "%s", package);
    loc_upper(pkg);
    ref[0] = '\0';
    p = (const char*)data;
    end = p + size;
    while (p < end) {
        const char* line = p;
        char word[32];
        const char* rest;
        while (p < end && *p != '\n') p++;
        rest = line_word(line, p, word, sizeof(word));
        if (cod_strcasecmp(word, "REFERENCE") == 0) {
            line_word(rest, p, ref, sizeof(ref));
        } else if (cod_strcasecmp(word, "LANG_ENGLISH") == 0 && ref[0]) {
            char value[512];
            char key[192];
            if (line_quoted(rest, p, value, sizeof(value)) && strcmp(value, "#same") != 0) {
                snprintf(key, sizeof(key), "%s_%s", pkg, ref);
                loc_store(key, value);
            }
        }
        if (p < end && *p == '\n') p++;
    }
    return 1;
}

static void pretty_menu_text(const char* in, char* out, size_t cap) {
    const char* s = in ? in : "";
    const char* hit;
    size_t i = 0;
    int cap_next = 1;
    if (cap == 0) return;
    if (*s == '@' && (hit = ui_loc_find(s + 1)) != NULL && hit[0]) {
        snprintf(out, cap, "%s", hit);
        return;
    }
    if (*s != '@') {
        snprintf(out, cap, "%s", in ? in : "");
        return;
    }
    s++;
    if (starts_ci(s, "MENU_")) s += 5;
    for (; *s && i + 1 < cap; s++) {
        char c = *s;
        if (c == '_') {
            if (i > 0 && out[i - 1] != ' ') out[i++] = ' ';
            cap_next = 1;
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (cap_next && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
            cap_next = 0;
        } else if (c != ' ') {
            cap_next = 0;
        }
        out[i++] = c;
    }
    out[i] = '\0';
}

static int emit_actions(CodEmit* e, const char* key, const CodUiAction* actions, int count, const char* indent) {
    int i;
    int any = 0;
    if (count <= 0) return 1;
    for (i = 0; i < count; i++) {
        if (is_script_response(actions[i].cmd)) {
            any = 1;
            break;
        }
        if (is_uiscript(actions[i].cmd)) {
            if (actions[i].arg1 && cod_strcasecmp(actions[i].arg1, "startserver") == 0) {
                any = 1;
                break;
            }
            if (uiscript_exec(actions[i].arg1)) {
                any = 1;
                break;
            }
            continue;
        }
        if (action_cmd(actions[i].cmd)) {
            if (strcmp(action_cmd(actions[i].cmd), "set") == 0 || strcmp(action_cmd(actions[i].cmd), "toggle") == 0) {
                if (!set_cvar(actions[i].arg1)) continue;
            }
            if (strcmp(action_cmd(actions[i].cmd), "exec") == 0 && exec_blocked(actions[i].arg1)) continue;
            any = 1;
            break;
        }
    }
    if (!any) return 1;
    if (!cod_emit_fmt(e, "%s%s {", indent, key)) return 0;
    for (i = 0; i < count; i++) {
        const char* cmd;
        if (is_script_response(actions[i].cmd)) {
            const char* arg = actions[i].arg1;
            if (cod_strcasecmp(arg, "allies") == 0) {
                if (!cod_emit_add(e, " exec \"team allies\" ; open \"weapon_american\" ;")) return 0;
            } else if (cod_strcasecmp(arg, "axis") == 0) {
                if (!cod_emit_add(e, " exec \"team axis\" ; open \"weapon_german\" ;")) return 0;
            } else if (cod_strcasecmp(arg, "autoassign") == 0) {
                if (!cod_emit_add(e, " exec \"team autoassign\" ;")) return 0;
            } else if (cod_strcasecmp(arg, "spectator") == 0) {
                if (!cod_emit_add(e, " exec \"team spectator\" ;")) return 0;
            } else if (cod_strcasecmp(arg, "close") == 0 || cod_strcasecmp(arg, "open") == 0) {
                /* lifecycle response */
            } else {
                char wcmd[160];
                snprintf(wcmd, sizeof(wcmd), " exec \"weapon %s\" ;", arg);
                if (!cod_emit_add(e, wcmd)) return 0;
            }
            continue;
        }
        if (is_uiscript(actions[i].cmd)) {
            if (actions[i].arg1 && cod_strcasecmp(actions[i].arg1, "startserver") == 0) {
                if (!cod_emit_add(e, " open \"map_select\" ; exec \"map\" ; close \"createserver\" ; close \"createserver_maps\" ; close \"createserver_op\" ;")) return 0;
                continue;
            }
            const char* ex = uiscript_exec(actions[i].arg1);
            if (!ex) continue;
            if (!cod_emit_fmt(e, " exec \"%s\" ;", ex)) return 0;
            continue;
        }
        cmd = action_cmd(actions[i].cmd);
        if (!cmd) continue;
        if (strcmp(cmd, "set") == 0 || strcmp(cmd, "toggle") == 0) {
            const char* cvar = set_cvar(actions[i].arg1);
            if (!cvar) continue;
            if (strcmp(cmd, "set") == 0) {
                if (!cod_emit_fmt(e, " %s \"%s\" \"%s\" ;", cmd, cvar, actions[i].arg2)) return 0;
            } else if (!cod_emit_fmt(e, " %s \"%s\" ;", cmd, cvar)) return 0;
        } else if (strcmp(cmd, "stopmusic") == 0) {
            if (!cod_emit_add(e, " stopmusic ;")) return 0;
        } else {
            if (strcmp(cmd, "exec") == 0 && exec_blocked(actions[i].arg1)) continue;
            if (!cod_emit_fmt(e, " %s \"%s\" ;", cmd, actions[i].arg1)) return 0;
        }
    }
    return cod_emit_add(e, " }\n");
}

static int fade_image(const char* image) {
    return image && image[0] && (strstr(image, "fadebox") != NULL || strstr(image, "fade") != NULL);
}

static int map_preview_item(const CodUiItem* it) {
    if (!it) return 0;
    if (cod_strcasecmp(it->name, "mappreview") == 0) return 1;
    if (cod_strcasecmp(it->ownerdraw, "UI_STARTMAPCINEMATIC") == 0 || strcmp(it->ownerdraw, "255") == 0) return 1;
    return 0;
}

static const char* map_feeder(const char* raw);

/* The cinematic rect sits a couple of units right of the map list. Share the list's left edge. */
static void align_preview_rect(const CodUiMenu* menu, const CodUiItem* it, float rect[4]) {
    int k;
    rect[0] = it->rect[0];
    rect[1] = it->rect[1];
    rect[2] = it->rect[2];
    rect[3] = it->rect[3];
    if (!menu || !map_preview_item(it)) return;
    for (k = 0; k < menu->nitems; k++) {
        const char* feeder = map_feeder(menu->items[k].feeder);
        if (!feeder || strcmp(feeder, "maps") != 0 || !menu->items[k].has_rect) continue;
        rect[0] = menu->items[k].rect[0];
        rect[2] = menu->items[k].rect[2];
        return;
    }
}

/* CoD feeder ids become the host lists. Unknown feeders stay mock (no rows). */
/* CoD bind cvars that Helix actually has. The row stays; the key on the right is live. */
static const char* helix_bind(const char* cvar) {
    if (!cvar || !cvar[0]) return NULL;
    if (strcmp(cvar, "+forward") == 0) return "forward";
    if (strcmp(cvar, "+back") == 0) return "back";
    if (strcmp(cvar, "+moveleft") == 0) return "left";
    if (strcmp(cvar, "+moveright") == 0) return "right";
    if (strcmp(cvar, "+moveup") == 0 || strcmp(cvar, "+gostand") == 0) return "jump";
    if (strcmp(cvar, "+movedown") == 0) return "crouch";
    if (strcmp(cvar, "+attack") == 0) return "attack";
    if (strcmp(cvar, "+reload") == 0) return "reload";
    if (strcmp(cvar, "weaponslot primary") == 0) return "weap1";
    if (strcmp(cvar, "weaponslot primaryb") == 0) return "weap2";
    if (strcmp(cvar, "+activate") == 0) return "use";
    return NULL;
}

static int emit_option_row(CodEmit* e, const char* name, const char* type, float y, const char* text,
                           const char* cvar, const char* exec_cmd) {
    if (!cod_emit_fmt(e,
                      "    itemDef {\n"
                      "        name \"%s\"\n"
                      "        type %s\n"
                      "        rect 5 %g 350 13\n"
                      "        text \"%s\"\n"
                      "        textscale 0.225\n"
                      "        textalign right\n"
                      "        textalignx 185\n"
                      "        textaligny 11\n"
                      "        forecolor 0.9 0.9 0.9 1\n",
                      name, type, y, text))
        return 0;
    if (cvar && cvar[0] && !cod_emit_fmt(e, "        cvar \"%s\"\n", cvar)) return 0;
    if (exec_cmd && exec_cmd[0] && !cod_emit_fmt(e, "        action { exec \"%s\" ; }\n", exec_cmd)) return 0;
    return cod_emit_add(e, "    }\n");
}

static const char* map_feeder(const char* raw) {
    if (!raw || !raw[0]) return NULL;
    if (cod_strcasecmp(raw, "FEEDER_ALLMAPS") == 0 || cod_strcasecmp(raw, "FEEDER_MAPS") == 0 ||
        strcmp(raw, "0x04") == 0 || strcmp(raw, "0x4") == 0 || strcmp(raw, "4") == 0 ||
        strcmp(raw, "0x01") == 0 || strcmp(raw, "0x1") == 0 || strcmp(raw, "1") == 0)
        return "maps";
    if (cod_strcasecmp(raw, "FEEDER_MODS") == 0 || strcmp(raw, "0x09") == 0 || strcmp(raw, "0x9") == 0 ||
        strcmp(raw, "9") == 0)
        return "codmods";
    return NULL;
}

int ui_to_helix(const CodUiFile* menus, unsigned char** out_buf, unsigned int* out_size) {
    CodEmit e;
    int i, j;
    int kept = 0;
    if (!menus || !out_buf || !out_size || menus->nmenus <= 0) return 0;
    if (!cod_emit_init(&e)) return 0;
    for (i = 0; i < menus->nmenus; i++) {
        const CodUiMenu* m = &menus->menus[i];
        const char* align;
        if (!cod_emit_add(&e, "menuDef {\n") ||
            !cod_emit_fmt(&e, "    name \"%s\"\n", m->name)) {
            cod_emit_free(&e);
            return 0;
        }
        if (m->has_rect && !cod_emit_fmt(&e, "    rect %g %g %g %g\n",
                                         m->rect[0], m->rect[1], m->rect[2], m->rect[3])) {
            cod_emit_free(&e);
            return 0;
        }
        if (m->has_fullscreen && !cod_emit_fmt(&e, "    fullscreen %d\n", m->fullscreen ? 1 : 0)) {
            cod_emit_free(&e);
            return 0;
        }
        if (m->has_visible && !cod_emit_fmt(&e, "    visible %d\n", m->visible ? 1 : 0)) {
            cod_emit_free(&e);
            return 0;
        }
        if (!emit_actions(&e, "onOpen", m->on_open, m->n_on_open, "    ") ||
            !emit_actions(&e, "onESC", m->on_esc, m->n_on_esc, "    ")) {
            cod_emit_free(&e);
            return 0;
        }
        if (m->background[0]) {
            float x = 0, y = 0, w = 640, h = 480;
            if (m->has_rect) {
                w = m->rect[2];
                h = m->rect[3];
            }
            kept++;
            if (!cod_emit_add(&e, "    itemDef {\n") ||
                !cod_emit_add(&e, "        name \"background\"\n") ||
                !cod_emit_add(&e, "        type image\n") ||
                !cod_emit_fmt(&e, "        rect %g %g %g %g\n", x, y, w, h) ||
                !cod_emit_fmt(&e, "        image \"%s\"\n", m->background) ||
                !cod_emit_add(&e, "        decoration 1\n") ||
                !cod_emit_add(&e, "    }\n")) {
                cod_emit_free(&e);
                return 0;
            }
        }
        for (j = 0; j < m->nitems; j++) {
            const CodUiItem* it = &m->items[j];
            const char* kind = item_type(it->type);
            const char* cvar_name = it->cvar;
            const char* feeder;
            int preview;
            int fade;
            char shown[160];
            char bind_cvar[64];
            char extra_cmd[80];
            char* sp;
            if (!kind) continue;
            preview = map_preview_item(it);
            fade = fade_image(it->image);
            feeder = map_feeder(it->feeder);
            if (preview) kind = "image";
            else if (fade) kind = "label";
            else if (it->image[0] && !it->type[0]) kind = "image";

            shown[0] = '\0';
            if (it->text[0]) {
                pretty_menu_text(it->text, shown, sizeof(shown));
                for (sp = shown; *sp; sp++) {
                    if (*sp == '"' || *sp == '\n' || *sp == '\r') *sp = ' ';
                }
            }

            /* Remap CoD specific cvars to Helix engine cvars */
            extra_cmd[0] = '\0';
            bind_cvar[0] = '\0';
            if (preview) cvar_name = "ui_map";
            else if (feeder && strcmp(feeder, "maps") == 0 && !cvar_name[0]) cvar_name = "ui_map";
            else if (feeder && strcmp(feeder, "codmods") == 0) cvar_name = "ui_helix_mod";
            if (it->has_rect && it->rect[3] > 0.f && it->rect[3] <= 20.f && shown[0] &&
                strcmp(kind, "listbox") == 0)
                kind = "button";
            {
                const char* bid = helix_bind(it->cvar);
                if (bid) {
                    snprintf(bind_cvar, sizeof(bind_cvar), "ui_bind_%s", bid);
                    snprintf(extra_cmd, sizeof(extra_cmd), "bind_action %s", bid);
                    cvar_name = bind_cvar;
                    kind = "button";
                }
            }
            if (cod_strcasecmp(cvar_name, "ui_mousePitch") == 0 ||
                strstr(shown, "Invert Mouse") != NULL || strstr(it->text, "INVERT_MOUSE") != NULL) {
                cvar_name = "m_invert";
            } else if (cod_strcasecmp(cvar_name, "ui_r_fullscreen") == 0) {
                cvar_name = "r_fullscreen";
            } else if (cod_strcasecmp(cvar_name, "ui_r_mode") == 0) {
                cvar_name = "ui_vidmode";
                snprintf(extra_cmd, sizeof(extra_cmd), "vid_mode_next");
                kind = "button";
            } else if (cod_strcasecmp(cvar_name, "r_swapinterval") == 0) {
                cvar_name = "r_vsync";
            } else if (cod_strcasecmp(cvar_name, "mss_volume") == 0) {
                cvar_name = "s_volume";
            } else if (cod_strcasecmp(cvar_name, "mss_musicvolume") == 0) {
                cvar_name = "s_musicvolume";
            }

            kept++;
            if (!cod_emit_add(&e, "    itemDef {\n") ||
                !cod_emit_fmt(&e, "        name \"%s\"\n", it->name) ||
                !cod_emit_fmt(&e, "        type %s\n", kind)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->has_rect) {
                float rect[4];
                align_preview_rect(m, it, rect);
                if (!cod_emit_fmt(&e, "        rect %g %g %g %g\n", rect[0], rect[1], rect[2], rect[3])) {
                    cod_emit_free(&e);
                    return 0;
                }
            }
            if (it->group[0] && !cod_emit_fmt(&e, "        group \"%s\"\n", it->group)) {
                cod_emit_free(&e);
                return 0;
            }
            if (shown[0]) {
                if (!cod_emit_fmt(&e, "        text \"%s\"\n", shown)) {
                    cod_emit_free(&e);
                    return 0;
                }
            }
            if (preview) {
                if (!cod_emit_add(&e, "        image \"levelshots/mp_harbor.png\"\n") ||
                    !cod_emit_add(&e, "        decoration 1\n")) {
                    cod_emit_free(&e);
                    return 0;
                }
            } else if (!fade && it->image[0] && !cod_emit_fmt(&e, "        image \"%s\"\n", it->image)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->has_textscale && !cod_emit_fmt(&e, "        textscale %g\n", it->textscale)) {
                cod_emit_free(&e);
                return 0;
            }
            align = align_name(it->textalign);
            if (align && !cod_emit_fmt(&e, "        textalign %s\n", align)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->has_text_anchor &&
                !cod_emit_fmt(&e, "        textalignx %g\n        textaligny %g\n", it->textalignx, it->textaligny)) {
                cod_emit_free(&e);
                return 0;
            }
            if (feeder && !cod_emit_fmt(&e, "        feeder \"%s\"\n", feeder)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->has_range && !cod_emit_fmt(&e, "        min %g\n        max %g\n", it->range_min, it->range_max)) {
                cod_emit_free(&e);
                return 0;
            }
            if (cvar_name[0] && !cod_emit_fmt(&e, "        cvar \"%s\"\n", cvar_name)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->cvar_test[0] && !cod_emit_fmt(&e, "        cvarTest \"%s\"\n", it->cvar_test)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->show_cvar[0] && !cod_emit_fmt(&e, "        showCvar \"%s\"\n", it->show_cvar)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->hide_cvar[0] && !cod_emit_fmt(&e, "        hideCvar \"%s\"\n", it->hide_cvar)) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->has_forecolor && !cod_emit_fmt(&e, "        forecolor %g %g %g %g\n",
                                                   it->forecolor[0], it->forecolor[1],
                                                   it->forecolor[2], it->forecolor[3])) {
                cod_emit_free(&e);
                return 0;
            }
            if (fade && !it->has_backcolor && it->has_forecolor) {
                if (!cod_emit_fmt(&e, "        backcolor %g %g %g %g\n",
                                  it->forecolor[0], it->forecolor[1], it->forecolor[2], it->forecolor[3])) {
                    cod_emit_free(&e);
                    return 0;
                }
            } else if (it->has_backcolor && !cod_emit_fmt(&e, "        backcolor %g %g %g %g\n",
                                                   it->backcolor[0], it->backcolor[1],
                                                   it->backcolor[2], it->backcolor[3])) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->has_focuscolor && !cod_emit_fmt(&e, "        focuscolor %g %g %g %g\n",
                                                    it->focuscolor[0], it->focuscolor[1],
                                                    it->focuscolor[2], it->focuscolor[3])) {
                cod_emit_free(&e);
                return 0;
            }
            if (it->decoration && !cod_emit_add(&e, "        decoration 1\n")) {
                cod_emit_free(&e);
                return 0;
            }
            if (extra_cmd[0] && !cod_emit_fmt(&e, "        action { exec \"%s\" ; }\n", extra_cmd)) {
                cod_emit_free(&e);
                return 0;
            }
            if (!emit_actions(&e, "action", it->actions, it->nactions, "        ") ||
                !emit_actions(&e, "onFocus", it->on_focus, it->n_on_focus, "        ") ||
                !emit_actions(&e, "onLeave", it->on_leave, it->n_on_leave, "        ") ||
                !cod_emit_add(&e, "    }\n")) {
                cod_emit_free(&e);
                return 0;
            }
        }
        if (strcmp(m->name, "options_graphics") == 0) {
            /* Helix-only rows, under the CoD graphics list and above Apply. */
            kept += 4;
            if (!emit_option_row(&e, "hx_renderer", "button", 215, "Renderer", "r_renderer", "vid_renderer_next") ||
                !emit_option_row(&e, "hx_borderless", "checkbox", 230, "Borderless Window", "r_borderless", NULL) ||
                !emit_option_row(&e, "hx_aces", "checkbox", 245, "ACES Tonemapping", "r_aces", NULL) ||
                !emit_option_row(&e, "hx_falloff", "checkbox", 260, "Physical Light Falloff", "r_physical_falloff", NULL)) {
                cod_emit_free(&e);
                return 0;
            }
        } else if (strcmp(m->name, "options_look") == 0) {
            kept += 1;
            if (!cod_emit_add(&e,
                              "    itemDef {\n"
                              "        name \"hx_pad\"\n"
                              "        type label\n"
                              "        rect 5 170 350 13\n"
                              "        cvar \"ui_gamepad\"\n"
                              "        textscale 0.225\n"
                              "        textalign left\n"
                              "        textalignx 8\n"
                              "        textaligny 11\n"
                              "        forecolor 0.9 0.9 0.9 1\n"
                              "        decoration 1\n"
                              "    }\n")) {
                cod_emit_free(&e);
                return 0;
            }
        }
        if (!cod_emit_add(&e, "}\n")) {
            cod_emit_free(&e);
            return 0;
        }
    }
    if (kept <= 0 || !cod_emit_take(&e, out_buf, out_size)) {
        cod_emit_free(&e);
        return 0;
    }
    return 1;
}

typedef struct MenuMacro {
    char name[64];
    char value[192];
} MenuMacro;

static MenuMacro* g_mac;
static int g_nmac;
static int g_cmac;
static UiIncludeFn g_inc_fn;
static void* g_inc_user;

void ui_menu_set_include(UiIncludeFn fn, void* user) {
    g_inc_fn = fn;
    g_inc_user = user;
}

static const char* mac_find(const char* name) {
    int i;
    for (i = 0; i < g_nmac; i++) {
        if (strcmp(g_mac[i].name, name) == 0) return g_mac[i].value;
    }
    return NULL;
}

static void mac_store(const char* name, const char* value) {
    int i;
    MenuMacro* grown;
    if (!name || !name[0]) return;
    for (i = 0; i < g_nmac; i++) {
        if (strcmp(g_mac[i].name, name) == 0) {
            snprintf(g_mac[i].value, sizeof(g_mac[i].value), "%s", value ? value : "");
            return;
        }
    }
    if (g_nmac >= g_cmac) {
        int ncap = g_cmac ? g_cmac * 2 : 64;
        grown = (MenuMacro*)realloc(g_mac, (size_t)ncap * sizeof(MenuMacro));
        if (!grown) return;
        g_mac = grown;
        g_cmac = ncap;
    }
    snprintf(g_mac[g_nmac].name, sizeof(g_mac[g_nmac].name), "%s", name);
    snprintf(g_mac[g_nmac].value, sizeof(g_mac[g_nmac].value), "%s", value ? value : "");
    g_nmac++;
}

static int emit_bytes(CodEmit* e, const char* s, size_t n) {
    char tmp[256];
    if (!s) return 0;
    while (n) {
        size_t c = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;
        memcpy(tmp, s, c);
        tmp[c] = '\0';
        if (!cod_emit_add(e, tmp)) return 0;
        s += c;
        n -= c;
    }
    return 1;
}

static int line_include(const char* s, size_t n, char* path, size_t cap) {
    size_t i = 0;
    size_t k = 0;
    char endc;
    while (i < n && isspace((unsigned char)s[i])) i++;
    if (i + 8 > n || strncmp(s + i, "#include", 8) != 0) return 0;
    i += 8;
    if (i < n && !isspace((unsigned char)s[i])) return 0;
    while (i < n && isspace((unsigned char)s[i])) i++;
    if (i >= n || (s[i] != '"' && s[i] != '<')) return 0;
    endc = s[i] == '<' ? '>' : '"';
    i++;
    while (i < n && s[i] != endc && s[i] != '\r') {
        char c = s[i] == '\\' ? '/' : s[i];
        if (k + 1 < cap) path[k++] = c;
        i++;
    }
    path[k] = '\0';
    return k > 0;
}

static int stack_has(const char** stack, int n, const char* path) {
    int i;
    for (i = 0; i < n; i++) {
        if (cod_strcasecmp(stack[i], path) == 0) return 1;
    }
    return 0;
}

static int inline_includes(const char* s, size_t n, CodEmit* out,
                           const char** stack, int depth) {
    size_t i = 0;
    if (!s) return 0;
    while (i < n) {
        size_t line = i;
        size_t end;
        int nl = 0;
        char path[256];
        while (i < n && s[i] != '\n') i++;
        end = i;
        if (end > line && s[end - 1] == '\r') end--;
        if (i < n && s[i] == '\n') {
            nl = 1;
            i++;
        }
        if (depth < 8 && line_include(s + line, end - line, path, sizeof(path)) &&
            !stack_has(stack, depth, path) && g_inc_fn) {
            unsigned char* inc = NULL;
            unsigned int isz = 0;
            const char* next[8];
            int d;
            if (g_inc_fn(g_inc_user, path, &inc, &isz) && inc) {
                for (d = 0; d < depth && d < 8; d++) next[d] = stack[d];
                next[depth] = path;
                if (!inline_includes((const char*)inc, isz, out, next, depth + 1)) {
                    free(inc);
                    return 0;
                }
                free(inc);
                if (nl && !cod_emit_add(out, "\n")) return 0;
                continue;
            }
            free(inc);
        }
        if (!emit_bytes(out, s + line, end - line)) return 0;
        if (nl && !cod_emit_add(out, "\n")) return 0;
    }
    return 1;
}

static void collect_defines(const char* s, size_t n) {
    size_t i = 0;
    g_nmac = 0;
    if (!s) return;
    while (i < n) {
        size_t line = i;
        size_t end;
        size_t k;
        char name[64];
        char value[192];
        size_t vn;
        while (i < n && s[i] != '\n') i++;
        end = i;
        if (end > line && s[end - 1] == '\r') end--;
        if (i < n && s[i] == '\n') i++;
        while (line < end && isspace((unsigned char)s[line])) line++;
        if (line + 7 > end || strncmp(s + line, "#define", 7) != 0) continue;
        line += 7;
        if (line < end && !isspace((unsigned char)s[line])) continue;
        while (line < end && isspace((unsigned char)s[line])) line++;
        k = 0;
        if (line >= end || !(isalpha((unsigned char)s[line]) || s[line] == '_')) continue;
        while (line < end && (isalnum((unsigned char)s[line]) || s[line] == '_')) {
            if (k + 1 < sizeof(name)) name[k++] = s[line];
            line++;
        }
        name[k] = '\0';
        if (line < end && s[line] == '(') continue;
        while (line < end && isspace((unsigned char)s[line])) line++;
        vn = 0;
        while (line < end && vn + 1 < sizeof(value)) {
            if (s[line] == '/' && line + 1 < end && s[line + 1] == '/') break;
            value[vn++] = s[line++];
        }
        while (vn > 0 && isspace((unsigned char)value[vn - 1])) vn--;
        value[vn] = '\0';
        mac_store(name, value);
    }
}

static int expand_idents(const char* s, size_t n, CodEmit* out, int depth) {
    size_t i = 0;
    int in_str = 0;
    if (!s) return 0;
    while (i < n) {
        char c = s[i];
        if (!in_str && c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') {
                char one[2] = { s[i], '\0' };
                if (!cod_emit_add(out, one)) return 0;
                i++;
            }
            continue;
        }
        if (!in_str && c == '"' ) {
            in_str = 1;
            if (!cod_emit_add(out, "\"")) return 0;
            i++;
            continue;
        }
        if (in_str && c == '\\' && i + 1 < n) {
            char one[3] = { s[i], s[i + 1], '\0' };
            if (!cod_emit_add(out, one)) return 0;
            i += 2;
            continue;
        }
        if (in_str && c == '"') in_str = 0;
        if (!in_str && (isalpha((unsigned char)c) || c == '_')) {
            char name[64];
            size_t k = 0;
            const char* val;
            while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '_')) {
                if (k + 1 < sizeof(name)) name[k++] = s[i];
                i++;
            }
            name[k] = '\0';
            val = mac_find(name);
            if (val && depth < 8) {
                if (!expand_idents(val, strlen(val), out, depth + 1)) return 0;
            } else if (!cod_emit_add(out, name)) return 0;
            continue;
        }
        {
            char one[2] = { c, '\0' };
            if (!cod_emit_add(out, one)) return 0;
        }
        i++;
    }
    return 1;
}

/* Inline #include, then replace #define names so rect OPTIONS_WINDOW_POS
 * becomes numbers before the menu lexer runs. */
static int prepare_menu(const void* data, size_t size,
                        unsigned char** out, unsigned int* out_size) {
    CodEmit inlined;
    CodEmit expanded;
    unsigned char* mid = NULL;
    unsigned int msz = 0;
    int ok;
    if (!data || !out || !out_size) return 0;
    if (!cod_emit_init(&inlined)) return 0;
    if (!inline_includes((const char*)data, size, &inlined, NULL, 0) ||
        !cod_emit_take(&inlined, &mid, &msz)) {
        cod_emit_free(&inlined);
        free(mid);
        return 0;
    }
    collect_defines((const char*)mid, msz);
    if (!cod_emit_init(&expanded)) {
        free(mid);
        return 0;
    }
    ok = expand_idents((const char*)mid, msz, &expanded, 0) &&
         cod_emit_take(&expanded, out, out_size);
    free(mid);
    if (!ok) cod_emit_free(&expanded);
    return ok;
}

int ui_menu_to_helix(const void* data, size_t size,
                     unsigned char** out_buf, unsigned int* out_size) {
    CodUiFile file;
    unsigned char* prepared = NULL;
    unsigned int psz = 0;
    const void* src = data;
    size_t src_n = size;
    int ok;
    if (prepare_menu(data, size, &prepared, &psz) && prepared) {
        src = prepared;
        src_n = psz;
    }
    if (!ui_read(src, src_n, &file)) {
        free(prepared);
        return 0;
    }
    ok = ui_to_helix(&file, out_buf, out_size);
    ui_read_free(&file);
    free(prepared);
    return ok;
}
