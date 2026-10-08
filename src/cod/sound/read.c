/*
 * CoD sound-alias CSV reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "sound/read.h"
#include "cod_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    COL_NONE = 0,
    COL_NAME,
    COL_SEQUENCE,
    COL_FILE,
    COL_SUBTITLE,
    COL_VOL_MIN,
    COL_VOL_MAX,
    COL_PITCH_MIN,
    COL_PITCH_MAX,
    COL_DIST_MIN,
    COL_DIST_MAX,
    COL_CHANNEL,
    COL_TYPE,
    COL_LOOP,
    COL_PROBABILITY
};

int sound_read_is_wav(const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    if (!data || size < 12) return 0;
    return memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WAVE", 4) == 0;
}

static const char* skip_bom(const char* p, const char* end) {
    const unsigned char* u = (const unsigned char*)p;
    if ((size_t)(end - p) >= 3 && u[0] == 0xef && u[1] == 0xbb && u[2] == 0xbf) return p + 3;
    return p;
}

static int column_id(const char* name) {
    if (cod_strcasecmp(name, "name") == 0) return COL_NAME;
    if (cod_strcasecmp(name, "sequence") == 0) return COL_SEQUENCE;
    if (cod_strcasecmp(name, "file") == 0) return COL_FILE;
    if (cod_strcasecmp(name, "subtitle") == 0) return COL_SUBTITLE;
    if (cod_strcasecmp(name, "vol_min") == 0) return COL_VOL_MIN;
    if (cod_strcasecmp(name, "vol_max") == 0) return COL_VOL_MAX;
    if (cod_strcasecmp(name, "pitch_min") == 0) return COL_PITCH_MIN;
    if (cod_strcasecmp(name, "pitch_max") == 0) return COL_PITCH_MAX;
    if (cod_strcasecmp(name, "dist_min") == 0) return COL_DIST_MIN;
    if (cod_strcasecmp(name, "dist_max") == 0) return COL_DIST_MAX;
    if (cod_strcasecmp(name, "channel") == 0) return COL_CHANNEL;
    if (cod_strcasecmp(name, "type") == 0) return COL_TYPE;
    if (cod_strcasecmp(name, "loop") == 0) return COL_LOOP;
    if (cod_strcasecmp(name, "probability") == 0) return COL_PROBABILITY;
    return COL_NONE;
}

/* One CSV field. *eol is set when the field ended the line. */
static int next_field(const char** pp, const char* end, char* out, size_t cap, int* eol) {
    const char* p = *pp;
    size_t n = 0;
    if (eol) *eol = 0;
    if (p >= end) {
        if (cap) out[0] = '\0';
        if (eol) *eol = 1;
        return 0;
    }
    if (*p == '"') {
        p++;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\' && p < end) c = *p++;
            if (n + 1 < cap) out[n++] = c;
        }
        if (p < end && *p == '"') p++;
    } else {
        while (p < end && *p != ',' && *p != '\n' && *p != '\r') {
            if (n + 1 < cap) out[n++] = *p;
            p++;
        }
    }
    out[n] = '\0';
    if (p < end && *p == ',') {
        p++;
    } else {
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
        if (eol) *eol = 1;
    }
    *pp = p;
    return 1;
}

static void alias_defaults(SoundAlias* a) {
    memset(a, 0, sizeof(*a));
    a->vol_min = 1.f;
    a->vol_max = 1.f;
    a->pitch_min = 1.f;
    a->pitch_max = 1.f;
    a->dist_min = 120.f;
    a->dist_max = 0.f;
    a->probability = 1.f;
    memcpy(a->channel, "auto", 5);
    memcpy(a->type, "loaded", 7);
}

static void apply_field(SoundAlias* a, int col, const char* text, int* saw_vol_max) {
    switch (col) {
    case COL_NAME: snprintf(a->name, sizeof(a->name), "%s", text); break;
    case COL_SEQUENCE: a->sequence = atoi(text); break;
    case COL_FILE: snprintf(a->file, sizeof(a->file), "%s", text); break;
    case COL_SUBTITLE: snprintf(a->subtitle, sizeof(a->subtitle), "%s", text); break;
    case COL_VOL_MIN: a->vol_min = (float)atof(text); break;
    case COL_VOL_MAX:
        a->vol_max = (float)atof(text);
        if (saw_vol_max) *saw_vol_max = 1;
        break;
    case COL_PITCH_MIN:
        a->pitch_min = (float)atof(text);
        a->pitch_max = a->pitch_min;
        break;
    case COL_PITCH_MAX: a->pitch_max = (float)atof(text); break;
    case COL_DIST_MIN: a->dist_min = (float)atof(text); break;
    case COL_DIST_MAX: a->dist_max = (float)atof(text); break;
    case COL_CHANNEL: snprintf(a->channel, sizeof(a->channel), "%s", text); break;
    case COL_TYPE: snprintf(a->type, sizeof(a->type), "%s", text); break;
    case COL_LOOP: a->looping = cod_strcasecmp(text, "looping") == 0; break;
    case COL_PROBABILITY: a->probability = (float)atof(text); break;
    default: break;
    }
}

static int push_alias(SoundAliasFile* out, const SoundAlias* a) {
    SoundAlias* grown;
    if (!a->name[0] || !a->file[0]) return 1;
    grown = (SoundAlias*)realloc(out->aliases, (size_t)(out->count + 1) * sizeof(SoundAlias));
    if (!grown) return 0;
    out->aliases = grown;
    out->aliases[out->count++] = *a;
    return 1;
}

int sound_read_aliases(const void* data, size_t size, SoundAliasFile* out) {
    const char* p;
    const char* end;
    int cols[64];
    int ncol = 0;
    int saw_name = 0, saw_file = 0;

    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return 0;
    p = skip_bom((const char*)data, (const char*)data + size);
    end = (const char*)data + size;

    while (p < end) {
        int eol = 0;
        char field[256];
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p < end && (*p == '\r' || *p == '\n')) {
            if (*p == '\r') p++;
            if (p < end && *p == '\n') p++;
            continue;
        }
        if (p < end && *p == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        if (ncol == 0) {
            do {
                if (!next_field(&p, end, field, sizeof(field), &eol)) break;
                if (ncol < 64) {
                    cols[ncol] = column_id(field);
                    if (cols[ncol] == COL_NAME) saw_name = 1;
                    if (cols[ncol] == COL_FILE) saw_file = 1;
                    ncol++;
                }
            } while (!eol && p < end);
            if (!saw_name || !saw_file) {
                sound_read_free(out);
                return 0;
            }
            continue;
        }
        {
            SoundAlias alias;
            int col = 0;
            int saw_vol_max = 0;
            alias_defaults(&alias);
            do {
                if (!next_field(&p, end, field, sizeof(field), &eol)) break;
                if (col < ncol && field[0]) apply_field(&alias, cols[col], field, &saw_vol_max);
                col++;
            } while (!eol && p < end);
            if (!saw_vol_max) alias.vol_max = alias.vol_min;
            if (!push_alias(out, &alias)) {
                sound_read_free(out);
                return 0;
            }
        }
    }
    return 1;
}

void sound_read_free(SoundAliasFile* file) {
    if (!file) return;
    free(file->aliases);
    file->aliases = NULL;
    file->count = 0;
}
