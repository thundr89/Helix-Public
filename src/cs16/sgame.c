#include "helix/game/iserver_game.h"
#include "bot/zbot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifdef _WIN32
  #define CS16_SG_EXPORT __declspec(dllexport)
#else
  #define CS16_SG_EXPORT __attribute__((visibility("default")))
#endif

#define MAX_CLIENTS 32
#define MAX_BOTS    31
#define MAX_OBJS    16

typedef enum {
    MODE_TDM = 0,
    MODE_DE,
    MODE_CS,
    MODE_AS
} CsMode;

typedef enum {
    ROUND_FREEZE = 0,
    ROUND_ACTIVE = 1,
    ROUND_OVER   = 2
} CsRoundPhase;

typedef struct {
    int active;
    int team; /* 0 = Spectator, 1 = T, 2 = CT */
    int class_id; /* 1..4 */
    int money;
    int is_bot;
    int is_vip;
    int has_bomb;
    int has_defuser;
    int has_awp;
    int has_helmet;
    int has_hegrenade;
    int armor;
    int frags;
    int deaths;
    int survived_round;
    int following_hostage;
    int plant_ticks;
    int defuse_ticks;
    int plant_ms;
    int defuse_ms;
    int last_buttons;
    char primary_weap[32];
    char secondary_weap[32];
} CsClient;

typedef struct {
    float origin[3];
    float mins[3];
    float maxs[3];
    int has_bounds;
    int active;
    int rescued;
    int leader_client;
    int entity_handle;
    float yaw;
} CsObjective;

typedef struct {
    hxServerGame base;
    hxSGEngine* eng;
    hxSGImports* imp;

    char map_name[64];
    CsMode mode;
    CsRoundPhase round_phase;
    int server_time_ms;
    int last_server_time_ms;
    int round_start_ms;
    int freeze_end_ms;
    int round_over_end_ms;
    int round_number;
    int score_t;
    int score_ct;
    int round_ended;
    int round_had_both_teams;
    int loss_streak_t;
    int loss_streak_ct;
    char banner_text[128];
    int banner_until_ms;

    /* Map spawn origins and angles */
    float t_spawns[MAX_OBJS][3];
    float t_spawn_angles[MAX_OBJS];
    int num_t_spawns;
    float ct_spawns[MAX_OBJS][3];
    float ct_spawn_angles[MAX_OBJS];
    int num_ct_spawns;

    /* Bomb state (de_) */
    int bomb_planted;
    int bomb_plant_ms;
    int last_beep_ms;
    float bomb_pos[3];
    int bomb_carrier_client;
    int bomb_entity_handle;

    /* VIP state (as_) */
    int vip_client;

    /* Objectives */
    CsObjective bomb_sites[MAX_OBJS];
    int num_bomb_sites;
    CsObjective rescue_zones[MAX_OBJS];
    int num_rescue_zones;
    CsObjective hostages[MAX_OBJS];
    int num_hostages;
    CsObjective vip_zones[MAX_OBJS];
    int num_vip_zones;

    CsClient clients[MAX_CLIENTS];
    int num_bots;
    int bot_quota;

    ZBotManager zbot;
} Cs16SGame;

static int cs_strcasecmp(const char* a, const char* b) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static float dist3(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static float dist2d(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    return sqrtf(dx * dx + dy * dy);
}

static int point_near_objective(const float pt[3], const CsObjective* obj, float fallback_radius) {
    if (obj->has_bounds) {
        float pad = 48.0f;
        if (pt[0] >= obj->mins[0] - pad && pt[0] <= obj->maxs[0] + pad &&
            pt[1] >= obj->mins[1] - pad && pt[1] <= obj->maxs[1] + pad &&
            pt[2] >= obj->mins[2] - 64.0f && pt[2] <= obj->maxs[2] + 96.0f) {
            return 1;
        }
    }
    return dist3(pt, obj->origin) <= fallback_radius;
}

static void snap_feet_to_floor(Cs16SGame* g, float feet[3]) {
    if (!g->imp || !g->imp->trace_query) {
        feet[2] -= 36.0f;
        return;
    }
    float start[3] = { feet[0], feet[1], feet[2] + 16.0f };
    float end[3]   = { feet[0], feet[1], feet[2] - 128.0f };
    float mins[3]  = { -14.0f, -14.0f, 0.0f };
    float maxs[3]  = {  14.0f,  14.0f, 32.0f };
    hxSGTrace tr;
    memset(&tr, 0, sizeof(tr));
    if (g->imp->trace_query(g->eng, start, end, mins, maxs, 1 /* MASK_SOLID */, -1, &tr)) {
        if (!tr.startsolid && tr.fraction < 1.0f) {
            feet[2] = tr.position[2] + 1.0f;
            return;
        }
    }
    feet[2] -= 36.0f;
}

static void parse_obj_entity(Cs16SGame* g, int ent_id, CsObjective* out) {
    memset(out, 0, sizeof(*out));
    out->leader_client = -1;
    out->entity_handle = -1;
    const char* org = g->imp->entity_key(g->eng, ent_id, "origin");
    const char* mins_s = g->imp->entity_key(g->eng, ent_id, "mins");
    const char* maxs_s = g->imp->entity_key(g->eng, ent_id, "maxs");
    if (mins_s && maxs_s && *mins_s && *maxs_s) {
        if (sscanf(mins_s, "%f %f %f", &out->mins[0], &out->mins[1], &out->mins[2]) == 3 &&
            sscanf(maxs_s, "%f %f %f", &out->maxs[0], &out->maxs[1], &out->maxs[2]) == 3) {
            out->has_bounds = 1;
            out->origin[0] = 0.5f * (out->mins[0] + out->maxs[0]);
            out->origin[1] = 0.5f * (out->mins[1] + out->maxs[1]);
            out->origin[2] = 0.5f * (out->mins[2] + out->maxs[2]);
            out->active = 1;
        }
    }
    if (org && *org) {
        float x = 0, y = 0, z = 0;
        if (sscanf(org, "%f %f %f", &x, &y, &z) == 3) {
            out->origin[0] = x;
            out->origin[1] = y;
            out->origin[2] = z;
            out->active = 1;
        }
    }
}

static void query_objectives_from_bsp(Cs16SGame* g) {
    g->num_bomb_sites = 0;
    g->num_hostages = 0;
    g->num_rescue_zones = 0;
    g->num_vip_zones = 0;
    if (!g->imp || !g->imp->entity_find || !g->imp->entity_key) return;
    int ids[MAX_OBJS];

    /* DE: Bomb targets (func_bomb_target & info_bomb_target) */
    const char* bclasses[] = { "func_bomb_target", "info_bomb_target" };
    for (int c = 0; c < 2; ++c) {
        int bcount = g->imp->entity_find(g->eng, "classname", bclasses[c], ids, MAX_OBJS);
        for (int i = 0; i < bcount && g->num_bomb_sites < MAX_OBJS; ++i) {
            CsObjective obj;
            parse_obj_entity(g, ids[i], &obj);
            if (obj.active) g->bomb_sites[g->num_bomb_sites++] = obj;
        }
    }

    /* CS: Hostages */
    const char* hclasses[] = { "hostage_entity", "monster_hostage" };
    for (int c = 0; c < 2; ++c) {
        int hcount = g->imp->entity_find(g->eng, "classname", hclasses[c], ids, MAX_OBJS);
        for (int i = 0; i < hcount && g->num_hostages < MAX_OBJS; ++i) {
            CsObjective obj;
            parse_obj_entity(g, ids[i], &obj);
            if (obj.active) {
                snap_feet_to_floor(g, obj.origin);
                g->hostages[g->num_hostages++] = obj;
            }
        }
    }

    /* CS: Rescue zones */
    const char* rclasses[] = { "func_hostage_rescue", "info_hostage_rescue" };
    for (int c = 0; c < 2; ++c) {
        int rcount = g->imp->entity_find(g->eng, "classname", rclasses[c], ids, MAX_OBJS);
        for (int i = 0; i < rcount && g->num_rescue_zones < MAX_OBJS; ++i) {
            CsObjective obj;
            parse_obj_entity(g, ids[i], &obj);
            if (obj.active) g->rescue_zones[g->num_rescue_zones++] = obj;
        }
    }
    /* Fallback rescue zone at CT spawn 0 if map uses implicit CT-spawn rescue */
    if (g->mode == MODE_CS && g->num_rescue_zones == 0 && g->num_ct_spawns > 0) {
        memset(&g->rescue_zones[0], 0, sizeof(CsObjective));
        memcpy(g->rescue_zones[0].origin, g->ct_spawns[0], sizeof(float) * 3);
        g->rescue_zones[0].active = 1;
        g->num_rescue_zones = 1;
    }

    /* AS: VIP escape zones */
    const char* vclasses[] = { "func_vip_safetyzone", "info_vip_safetyzone" };
    for (int c = 0; c < 2; ++c) {
        int vcount = g->imp->entity_find(g->eng, "classname", vclasses[c], ids, MAX_OBJS);
        for (int i = 0; i < vcount && g->num_vip_zones < MAX_OBJS; ++i) {
            CsObjective obj;
            parse_obj_entity(g, ids[i], &obj);
            if (obj.active) g->vip_zones[g->num_vip_zones++] = obj;
        }
    }
}

static void query_map_spawns(Cs16SGame* g) {
    g->num_t_spawns = 0;
    g->num_ct_spawns = 0;
    if (!g->imp || !g->imp->entity_find || !g->imp->entity_key) return;

    int ids[MAX_OBJS];
    int count = g->imp->entity_find(g->eng, "classname", "info_player_deathmatch", ids, MAX_OBJS);
    for (int i = 0; i < count && g->num_t_spawns < MAX_OBJS; ++i) {
        const char* org = g->imp->entity_key(g->eng, ids[i], "origin");
        const char* ang = g->imp->entity_key(g->eng, ids[i], "angle");
        const char* angs = g->imp->entity_key(g->eng, ids[i], "angles");
        if (org && *org) {
            float feet[3] = {0, 0, 0}, a = 0;
            if (sscanf(org, "%f %f %f", &feet[0], &feet[1], &feet[2]) == 3) {
                if (ang && *ang) sscanf(ang, "%f", &a);
                else if (angs && *angs) { float p = 0, y = 0; if (sscanf(angs, "%f %f", &p, &y) == 2) a = y; }
                snap_feet_to_floor(g, feet);
                memcpy(g->t_spawns[g->num_t_spawns], feet, sizeof(feet));
                g->t_spawn_angles[g->num_t_spawns] = a;
                g->num_t_spawns++;
            }
        }
    }

    count = g->imp->entity_find(g->eng, "classname", "info_player_start", ids, MAX_OBJS);
    for (int i = 0; i < count && g->num_ct_spawns < MAX_OBJS; ++i) {
        const char* org = g->imp->entity_key(g->eng, ids[i], "origin");
        const char* ang = g->imp->entity_key(g->eng, ids[i], "angle");
        const char* angs = g->imp->entity_key(g->eng, ids[i], "angles");
        if (org && *org) {
            float feet[3] = {0, 0, 0}, a = 0;
            if (sscanf(org, "%f %f %f", &feet[0], &feet[1], &feet[2]) == 3) {
                if (ang && *ang) sscanf(ang, "%f", &a);
                else if (angs && *angs) { float p = 0, y = 0; if (sscanf(angs, "%f %f", &p, &y) == 2) a = y; }
                snap_feet_to_floor(g, feet);
                memcpy(g->ct_spawns[g->num_ct_spawns], feet, sizeof(feet));
                g->ct_spawn_angles[g->num_ct_spawns] = a;
                g->num_ct_spawns++;
            }
        }
    }
}

static int client_in_buyzone(Cs16SGame* g, int cid) {
    if (!g || cid < 0 || cid >= MAX_CLIENTS) return 0;
    int team = g->clients[cid].team;
    if (team != 1 && team != 2) return 0;
    if (!g->imp || !g->imp->bot_state) return 1;
    hxSGBotState st;
    if (!g->imp->bot_state(g->eng, cid, &st) || st.health <= 0) return 0;

    int num_sp = (team == 1) ? g->num_t_spawns : g->num_ct_spawns;
    for (int i = 0; i < num_sp; ++i) {
        const float* sp = (team == 1) ? g->t_spawns[i] : g->ct_spawns[i];
        if (dist3(st.origin, sp) <= 320.0f) return 1;
    }
    return 0;
}

static void sync_team_scores(Cs16SGame* g) {
    if (!g->imp) return;
    if (g->imp->team_score) {
        /* Engine team 0 = CT (Allies), team 1 = T (Axis) */
        g->imp->team_score(g->eng, 0, &g->score_ct, NULL);
        g->imp->team_score(g->eng, 1, &g->score_t, NULL);
    }
    if (g->imp->cvar_set) {
        g->imp->cvar_set(g->eng, "ui_scoreboard_teamplay", "1");
        g->imp->cvar_set(g->eng, "ui_scoreboard_team_0_label", "Counter-Terrorists");
        g->imp->cvar_set(g->eng, "ui_scoreboard_team_1_label", "Terrorists");
        g->imp->cvar_set(g->eng, "ui_scoreboard_sort_team_score", "1");
    }
}

static void apply_player_model(Cs16SGame* g, int cid) {
    if (!g->imp || !g->imp->player_model) return;
    char model_name[64];
    if (g->clients[cid].is_vip) {
        snprintf(model_name, sizeof(model_name), "models/player_vip.obj");
    } else if (g->clients[cid].team == 1) {
        int cls = g->clients[cid].class_id;
        if (cls < 1 || cls > 4) cls = (cid % 4) + 1;
        if (g->clients[cid].has_bomb) {
            snprintf(model_name, sizeof(model_name), "models/player_t%d_bomb.obj", cls);
        } else {
            snprintf(model_name, sizeof(model_name), "models/player_t%d.obj", cls);
        }
    } else {
        int cls = g->clients[cid].class_id;
        if (cls < 1 || cls > 4) cls = (cid % 4) + 1;
        snprintf(model_name, sizeof(model_name), "models/player_ct%d.obj", cls);
    }
    if (!g->imp->player_model(g->eng, cid, 0, model_name, NULL)) {
        g->imp->player_model(g->eng, cid, 0, "models/player.obj", NULL);
    }
    g->imp->player_model(g->eng, cid, 2, NULL, NULL);
}

static void cs16_bot_buy_decision(Cs16SGame* g, int cid) {
    if (!g || cid < 0 || cid >= MAX_CLIENTS || !g->clients[cid].active || !g->clients[cid].is_bot || g->clients[cid].is_vip) return;
    if (!g->imp || !g->imp->player_weapon) return;

    int team = g->clients[cid].team;
    int money = g->clients[cid].money;

    /* If bot already survived with a primary weapon, just buy armor & grenades */
    if (g->clients[cid].primary_weap[0]) {
        if (g->clients[cid].armor < 50 && g->clients[cid].money >= 1000) {
            g->clients[cid].money -= 1000;
            g->clients[cid].armor = 100;
            g->clients[cid].has_helmet = 1;
        }
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 0, g->clients[cid].primary_weap, 1);
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 0, g->clients[cid].primary_weap, 0);
        return;
    }

    int team_awps = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (g->clients[i].active && g->clients[i].team == team && g->clients[i].has_awp) {
            team_awps++;
        }
    }
    g->clients[cid].has_awp = 0;

    /* 1. Pistol Round (Round 1 or money < $1500) */
    if (g->round_number <= 1 || money < 1500) {
        int r = (cid * 17 + g->round_number * 31) % 100;
        if (r < 55 && money >= 650) {
            g->clients[cid].money -= 650;
            g->clients[cid].armor = 100;
        } else if (money >= 650) {
            g->clients[cid].money -= 650;
            snprintf(g->clients[cid].secondary_weap, sizeof(g->clients[cid].secondary_weap), "deagle");
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 1, "deagle", 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 1, "deagle", 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 1, "deagle", 0);
        }
        return;
    }

    /* 2. Force/Eco Buy ($1500 <= money < $3200) */
    if (money < 3200) {
        if (g->clients[cid].money >= 650) {
            g->clients[cid].money -= 650;
            g->clients[cid].armor = 100;
        }
        const char* wep = (team == 1) ? "galil" : "famas";
        int price = (team == 1) ? 2000 : 2250;
        if (g->clients[cid].money < price) {
            wep = ((cid % 3) == 0) ? "m3" : "mp5";
            price = (strcmp(wep, "m3") == 0) ? 1700 : 1500;
        }
        if (g->clients[cid].money >= price) {
            g->clients[cid].money -= price;
            snprintf(g->clients[cid].primary_weap, sizeof(g->clients[cid].primary_weap), "%s", wep);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 0, wep, 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 0, wep, 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 0, wep, 0);
        }
        return;
    }

    /* 3. Full Buy (money >= $3200) */
    if (g->clients[cid].money >= 1000) {
        g->clients[cid].money -= 1000;
        g->clients[cid].armor = 100;
        g->clients[cid].has_helmet = 1;
    } else if (g->clients[cid].money >= 650) {
        g->clients[cid].money -= 650;
        g->clients[cid].armor = 100;
    }

    if (team == 2 && g->mode == MODE_DE && g->clients[cid].money >= 200) {
        g->clients[cid].money -= 200;
        g->clients[cid].has_defuser = 1;
    }

    int want_awp = (team_awps == 0 && g->clients[cid].money >= 4750 && ((cid % 5) == 0));
    if (want_awp) {
        g->clients[cid].money -= 4750;
        g->clients[cid].has_awp = 1;
        snprintf(g->clients[cid].primary_weap, sizeof(g->clients[cid].primary_weap), "awp");
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 0, "awp", 1);
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 0, "awp", 1);
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 0, "awp", 0);
    } else {
        const char* rifle = (team == 1) ? "ak47" : "m4a1";
        int rifle_cost = (team == 1) ? 2500 : 3100;
        if (g->clients[cid].money < rifle_cost) {
            rifle = (team == 1) ? "galil" : "famas";
            rifle_cost = (team == 1) ? 2000 : 2250;
        }
        if (g->clients[cid].money >= rifle_cost) {
            g->clients[cid].money -= rifle_cost;
            snprintf(g->clients[cid].primary_weap, sizeof(g->clients[cid].primary_weap), "%s", rifle);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 0, rifle, 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 0, rifle, 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 0, rifle, 0);
        }
    }

    if (!g->clients[cid].has_bomb && g->clients[cid].money >= 300) {
        g->clients[cid].money -= 300;
        g->clients[cid].has_hegrenade = 1;
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 3, "hegrenade", 1);
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 3, "hegrenade", 1);
    }
}

static void spawn_client_on_team(Cs16SGame* g, int cid) {
    int team = g->clients[cid].team;
    if (team <= 0) {
        /* Spectator mode */
        if (g->imp && g->imp->player_set_team) g->imp->player_set_team(g->eng, cid, -1);
        if (g->imp && g->imp->player_state) {
            hxSGPlayerState ps;
            memset(&ps, 0, sizeof(ps));
            ps.mask = HX_PLAYER_TEAM | HX_PLAYER_STATE | HX_PLAYER_SPECTATE;
            ps.team = -1;
            ps.state = 0; /* 0 = PMF_SPECTATOR in Helix engine */
            ps.spectator = -1;
            for (int i = 1; i < MAX_CLIENTS; ++i) {
                if (g->clients[i].active && g->clients[i].team > 0) { ps.spectator = i; break; }
            }
            g->imp->player_state(g->eng, cid, &ps, NULL);
        }
        return;
    }

    float feet[3] = { 0, 0, 0 };
    float angles[3] = { 0, 0, 0 };
    int found = 0;

    /* Collision-free spawn selection across team spawn points */
    int num_sp = (team == 1) ? g->num_t_spawns : g->num_ct_spawns;
    if (num_sp > 0) {
        int base_idx = (cid + g->round_number * 3) % num_sp;
        int chosen = base_idx;
        for (int attempt = 0; attempt < num_sp; ++attempt) {
            int idx = (base_idx + attempt) % num_sp;
            const float* cand = (team == 1) ? g->t_spawns[idx] : g->ct_spawns[idx];
            int occupied = 0;
            if (g->imp && g->imp->bot_state) {
                for (int other = 0; other < MAX_CLIENTS; ++other) {
                    if (other == cid || !g->clients[other].active || g->clients[other].team != team) continue;
                    hxSGBotState ost;
                    if (g->imp->bot_state(g->eng, other, &ost) && ost.health > 0) {
                        if (dist2d(cand, ost.origin) < 52.0f) { occupied = 1; break; }
                    }
                }
            }
            if (!occupied) { chosen = idx; break; }
        }
        if (team == 1) {
            memcpy(feet, g->t_spawns[chosen], sizeof(feet));
            angles[1] = g->t_spawn_angles[chosen];
        } else {
            memcpy(feet, g->ct_spawns[chosen], sizeof(feet));
            angles[1] = g->ct_spawn_angles[chosen];
        }
        found = 1;
    }

    int eng_team = (team == 1) ? 1 : 0;
    if (g->imp && g->imp->player_set_team) {
        g->imp->player_set_team(g->eng, cid, eng_team);
    }

    if (found && g->imp && g->imp->player_spawn) {
        g->imp->player_spawn(g->eng, cid, feet, angles);
    }

    if (g->imp && g->imp->player_state) {
        hxSGPlayerState ps;
        memset(&ps, 0, sizeof(ps));
        ps.mask = HX_PLAYER_HEALTH | HX_PLAYER_TEAM | HX_PLAYER_STATE |
                  HX_PLAYER_ORIGIN | HX_PLAYER_ANGLES | HX_PLAYER_SCORE |
                  HX_PLAYER_DEATHS | HX_PLAYER_VIEW_VARIANT;
        ps.health = 100;
        ps.team = eng_team;
        ps.state = 1; /* 1 = Active Playing (clears PMF_SPECTATOR so player/bot mesh is visible) */
        ps.score = g->clients[cid].frags;
        ps.deaths = g->clients[cid].deaths;
        ps.view_variant = 1;
        ps.origin[0] = feet[0];
        ps.origin[1] = feet[1];
        ps.origin[2] = feet[2];
        ps.angles[0] = angles[0];
        ps.angles[1] = angles[1];
        ps.angles[2] = angles[2];
        g->imp->player_state(g->eng, cid, &ps, NULL);
    }

    /* Strip existing weapons so ps.weapon starts at 0 before immediate switch */
    if (g->imp && g->imp->player_take_weapons) {
        g->imp->player_take_weapons(g->eng, cid);
    }

    if (!g->clients[cid].survived_round) {
        g->clients[cid].primary_weap[0] = '\0';
        g->clients[cid].secondary_weap[0] = '\0';
        g->clients[cid].has_hegrenade = 0;
        g->clients[cid].has_defuser = 0;
        g->clients[cid].has_helmet = 0;
        g->clients[cid].armor = 0;
    }

    if (g->imp && g->imp->player_weapon) {
        /* Slot 2 (3 key / pistol_weapon): Knife is ALWAYS equipped by every player */
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 2, "knife", 1);
        g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 2, "knife", 1);

        if (g->clients[cid].is_vip) {
            /* VIP in AS mode: 200 Armor + Helmet, USP + Knife only */
            g->clients[cid].armor = 200;
            g->clients[cid].has_helmet = 1;
            g->clients[cid].primary_weap[0] = '\0';
            snprintf(g->clients[cid].secondary_weap, sizeof(g->clients[cid].secondary_weap), "usp");
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 1, "usp", 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 1, "usp", 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 1, "usp", 0);
        } else {
            const char* default_pistol = (team == 1) ? "glock" : "usp";
            const char* sidearm = g->clients[cid].secondary_weap[0] ? g->clients[cid].secondary_weap : default_pistol;
            snprintf(g->clients[cid].secondary_weap, sizeof(g->clients[cid].secondary_weap), "%s", sidearm);

            /* Slot 1 (2 key / secondary_weapon): Sidearm pistol */
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 1, sidearm, 1);
            g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 1, sidearm, 1);

            /* Slot 0 (1 key / primary_weapon): Retained primary if survived */
            if (g->clients[cid].primary_weap[0]) {
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 0, g->clients[cid].primary_weap, 1);
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 0, g->clients[cid].primary_weap, 1);
            }

            /* Slot 3 (4 key / grenade_weapon): C4 for bomb carrier or HE grenade */
            if (g->clients[cid].has_bomb) {
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 3, "c4", 1);
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 3, "c4", 1);
            } else if (g->clients[cid].has_hegrenade) {
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_SLOT_SET, 3, "hegrenade", 1);
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_MAX_AMMO, 3, "hegrenade", 1);
            }

            /* Switch immediately to primary if owned, else sidearm pistol */
            if (g->clients[cid].primary_weap[0]) {
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 0, g->clients[cid].primary_weap, 0);
            } else {
                g->imp->player_weapon(g->eng, cid, HX_WEAPON_SWITCH, 1, sidearm, 0);
            }

            if (g->clients[cid].is_bot) {
                cs16_bot_buy_decision(g, cid);
            }
        }
    }

    /* Apply freeze time lock if round is in freeze phase */
    if (g->imp && g->imp->player_script_control) {
        int locked = (g->round_phase == ROUND_FREEZE) ? 1 : 0;
        g->imp->player_script_control(g->eng, cid, locked, locked);
    }
}

static void cleanup_round_entities(Cs16SGame* g) {
    if (!g->imp || !g->imp->entity_remove) return;
    if (g->bomb_entity_handle >= 0) {
        g->imp->entity_remove(g->eng, g->bomb_entity_handle);
        g->bomb_entity_handle = -1;
    }
    for (int h = 0; h < g->num_hostages; ++h) {
        if (g->hostages[h].entity_handle >= 0) {
            g->imp->entity_remove(g->eng, g->hostages[h].entity_handle);
            g->hostages[h].entity_handle = -1;
        }
    }
}

static void spawn_round_entities(Cs16SGame* g) {
    if (!g->imp || !g->imp->entity_spawn || !g->imp->entity_model) return;
    if (g->mode == MODE_CS) {
        for (int h = 0; h < g->num_hostages; ++h) {
            g->hostages[h].rescued = 0;
            g->hostages[h].leader_client = -1;
            int ent = g->imp->entity_spawn(g->eng, "script_model", g->hostages[h].origin);
            if (ent >= 0) {
                g->imp->entity_model(g->eng, ent, "models/hostage.obj");
                g->hostages[h].entity_handle = ent;
            }
        }
    }
}

static void update_radar_objectives(Cs16SGame* g) {
    if (!g->imp || !g->imp->objective_update) return;
    /* Clear slots 0..7 first */
    for (int i = 0; i < 8; ++i) g->imp->objective_update(g->eng, i, NULL);

    if (g->mode == MODE_DE) {
        if (g->bomb_planted) {
            hxSGObjective obj;
            memset(&obj, 0, sizeof(obj));
            obj.state = 1;
            obj.team = -1;
            obj.entity = -1;
            memcpy(obj.position, g->bomb_pos, sizeof(float) * 3);
            g->imp->objective_update(g->eng, 0, &obj);
        } else {
            for (int s = 0; s < g->num_bomb_sites && s < 4; ++s) {
                hxSGObjective obj;
                memset(&obj, 0, sizeof(obj));
                obj.state = 1;
                obj.team = -1;
                obj.entity = -1;
                memcpy(obj.position, g->bomb_sites[s].origin, sizeof(float) * 3);
                g->imp->objective_update(g->eng, s, &obj);
            }
        }
    } else if (g->mode == MODE_CS) {
        int slot = 0;
        for (int h = 0; h < g->num_hostages && slot < 4; ++h) {
            if (g->hostages[h].rescued) continue;
            hxSGObjective obj;
            memset(&obj, 0, sizeof(obj));
            obj.state = 1;
            obj.team = -1;
            obj.entity = -1;
            memcpy(obj.position, g->hostages[h].origin, sizeof(float) * 3);
            g->imp->objective_update(g->eng, slot++, &obj);
        }
        if (g->num_rescue_zones > 0 && slot < 6) {
            hxSGObjective obj;
            memset(&obj, 0, sizeof(obj));
            obj.state = 1;
            obj.team = -1;
            obj.entity = -1;
            memcpy(obj.position, g->rescue_zones[0].origin, sizeof(float) * 3);
            g->imp->objective_update(g->eng, slot++, &obj);
        }
    } else if (g->mode == MODE_AS) {
        if (g->num_vip_zones > 0) {
            hxSGObjective obj;
            memset(&obj, 0, sizeof(obj));
            obj.state = 1;
            obj.team = -1;
            obj.entity = -1;
            memcpy(obj.position, g->vip_zones[0].origin, sizeof(float) * 3);
            g->imp->objective_update(g->eng, 0, &obj);
        }
    }
}

static void start_round(Cs16SGame* g) {
    cleanup_round_entities(g);

    g->round_number++;
    g->round_start_ms = g->server_time_ms;
    int freezetime_s = 3;
    if (g->imp && g->imp->cvar_int) {
        int ft = g->imp->cvar_int(g->eng, "mp_freezetime");
        if (ft >= 0 && ft <= 15) freezetime_s = ft;
    }
    g->freeze_end_ms = g->server_time_ms + freezetime_s * 1000;
    g->round_phase = (freezetime_s > 0) ? ROUND_FREEZE : ROUND_ACTIVE;
    g->round_ended = 0;
    g->round_had_both_teams = 0;
    g->bomb_planted = 0;
    g->bomb_plant_ms = 0;
    g->last_beep_ms = 0;
    g->bomb_carrier_client = -1;
    g->vip_client = -1;

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        g->clients[i].has_bomb = 0;
        g->clients[i].is_vip = 0;
        g->clients[i].following_hostage = 0;
        g->clients[i].plant_ticks = 0;
        g->clients[i].defuse_ticks = 0;
        if (g->clients[i].active && g->imp && g->imp->bot_state) {
            hxSGBotState st;
            g->clients[i].survived_round = (g->round_number > 1 && g->imp->bot_state(g->eng, i, &st) && st.health > 0) ? 1 : 0;
        } else {
            g->clients[i].survived_round = 0;
        }
    }

    /* DE mode: assign C4 bomb to ONE randomly selected Terrorist */
    if (g->mode == MODE_DE) {
        int t_candidates[MAX_CLIENTS];
        int num_t = 0;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (g->clients[i].active && g->clients[i].team == 1) {
                t_candidates[num_t++] = i;
            }
        }
        if (num_t > 0) {
            int chosen = t_candidates[rand() % num_t];
            g->clients[chosen].has_bomb = 1;
            g->bomb_carrier_client = chosen;
        }
    }

    /* AS mode: assign VIP to ONE randomly selected Counter-Terrorist */
    if (g->mode == MODE_AS) {
        int ct_candidates[MAX_CLIENTS];
        int num_ct = 0;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (g->clients[i].active && g->clients[i].team == 2) {
                ct_candidates[num_ct++] = i;
            }
        }
        if (num_ct > 0) {
            int chosen_vip = ct_candidates[rand() % num_ct];
            g->clients[chosen_vip].is_vip = 1;
            g->vip_client = chosen_vip;
        }
    }

    if (g->zbot.initialized) {
        zbot_restart_round(&g->zbot);
        g->zbot.attacking_team = 1;
        g->zbot.planter = g->bomb_carrier_client;
    }

    spawn_round_entities(g);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (g->clients[i].active && g->clients[i].team > 0) {
            spawn_client_on_team(g, i);
            apply_player_model(g, i);
        }
    }

    update_radar_objectives(g);
    sync_team_scores(g);

    snprintf(g->banner_text, sizeof(g->banner_text), "Round %d - Prepare to Fight!", g->round_number);
    g->banner_until_ms = g->server_time_ms + 2500;

    if (g->imp && g->imp->print) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[CS16] Round %d started (Mode: %s)\n",
                 g->round_number,
                 g->mode == MODE_DE ? "Bomb Defusal" :
                 g->mode == MODE_CS ? "Hostage Rescue" :
                 g->mode == MODE_AS ? "VIP Assassination" : "Team Deathmatch");
        g->imp->print(g->eng, msg);
    }
}

static void end_round(Cs16SGame* g, int winner_team, const char* reason) {
    if (g->round_ended) return;
    g->round_ended = 1;
    g->round_phase = ROUND_OVER;
    g->round_over_end_ms = g->server_time_ms + 4000;

    int win_reward = 3250;
    int lose_reward = 1400;
    if (winner_team == 1) {
        g->score_t++;
        g->loss_streak_t = 0;
        g->loss_streak_ct++;
        lose_reward = 1400 + (g->loss_streak_ct > 4 ? 4 : g->loss_streak_ct - 1) * 500;
    } else if (winner_team == 2) {
        g->score_ct++;
        g->loss_streak_ct = 0;
        g->loss_streak_t++;
        lose_reward = 1400 + (g->loss_streak_t > 4 ? 4 : g->loss_streak_t - 1) * 500;
    }

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!g->clients[i].active || g->clients[i].team <= 0) continue;
        g->clients[i].money += (g->clients[i].team == winner_team) ? win_reward : lose_reward;
        if (g->clients[i].money > 16000) g->clients[i].money = 16000;
    }

    sync_team_scores(g);

    snprintf(g->banner_text, sizeof(g->banner_text), "%s",
             reason && *reason ? reason :
             (winner_team == 1 ? "Terrorists Win!" :
              winner_team == 2 ? "Counter-Terrorists Win!" : "Round Draw!"));
    g->banner_until_ms = g->server_time_ms + 4000;

    if (g->imp && g->imp->player_ui) {
        g->imp->player_ui(g->eng, 0, 5, g->banner_text, NULL);
    }
    if (g->imp && g->imp->print) {
        char msg[160];
        snprintf(msg, sizeof(msg), "[CS16] Round %d won by %s: %s (T: %d, CT: %d)\n",
                 g->round_number,
                 winner_team == 1 ? "TERRORISTS" : (winner_team == 2 ? "COUNTER-TERRORISTS" : "DRAW"),
                 reason ? reason : "", g->score_t, g->score_ct);
        g->imp->print(g->eng, msg);
    }
}

static int cs16_bot_client_cmd(void* host_game, int client_num, const char* cmd) {
    Cs16SGame* g = (Cs16SGame*)host_game;
    if (!g || !cmd || client_num < 0 || client_num >= MAX_CLIENTS) return 0;

    if (strncmp(cmd, "botmenu", 7) == 0) {
        ZBotAgent* b = &g->zbot.bots[client_num];
        int team = 0;
        if (b->desired_team == 0) team = 2; /* CT */
        else if (b->desired_team == 1) team = 1; /* T */
        else {
            int count_t = 0, count_ct = 0;
            for (int k = 0; k < MAX_CLIENTS; ++k) {
                if (g->clients[k].active) {
                    if (g->clients[k].team == 1) count_t++;
                    else if (g->clients[k].team == 2) count_ct++;
                }
            }
            team = (count_t <= count_ct) ? 1 : 2;
        }
        int start_money = 800;
        if (g->imp && g->imp->cvar_int) {
            int sm = g->imp->cvar_int(g->eng, "mp_startmoney");
            if (sm >= 800 && sm <= 16000) start_money = sm;
        }
        g->clients[client_num].active = 1;
        g->clients[client_num].is_bot = 1;
        g->clients[client_num].team = team;
        g->clients[client_num].class_id = (b->profile_idx % 4) + 1;
        g->clients[client_num].money = start_money;
        g->clients[client_num].survived_round = 0;

        spawn_client_on_team(g, client_num);
        apply_player_model(g, client_num);
        return 2;
    }
    return 1;
}

static void add_bot(Cs16SGame* g, int team) {
    if (!g->zbot.initialized) return;
    const char* team_str = (team == 1) ? "terrorist" : (team == 2) ? "counterterrorist" : NULL;
    int cid = zbot_add_bot(&g->zbot, NULL, team_str, NULL);
    if (cid >= 0 && cid < MAX_CLIENTS) {
        g->num_bots++;
        g->zbot.bots[cid].joined = 1;
        g->zbot.bots[cid].next_join_ms = 0;
        cs16_bot_client_cmd(g, cid, "botmenu");
    }
}

static void kick_all_bots(Cs16SGame* g) {
    if (!g->zbot.initialized) return;
    zbot_kick_bot(&g->zbot, "all");
    g->num_bots = 0;
}

static void update_player_animations(Cs16SGame* g) {
    if (!g->imp || !g->imp->animation_input || !g->imp->entity_animation) return;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!g->clients[i].active || g->clients[i].team <= 0) continue;
        hxSGAnimationInput in;
        if (!g->imp->animation_input(g->eng, i, &in) || in.health <= 0 || in.state != 1) continue;

        float speed2d = sqrtf(in.velocity[0] * in.velocity[0] + in.velocity[1] * in.velocity[1]);
        const char* lower_clip = "cod/animations/cs16_idle.hxac";
        unsigned once_mask = 0;

        if (!in.grounded) {
            lower_clip = "cod/animations/cs16_jump.hxac";
            once_mask |= (1u << 1);
        } else if (in.stance == 1) {
            lower_clip = (speed2d > 18.0f) ? "cod/animations/cs16_crouch_walk.hxac"
                                           : "cod/animations/cs16_crouch_idle.hxac";
        } else {
            if (speed2d > 135.0f) lower_clip = "cod/animations/cs16_run.hxac";
            else if (speed2d > 15.0f) lower_clip = "cod/animations/cs16_walk.hxac";
            else lower_clip = "cod/animations/cs16_idle.hxac";
        }

        const char* wname = in.weapon_name ? in.weapon_name : "";
        int is_knife_or_c4 = (strcmp(wname, "knife") == 0 || strcmp(wname, "c4") == 0 || strcmp(wname, "hegrenade") == 0);
        int is_pistol = (!is_knife_or_c4 && (strcmp(wname, "glock") == 0 || strcmp(wname, "usp") == 0 ||
                         strcmp(wname, "deagle") == 0 || strcmp(wname, "p228") == 0 || strcmp(wname, "fiveseven") == 0));

        const char* upper_clip = "cod/animations/cs16_aim_rifle.hxac";
        if (is_knife_or_c4) {
            if (in.firing) { upper_clip = "cod/animations/cs16_shoot_knife.hxac"; once_mask |= (1u << 2); }
            else upper_clip = "cod/animations/cs16_aim_knife.hxac";
        } else if (is_pistol) {
            if (in.reloading) { upper_clip = "cod/animations/cs16_reload_pistol.hxac"; once_mask |= (1u << 2); }
            else if (in.firing) { upper_clip = "cod/animations/cs16_shoot_pistol.hxac"; once_mask |= (1u << 2); }
            else upper_clip = "cod/animations/cs16_aim_pistol.hxac";
        } else {
            if (in.reloading) { upper_clip = "cod/animations/cs16_reload_rifle.hxac"; once_mask |= (1u << 2); }
            else if (in.firing) { upper_clip = "cod/animations/cs16_shoot_rifle.hxac"; once_mask |= (1u << 2); }
            else upper_clip = "cod/animations/cs16_aim_rifle.hxac";
        }

        hxSGAnimation cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.clips[0] = NULL;
        cmd.clips[1] = lower_clip;
        cmd.clips[2] = upper_clip;
        cmd.rates[0] = 1.0f;
        cmd.rates[1] = 1.0f;
        cmd.rates[2] = 1.0f;
        cmd.fade_seconds = 0.12f;
        cmd.once_mask = once_mask;
        cmd.restart_mask = 0;
        cmd.held_tag = "Bip01 R Hand";
        g->imp->entity_animation(g->eng, -3 - i, &cmd);
    }
}

static void update_hud(Cs16SGame* g) {
    if (!g->imp || !g->imp->player_ui) return;
    const char* mode_str = "TDM";
    if (g->mode == MODE_DE) mode_str = "DE";
    else if (g->mode == MODE_CS) mode_str = "CS";
    else if (g->mode == MODE_AS) mode_str = "AS";

    int round_limit_s = 120;
    if (g->imp->cvar_float) {
        float rt = g->imp->cvar_float(g->eng, "mp_roundtime");
        if (rt >= 1.0f && rt <= 9.0f) round_limit_s = (int)(rt * 60.0f);
    }

    char timer_buf[48];
    if (g->round_phase == ROUND_FREEZE) {
        int freeze_rem = (g->freeze_end_ms - g->server_time_ms + 999) / 1000;
        if (freeze_rem < 0) freeze_rem = 0;
        snprintf(timer_buf, sizeof(timer_buf), "FREEZE 00:%02d", freeze_rem);
    } else if (g->mode == MODE_DE && g->bomb_planted) {
        int bomb_rem = 40 - (g->server_time_ms - g->bomb_plant_ms) / 1000;
        if (bomb_rem < 0) bomb_rem = 0;
        snprintf(timer_buf, sizeof(timer_buf), "*C4 00:%02d*", bomb_rem);
    } else {
        int elapsed_s = (g->server_time_ms - g->freeze_end_ms) / 1000;
        if (elapsed_s < 0) elapsed_s = 0;
        int rem = round_limit_s - elapsed_s;
        if (rem < 0) rem = 0;
        snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", rem / 60, rem % 60);
    }

    int hp = 100;
    if (g->imp->bot_state) {
        hxSGBotState pst;
        if (g->imp->bot_state(g->eng, 0, &pst)) {
            hp = pst.health;
            if (hp < 0) hp = 0;
        }
    }

    char hp_buf[16];
    snprintf(hp_buf, sizeof(hp_buf), "%d", hp);

    char arm_buf[24];
    snprintf(arm_buf, sizeof(arm_buf), "%d%s", g->clients[0].armor, g->clients[0].has_helmet ? "+H" : "");

    char money_buf[16];
    snprintf(money_buf, sizeof(money_buf), "$%d", g->clients[0].money);

    char ammo_buf[48];
    const char* active_weap = g->imp->player_weapon_name ? g->imp->player_weapon_name(g->eng, 0, -1) : "";
    if (!active_weap) active_weap = "";
    if (strcmp(active_weap, "knife") == 0) {
        snprintf(ammo_buf, sizeof(ammo_buf), "KNIFE");
    } else if (strcmp(active_weap, "c4") == 0) {
        snprintf(ammo_buf, sizeof(ammo_buf), "C4 BOMB");
    } else {
        int slot = 0;
        if (strcmp(active_weap, "glock") == 0 || strcmp(active_weap, "usp") == 0 ||
            strcmp(active_weap, "deagle") == 0 || strcmp(active_weap, "p228") == 0 ||
            strcmp(active_weap, "fiveseven") == 0) {
            slot = 1;
        } else if (strcmp(active_weap, "hegrenade") == 0) {
            slot = 3;
        }
        int clip = g->imp->player_weapon ? g->imp->player_weapon(g->eng, 0, HX_WEAPON_GET_CLIP, slot, NULL, 0) : -1;
        int stock = g->imp->player_weapon ? g->imp->player_weapon(g->eng, 0, HX_WEAPON_GET_STOCK, slot, NULL, 0) : -1;
        if (clip >= 0 && stock >= 0) {
            snprintf(ammo_buf, sizeof(ammo_buf), "%d | %d", clip, stock);
        } else {
            snprintf(ammo_buf, sizeof(ammo_buf), "-- | --");
        }
    }

    char score_buf[64];
    snprintf(score_buf, sizeof(score_buf), "[%s]  T: %d   CT: %d", mode_str, g->score_t, g->score_ct);

    char status_buf[96];
    status_buf[0] = '\0';
    if (client_in_buyzone(g, 0)) strncat(status_buf, "[BUY] ", sizeof(status_buf) - strlen(status_buf) - 1);
    if (g->clients[0].has_bomb) strncat(status_buf, "[C4] ", sizeof(status_buf) - strlen(status_buf) - 1);
    if (g->clients[0].has_defuser) strncat(status_buf, "[KIT] ", sizeof(status_buf) - strlen(status_buf) - 1);
    if (g->clients[0].is_vip) strncat(status_buf, "[VIP] ", sizeof(status_buf) - strlen(status_buf) - 1);

    char progress_buf[96];
    progress_buf[0] = '\0';
    if (g->clients[0].plant_ms > 0) {
        int pct = (g->clients[0].plant_ms * 100) / 3500;
        if (pct > 100) pct = 100;
        snprintf(progress_buf, sizeof(progress_buf), "PLANTING C4... %d%%", pct);
    } else if (g->clients[0].defuse_ms > 0) {
        int needed_ms = g->clients[0].has_defuser ? 5000 : 10000;
        int pct = (g->clients[0].defuse_ms * 100) / needed_ms;
        if (pct > 100) pct = 100;
        snprintf(progress_buf, sizeof(progress_buf), "DEFUSING BOMB... %d%%", pct);
    } else if (g->server_time_ms < g->banner_until_ms && g->banner_text[0]) {
        snprintf(progress_buf, sizeof(progress_buf), "%s", g->banner_text);
    }

    g->imp->player_ui(g->eng, 0, 0, "ui_health", hp_buf);
    g->imp->player_ui(g->eng, 0, 0, "ui_armor", arm_buf);
    g->imp->player_ui(g->eng, 0, 0, "ui_timer", timer_buf);
    g->imp->player_ui(g->eng, 0, 0, "ui_money", money_buf);
    g->imp->player_ui(g->eng, 0, 0, "ui_ammo", ammo_buf);
    g->imp->player_ui(g->eng, 0, 0, "ui_score", score_buf);
    g->imp->player_ui(g->eng, 0, 0, "ui_status", status_buf);
    g->imp->player_ui(g->eng, 0, 0, "ui_progress", progress_buf);
}

static void plant_bomb_at(Cs16SGame* g, int planter_cid, const float pos[3]) {
    if (g->bomb_planted || g->round_ended) return;
    g->bomb_planted = 1;
    g->bomb_plant_ms = g->server_time_ms;
    g->last_beep_ms = g->server_time_ms;
    memcpy(g->bomb_pos, pos, sizeof(g->bomb_pos));
    snap_feet_to_floor(g, g->bomb_pos);
    g->clients[planter_cid].has_bomb = 0;
    g->clients[planter_cid].plant_ticks = 0;
    g->clients[planter_cid].plant_ms = 0;
    apply_player_model(g, planter_cid);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        g->clients[i].defuse_ticks = 0;
        g->clients[i].defuse_ms = 0;
    }
    g->clients[planter_cid].money += 300;
    if (g->clients[planter_cid].money > 16000) g->clients[planter_cid].money = 16000;

    if (g->imp && g->imp->player_weapon) {
        g->imp->player_weapon(g->eng, planter_cid, HX_WEAPON_TAKE, 3, "c4", 0);
        if (g->clients[planter_cid].primary_weap[0])
            g->imp->player_weapon(g->eng, planter_cid, HX_WEAPON_SWITCH, 0, g->clients[planter_cid].primary_weap, 0);
        else
            g->imp->player_weapon(g->eng, planter_cid, HX_WEAPON_SWITCH, 1, g->clients[planter_cid].secondary_weap, 0);
    }

    if (g->imp && g->imp->entity_spawn && g->imp->entity_model) {
        int ent = g->imp->entity_spawn(g->eng, "script_model", g->bomb_pos);
        if (ent >= 0) {
            g->imp->entity_model(g->eng, ent, "models/w_c4.obj");
            g->bomb_entity_handle = ent;
        }
    }

    update_radar_objectives(g);
    snprintf(g->banner_text, sizeof(g->banner_text), "The bomb has been planted!");
    g->banner_until_ms = g->server_time_ms + 3500;
    if (g->imp && g->imp->player_ui) g->imp->player_ui(g->eng, 0, 5, "The bomb has been planted!", NULL);
    if (g->imp && g->imp->print) g->imp->print(g->eng, "[CS16] The bomb has been planted!\n");
}

static void cs16_bot_objectives(Cs16SGame* g, int delta_ms) {
    if (!g || !g->zbot.initialized || g->round_phase != ROUND_ACTIVE) return;

    if (g->mode == MODE_DE) {
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (!zbot_is_bot(&g->zbot, i)) continue;
            hxSGBotState st;
            if (!g->imp->bot_state || !g->imp->bot_state(g->eng, i, &st) || st.health <= 0) {
                zbot_set_objective(&g->zbot, i, NULL);
                continue;
            }
            int team = g->clients[i].team;
            if (team == 1) { /* Terrorist */
                if (!g->bomb_planted && g->num_bomb_sites > 0) {
                    int site_idx = (i + g->round_number) % g->num_bomb_sites;
                    ZBotObjectiveTask task;
                    memset(&task, 0, sizeof(task));
                    if (g->clients[i].has_bomb) {
                        task.kind = ZBOT_OBJECTIVE_HQ;
                        memcpy(task.origin, g->bomb_sites[site_idx].origin, sizeof(task.origin));
                        task.radius = 90.0f;
                        zbot_set_objective(&g->zbot, i, &task);
                        if (point_near_objective(st.origin, &g->bomb_sites[site_idx], 150.0f)) {
                            task.kind = ZBOT_OBJECTIVE_GUARD;
                            task.radius = 0.0f;
                            memcpy(task.origin, st.origin, sizeof(task.origin));
                            zbot_set_objective(&g->zbot, i, &task);
                            g->clients[i].plant_ms += delta_ms;
                            g->clients[i].plant_ticks = (g->clients[i].plant_ms * 45) / 3500;
                            if (g->clients[i].plant_ms >= 3500) {
                                g->clients[i].plant_ms = 0;
                                g->clients[i].plant_ticks = 0;
                                plant_bomb_at(g, i, st.origin);
                            }
                        } else {
                            g->clients[i].plant_ms = 0;
                            g->clients[i].plant_ticks = 0;
                        }
                    } else {
                        task.kind = ZBOT_OBJECTIVE_GUARD;
                        memcpy(task.origin, g->bomb_sites[site_idx].origin, sizeof(task.origin));
                        task.radius = 220.0f;
                        zbot_set_objective(&g->zbot, i, &task);
                    }
                } else if (g->bomb_planted) {
                    ZBotObjectiveTask task;
                    memset(&task, 0, sizeof(task));
                    task.kind = ZBOT_OBJECTIVE_GUARD;
                    memcpy(task.origin, g->bomb_pos, sizeof(task.origin));
                    task.radius = 200.0f;
                    zbot_set_objective(&g->zbot, i, &task);
                }
            } else if (team == 2) { /* Counter-Terrorist */
                if (g->bomb_planted) {
                    ZBotObjectiveTask task;
                    memset(&task, 0, sizeof(task));
                    task.kind = ZBOT_OBJECTIVE_HQ;
                    memcpy(task.origin, g->bomb_pos, sizeof(task.origin));
                    task.radius = 65.0f;
                    zbot_set_objective(&g->zbot, i, &task);
                    if (dist3(st.origin, g->bomb_pos) < 100.0f) {
                        task.kind = ZBOT_OBJECTIVE_GUARD;
                        task.radius = 0.0f;
                        memcpy(task.origin, st.origin, sizeof(task.origin));
                        zbot_set_objective(&g->zbot, i, &task);
                        g->clients[i].defuse_ms += delta_ms;
                        int needed_ms = g->clients[i].has_defuser ? 5000 : 10000;
                        int needed_ticks = g->clients[i].has_defuser ? 45 : 90;
                        g->clients[i].defuse_ticks = (g->clients[i].defuse_ms * needed_ticks) / needed_ms;
                        if (g->clients[i].defuse_ms >= needed_ms) {
                            g->clients[i].defuse_ms = 0;
                            g->clients[i].defuse_ticks = 0;
                            end_round(g, 2, "The bomb has been defused! Counter-Terrorists Win!");
                        }
                    } else {
                        g->clients[i].defuse_ms = 0;
                        g->clients[i].defuse_ticks = 0;
                    }
                } else if (g->num_bomb_sites > 0) {
                    int site_idx = i % g->num_bomb_sites;
                    ZBotObjectiveTask task;
                    memset(&task, 0, sizeof(task));
                    task.kind = ZBOT_OBJECTIVE_GUARD;
                    memcpy(task.origin, g->bomb_sites[site_idx].origin, sizeof(task.origin));
                    task.radius = 240.0f;
                    zbot_set_objective(&g->zbot, i, &task);
                }
            }
        }
    } else if (g->mode == MODE_CS) {
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (!zbot_is_bot(&g->zbot, i)) continue;
            hxSGBotState st;
            if (!g->imp->bot_state || !g->imp->bot_state(g->eng, i, &st) || st.health <= 0) {
                zbot_set_objective(&g->zbot, i, NULL);
                continue;
            }
            int team = g->clients[i].team;
            if (team == 2) { /* CT rescues hostages */
                if (g->clients[i].following_hostage && g->num_rescue_zones > 0) {
                    ZBotObjectiveTask task;
                    memset(&task, 0, sizeof(task));
                    task.kind = ZBOT_OBJECTIVE_RE_DELIVER;
                    memcpy(task.origin, g->rescue_zones[0].origin, sizeof(task.origin));
                    task.radius = 120.0f;
                    zbot_set_objective(&g->zbot, i, &task);
                } else {
                    int best_h = -1;
                    float min_hd = 1e9f;
                    for (int h = 0; h < g->num_hostages; ++h) {
                        if (!g->hostages[h].rescued && g->hostages[h].leader_client < 0) {
                            float d = dist3(st.origin, g->hostages[h].origin);
                            if (d < min_hd) { min_hd = d; best_h = h; }
                        }
                    }
                    if (best_h >= 0) {
                        ZBotObjectiveTask task;
                        memset(&task, 0, sizeof(task));
                        task.kind = ZBOT_OBJECTIVE_RE_PICKUP;
                        memcpy(task.origin, g->hostages[best_h].origin, sizeof(task.origin));
                        task.radius = 80.0f;
                        zbot_set_objective(&g->zbot, i, &task);
                        if (min_hd < 110.0f) {
                            g->hostages[best_h].leader_client = i;
                            g->clients[i].following_hostage = 1;
                        }
                    }
                }
            } else if (team == 1 && g->num_hostages > 0) {
                int h_idx = i % g->num_hostages;
                ZBotObjectiveTask task;
                memset(&task, 0, sizeof(task));
                task.kind = ZBOT_OBJECTIVE_GUARD;
                memcpy(task.origin, g->hostages[h_idx].origin, sizeof(task.origin));
                task.radius = 250.0f;
                zbot_set_objective(&g->zbot, i, &task);
            }
        }
    } else if (g->mode == MODE_AS) {
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (!zbot_is_bot(&g->zbot, i)) continue;
            hxSGBotState st;
            if (!g->imp->bot_state || !g->imp->bot_state(g->eng, i, &st) || st.health <= 0) {
                zbot_set_objective(&g->zbot, i, NULL);
                continue;
            }
            if (g->clients[i].is_vip && g->num_vip_zones > 0) {
                ZBotObjectiveTask task;
                memset(&task, 0, sizeof(task));
                task.kind = ZBOT_OBJECTIVE_RE_DELIVER;
                memcpy(task.origin, g->vip_zones[0].origin, sizeof(task.origin));
                task.radius = 110.0f;
                zbot_set_objective(&g->zbot, i, &task);
                if (point_near_objective(st.origin, &g->vip_zones[0], 150.0f)) {
                    end_round(g, 2, "The VIP has escaped! Counter-Terrorists Win!");
                }
            } else if (g->vip_client >= 0) {
                hxSGBotState vst;
                if (g->imp->bot_state(g->eng, g->vip_client, &vst) && vst.health > 0) {
                    ZBotObjectiveTask task;
                    memset(&task, 0, sizeof(task));
                    task.kind = ZBOT_OBJECTIVE_GUARD;
                    memcpy(task.origin, vst.origin, sizeof(task.origin));
                    task.radius = (g->clients[i].team == 2) ? 180.0f : 260.0f;
                    zbot_set_objective(&g->zbot, i, &task);
                }
            }
        }
    }
}

static void update_hostages_and_objectives(Cs16SGame* g, float dt_s) {
    if (g->mode != MODE_CS || g->round_ended) return;
    for (int h = 0; h < g->num_hostages; ++h) {
        if (g->hostages[h].rescued) continue;
        int leader = g->hostages[h].leader_client;
        if (leader >= 0 && leader < MAX_CLIENTS && g->clients[leader].active) {
            hxSGBotState lst;
            if (!g->imp->bot_state || !g->imp->bot_state(g->eng, leader, &lst) || lst.health <= 0) {
                g->hostages[h].leader_client = -1;
                g->clients[leader].following_hostage = 0;
                continue;
            }
            /* Move hostage smoothly toward leader if distance > 64 */
            float dx = lst.origin[0] - g->hostages[h].origin[0];
            float dy = lst.origin[1] - g->hostages[h].origin[1];
            float d2 = sqrtf(dx * dx + dy * dy);
            if (d2 > 64.0f) {
                float step = 195.0f * dt_s;
                if (step > d2 - 56.0f) step = d2 - 56.0f;
                g->hostages[h].origin[0] += (dx / d2) * step;
                g->hostages[h].origin[1] += (dy / d2) * step;
                g->hostages[h].origin[2] = lst.origin[2];
                snap_feet_to_floor(g, g->hostages[h].origin);
                g->hostages[h].yaw = atan2f(dy, dx) * (180.0f / 3.14159265f);
                if (g->hostages[h].entity_handle >= 0 && g->imp->entity_transform) {
                    float ang[3] = { 0.0f, g->hostages[h].yaw, 0.0f };
                    g->imp->entity_transform(g->eng, g->hostages[h].entity_handle, g->hostages[h].origin, ang, NULL, NULL);
                }
            }
            /* Check if hostage reached any rescue zone */
            for (int r = 0; r < g->num_rescue_zones; ++r) {
                if (point_near_objective(g->hostages[h].origin, &g->rescue_zones[r], 200.0f) ||
                    point_near_objective(lst.origin, &g->rescue_zones[r], 180.0f)) {
                    g->hostages[h].rescued = 1;
                    g->hostages[h].leader_client = -1;
                    if (g->hostages[h].entity_handle >= 0 && g->imp->entity_remove) {
                        g->imp->entity_remove(g->eng, g->hostages[h].entity_handle);
                        g->hostages[h].entity_handle = -1;
                    }
                    g->clients[leader].money += 1000;
                    if (g->clients[leader].money > 16000) g->clients[leader].money = 16000;
                    if (g->imp->print) g->imp->print(g->eng, "[CS16] A hostage has been rescued! (+$1000)\n");
                    update_radar_objectives(g);
                    break;
                }
            }
        }
    }
    if (g->num_hostages > 0) {
        int all_rescued = 1;
        for (int h = 0; h < g->num_hostages; ++h) {
            if (!g->hostages[h].rescued) { all_rescued = 0; break; }
        }
        if (all_rescued) {
            end_round(g, 2, "All Hostages Rescued! Counter-Terrorists Win!");
        }
    }
}

static void check_team_elimination(Cs16SGame* g) {
    if (g->round_ended || g->round_phase != ROUND_ACTIVE || !g->imp || !g->imp->bot_state) return;
    if ((g->server_time_ms - g->round_start_ms) < 2000) return;

    int total_t = 0, total_ct = 0;
    int alive_t = 0, alive_ct = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!g->clients[i].active) continue;
        if (g->clients[i].team == 1) {
            total_t++;
            hxSGBotState st;
            if (g->imp->bot_state(g->eng, i, &st) && st.health > 0) alive_t++;
        } else if (g->clients[i].team == 2) {
            total_ct++;
            hxSGBotState st;
            if (g->imp->bot_state(g->eng, i, &st) && st.health > 0) alive_ct++;
        }
    }

    if (alive_t > 0 && alive_ct > 0) {
        g->round_had_both_teams = 1;
    }
    if (!g->round_had_both_teams) return;

    if (total_t > 0 && total_ct > 0) {
        if (alive_ct == 0) {
            end_round(g, 1, "Terrorists Win! (All Counter-Terrorists Eliminated)");
        } else if (alive_t == 0 && !(g->mode == MODE_DE && g->bomb_planted)) {
            end_round(g, 2, "Counter-Terrorists Win! (All Terrorists Eliminated)");
        }
    }
}

static void cs16_init(hxServerGame* self, hxSGEngine* eng, hxSGImports* imp,
                      const char* map_name, int restart) {
    (void)restart;
    Cs16SGame* g = (Cs16SGame*)self;
    g->eng = eng;
    g->imp = imp;
    g->bomb_entity_handle = -1;
    snprintf(g->map_name, sizeof(g->map_name), "%s", map_name ? map_name : "de_dust2");

    if (g->imp && g->imp->script_player_rules) {
        g->imp->script_player_rules(g->eng, 1);
    }
    if (g->imp && g->imp->cvar_set) {
        g->imp->cvar_set(g->eng, "ui_menu_session", "ingame");
        g->imp->cvar_set(g->eng, "mp_allow_teamchange", "1");
    }

    if (g->imp && g->imp->precache_asset) {
        static const char* models_to_precache[] = {
            "models/player.obj", "models/player_t1.obj", "models/player_t2.obj",
            "models/player_t3.obj", "models/player_t4.obj",
            "models/player_t1_bomb.obj", "models/player_t2_bomb.obj",
            "models/player_t3_bomb.obj", "models/player_t4_bomb.obj",
            "models/player_ct.obj",
            "models/player_ct1.obj", "models/player_ct2.obj", "models/player_ct3.obj",
            "models/player_ct4.obj", "models/player_vip.obj", "models/hostage.obj",
            "models/view_mg.obj", "models/weapon_mg.obj", "models/view_sg.obj", "models/weapon_sg.obj",
            "models/v_knife.obj", "models/p_knife.obj",
            "models/v_glock.obj", "models/p_glock.obj",
            "models/v_usp.obj", "models/p_usp.obj",
            "models/v_deagle.obj", "models/p_deagle.obj",
            "models/v_p228.obj", "models/p_p228.obj",
            "models/v_fiveseven.obj", "models/p_fiveseven.obj",
            "models/v_m3.obj", "models/p_m3.obj",
            "models/v_xm1014.obj", "models/p_xm1014.obj",
            "models/v_mp5.obj", "models/p_mp5.obj",
            "models/v_p90.obj", "models/p_p90.obj",
            "models/v_galil.obj", "models/p_galil.obj",
            "models/v_famas.obj", "models/p_famas.obj",
            "models/v_ak47.obj", "models/p_ak47.obj",
            "models/v_m4a1.obj", "models/p_m4a1.obj",
            "models/v_scout.obj", "models/p_scout.obj",
            "models/v_awp.obj", "models/p_awp.obj",
            "models/v_hegrenade.obj", "models/p_hegrenade.obj", "models/w_hegrenade.obj",
            "models/v_c4.obj", "models/p_c4.obj", "models/w_c4.obj"
        };
        for (size_t i = 0; i < sizeof(models_to_precache) / sizeof(models_to_precache[0]); ++i) {
            g->imp->precache_asset(g->eng, HX_PRECACHE_MODEL, models_to_precache[i]);
        }

        static const char* anims_to_precache[] = {
            "cod/animations/cs16_idle.hxac",
            "cod/animations/cs16_walk.hxac",
            "cod/animations/cs16_run.hxac",
            "cod/animations/cs16_crouch_idle.hxac",
            "cod/animations/cs16_crouch_walk.hxac",
            "cod/animations/cs16_jump.hxac",
            "cod/animations/cs16_aim_rifle.hxac",
            "cod/animations/cs16_shoot_rifle.hxac",
            "cod/animations/cs16_reload_rifle.hxac",
            "cod/animations/cs16_aim_pistol.hxac",
            "cod/animations/cs16_shoot_pistol.hxac",
            "cod/animations/cs16_reload_pistol.hxac",
            "cod/animations/cs16_aim_knife.hxac",
            "cod/animations/cs16_shoot_knife.hxac"
        };
        for (size_t i = 0; i < sizeof(anims_to_precache) / sizeof(anims_to_precache[0]); ++i) {
            g->imp->precache_asset(g->eng, HX_PRECACHE_ANIMATION, anims_to_precache[i]);
        }
    }

    const char* base_map = g->map_name;
    const char* slash = strrchr(base_map, '/');
    if (!slash) slash = strrchr(base_map, '\\');
    if (slash) base_map = slash + 1;

    if (strncmp(base_map, "de_", 3) == 0) g->mode = MODE_DE;
    else if (strncmp(base_map, "cs_", 3) == 0) g->mode = MODE_CS;
    else if (strncmp(base_map, "as_", 3) == 0) g->mode = MODE_AS;
    else g->mode = MODE_DE;

    const char* gt_str = (g->mode == MODE_DE) ? "sd" : (g->mode == MODE_CS) ? "re" : (g->mode == MODE_AS) ? "as" : "dm";
    if (g->imp && g->imp->cvar_set) {
        g->imp->cvar_set(g->eng, "g_gametype", gt_str);
    }

    g->score_t = 0;
    g->score_ct = 0;
    g->round_number = 0;
    g->num_bots = 0;
    g->bot_quota = 9;
    if (g->imp && g->imp->cvar_int) {
        int q = g->imp->cvar_int(g->eng, "bot_quota");
        if (q > 0) g->bot_quota = q;
    }

    query_map_spawns(g);
    query_objectives_from_bsp(g);
    zbot_init(&g->zbot, g->eng, g->imp, g, cs16_bot_client_cmd, g->map_name, gt_str, "counterterrorist", "terrorist");
    g->zbot.attacking_team = 1;

    while (g->bot_quota > 0 && g->num_bots < g->bot_quota && g->num_bots < MAX_BOTS) {
        add_bot(g, 0);
    }

    start_round(g);
}

static void cs16_shutdown(hxServerGame* self, int restart) {
    (void)restart;
    Cs16SGame* g = (Cs16SGame*)self;
    cleanup_round_entities(g);
    if (g->zbot.initialized) zbot_shutdown(&g->zbot);
}

static void cs16_run_frame(hxServerGame* self, int server_time_ms) {
    Cs16SGame* g = (Cs16SGame*)self;
    int delta_ms = (g->last_server_time_ms > 0) ? (server_time_ms - g->last_server_time_ms) : 16;
    if (delta_ms <= 0) delta_ms = 16;
    if (delta_ms > 100) delta_ms = 100;
    g->last_server_time_ms = server_time_ms;
    g->server_time_ms = server_time_ms;

    /* 1. Freeze time transition -> Active round */
    if (g->round_phase == ROUND_FREEZE && server_time_ms >= g->freeze_end_ms) {
        g->round_phase = ROUND_ACTIVE;
        if (g->imp && g->imp->player_script_control) {
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (g->clients[i].active) g->imp->player_script_control(g->eng, i, 0, 0);
            }
        }
        snprintf(g->banner_text, sizeof(g->banner_text), "GO! GO! GO!");
        g->banner_until_ms = server_time_ms + 1500;
    }

    /* 2. Round over transition -> Next round */
    if (g->round_phase == ROUND_OVER && server_time_ms >= g->round_over_end_ms) {
        start_round(g);
        return;
    }

    /* 3. C4 Bomb countdown & explosion (40 seconds) */
    if (g->mode == MODE_DE && g->bomb_planted && !g->round_ended) {
        int elapsed_bomb = server_time_ms - g->bomb_plant_ms;
        int beep_interval = 1000 - (elapsed_bomb * 850) / 40000;
        if (beep_interval < 120) beep_interval = 120;
        if (server_time_ms - g->last_beep_ms >= beep_interval) {
            g->last_beep_ms = server_time_ms;
            if (g->imp && g->imp->fx_sound) {
                g->imp->fx_sound(g->eng, "weapons/c4_beep1.wav", g->bomb_pos[0], g->bomb_pos[1], g->bomb_pos[2]);
            }
        }
        if (elapsed_bomb >= 40000) {
            if (g->imp && g->imp->radius_damage) {
                g->imp->radius_damage(g->eng, g->bomb_pos, 850.0f, 500, 40, -1, -1, 0);
            }
            end_round(g, 1, "Target Bombed! Terrorists Win!");
        }
    }

    /* 4. Round timer expiration */
    int round_limit_ms = 120000;
    if (g->imp && g->imp->cvar_float) {
        float rt = g->imp->cvar_float(g->eng, "mp_roundtime");
        if (rt >= 1.0f && rt <= 9.0f) round_limit_ms = (int)(rt * 60000.0f);
    }
    if (g->round_phase == ROUND_ACTIVE && (server_time_ms - g->freeze_end_ms) >= round_limit_ms && !g->bomb_planted && !g->round_ended) {
        if (g->mode == MODE_DE) end_round(g, 2, "Target Saved! Counter-Terrorists Win!");
        else if (g->mode == MODE_CS) end_round(g, 1, "Hostages Not Rescued! Terrorists Win!");
        else if (g->mode == MODE_AS) end_round(g, 1, "VIP Did Not Escape! Terrorists Win!");
        else end_round(g, 0, "Round Draw!");
    }

    /* 5. VIP death check in AS mode */
    if (g->mode == MODE_AS && g->vip_client >= 0 && !g->round_ended && g->round_phase == ROUND_ACTIVE) {
        hxSGBotState vst;
        if (g->imp->bot_state && g->imp->bot_state(g->eng, g->vip_client, &vst) && vst.health <= 0) {
            end_round(g, 1, "The VIP has been assassinated! Terrorists Win!");
        }
    }

    /* 6. Bot quota maintenance */
    if (g->imp && g->imp->cvar_int) {
        int en = g->imp->cvar_int(g->eng, "bot_enable");
        int q = g->imp->cvar_int(g->eng, "bot_quota");
        if (en == 0) q = 0;
        if (q >= 0 && q != g->bot_quota) {
            g->bot_quota = q;
            if (q == 0) kick_all_bots(g);
        }
    }
    if (g->bot_quota > 0 && g->num_bots < g->bot_quota && g->num_bots < MAX_BOTS) {
        add_bot(g, 0);
    }

    /* 7. Human player (client 0) objective interactions */
    if (g->clients[0].active && g->clients[0].team > 0 && g->round_phase == ROUND_ACTIVE && !g->round_ended) {
        hxSGBotState pst;
        if (g->imp && g->imp->bot_state && g->imp->bot_state(g->eng, 0, &pst) && pst.health > 0) {
            /* Determine if player is holding plant or defuse action */
            int btn = g->clients[0].last_buttons;
            int action_held = (btn & 1) || (btn & 8); /* BUTTON_ATTACK (left click) or BUTTON_USE ('E') */

            /* DE mode: Bomb planting at bombsite */
            if (g->mode == MODE_DE && g->clients[0].has_bomb && !g->bomb_planted && g->clients[0].team == 1) {
                int near_site = 0;
                for (int s = 0; s < g->num_bomb_sites; ++s) {
                    if (point_near_objective(pst.origin, &g->bomb_sites[s], 180.0f)) {
                        near_site = 1;
                        break;
                    }
                }
                if (near_site && action_held) {
                    g->clients[0].plant_ms += delta_ms;
                    g->clients[0].plant_ticks = (g->clients[0].plant_ms * 45) / 3500;
                    if (g->clients[0].plant_ms >= 3500) {
                        g->clients[0].plant_ms = 0;
                        g->clients[0].plant_ticks = 0;
                        plant_bomb_at(g, 0, pst.origin);
                    }
                } else {
                    g->clients[0].plant_ms = 0;
                    g->clients[0].plant_ticks = 0;
                }
            }
            /* DE mode: Bomb defusal by CT */
            else if (g->mode == MODE_DE && g->bomb_planted && g->clients[0].team == 2) {
                if (dist3(pst.origin, g->bomb_pos) < 115.0f && action_held) {
                    g->clients[0].defuse_ms += delta_ms;
                    int needed_ms = g->clients[0].has_defuser ? 5000 : 10000;
                    int needed_ticks = g->clients[0].has_defuser ? 45 : 90;
                    g->clients[0].defuse_ticks = (g->clients[0].defuse_ms * needed_ticks) / needed_ms;
                    if (g->clients[0].defuse_ms >= needed_ms) {
                        g->clients[0].defuse_ms = 0;
                        g->clients[0].defuse_ticks = 0;
                        end_round(g, 2, "The bomb has been defused! Counter-Terrorists Win!");
                    }
                } else {
                    g->clients[0].defuse_ms = 0;
                    g->clients[0].defuse_ticks = 0;
                }
            }
            /* AS mode: VIP escape */
            else if (g->mode == MODE_AS && g->clients[0].is_vip) {
                for (int v = 0; v < g->num_vip_zones; ++v) {
                    if (point_near_objective(pst.origin, &g->vip_zones[v], 160.0f)) {
                        end_round(g, 2, "The VIP has escaped! Counter-Terrorists Win!");
                        break;
                    }
                }
            }
            /* CS mode: Hostage pickup when approaching or pressing use */
            else if (g->mode == MODE_CS && g->clients[0].team == 2) {
                for (int h = 0; h < g->num_hostages; ++h) {
                    if (!g->hostages[h].rescued && g->hostages[h].leader_client < 0) {
                        if (dist3(pst.origin, g->hostages[h].origin) < 105.0f) {
                            g->hostages[h].leader_client = 0;
                            g->clients[0].following_hostage = 1;
                            if (g->imp->print) g->imp->print(g->eng, "[CS16] Hostage is following you! Lead them to the rescue zone.\n");
                        }
                    }
                }
            }
        }
    }

    update_hostages_and_objectives(g, (float)delta_ms * 0.001f);
    check_team_elimination(g);
    cs16_bot_objectives(g, delta_ms);
    if (g->zbot.initialized && g->round_phase == ROUND_ACTIVE) {
        zbot_run_frame(&g->zbot, server_time_ms);
    }
    update_player_animations(g);
    update_hud(g);
}

static void cs16_client_think(hxServerGame* self, int client_num, int cmd_msec,
                              int forward, int right, int up,
                              int angle_pitch, int angle_yaw, int angle_roll,
                              int buttons, int weapon) {
    (void)cmd_msec; (void)forward; (void)right; (void)up;
    (void)angle_pitch; (void)angle_yaw; (void)angle_roll; (void)weapon;
    Cs16SGame* g = (Cs16SGame*)self;
    if (!g || client_num < 0 || client_num >= MAX_CLIENTS) return;
    g->clients[client_num].last_buttons = buttons;
    if (client_num != 0 || !g->clients[0].active || g->round_ended) return;

    /* Button bit 3 (1 << 3 = 8) is BUTTON_USE ('E' key) */
    if ((buttons & 8) && g->round_phase == ROUND_ACTIVE && g->imp && g->imp->bot_state) {
        hxSGBotState pst;
        if (g->imp->bot_state(g->eng, 0, &pst) && pst.health > 0) {
            /* E key toggles hostage following for CT */
            if (g->mode == MODE_CS && g->clients[0].team == 2) {
                for (int h = 0; h < g->num_hostages; ++h) {
                    if (!g->hostages[h].rescued && dist3(pst.origin, g->hostages[h].origin) < 120.0f) {
                        g->hostages[h].leader_client = 0;
                        g->clients[0].following_hostage = 1;
                    }
                }
            }
        }
    }
}

static int cs16_player_damage_detail(hxServerGame* self, const hxSGDamage* dmg) {
    Cs16SGame* g = (Cs16SGame*)self;
    if (!g || !dmg || dmg->victim < 0 || dmg->victim >= MAX_CLIENTS) return 0;

    /* Freeze time or round over: no damage */
    if (g->round_phase == ROUND_FREEZE) return 1;

    int vic = dmg->victim;
    int att = dmg->attacker;

    /* Friendly fire check */
    if (att >= 0 && att < MAX_CLIENTS && att != vic) {
        int ff = (g->imp && g->imp->cvar_int) ? g->imp->cvar_int(g->eng, "mp_friendlyfire") : 0;
        if (!ff && g->clients[att].team > 0 && g->clients[att].team == g->clients[vic].team) {
            return 1; /* Absorb friendly fire */
        }
    }

    int amount = dmg->amount;

    /* Knife range & backstab check */
    if (dmg->weapon && strcmp(dmg->weapon, "knife") == 0 && att >= 0 && att < MAX_CLIENTS) {
        if (g->imp && g->imp->bot_state) {
            hxSGBotState ast, vst;
            if (g->imp->bot_state(g->eng, att, &ast) && g->imp->bot_state(g->eng, vic, &vst)) {
                float d = dist3(ast.origin, vst.origin);
                if (d > 84.0f) {
                    return 1; /* Out of knife melee reach */
                }
                /* Backstab bonus if attacker and victim face the same direction */
                float diff = fabsf(ast.yaw - vst.yaw);
                while (diff > 360.0f) diff -= 360.0f;
                if (diff > 180.0f) diff = 360.0f - diff;
                if (diff < 65.0f) amount = 135;
            }
        }
    }

    /* Kevlar & Helmet armor absorption */
    if (g->clients[vic].armor > 0 && amount > 1) {
        int absorbed = (amount * 35) / 100;
        if (dmg->hit_group == 1 && !g->clients[vic].has_helmet) {
            absorbed = 0; /* Unhelmeted headshot bypasses vest */
        }
        if (absorbed > g->clients[vic].armor) absorbed = g->clients[vic].armor;
        g->clients[vic].armor -= absorbed;
        amount -= absorbed;
    }

    if (amount > 0 && g->imp && g->imp->player_finish_damage) {
        g->imp->player_finish_damage(g->eng, vic, att, amount);
    }
    return 1;
}

static void cs16_player_killed_detail(hxServerGame* self, const hxSGDamage* dmg) {
    Cs16SGame* g = (Cs16SGame*)self;
    if (!g || !dmg || dmg->victim < 0 || dmg->victim >= MAX_CLIENTS) return;

    int vic = dmg->victim;
    int att = dmg->attacker;
    g->clients[vic].deaths++;
    g->clients[vic].survived_round = 0;

    /* If bomb carrier dies, drop C4 at their position or transfer */
    if (g->clients[vic].has_bomb) {
        g->clients[vic].has_bomb = 0;
        apply_player_model(g, vic);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (i != vic && g->clients[i].active && g->clients[i].team == 1) {
                hxSGBotState st;
                if (g->imp->bot_state && g->imp->bot_state(g->eng, i, &st) && st.health > 0) {
                    g->clients[i].has_bomb = 1;
                    g->bomb_carrier_client = i;
                    apply_player_model(g, i);
                    if (g->imp->player_weapon) {
                        g->imp->player_weapon(g->eng, i, HX_WEAPON_SLOT_SET, 3, "c4", 1);
                        g->imp->player_weapon(g->eng, i, HX_WEAPON_MAX_AMMO, 3, "c4", 1);
                    }
                    break;
                }
            }
        }
    }

    if (att >= 0 && att < MAX_CLIENTS && att != vic) {
        if (g->clients[att].team != g->clients[vic].team) {
            g->clients[att].frags++;
            g->clients[att].money += 300;
            if (g->clients[att].money > 16000) g->clients[att].money = 16000;
        } else {
            g->clients[att].frags--;
        }
        if (g->imp && g->imp->player_state) {
            hxSGPlayerState aps;
            memset(&aps, 0, sizeof(aps));
            aps.mask = HX_PLAYER_SCORE;
            aps.score = g->clients[att].frags;
            g->imp->player_state(g->eng, att, &aps, NULL);
        }
    }

    if (g->imp && g->imp->player_state) {
        hxSGPlayerState vps;
        memset(&vps, 0, sizeof(vps));
        vps.mask = HX_PLAYER_DEATHS;
        vps.deaths = g->clients[vic].deaths;
        g->imp->player_state(g->eng, vic, &vps, NULL);
    }

    if (g->imp && g->imp->player_obituary) {
        const char* w = (dmg->weapon && *dmg->weapon) ? dmg->weapon : "glock";
        g->imp->player_obituary(g->eng, vic, (att >= 0 && att < MAX_CLIENTS) ? att : vic, w);
    }
}

static int cs16_client_connect(hxServerGame* self, int client, const char* name) {
    (void)name;
    Cs16SGame* g = (Cs16SGame*)self;
    if (client < 0 || client >= MAX_CLIENTS) return -1;
    g->clients[client].active = 1;
    g->clients[client].is_bot = 0;
    int initial_team = (client == 0) ? 1 : ((client % 2) ? 1 : 2);
    if (client == 0 && g->imp && g->imp->cvar_string) {
        const char* ut = g->imp->cvar_string(g->eng, "ui_team");
        if (!ut || !*ut) ut = g->imp->cvar_string(g->eng, "team");
        if (ut && (strcmp(ut, "2") == 0 || cs_strcasecmp(ut, "ct") == 0 || cs_strcasecmp(ut, "counterterrorist") == 0)) {
            initial_team = 2;
        } else if (ut && (strcmp(ut, "0") == 0 || cs_strcasecmp(ut, "spectator") == 0)) {
            initial_team = 0;
        } else {
            initial_team = 1;
        }
    }
    int start_money = 800;
    if (g->imp && g->imp->cvar_int) {
        int sm = g->imp->cvar_int(g->eng, "mp_startmoney");
        if (sm >= 800 && sm <= 16000) start_money = sm;
    }
    g->clients[client].team = initial_team;
    g->clients[client].class_id = 1;
    g->clients[client].money = start_money;
    g->clients[client].frags = 0;
    g->clients[client].deaths = 0;
    g->clients[client].survived_round = 0;
    int eng_team = (initial_team == 1) ? 1 : (initial_team == 2 ? 0 : -1);
    if (g->imp && g->imp->player_set_team) {
        g->imp->player_set_team(g->eng, client, eng_team);
    }
    return 0;
}

static void cs16_client_disconnect(hxServerGame* self, int client) {
    Cs16SGame* g = (Cs16SGame*)self;
    if (client < 0 || client >= MAX_CLIENTS) return;
    g->clients[client].active = 0;
    if (g->zbot.initialized) zbot_client_disconnect(&g->zbot, client);
}

static void cs16_client_spawn(hxServerGame* self, int client) {
    Cs16SGame* g = (Cs16SGame*)self;
    if (client < 0 || client >= MAX_CLIENTS) return;
    g->clients[client].active = 1;
    if (client == 0 && g->clients[0].team <= 0) {
        g->clients[0].team = 1;
    }
    spawn_client_on_team(g, client);
    apply_player_model(g, client);
}

static int cs16_console_command(hxServerGame* self, const char* cmd) {
    Cs16SGame* g = (Cs16SGame*)self;
    if (!cmd) return 0;
    if (g->zbot.initialized && zbot_console_command(&g->zbot, cmd)) return 1;

    if (strncmp(cmd, "g_gametype ", 11) == 0 || strncmp(cmd, "gametype ", 9) == 0) {
        const char* mode_str = strchr(cmd, ' ') + 1;
        if (cs_strcasecmp(mode_str, "de") == 0) g->mode = MODE_DE;
        else if (cs_strcasecmp(mode_str, "cs") == 0) g->mode = MODE_CS;
        else if (cs_strcasecmp(mode_str, "as") == 0) g->mode = MODE_AS;
        query_objectives_from_bsp(g);
        start_round(g);
        return 1;
    }

    if (cs_strcasecmp(cmd, "bot_add") == 0) { add_bot(g, 0); return 1; }
    if (cs_strcasecmp(cmd, "bot_add_t") == 0) { add_bot(g, 1); return 1; }
    if (cs_strcasecmp(cmd, "bot_add_ct") == 0) { add_bot(g, 2); return 1; }
    if (cs_strcasecmp(cmd, "bot_kick") == 0) { kick_all_bots(g); return 1; }
    if (strncmp(cmd, "bot_quota ", 10) == 0) {
        int q = atoi(cmd + 10);
        if (q < 0) q = 0;
        if (q > MAX_BOTS) q = MAX_BOTS;
        g->bot_quota = q;
        if (q == 0) kick_all_bots(g);
        while (g->num_bots < q) add_bot(g, 0);
        return 1;
    }

    /* Team selection commands */
    if (cs_strcasecmp(cmd, "chooseteam") == 0) {
        if (g->imp && g->imp->player_ui) {
            g->imp->player_ui(g->eng, 0, 1, "teamselect", NULL);
        }
        return 1;
    }
    if (cs_strcasecmp(cmd, "jointeam 1") == 0 || cs_strcasecmp(cmd, "team_t") == 0) {
        if (g->imp && g->imp->cvar_int && g->imp->cvar_int(g->eng, "mp_allow_teamchange") == 0 && g->round_number > 1) {
            if (g->imp->print) g->imp->print(g->eng, "[CS16] Team switching is disabled by mp_allow_teamchange.\n");
            return 1;
        }
        g->clients[0].team = 1;
        g->clients[0].survived_round = 0;
        if (g->imp && g->imp->cvar_set) g->imp->cvar_set(g->eng, "ui_team", "1");
        spawn_client_on_team(g, 0);
        apply_player_model(g, 0);
        if (g->imp && g->imp->player_ui) {
            g->imp->player_ui(g->eng, 0, 2, "teamselect", NULL);
            g->imp->player_ui(g->eng, 0, 2, "ingame", NULL);
            g->imp->player_ui(g->eng, 0, 1, "classselect_t", NULL);
        }
        return 1;
    }
    if (cs_strcasecmp(cmd, "jointeam 2") == 0 || cs_strcasecmp(cmd, "team_ct") == 0) {
        if (g->imp && g->imp->cvar_int && g->imp->cvar_int(g->eng, "mp_allow_teamchange") == 0 && g->round_number > 1) {
            if (g->imp->print) g->imp->print(g->eng, "[CS16] Team switching is disabled by mp_allow_teamchange.\n");
            return 1;
        }
        g->clients[0].team = 2;
        g->clients[0].survived_round = 0;
        if (g->imp && g->imp->cvar_set) g->imp->cvar_set(g->eng, "ui_team", "2");
        spawn_client_on_team(g, 0);
        apply_player_model(g, 0);
        if (g->imp && g->imp->player_ui) {
            g->imp->player_ui(g->eng, 0, 2, "teamselect", NULL);
            g->imp->player_ui(g->eng, 0, 2, "ingame", NULL);
            g->imp->player_ui(g->eng, 0, 1, "classselect_ct", NULL);
        }
        return 1;
    }
    if (cs_strcasecmp(cmd, "jointeam 5") == 0) {
        int count_t = 0, count_ct = 0;
        for (int i = 1; i < MAX_CLIENTS; ++i) {
            if (g->clients[i].active) {
                if (g->clients[i].team == 1) count_t++;
                else if (g->clients[i].team == 2) count_ct++;
            }
        }
        g->clients[0].team = (count_t <= count_ct) ? 1 : 2;
        g->clients[0].class_id = (rand() % 4) + 1;
        g->clients[0].survived_round = 0;
        spawn_client_on_team(g, 0);
        apply_player_model(g, 0);
        if (g->imp && g->imp->player_ui) {
            g->imp->player_ui(g->eng, 0, 2, "teamselect", NULL);
            g->imp->player_ui(g->eng, 0, 2, "ingame", NULL);
            g->imp->player_ui(g->eng, 0, 2, "classselect_t", NULL);
            g->imp->player_ui(g->eng, 0, 2, "classselect_ct", NULL);
        }
        return 1;
    }
    if (cs_strcasecmp(cmd, "jointeam 6") == 0 || cs_strcasecmp(cmd, "spectate") == 0) {
        g->clients[0].team = 0;
        g->clients[0].survived_round = 0;
        spawn_client_on_team(g, 0);
        if (g->imp && g->imp->player_ui) {
            g->imp->player_ui(g->eng, 0, 2, "teamselect", NULL);
            g->imp->player_ui(g->eng, 0, 2, "ingame", NULL);
            g->imp->player_ui(g->eng, 0, 2, "classselect_t", NULL);
            g->imp->player_ui(g->eng, 0, 2, "classselect_ct", NULL);
        }
        if (g->imp && g->imp->print) g->imp->print(g->eng, "[CS16] Entered Spectator Mode\n");
        return 1;
    }

    /* Class / Skin selection commands */
    if (strncmp(cmd, "joinclass ", 10) == 0) {
        int cls = atoi(cmd + 10);
        if (cls == 5) cls = (rand() % 4) + 1;
        if (cls >= 1 && cls <= 4) {
            g->clients[0].class_id = cls;
            spawn_client_on_team(g, 0);
            apply_player_model(g, 0);
            if (g->imp && g->imp->player_ui) {
                g->imp->player_ui(g->eng, 0, 2, "teamselect", NULL);
                g->imp->player_ui(g->eng, 0, 2, "classselect_t", NULL);
                g->imp->player_ui(g->eng, 0, 2, "classselect_ct", NULL);
            }
            return 1;
        }
    }

    /* Open Buy Menu */
    if (cs_strcasecmp(cmd, "buy") == 0 || cs_strcasecmp(cmd, "buymenu") == 0) {
        if (g->imp && g->imp->player_ui) {
            g->imp->player_ui(g->eng, 0, 1, "buymenu", NULL);
        }
        return 1;
    }

    if (cs_strcasecmp(cmd, "plant") == 0 && g->mode == MODE_DE && !g->bomb_planted) {
        hxSGBotState pst;
        if (g->imp && g->imp->bot_state && g->imp->bot_state(g->eng, 0, &pst)) {
            plant_bomb_at(g, 0, pst.origin);
        }
        return 1;
    }
    if (cs_strcasecmp(cmd, "defuse") == 0 && g->mode == MODE_DE && g->bomb_planted) {
        end_round(g, 2, "The bomb has been defused! Counter-Terrorists Win!");
        return 1;
    }

    /* Classic CS 1.6 Buy menu commands */
    if (strncmp(cmd, "buy ", 4) == 0) {
        if (g->clients[0].is_vip) {
            if (g->imp && g->imp->print) g->imp->print(g->eng, "[CS16] You are the VIP - you cannot buy weapons!\n");
            return 1;
        }
        if (!client_in_buyzone(g, 0)) {
            if (g->imp && g->imp->print) g->imp->print(g->eng, "[CS16] You are outside the buy zone!\n");
            return 1;
        }

        const char* w = cmd + 4;
        int cost = 0;
        const char* hx_weapon = NULL;
        int slot = 0;
        int is_armor = 0;
        int is_helmet = 0;
        int is_ammo_slot = -1;

        /* Pistols -> ABI slot 1 (2 key) */
        if (cs_strcasecmp(w, "glock") == 0) { cost = 400; hx_weapon = "glock"; slot = 1; }
        else if (cs_strcasecmp(w, "usp") == 0) { cost = 500; hx_weapon = "usp"; slot = 1; }
        else if (cs_strcasecmp(w, "p228") == 0) { cost = 600; hx_weapon = "p228"; slot = 1; }
        else if (cs_strcasecmp(w, "deagle") == 0) { cost = 650; hx_weapon = "deagle"; slot = 1; }
        else if (cs_strcasecmp(w, "elites") == 0 || cs_strcasecmp(w, "fn57") == 0 || cs_strcasecmp(w, "fiveseven") == 0) {
            cost = 750; hx_weapon = "fiveseven"; slot = 1;
        }

        /* Shotguns -> ABI slot 0 (1 key) */
        else if (cs_strcasecmp(w, "m3") == 0) { cost = 1700; hx_weapon = "m3"; slot = 0; }
        else if (cs_strcasecmp(w, "xm1014") == 0) { cost = 3000; hx_weapon = "xm1014"; slot = 0; }

        /* SMGs -> ABI slot 0 (1 key) */
        else if (cs_strcasecmp(w, "mp5") == 0 || cs_strcasecmp(w, "tmp") == 0 || cs_strcasecmp(w, "mac10") == 0 || cs_strcasecmp(w, "ump45") == 0) {
            cost = 1500; hx_weapon = "mp5"; slot = 0;
        }
        else if (cs_strcasecmp(w, "p90") == 0) { cost = 2350; hx_weapon = "p90"; slot = 0; }

        /* Rifles & Snipers -> ABI slot 0 (1 key) */
        else if (cs_strcasecmp(w, "galil") == 0) { cost = 2000; hx_weapon = "galil"; slot = 0; }
        else if (cs_strcasecmp(w, "famas") == 0) { cost = 2250; hx_weapon = "famas"; slot = 0; }
        else if (cs_strcasecmp(w, "ak47") == 0 || cs_strcasecmp(w, "sg552") == 0) { cost = 2500; hx_weapon = "ak47"; slot = 0; }
        else if (cs_strcasecmp(w, "m4a1") == 0 || cs_strcasecmp(w, "aug") == 0 || cs_strcasecmp(w, "m249") == 0) { cost = 3100; hx_weapon = "m4a1"; slot = 0; }
        else if (cs_strcasecmp(w, "scout") == 0) { cost = 2750; hx_weapon = "scout"; slot = 0; }
        else if (cs_strcasecmp(w, "awp") == 0 || cs_strcasecmp(w, "g3sg1") == 0 || cs_strcasecmp(w, "sg550") == 0) { cost = 4750; hx_weapon = "awp"; slot = 0; }

        /* Ammo & Equipment */
        else if (cs_strcasecmp(w, "primammo") == 0) { cost = 60; is_ammo_slot = 0; }
        else if (cs_strcasecmp(w, "secammo") == 0) { cost = 50; is_ammo_slot = 1; }
        else if (cs_strcasecmp(w, "kevlar") == 0 || cs_strcasecmp(w, "vest") == 0) { cost = 650; is_armor = 1; }
        else if (cs_strcasecmp(w, "vesthelm") == 0) { cost = 1000; is_armor = 1; is_helmet = 1; }
        else if (cs_strcasecmp(w, "hegren") == 0 || cs_strcasecmp(w, "hegrenade") == 0 || cs_strcasecmp(w, "flash") == 0 || cs_strcasecmp(w, "sgren") == 0) {
            cost = 300; hx_weapon = "hegrenade"; slot = 3;
        }
        else if (cs_strcasecmp(w, "defuser") == 0) {
            if (g->clients[0].team != 2) return 1;
            cost = 200; slot = -1;
        }
        else if (cs_strcasecmp(w, "autobuy") == 0) {
            if (g->clients[0].team == 1) cs16_console_command(self, "buy ak47");
            else cs16_console_command(self, "buy m4a1");
            cs16_console_command(self, "buy vesthelm");
            cs16_console_command(self, "buy hegren");
            if (g->clients[0].team == 2) cs16_console_command(self, "buy defuser");
            return 1;
        }
        else if (cs_strcasecmp(w, "rebuy") == 0) {
            cs16_console_command(self, "buy vesthelm");
            cs16_console_command(self, "buy primammo");
            cs16_console_command(self, "buy secammo");
            cs16_console_command(self, "buy hegren");
            return 1;
        }

        if (cost > 0 && g->clients[0].money >= cost) {
            g->clients[0].money -= cost;
            if (is_armor) {
                g->clients[0].armor = 100;
                if (is_helmet) g->clients[0].has_helmet = 1;
            } else if (is_ammo_slot >= 0 && g->imp && g->imp->player_weapon) {
                const char* wname = (is_ammo_slot == 0) ? g->clients[0].primary_weap : g->clients[0].secondary_weap;
                if (wname && *wname) g->imp->player_weapon(g->eng, 0, HX_WEAPON_MAX_AMMO, is_ammo_slot, wname, 1);
            } else if (slot == -1) {
                g->clients[0].has_defuser = 1;
            } else if (hx_weapon && g->imp && g->imp->player_weapon) {
                if (slot == 0) snprintf(g->clients[0].primary_weap, sizeof(g->clients[0].primary_weap), "%s", hx_weapon);
                else if (slot == 1) snprintf(g->clients[0].secondary_weap, sizeof(g->clients[0].secondary_weap), "%s", hx_weapon);
                else if (slot == 3) g->clients[0].has_hegrenade = 1;

                /* Ensure Knife remains on slot 2 (key 3) */
                g->imp->player_weapon(g->eng, 0, HX_WEAPON_SLOT_SET, 2, "knife", 1);
                g->imp->player_weapon(g->eng, 0, HX_WEAPON_MAX_AMMO, 2, "knife", 1);

                g->imp->player_weapon(g->eng, 0, HX_WEAPON_SLOT_SET, slot, hx_weapon, 1);
                g->imp->player_weapon(g->eng, 0, HX_WEAPON_MAX_AMMO, slot, hx_weapon, 1);
                g->imp->player_weapon(g->eng, 0, HX_WEAPON_SWITCH, slot, hx_weapon, 0);
            }
            if (g->imp && g->imp->print) {
                char msg[96];
                snprintf(msg, sizeof(msg), "[CS16] Purchased %s ($%d, balance: $%d)\n", w, cost, g->clients[0].money);
                g->imp->print(g->eng, msg);
            }
        } else if (cost > 0 && g->imp && g->imp->print) {
            char msg[96];
            snprintf(msg, sizeof(msg), "[CS16] Insufficient funds: need $%d for %s (have: $%d)\n", cost, w, g->clients[0].money);
            g->imp->print(g->eng, msg);
        }
        return 1;
    }

    return 0;
}

static int cs16_client_command(hxServerGame* self, int client_num, const char* cmd) {
    (void)client_num;
    return cs16_console_command(self, cmd);
}

static void cs16_destroy(hxServerGame* self) {
    free(self);
}

CS16_SG_EXPORT hxServerGame* helix_sgame_create(void) {
    Cs16SGame* g = (Cs16SGame*)calloc(1, sizeof(Cs16SGame));
    if (!g) return NULL;
    g->base.api_version = HELIX_SGAME_API_VERSION;
    g->base.init = cs16_init;
    g->base.shutdown = cs16_shutdown;
    g->base.run_frame = cs16_run_frame;
    g->base.client_connect = cs16_client_connect;
    g->base.client_disconnect = cs16_client_disconnect;
    g->base.client_begin = cs16_client_spawn;
    g->base.client_command = cs16_client_command;
    g->base.client_think = cs16_client_think;
    g->base.console_command = cs16_console_command;
    g->base.player_damage_detail = cs16_player_damage_detail;
    g->base.player_killed_detail = cs16_player_killed_detail;
    g->base.destroy = cs16_destroy;
    return &g->base;
}
