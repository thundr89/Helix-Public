/*
 * CoD1 .shader text. One file holds many shader definitions.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef SHADER_READ_H
#define SHADER_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CodShader {
    char name[128];
    char image[128]; /* gameplay map / clampmap; editor image is only a fallback */
    float alpha_test; /* 0 = off, otherwise discard threshold in 0..1 */
    int blend; /* 0 opaque, 1 alpha, 2 additive */
    int two_sided;
    int polygon_offset; /* decal: draw in front of the surface it sits on */
    int invisible; /* nodraw, caulk, hint, skip, portal */
    int has_uv_xform; /* tcMod transform: s' = a*s + c*t + tx, t' = b*s + d*t + ty */
    float uv_xform[6];
} CodShader;

typedef struct CodShaderFile {
    CodShader* shaders;
    int count;
} CodShaderFile;

int shader_read(const void* data, size_t size, CodShaderFile* out);
void shader_read_free(CodShaderFile* file);

#ifdef __cplusplus
}
#endif

#endif /* SHADER_READ_H */
