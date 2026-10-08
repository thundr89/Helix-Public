/*
 * CoD1 .shader text. One file holds many shader definitions.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "shader/read.h"
#include "cod_util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char* skip_ws_comments(const char* p, const char* end) {
    for (;;) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
            if (p + 1 < end) p += 2;
            continue;
        }
        return p;
    }
}

static int take_token(const char** pp, const char* end, char* out, size_t cap) {
    const char* p = skip_ws_comments(*pp, end);
    size_t n = 0;
    if (p >= end) return 0;
    if (*p == '"') {
        p++;
        while (p < end && *p != '"') {
            if (n + 1 < cap) out[n++] = *p;
            p++;
        }
        if (p < end && *p == '"') p++;
        out[n] = '\0';
        *pp = p;
        return 1;
    }
    if (*p == '{' || *p == '}') {
        if (cap < 2) return 0;
        out[0] = *p;
        out[1] = '\0';
        *pp = p + 1;
        return 1;
    }
    while (p < end && !isspace((unsigned char)*p) && *p != '{' && *p != '}' && *p != '"') {
        if (n + 1 < cap) out[n++] = *p;
        p++;
    }
    if (n == 0) return 0;
    out[n] = '\0';
    *pp = p;
    return 1;
}

static int useful_image(const char* path) {
    if (!path || !path[0]) return 0;
    if (path[0] == '$') return 0;
    if (strcmp(path, "*white") == 0) return 0;
    if (strstr(path, "lightmap")) return 0;
    return 1;
}

static int parm_invisible(const char* t) {
    return strcmp(t, "nodraw") == 0 || strcmp(t, "caulk") == 0 || strcmp(t, "hint") == 0 ||
           strcmp(t, "skip") == 0 || strcmp(t, "clusterportal") == 0 ||
           strcmp(t, "areaportal") == 0 || strcmp(t, "portal") == 0;
}

int shader_read(const void* data, size_t size, CodShaderFile* out) {
    const char* p;
    const char* end;
    char tok[256];
    char pending[128];
    int depth = 0;
    int have_pending = 0;
    int perlight = 0;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return 0;
    p = (const char*)data;
    end = p + size;
    pending[0] = '\0';
    while (take_token(&p, end, tok, sizeof(tok))) {
        if (strcmp(tok, "{") == 0) {
            if (depth == 0 && have_pending) {
                CodShader s;
                CodShader* grown;
                memset(&s, 0, sizeof(s));
                memcpy(s.name, pending, sizeof(s.name) - 1);
                grown = (CodShader*)realloc(out->shaders, (size_t)(out->count + 1) * sizeof(CodShader));
                if (!grown) {
                    shader_read_free(out);
                    return 0;
                }
                out->shaders = grown;
                out->shaders[out->count++] = s;
                have_pending = 0;
            }
            depth++;
            continue;
        }
        if (strcmp(tok, "}") == 0) {
            if (depth > 0) depth--;
            /* A stage ends when the shader body (depth 1) is visible again. */
            if (depth < 2) perlight = 0;
            continue;
        }
        if (depth == 0) {
            strncpy(pending, tok, sizeof(pending) - 1);
            pending[sizeof(pending) - 1] = '\0';
            have_pending = 1;
            continue;
        }
        if (out->count > 0 &&
            (strcmp(tok, "map") == 0 || strcmp(tok, "clampmap") == 0 || strcmp(tok, "animmap") == 0)) {
            char image[256];
            CodShader* s = &out->shaders[out->count - 1];
            if (strcmp(tok, "animmap") == 0 && !take_token(&p, end, image, sizeof(image))) break;
            if (!take_token(&p, end, image, sizeof(image))) break;
            /* `map clamp textures/foo` — clamp is a modifier, not the path. */
            if (strcmp(tok, "map") == 0 &&
                (cod_strcasecmp(image, "clamp") == 0 || cod_strcasecmp(image, "clampx") == 0 ||
                 cod_strcasecmp(image, "clampy") == 0)) {
                if (!take_token(&p, end, image, sizeof(image))) break;
            }
            /* A gameplay map replaces qer_editorimage. */
            if (useful_image(image)) {
                strncpy(s->image, image, sizeof(s->image) - 1);
                s->image[sizeof(s->image) - 1] = '\0';
            }
        } else if (out->count > 0 && strcmp(tok, "qer_editorimage") == 0) {
            char image[256];
            CodShader* s = &out->shaders[out->count - 1];
            if (!take_token(&p, end, image, sizeof(image))) break;
            if (s->image[0] == '\0' && useful_image(image)) {
                strncpy(s->image, image, sizeof(s->image) - 1);
                s->image[sizeof(s->image) - 1] = '\0';
            }
        } else if (out->count > 0 && strcmp(tok, "surfaceparm") == 0) {
            char parm[64];
            CodShader* s = &out->shaders[out->count - 1];
            if (!take_token(&p, end, parm, sizeof(parm))) break;
            if (parm_invisible(parm)) s->invisible = 1;
        } else if (out->count > 0 && strcmp(tok, "blendFunc") == 0) {
            char a[64];
            char b[64];
            CodShader* s = &out->shaders[out->count - 1];
            a[0] = b[0] = '\0';
            if (!take_token(&p, end, a, sizeof(a))) break;
            /* `blend` and `add` are single tokens. GL_SRC_ALPHA takes a second. */
            if (strncmp(a, "GL_", 3) == 0) take_token(&p, end, b, sizeof(b));
            /* A perlight add lights the surface. It is not the base blend. */
            if (perlight) continue;
            if (strcmp(a, "add") == 0 || (strcmp(a, "GL_ONE") == 0 && strcmp(b, "GL_ONE") == 0))
                s->blend = 2;
            /* `filter` and GL_DST_COLOR multiply a lightmap onto the diffuse. Nothing
             * has painted a lightmap, so this stage must not multiply the clear color. */
            else if ((strcmp(a, "filter") != 0 && strcmp(a, "GL_DST_COLOR") != 0) &&
                     (strcmp(a, "blend") == 0 || strcmp(a, "GL_SRC_ALPHA") == 0 ||
                      strcmp(b, "GL_ONE_MINUS_SRC_ALPHA") == 0))
                s->blend = s->blend == 2 ? 2 : 1;
        } else if (out->count > 0 && strcmp(tok, "alphaFunc") == 0) {
            char fn[64];
            CodShader* s = &out->shaders[out->count - 1];
            if (!take_token(&p, end, fn, sizeof(fn))) break;
            if (strcmp(fn, "GE128") == 0 || strcmp(fn, "GT128") == 0) s->alpha_test = 0.5f;
            else if (strcmp(fn, "GT0") == 0 || strcmp(fn, "GE0") == 0) s->alpha_test = 0.004f;
        } else if (out->count > 0 && strcmp(tok, "perlight") == 0) {
            perlight = 1;
        } else if (out->count > 0 && strcmp(tok, "tcMod") == 0) {
            char kind[32];
            CodShader* s = &out->shaders[out->count - 1];
            if (!take_token(&p, end, kind, sizeof(kind))) break;
            if (strcmp(kind, "transform") == 0 && !s->has_uv_xform) {
                char num[32];
                int k;
                for (k = 0; k < 6; k++) {
                    if (!take_token(&p, end, num, sizeof(num))) break;
                    s->uv_xform[k] = (float)strtod(num, NULL);
                }
                if (k == 6) s->has_uv_xform = 1;
            }
        } else if (out->count > 0 && strcmp(tok, "polygonOffset") == 0) {
            out->shaders[out->count - 1].polygon_offset = 1;
        } else if (out->count > 0 &&
                   (strcmp(tok, "cull") == 0 || strcmp(tok, "culltwosided") == 0)) {
            CodShader* s = &out->shaders[out->count - 1];
            if (strcmp(tok, "culltwosided") == 0) {
                s->two_sided = 1;
            } else {
                char mode[64];
                if (!take_token(&p, end, mode, sizeof(mode))) break;
                if (strcmp(mode, "disable") == 0 || strcmp(mode, "none") == 0 ||
                    strcmp(mode, "twosided") == 0 || strcmp(mode, "twoSided") == 0)
                    s->two_sided = 1;
            }
        }
    }
    return out->count > 0;
}

void shader_read_free(CodShaderFile* file) {
    if (!file) return;
    free(file->shaders);
    file->shaders = NULL;
    file->count = 0;
}
