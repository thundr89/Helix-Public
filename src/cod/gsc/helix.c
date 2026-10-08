/*
 * GSC facts → Helix .hxsc game-mode text.
 * Helix only reads top-level level.* assignments (gametype, timelimit,
 * fraglimit, teamplay, warmup, capturelimit, nextmap, ambient).
 * ambientPlay("name") becomes level.ambient. getcvar is not given a number.
 * A literal gametype of tdm or ctf also sets teamplay, matching Helix.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "gsc/helix.h"
#include "emit.h"
#include "cod_util.h"

#include <stdio.h>

static int known_key(const char* key) {
    return cod_strcasecmp(key, "gametype") == 0 || cod_strcasecmp(key, "gamemode") == 0 ||
           cod_strcasecmp(key, "timelimit") == 0 || cod_strcasecmp(key, "fraglimit") == 0 ||
           cod_strcasecmp(key, "teamplay") == 0 || cod_strcasecmp(key, "warmup") == 0 ||
           cod_strcasecmp(key, "capturelimit") == 0 || cod_strcasecmp(key, "nextmap") == 0 ||
           cod_strcasecmp(key, "ambient") == 0;
}

int gsc_to_helix(const GscFile* script, unsigned char** out_buf, unsigned int* out_size) {
    CodEmit e;
    int i;
    int saw_teamplay = 0;
    const char* gametype = NULL;
    if (!script || !out_buf || !out_size) return 0;
    if (!cod_emit_init(&e)) return 0;
    if (!cod_emit_add(&e, "// helix game mode, from CoD GSC\n")) {
        cod_emit_free(&e);
        return 0;
    }
    for (i = 0; i < script->ncalls; i++) {
        if (cod_strcasecmp(script->calls[i].name, "ambientPlay") == 0 && script->calls[i].arg[0]) {
            if (!cod_emit_fmt(&e, "level.ambient = \"%s\";\n", script->calls[i].arg)) {
                cod_emit_free(&e);
                return 0;
            }
        }
    }
    for (i = 0; i < script->nassigns; i++) {
        const GscAssign* a = &script->assigns[i];
        if (!a->literal || !known_key(a->key)) continue;
        if (cod_strcasecmp(a->key, "teamplay") == 0) saw_teamplay = 1;
        if (cod_strcasecmp(a->key, "gametype") == 0 || cod_strcasecmp(a->key, "gamemode") == 0)
            gametype = a->value;
        if (a->is_string) {
            if (!cod_emit_fmt(&e, "level.%s = \"%s\";\n", a->key, a->value)) {
                cod_emit_free(&e);
                return 0;
            }
        } else if (!cod_emit_fmt(&e, "level.%s = %s;\n", a->key, a->value)) {
            cod_emit_free(&e);
            return 0;
        }
    }
    if (!saw_teamplay && gametype &&
        (cod_strcasecmp(gametype, "tdm") == 0 || cod_strcasecmp(gametype, "ctf") == 0)) {
        if (!cod_emit_add(&e, "level.teamplay = 1;\n")) {
            cod_emit_free(&e);
            return 0;
        }
    }
    if (!cod_emit_take(&e, out_buf, out_size)) {
        cod_emit_free(&e);
        return 0;
    }
    return 1;
}

static const char* assign_value(const GscFile* script, const char* key) {
    int i;
    if (!script || !key) return NULL;
    for (i = 0; i < script->nassigns; i++) {
        if (script->assigns[i].is_string && cod_strcasecmp(script->assigns[i].key, key) == 0)
            return script->assigns[i].value;
    }
    return NULL;
}

static int body_token(const char* s) {
    if (!s || !s[0]) return 0;
    for (; *s; s++) {
        char c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

int gsc_allied_body(const GscFile* script, char* out, size_t cap) {
    const char* nat;
    const char* type;
    const char* var;
    char type_key[80];
    char var_key[96];
    if (!script || !out || cap < 16) return 0;
    out[0] = '\0';
    nat = assign_value(script, "allies");
    if (!body_token(nat)) return 0;
    snprintf(type_key, sizeof(type_key), "%s_soldiertype", nat);
    snprintf(var_key, sizeof(var_key), "%s_soldiervariation", nat);
    type = assign_value(script, type_key);
    var = assign_value(script, var_key);
    if (!body_token(type)) return 0;
    if (var && var[0]) {
        if (!body_token(var)) return 0;
        snprintf(out, cap, "xmodel/playerbody_%s_%s_%s", nat, type, var);
    } else {
        snprintf(out, cap, "xmodel/playerbody_%s_%s", nat, type);
    }
    return out[0] != '\0';
}

int gsc_source_to_helix(const void* data, size_t size,
                        unsigned char** out_buf, unsigned int* out_size) {
    GscFile file;
    int ok;
    if (!gsc_read(data, size, &file)) return 0;
    ok = gsc_to_helix(&file, out_buf, out_size);
    gsc_read_free(&file);
    return ok;
}
