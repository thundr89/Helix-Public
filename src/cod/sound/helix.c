/*
 * Sound aliases → Helix sound-shader text.
 * Helix has one volume and a max distance. CoD's vol_min is the volume.
 * A CoD dist_max of 0 (the OpenCoDUO default, "no cap") becomes 1250,
 * which is the Helix sound-shader default range.
 * A file path without a sound/ prefix is stored under sound/, matching the PK3.
 * WAV bytes are already what Helix decodes, so they are not rewritten here.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "sound/helix.h"
#include "emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sound_to_helix(const SoundAliasFile* aliases,
                   unsigned char** out_buf, unsigned int* out_size) {
    CodEmit e;
    int i;
    if (!aliases || !out_buf || !out_size) return 0;
    if (!cod_emit_init(&e)) return 0;
    for (i = 0; i < aliases->count; i++) {
        const SoundAlias* a = &aliases->aliases[i];
        float max_dist = a->dist_max > 0.f ? a->dist_max : 1250.f;
        int looping = a->looping ? 1 : 0;
        const char* file = a->file;
        if (strstr(a->channel, "ambient") || strstr(a->channel, "music")) {
            max_dist = 100000.f;
            looping = 1;
        }
        char prefixed[128];
        if (strncmp(file, "sound/", 6) != 0 && strncmp(file, "sound\\", 6) != 0) {
            snprintf(prefixed, sizeof(prefixed), "sound/%s", file);
            file = prefixed;
        }
        if (!cod_emit_fmt(&e, "sound \"%s\" {\n", a->name) ||
            !cod_emit_fmt(&e, "  file \"%s\"\n", file) ||
            !cod_emit_fmt(&e, "  volume %g\n", a->vol_min) ||
            !cod_emit_fmt(&e, "  min_dist %g\n", a->dist_min) ||
            !cod_emit_fmt(&e, "  max_dist %g\n", max_dist) ||
            !cod_emit_fmt(&e, "  looping %d\n", looping) ||
            !cod_emit_fmt(&e, "  channel \"%s\"\n", a->channel) ||
            !cod_emit_fmt(&e, "  type \"%s\"\n", a->type) ||
            !cod_emit_add(&e, "}\n")) {
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

int sound_append_gameplay(const CodArchive* ar, unsigned char** buf, unsigned int* size) {
    static const char* files[] = {
        "sound/weapons/bar/bar_01.wav",
        "sound/weapons/m1garand/m1garand_01.wav",
    };
    static const char* shaders[] = {"weapon/mg", "weapon/sg"};
    int i;
    if (!ar || !buf || !*buf || !size) return 0;
    for (i = 0; i < 2; i++) {
        char marker[64];
        char line[256];
        size_t extra;
        unsigned char* grown;
        if (!cod_archive_contains(ar, files[i])) continue;
        snprintf(marker, sizeof(marker), "sound \"%s\"", shaders[i]);
        if (strstr((const char*)*buf, marker)) continue;
        snprintf(line, sizeof(line),
                 "sound \"%s\" {\n  file \"%s\"\n  volume 1\n  min_dist 24\n  max_dist 900\n  looping 0\n}\n",
                 shaders[i], files[i]);
        extra = strlen(line);
        grown = (unsigned char*)realloc(*buf, (size_t)(*size) + extra + 1);
        if (!grown) return 0;
        memcpy(grown + *size, line, extra + 1);
        *buf = grown;
        *size += (unsigned int)extra;
    }
    return 1;
}

int sound_csv_to_helix(const void* data, size_t size,
                       unsigned char** out_buf, unsigned int* out_size) {
    SoundAliasFile file;
    int ok;
    if (!sound_read_aliases(data, size, &file)) return 0;
    ok = sound_to_helix(&file, out_buf, out_size);
    sound_read_free(&file);
    return ok;
}
