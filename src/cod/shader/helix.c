#include <stdio.h>
/*
 * CoD .shader text to Helix .mtr. Image paths keep their extension.
 * Extensionless names stay extensionless so the host probes .dds first.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "shader/helix.h"
#include "emit.h"
#include "cod_util.h"

#include <string.h>

#define INVISIBLE_MAX 256
static char g_invisible[INVISIBLE_MAX][128];
static int g_invisible_n = 0;
static char g_sky_names[256][128],g_sky_boxes[256][128],g_sun_files[256][128];static int g_sky_count;
const char* shader_skybox(const char* name){int i;for(i=0;i<g_sky_count;i++)if(!strcmp(name,g_sky_names[i]))return g_sky_boxes[i];return "";}

const char* shader_sunfile(const char* name){int i;for(i=0;i<g_sky_count;i++)if(!strcmp(name,g_sky_names[i]))return g_sun_files[i];return "";}

void shader_note_invisible(const CodShaderFile* file) {
    int i;
    if (!file) return;
    for (i = 0; i < file->count; i++) {
        int k;
        int seen = 0;
        if(file->shaders[i].skybox[0]){
            for(k=0;k<g_sky_count;k++)if(!strcmp(g_sky_names[k],file->shaders[i].name))break;
            if(k<256){snprintf(g_sky_names[k],128,"%s",file->shaders[i].name);snprintf(g_sky_boxes[k],128,"%s",file->shaders[i].skybox);snprintf(g_sun_files[k],128,"%s",file->shaders[i].sunfile);if(k==g_sky_count)g_sky_count++;}
        }
        if (!file->shaders[i].invisible || !file->shaders[i].name[0]) continue;
        for (k = 0; k < g_invisible_n; k++) {
            if (strcmp(g_invisible[k], file->shaders[i].name) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen || g_invisible_n >= INVISIBLE_MAX) continue;
        strncpy(g_invisible[g_invisible_n], file->shaders[i].name, 127);
        g_invisible[g_invisible_n][127] = '\0';
        g_invisible_n++;
    }
}

int shader_is_invisible(const char* name) {
    int i;
    if (!name || !name[0]) return 0;
    for (i = 0; i < g_invisible_n; i++) {
        if (strcmp(g_invisible[i], name) == 0) return 1;
    }
    return 0;
}

static int decal_name(const char* name) {
    if (!name || !strstr(name, "decal")) return 0;
    if (strstr(name, "bullet") || strstr(name, "blood") || strstr(name, "impact") || strstr(name, "burn"))
        return 0;
    return 1;
}

static void image_path(const char* in, char* out, size_t cap) {
    size_t i;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!in) return;
    for (i = 0; in[i] && i + 1 < cap; i++) out[i] = (in[i] == '\\') ? '/' : in[i];
    out[i] = '\0';
}

int shader_to_helix(const CodShaderFile* file, unsigned char** out_buf, unsigned int* out_size) {
    CodEmit e;
    int i;
    int any = 0;
    if (!file || !out_buf || !out_size || file->count <= 0) return 0;
    if (!cod_emit_init(&e)) return 0;
    for (i = 0; i < file->count; i++) {
        char image[160];
        const CodShader* s = &file->shaders[i];
        int blend;
        int decal;
        if (!s->name[0]) continue;
        image_path(s->image[0] ? s->image : s->name, image, sizeof(image));
        if (!s->image[0] && strncmp(s->name, "textures/", 9) != 0 && strncmp(s->name, "gfx/", 4) != 0)
            image[0] = '\0';
        blend = s->blend;
        decal = s->polygon_offset || decal_name(s->name);
        if (decal && blend == 0) blend = 1;
        if (s->invisible) {
            if (!cod_emit_fmt(&e, "material %s {\n  nodraw\n}\n", s->name)) {
                cod_emit_free(&e);
                return 0;
            }
        } else if (image[0]) {
            if (!cod_emit_fmt(&e, "material %s {\n  albedo_map %s\n", s->name, image)) {
                cod_emit_free(&e);
                return 0;
            }
            if (s->two_sided && !cod_emit_add(&e, "  twoSided\n")) {
                cod_emit_free(&e);
                return 0;
            }
            if (s->has_uv_xform &&
                !cod_emit_fmt(&e, "  uvTransform %.9g %.9g %.9g %.9g %.9g %.9g\n", s->uv_xform[0], s->uv_xform[1],
                              s->uv_xform[2], s->uv_xform[3], s->uv_xform[4], s->uv_xform[5])) {
                cod_emit_free(&e);
                return 0;
            }
            if ((s->uv_scroll[0] != 0.f || s->uv_scroll[1] != 0.f) &&
                !cod_emit_fmt(&e, "  uvScroll %.9g %.9g\n", s->uv_scroll[0], s->uv_scroll[1])) {
                cod_emit_free(&e);
                return 0;
            }
            if (s->uv_rotate != 0.f &&
                !cod_emit_fmt(&e, "  uvRotate %.9g\n", s->uv_rotate)) {
                cod_emit_free(&e);
                return 0;
            }
            if (s->uv_turb[1] != 0.f &&
                !cod_emit_fmt(&e, "  uvTurb %.9g %.9g %.9g %.9g\n", s->uv_turb[0], s->uv_turb[1],
                              s->uv_turb[2], s->uv_turb[3])) {
                cod_emit_free(&e);
                return 0;
            }
            if (blend == 2 && !cod_emit_add(&e, "  blend additive\n")) {
                cod_emit_free(&e);
                return 0;
            } else if (blend == 1 && !cod_emit_add(&e, "  blend alpha\n")) {
                cod_emit_free(&e);
                return 0;
            }
            /* A second layer sits on the same plane. Offset it or the two fight. */
            if ((decal || blend) && !cod_emit_add(&e, "  polygonOffset\n")) {
                cod_emit_free(&e);
                return 0;
            }
            if (s->alpha_test > 0.f && !cod_emit_fmt(&e, "  alphaTest %g\n", s->alpha_test)) {
                cod_emit_free(&e);
                return 0;
            }
            if (!cod_emit_add(&e, "}\n")) {
                cod_emit_free(&e);
                return 0;
            }
        } else if (!cod_emit_fmt(&e, "material %s {\n}\n", s->name)) {
            cod_emit_free(&e);
            return 0;
        }
        any = 1;
    }
    if (!any || !cod_emit_take(&e, out_buf, out_size)) {
        cod_emit_free(&e);
        return 0;
    }
    return 1;
}

int shader_source_to_helix(const void* data, size_t size,
                           unsigned char** out_buf, unsigned int* out_size) {
    CodShaderFile file;
    int ok;
    if (!shader_read(data, size, &file)) return 0;
    shader_note_invisible(&file);
    ok = shader_to_helix(&file, out_buf, out_size);
    shader_read_free(&file);
    return ok;
}
