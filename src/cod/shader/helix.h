/*
 * CoD .shader text to Helix .mtr (albedo_map points at the PNG path).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef SHADER_HELIX_H
#define SHADER_HELIX_H

#include "shader/read.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int shader_to_helix(const CodShaderFile* file,
                    unsigned char** out_buf, unsigned int* out_size);
int shader_source_to_helix(const void* data, size_t size,
                           unsigned char** out_buf, unsigned int* out_size);

/* surfaceparm nodraw/caulk/hint/skip/portal names, filled while shaders are read. */
void shader_note_invisible(const CodShaderFile* file);
int shader_is_invisible(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* SHADER_HELIX_H */
