/*
 * CoD1 BSP Automatic Navigation Area & Reachability Generator (ZBot-style)
 * plus virtual bot profile & chatter catalog for hxfs_cod.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bot/cod_nav.h"
#include "emit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COD_NAV_MAX_AREAS 384

void cod_nav_init(CodNavBuilder* nb) {
    if (!nb) return;
    nb->boxes = NULL;
    nb->count = 0;
    nb->cap = 0;
}

void cod_nav_free(CodNavBuilder* nb) {
    if (!nb) return;
    free(nb->boxes);
    nb->boxes = NULL;
    nb->count = 0;
    nb->cap = 0;
}

static void nav_push_box(CodNavBuilder* nb, const float min[3], const float max[3], int flags) {
    int i;
    if (!nb || nb->count >= COD_NAV_MAX_AREAS) return;
    for (i = 0; i < 3; i++) {
        if (!isfinite(min[i]) || !isfinite(max[i]) || max[i] <= min[i]) return;
    }
    /* Deduplicate nearly identical areas */
    for (i = 0; i < nb->count; i++) {
        float cx0 = 0.5f * (nb->boxes[i].min[0] + nb->boxes[i].max[0]);
        float cy0 = 0.5f * (nb->boxes[i].min[1] + nb->boxes[i].max[1]);
        float cz0 = nb->boxes[i].min[2];
        float cx1 = 0.5f * (min[0] + max[0]);
        float cy1 = 0.5f * (min[1] + max[1]);
        float cz1 = min[2];
        float dx = cx1 - cx0, dy = cy1 - cy0, dz = fabsf(cz1 - cz0);
        if (dx * dx + dy * dy < 36.f * 36.f && dz < 24.f && nb->boxes[i].flags == flags) {
            return;
        }
    }
    if (nb->count == nb->cap) {
        int ncap = nb->cap ? nb->cap * 2 : 64;
        CodNavBox* grown;
        if (ncap > COD_NAV_MAX_AREAS) ncap = COD_NAV_MAX_AREAS;
        grown = (CodNavBox*)realloc(nb->boxes, (size_t)ncap * sizeof(CodNavBox));
        if (!grown) return;
        nb->boxes = grown;
        nb->cap = ncap;
    }
    memcpy(nb->boxes[nb->count].min, min, sizeof(float) * 3);
    memcpy(nb->boxes[nb->count].max, max, sizeof(float) * 3);
    nb->boxes[nb->count].flags = flags;
    nb->count++;
}

void cod_nav_add_walkable_box(CodNavBuilder* nb, const float min[3], const float max[3]) {
    float amin[3], amax[3];
    float wx, wy, wz;
    if (!nb || !min || !max) return;
    wx = max[0] - min[0];
    wy = max[1] - min[1];
    wz = max[2] - min[2];
    /* Must be wide enough for a player hull (30x30) and not a thin wall */
    if (wx < 32.f || wy < 32.f || wz > 128.f) return;
    /* Create a standing volume on top of the brush surface */
    amin[0] = min[0];
    amin[1] = min[1];
    amin[2] = max[2];
    amax[0] = max[0];
    amax[1] = max[1];
    amax[2] = max[2] + 72.f;
    nav_push_box(nb, amin, amax, 0);
}

void cod_nav_add_ladder_box(CodNavBuilder* nb, const float min[3], const float max[3]) {
    float amin[3], amax[3];
    if (!nb || !min || !max) return;
    if (max[2] - min[2] < 32.f) return;
    amin[0] = min[0] - 16.f;
    amin[1] = min[1] - 16.f;
    amin[2] = min[2];
    amax[0] = max[0] + 16.f;
    amax[1] = max[1] + 16.f;
    amax[2] = max[2] + 32.f;
    nav_push_box(nb, amin, amax, 1);
}

void cod_nav_add_anchor_point(CodNavBuilder* nb, const float origin[3], int is_crouch) {
    float amin[3], amax[3];
    if (!nb || !origin) return;
    amin[0] = origin[0] - 48.f;
    amin[1] = origin[1] - 48.f;
    amin[2] = origin[2] - 24.f;
    amax[0] = origin[0] + 48.f;
    amax[1] = origin[1] + 48.f;
    amax[2] = origin[2] + (is_crouch ? 48.f : 72.f);
    nav_push_box(nb, amin, amax, is_crouch ? 2 : 4);
}

static float box_gap_xy(const CodNavBox* a, const CodNavBox* b) {
    float gx = 0.f, gy = 0.f;
    if (a->max[0] < b->min[0]) gx = b->min[0] - a->max[0];
    else if (b->max[0] < a->min[0]) gx = a->min[0] - b->max[0];
    if (a->max[1] < b->min[1]) gy = b->min[1] - a->max[1];
    else if (b->max[1] < a->min[1]) gy = a->min[1] - b->max[1];
    return sqrtf(gx * gx + gy * gy);
}

char* cod_nav_emit_hxmap(const CodNavBuilder* nb, size_t* out_len) {
    CodEmit e;
    int i, j;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    if (out_len) *out_len = 0;
    if (!nb || nb->count <= 0) return NULL;
    if (!cod_emit_init(&e)) return NULL;

    for (i = 0; i < nb->count; i++) {
        const CodNavBox* b = &nb->boxes[i];
        if (!cod_emit_fmt(&e, "area {\n  mins %.1f %.1f %.1f\n  maxs %.1f %.1f %.1f\n}\n",
                          b->min[0], b->min[1], b->min[2],
                          b->max[0], b->max[1], b->max[2])) {
            cod_emit_free(&e);
            return NULL;
        }
    }

    for (i = 0; i < nb->count; i++) {
        const CodNavBox* a = &nb->boxes[i];
        for (j = 0; j < nb->count; j++) {
            const CodNavBox* b = &nb->boxes[j];
            float gap, dz;
            const char* rtype = NULL;
            if (i == j) continue;
            gap = box_gap_xy(a, b);
            dz = b->min[2] - a->min[2];

            if ((a->flags & 1) || (b->flags & 1)) {
                /* Ladder connection */
                if (gap <= 96.f && b->max[2] >= a->min[2] - 32.f && a->max[2] >= b->min[2] - 32.f) {
                    rtype = "ladder";
                }
            } else if (gap <= 320.f) {
                if ((b->flags & 2) && fabsf(dz) <= 24.f) {
                    rtype = "crouch";
                } else if (dz >= -64.f && dz <= 18.f) {
                    rtype = "walk";
                } else if (dz > 18.f && dz <= 46.f && gap <= 140.f) {
                    rtype = "jump";
                } else if (fabsf(dz) <= 96.f && gap <= 192.f) {
                    /* Stairs / sloped terrain transition */
                    rtype = "walk";
                }
            }

            if (rtype) {
                if (!cod_emit_fmt(&e, "reach {\n  from %d\n  to %d\n  type %s\n}\n", i, j, rtype)) {
                    cod_emit_free(&e);
                    return NULL;
                }
            }
        }
    }

    if (!cod_emit_take(&e, &buf, &sz)) {
        cod_emit_free(&e);
        return NULL;
    }
    if (out_len) *out_len = (size_t)sz;
    return (char*)buf;
}

static const char k_zbot_profiles_txt[] =
    "# Counter-Strike 1.6 / CSCZ ZBot-style CoD1 Bot Profiles\n"
    "# Format: profile <name> <skill:low|mid|pro> <class:rifle|smg|support|sniper> <aggression> <teamwork> <reaction_ms> <aim_speed> <aim_error>\n"
    "profile \"Phoenix\" pro rifle 0.85 0.90 90 620 0.40\n"
    "profile \"Wolf\" pro smg 0.92 0.85 85 640 0.45\n"
    "profile \"Stone\" pro sniper 0.65 0.95 95 590 0.30\n"
    "profile \"Viper\" pro support 0.80 0.88 100 600 0.50\n"
    "profile \"Maverick\" mid smg 0.75 0.80 220 350 2.00\n"
    "profile \"panzer_hans\" mid rifle 0.60 0.85 240 330 2.20\n"
    "profile \"Sgt_Miller\" mid support 0.70 0.90 230 340 2.10\n"
    "profile \"Vasily\" mid sniper 0.55 0.75 250 320 1.80\n"
    "profile \"Price\" pro rifle 0.82 0.95 90 610 0.38\n"
    "profile \"Foley\" mid rifle 0.68 0.92 235 345 2.05\n"
    "profile \"Gunther\" low smg 0.45 0.60 440 180 5.00\n"
    "profile \"Pvt_Ryan\" low rifle 0.40 0.65 480 165 5.50\n"
    "profile \"Conscript_Ivan\" low smg 0.50 0.55 460 175 5.20\n"
    "profile \"Tommy\" low support 0.35 0.70 500 160 5.80\n";

static const char k_zbot_chatter_txt[] =
    "# ZBot Voice (vsay) and Chat Roast Catalog\n"
    "vsay_spot enemy_spotted\n"
    "vsay_sniper sniper\n"
    "vsay_grenade grenade\n"
    "vsay_kill enemy_down\n"
    "vsay_kill great_shot\n"
    "vsay_attack move_in\n"
    "vsay_attack follow_me\n"
    "vsay_attack attack_left_flank\n"
    "vsay_attack attack_right_flank\n"
    "vsay_defend hold_position\n"
    "vsay_defend in_position\n"
    "vsay_defend area_secure\n"
    "vsay_help need_reinforcements\n"
    "vsay_help fall_back\n"
    "vsay_random yes_sir\n"
    "vsay_random on_my_way\n"
    "vsay_random took_long_enough\n"
    "vsay_random youre_crazy\n"
    "roast_kill \"Sit down %s, my grandma aims faster than that.\"\n"
    "roast_kill \"Nice reflexes %s, playing on a steering wheel?\"\n"
    "roast_kill \"Check your corners next time %s.\"\n"
    "roast_kill \"Free frag from %s, appreciate the donation.\"\n"
    "roast_kill \"Did your monitor turn off %s?\"\n"
    "roast_kill \"1944 called %s, they want their Stormtrooper aim back.\"\n"
    "roast_kill \"Too easy %s, lower the bot_skill if you need a chance.\"\n"
    "roast_death \"Lucky shot %s, pure packet loss.\"\n"
    "roast_death \"Camping in a bush again %s? Classic.\"\n"
    "roast_death \"Enjoy that one %s, I was reloading.\"\n"
    "roast_objective \"Objective secured while the other team was sightseeing.\"\n"
    "roast_objective \"Tick tock, defuse kit not included.\"\n";

int cod_bot_virtual_exists(const char* name) {
    if (!name) return 0;
    return strcmp(name, "cod/bot/zbot_profiles.txt") == 0 ||
           strcmp(name, "cod/bot/zbot_chatter.txt") == 0;
}

int cod_bot_virtual_read(const char* name, unsigned char** out_buf, unsigned int* out_size) {
    const char* src = NULL;
    size_t len = 0;
    unsigned char* buf;
    if (!name || !out_buf || !out_size) return 0;
    if (strcmp(name, "cod/bot/zbot_profiles.txt") == 0) {
        src = k_zbot_profiles_txt;
        len = sizeof(k_zbot_profiles_txt) - 1;
    } else if (strcmp(name, "cod/bot/zbot_chatter.txt") == 0) {
        src = k_zbot_chatter_txt;
        len = sizeof(k_zbot_chatter_txt) - 1;
    } else {
        return 0;
    }
    buf = (unsigned char*)malloc(len + 1);
    if (!buf) return 0;
    memcpy(buf, src, len + 1);
    *out_buf = buf;
    *out_size = (unsigned int)len;
    return 1;
}
