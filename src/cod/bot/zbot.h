/*
 * Counter-Strike 1.6 / Condition Zero ZBot-inspired AI Controller for CoD1 Multiplayer
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef HXFS_COD_BOT_ZBOT_H
#define HXFS_COD_BOT_ZBOT_H

#include "helix/game/iserver_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZBOT_MAX_CLIENTS   64
#define ZBOT_MAX_WAYPOINTS 32768
#define ZBOT_MAX_EDGES     12
#define ZBOT_MAX_PATH      256
#define ZBOT_MAX_ROASTS    32
#define ZBOT_VSAY_KINDS    6
#define ZBOT_VSAY_PER_KIND 8

/* Usercmd button bits (mirror of hx::BUTTON_* in pmove.hpp). */
#define ZBOT_BTN_ATTACK  (1 << 0)
#define ZBOT_BTN_JUMP    (1 << 1)
#define ZBOT_BTN_CROUCH  (1 << 2)
#define ZBOT_BTN_USE     (1 << 3)
#define ZBOT_BTN_WALK    (1 << 4)
#define ZBOT_BTN_RELOAD  (1 << 7)
#define ZBOT_BTN_ADS     (1 << 8)
#define ZBOT_BTN_LEAN_L  (1 << 9)
#define ZBOT_BTN_LEAN_R  (1 << 10)
#define ZBOT_BTN_PISTOL  (1 << 15)

typedef enum ZBotSkill {
    ZBOT_SKILL_LOW    = 0,
    ZBOT_SKILL_MID    = 1,
    ZBOT_SKILL_PRO    = 2,
    ZBOT_SKILL_RANDOM = 3
} ZBotSkill;

typedef enum ZBotRole {
    ZBOT_ROLE_NONE       = 0,
    ZBOT_ROLE_ROAM       = 1,
    ZBOT_ROLE_ESCORT     = 2,
    ZBOT_ROLE_ATTACK_OBJ = 3,
    ZBOT_ROLE_DEFEND_OBJ = 4,
    ZBOT_ROLE_PLANT      = 5,
    ZBOT_ROLE_DEFUSE     = 6
} ZBotRole;

typedef enum ZBotVsayKind {
    ZBOT_VSAY_SPOT = 0, ZBOT_VSAY_KILL, ZBOT_VSAY_ATTACK,
    ZBOT_VSAY_DEFEND, ZBOT_VSAY_HELP, ZBOT_VSAY_RANDOM
} ZBotVsayKind;

typedef enum ZBotWpFlags {
    ZBOT_WP_NORMAL    = 0,
    ZBOT_WP_SPAWN_ALL = 1 << 0,
    ZBOT_WP_SPAWN_AX  = 1 << 1,
    ZBOT_WP_OBJECTIVE = 1 << 2,
    ZBOT_WP_COVER     = 1 << 3,
    ZBOT_WP_LADDER    = 1 << 4,
    ZBOT_WP_CROUCH    = 1 << 5,
    ZBOT_WP_HAZARD    = 1 << 6
} ZBotWpFlags;

typedef struct ZBotWaypoint {
    float origin[3];      /* player centre position */
    unsigned int flags;
    int entity_id;
    int edge_count;
    int edges[ZBOT_MAX_EDGES];
    float dists[ZBOT_MAX_EDGES];
    int blocked_until[ZBOT_MAX_EDGES];
    float danger;         /* ZBot-style learned danger (deaths nearby), decays */
} ZBotWaypoint;

typedef struct ZBotProfile {
    char name[32];
    ZBotSkill base_skill;
    float aggression;
    float teamwork;
    char pref_class[16];
} ZBotProfile;

enum { ZBOT_OBJECTIVE_NONE, ZBOT_OBJECTIVE_HQ, ZBOT_OBJECTIVE_GUARD,
       ZBOT_OBJECTIVE_RE_PICKUP, ZBOT_OBJECTIVE_RE_DELIVER };
typedef struct ZBotObjectiveTask {
    int kind,entity,model;
    float origin[3],radius;
} ZBotObjectiveTask;

typedef struct ZBotAgent {
    int active;
    int client_num;
    int profile_idx;
    char name[32];
    ZBotSkill skill;
    ZBotRole role;
    int desired_team;     /* 0 allies, 1 axis, -1 auto */
    char desired_weapon[64];
    int next_join_ms;
    int joined;

    /* Navigation & pathing */
    int current_wp;
    int goal_wp;
    int path[ZBOT_MAX_PATH];
    int path_len;
    int path_idx;
    int next_goal_ms, next_path_ms;
    int assigned_goal, flank_wp, plan_revision, order_kind, order_sender, order_until_ms;
    float watch_yaw;
    float target_pos[3];
    int has_target_pos;
    ZBotObjectiveTask objective;
    int objective_wp;

    /* Stuck detection */
    float last_check_pos[3];
    int last_check_ms;
    int unstuck_until_ms;
    int unstuck_dir;

    /* Sensing & combat */
    int enemy_client;
    int heard_until_ms;
    float heard_pos[3];
    int next_sighting_ms;
    int grenade_phase, grenade_until_ms, next_grenade_ms;
    float grenade_target[3], grenade_pitch;
    int enemy_seen_ms;
    int enemy_react_ms;
    float enemy_last_pos[3];
    int memory_until_ms;

    /* Spring-damper human-like aim state */
    float look_pitch;
    float look_yaw;
    float aim_vel_pitch;
    float aim_vel_yaw;
    float err_pitch;
    float err_yaw;
    int next_err_ms;
    int next_burst_ms;
    int burst_until_ms;
    int fire_toggle;
    int strafe_dir;
    int next_strafe_ms;
    int next_scan_ms;
    int last_fwd, last_right, last_up, last_pitch, last_yaw, last_buttons;
    int crouch_until_ms;

    /* Objective interaction (+use hold for SD plant/defuse, RE, HQ) */
    int interact_wp;
    int interact_until_ms;

    /* Voice (vsay) & Chat/Roast cooldowns */
    int next_vsay_ms;
    int next_chat_ms;
    int next_random_vsay_ms;
    int last_known_deaths;
    int last_known_frags;
    int was_alive;
} ZBotAgent;

typedef int (*ZBotClientCmdFn)(void* host_game, int client_num, const char* cmd);

typedef struct ZBotManager {
    int initialized;
    hxSGEngine* eng;
    hxSGImports* imp;
    void* host_game;
    ZBotClientCmdFn client_cmd_cb;

    char map_name[64];
    char gametype[32];
    char allies_nation[32];
    char axis_nation[32];

    ZBotSkill configured_skill;
    int quota;
    int chat_enabled;
    int roast_enabled;
    int vsay_enabled;
    unsigned int rng_state;

    int now_ms;
    int last_frame_ms;
    int next_quota_check_ms;
    int bomb_planted;
    int attacking_team;
    float planted_pos[3];
    int site_count, sites[8], attack_site, previous_site, planter, defuser;
    int plan_revision, next_plan_ms, round_number;
    unsigned int route_seed;
    int order_cooldown[ZBOT_MAX_CLIENTS];
    /* Team-local evidence, retained across rounds; never populated from hidden positions. */
    float heat[2][ZBOT_MAX_WAYPOINTS];
    int path_team;
    int nav_plans_this_frame, grenade_checks_this_frame, last_heat_decay_ms;
    unsigned nav_searches, nav_expanded;
    /* Reused indexed heap; no per-search allocation or large stack arrays. */
    struct {float g[ZBOT_MAX_WAYPOINTS],f[ZBOT_MAX_WAYPOINTS];
            int parent[ZBOT_MAX_WAYPOINTS],heap[ZBOT_MAX_WAYPOINTS],position[ZBOT_MAX_WAYPOINTS];} search;

    int profile_count;
    ZBotProfile profiles[32];

    int kill_roast_count;
    char kill_roasts[ZBOT_MAX_ROASTS][128];
    int death_roast_count;
    char death_roasts[ZBOT_MAX_ROASTS][128];
    int obj_chat_count;
    char obj_chats[ZBOT_MAX_ROASTS][128];
    int vsay_count[ZBOT_VSAY_KINDS];
    char vsay[ZBOT_VSAY_KINDS][ZBOT_VSAY_PER_KIND][32];

    int wp_count;
    ZBotWaypoint waypoints[ZBOT_MAX_WAYPOINTS];

    ZBotAgent bots[ZBOT_MAX_CLIENTS];
} ZBotManager;

void zbot_init(ZBotManager* mgr, hxSGEngine* eng, hxSGImports* imp,
               void* host_game, ZBotClientCmdFn client_cmd_cb,
               const char* map_name, const char* gametype,
               const char* allies_nation, const char* axis_nation);
void zbot_hear_sound(ZBotManager*, int source, const float* origin, float radius);
/* Plugin-internal copied task, never a host ABI or VM-owned object pointer. */
void zbot_set_objective(ZBotManager*,int client,const ZBotObjectiveTask*);
void zbot_shutdown(ZBotManager* mgr);
int zbot_voice_order(ZBotManager* mgr, int sender, const char* alias);
void zbot_restart_round(ZBotManager* mgr);
void zbot_run_frame(ZBotManager* mgr, int level_time_ms);
void zbot_client_disconnect(ZBotManager* mgr, int client_num);
int  zbot_console_command(ZBotManager* mgr, const char* cmd);
int  zbot_is_bot(const ZBotManager* mgr, int client_num);
int  zbot_count(const ZBotManager* mgr);

/* Exported helpers (also used by unit tests) */
ZBotSkill   zbot_parse_skill(const char* str);
const char* zbot_skill_name(ZBotSkill skill);
int         zbot_add_waypoint(ZBotManager* mgr, const float feet[3], unsigned int flags, int entity_id);
void        zbot_link_waypoints(ZBotManager* mgr);
int         zbot_find_path(ZBotManager* mgr, int start_wp, int goal_wp,
                           int* out_path, int max_path);
int         zbot_add_bot(ZBotManager* mgr, const char* skill_str,
                         const char* team_str, const char* custom_name);
int         zbot_kick_bot(ZBotManager* mgr, const char* target);
const char* zbot_weapon_for(const char* nation, const char* pref_class);
int         zbot_format_roast(const char* templ, const char* name, char* out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* HXFS_COD_BOT_ZBOT_H */
