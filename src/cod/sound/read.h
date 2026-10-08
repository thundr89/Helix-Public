/*
 * CoD sound-alias CSV reader.
 * Column names follow OpenCoDUO (coduomp src/sound/alias): name, sequence,
 * file, subtitle, vol_min, vol_max, pitch_min, pitch_max, dist_min, dist_max,
 * channel, type, loop, probability, loadspec, masterslave, lod_min, lod_max.
 * Unknown columns (CoD1 vol_mod and later extras) are skipped.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SOUND_READ_H
#define SOUND_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoundAlias {
    char name[64];
    char file[96];
    char subtitle[128];
    char channel[24];
    char type[16];
    int sequence;
    float vol_min, vol_max;
    float pitch_min, pitch_max;
    float dist_min, dist_max;
    float probability;
    int looping; /* 1 = looping, 0 = nonlooping */
} SoundAlias;

typedef struct SoundAliasFile {
    SoundAlias* aliases;
    int count;
} SoundAliasFile;

/* 1 when the buffer is a RIFF/WAVE sample. */
int sound_read_is_wav(const void* data, size_t size);

/*
 * Parses a soundaliases CSV into CoD alias records.
 * Returns 0 when the header has no name and file columns.
 * Rows missing either field are skipped. Caller frees with sound_read_free.
 */
int sound_read_aliases(const void* data, size_t size, SoundAliasFile* out);
void sound_read_free(SoundAliasFile* file);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_READ_H */
