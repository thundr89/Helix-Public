/*
 * Counter-Strike 1.6 / Condition Zero ZBot-inspired AI Controller for CoD1 Multiplayer
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "zbot.h"
#include "cod_nav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define PI 3.14159265358979323846f
/* Static movement collision: solid | playerclip, excluding transient bodies. */
#define ZBOT_MOVE_MASK 17
#define DEG2RAD(a) ((a) * (PI / 180.0f))
#define RAD2DEG(a) ((a) * (180.0f / PI))

static float zbot_rand_float(ZBotManager* mgr) {
    mgr->rng_state = mgr->rng_state * 1664525u + 1013904223u;
    return (float)(mgr->rng_state & 0x00ffffff) / (float)0x01000000;
}

static int zbot_rand_int(ZBotManager* mgr, int max_val) {
    if (max_val <= 0) return 0;
    mgr->rng_state = mgr->rng_state * 1664525u + 1013904223u;
    return (int)((mgr->rng_state >> 8) % (unsigned int)max_val);
}

static float vec3_dist_sq(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

static float vec3_dist(const float a[3], const float b[3]) {
    return sqrtf(vec3_dist_sq(a, b));
}

static float angle_normalize180(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

ZBotSkill zbot_parse_skill(const char* str) {
    if (!str || !str[0]) return ZBOT_SKILL_MID;
    if (!strcmp(str, "low") || !strcmp(str, "0")) return ZBOT_SKILL_LOW;
    if (!strcmp(str, "mid") || !strcmp(str, "medium") || !strcmp(str, "1")) return ZBOT_SKILL_MID;
    if (!strcmp(str, "high") || !strcmp(str, "pro") || !strcmp(str, "hard") || !strcmp(str, "expert") || !strcmp(str, "2")) return ZBOT_SKILL_PRO;
    if (!strcmp(str, "random") || !strcmp(str, "3")) return ZBOT_SKILL_RANDOM;
    return ZBOT_SKILL_MID;
}


const char* zbot_skill_name(ZBotSkill skill) {
    switch (skill) {
        case ZBOT_SKILL_LOW:    return "low";
        case ZBOT_SKILL_MID:    return "mid";
        case ZBOT_SKILL_PRO:    return "pro";
        case ZBOT_SKILL_RANDOM: return "random";
        default:                return "mid";
    }
}

const char* zbot_weapon_for(const char* nation, const char* pref_class) {
    int is_t = nation && (!strcmp(nation, "terrorist") || !strcmp(nation, "t"));
    int is_ct = nation && (!strcmp(nation, "counterterrorist") || !strcmp(nation, "ct"));

    if (is_t) {
        if (!pref_class || !pref_class[0] || !strcmp(pref_class, "rifle") || !strcmp(pref_class, "assault")) return "ak47";
        if (!strcmp(pref_class, "smg")) return "mp5";
        if (!strcmp(pref_class, "sniper")) return "awp";
        if (!strcmp(pref_class, "shotgun")) return "m3";
        if (!strcmp(pref_class, "pistol")) return "glock";
        return "ak47";
    }
    if (is_ct) {
        if (!pref_class || !pref_class[0] || !strcmp(pref_class, "rifle") || !strcmp(pref_class, "assault")) return "m4a1";
        if (!strcmp(pref_class, "smg")) return "mp5";
        if (!strcmp(pref_class, "sniper")) return "awp";
        if (!strcmp(pref_class, "shotgun")) return "m3";
        if (!strcmp(pref_class, "pistol")) return "usp";
        return "m4a1";
    }

    int is_german = nation && !strcmp(nation, "german");
    int is_russian = nation && !strcmp(nation, "russian");
    int is_british = nation && !strcmp(nation, "british");

    if (!pref_class || !pref_class[0] || !strcmp(pref_class, "rifle")) {
        if (is_german) return "kar98k_mp";
        if (is_russian) return "mosin_nagant_mp";
        if (is_british) return "enfield_mp";
        return "m1garand_mp";
    }
    if (!strcmp(pref_class, "smg")) {
        if (is_german) return "mp40_mp";
        if (is_russian) return "ppsh_mp";
        if (is_british) return "sten_mp";
        return "thompson_mp";
    }
    if (!strcmp(pref_class, "sniper")) {
        if (is_german) return "kar98k_sniper_mp";
        if (is_russian) return "mosin_nagant_sniper_mp";
        if (is_british) return "springfield_mp";
        return "springfield_mp";
    }
    if (!strcmp(pref_class, "support") || !strcmp(pref_class, "mg")) {
        if (is_german) return "mp44_mp";
        if (is_russian) return "ppsh_mp";
        if (is_british) return "bren_mp";
        return "bar_mp";
    }
    return is_german ? "mp40_mp" : "m1garand_mp";
}

int zbot_format_roast(const char* templ, const char* name, char* out, int cap) {
    const char* p;
    char safe_name[32];
    int o = 0;
    if (!templ || !out || cap <= 0) return 0;
    snprintf(safe_name, sizeof(safe_name), "%s", (name && name[0]) ? name : "opponent");

    p = templ;
    while (*p && o < cap - 1) {
        if (p[0] == '%' && p[1] == 's') {
            int nlen = (int)strlen(safe_name);
            if (o + nlen < cap - 1) {
                memcpy(out + o, safe_name, (size_t)nlen);
                o += nlen;
            }
            p += 2;
        } else if (strncmp(p, "{name}", 6) == 0) {
            int nlen = (int)strlen(safe_name);
            if (o + nlen < cap - 1) {
                memcpy(out + o, safe_name, (size_t)nlen);
                o += nlen;
            }
            p += 6;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return o;
}

int zbot_add_waypoint(ZBotManager* mgr, const float feet[3], unsigned int flags, int entity_id) {
    int i;
    if (!mgr || mgr->wp_count >= ZBOT_MAX_WAYPOINTS) return -1;
    /* Avoid duplicates too close together */
    for (i = 0; i < mgr->wp_count; ++i) {
        if (vec3_dist_sq(mgr->waypoints[i].origin, feet) < 48.0f * 48.0f) {
            mgr->waypoints[i].flags |= flags;
            return i;
        }
    }
    i = mgr->wp_count++;
    mgr->waypoints[i].origin[0] = feet[0];
    mgr->waypoints[i].origin[1] = feet[1];
    mgr->waypoints[i].origin[2] = feet[2];
    mgr->waypoints[i].flags = flags;
    mgr->waypoints[i].entity_id = entity_id;
    mgr->waypoints[i].edge_count = 0;
    mgr->waypoints[i].danger = 0.0f;
    return i;
}

void zbot_link_waypoints(ZBotManager* mgr) {
    int i, j;
    if (!mgr || mgr->wp_count <= 1) return;
    for (i = 0; i < mgr->wp_count; ++i) {
        mgr->waypoints[i].edge_count = 0;
        for (j = 0; j < mgr->wp_count; ++j) {
            float d, dz;
            if (i == j) continue;
            d = vec3_dist(mgr->waypoints[i].origin, mgr->waypoints[j].origin);
            dz = fabsf(mgr->waypoints[i].origin[2] - mgr->waypoints[j].origin[2]);
            if (d < 450.0f && (dz < 90.0f || ((mgr->waypoints[i].flags | mgr->waypoints[j].flags) & ZBOT_WP_LADDER))) {
                if (mgr->waypoints[i].edge_count < ZBOT_MAX_EDGES) {
                    int idx = mgr->waypoints[i].edge_count++;
                    mgr->waypoints[i].edges[idx] = j;
                    mgr->waypoints[i].dists[idx] = d;
                }
            }
        }
    }
}

static int find_nearest_waypoint(const ZBotManager* mgr, const float pos[3]) {
    int i, best = -1;
    float best_sq = 1e12f;
    if (!mgr || mgr->wp_count <= 0) return -1;
    for (i = 0; i < mgr->wp_count; ++i) {
        float dsq = vec3_dist_sq(mgr->waypoints[i].origin, pos);
        if (dsq < best_sq) {
            best_sq = dsq;
            best = i;
        }
    }
    return best;
}

static int zbot_walk_sweep(ZBotManager*,const float*,const float*);
static int zbot_route_start(ZBotManager* mgr,const float* origin){
    int candidate[8],i,j,k;float distance[8];
    for(i=0;i<8;i++){candidate[i]=-1;distance[i]=1e30f;}
    for(i=0;i<mgr->wp_count;i++){
        float dsq=vec3_dist_sq(origin,mgr->waypoints[i].origin);
        if(fabsf(origin[2]-mgr->waypoints[i].origin[2])>64)continue;
        for(j=0;j<8;j++)if(dsq<distance[j]){for(k=7;k>j;k--){distance[k]=distance[k-1];candidate[k]=candidate[k-1];}distance[j]=dsq;candidate[j]=i;break;}
    }
    for(j=0;j<8 && candidate[j]>=0 && distance[j]<160*160;j++)
        if(!mgr->imp->trace_query || zbot_walk_sweep(mgr,origin,mgr->waypoints[candidate[j]].origin))return candidate[j];
    return -1;
}
static int zbot_heap_less(const ZBotManager* m,int a,int b){
    return m->search.f[a]<m->search.f[b] || (m->search.f[a]==m->search.f[b] && a<b);
}
static void zbot_heap_up(ZBotManager* m,int index){
    int node=m->search.heap[index];
    while(index>0){int parent=(index-1)/2,other=m->search.heap[parent];
        if(!zbot_heap_less(m,node,other))break;
        m->search.heap[index]=other;m->search.position[other]=index;index=parent;
    }
    m->search.heap[index]=node;m->search.position[node]=index;
}
int zbot_find_path(ZBotManager* mgr,int start_wp,int goal_wp,int* out_path,int max_path){
    int i,count=0,open_count=0;
    if(!mgr || start_wp<0 || start_wp>=mgr->wp_count || goal_wp<0 || goal_wp>=mgr->wp_count || !out_path || max_path<=0)return 0;
    mgr->nav_searches++;
    for(i=0;i<mgr->wp_count;i++){mgr->search.g[i]=1e30f;mgr->search.parent[i]=-1;mgr->search.position[i]=-1;}
    mgr->search.g[start_wp]=0;mgr->search.f[start_wp]=vec3_dist(mgr->waypoints[start_wp].origin,mgr->waypoints[goal_wp].origin);
    mgr->search.heap[open_count++]=start_wp;mgr->search.position[start_wp]=0;
    while(open_count){
        int current=mgr->search.heap[0],last=mgr->search.heap[--open_count],index=0;
        if(open_count){
            while(index*2+1<open_count){int child=index*2+1;
                if(child+1<open_count && zbot_heap_less(mgr,mgr->search.heap[child+1],mgr->search.heap[child]))child++;
                if(!zbot_heap_less(mgr,mgr->search.heap[child],last))break;
                mgr->search.heap[index]=mgr->search.heap[child];mgr->search.position[mgr->search.heap[index]]=index;index=child;
            }
            mgr->search.heap[index]=last;mgr->search.position[last]=index;
        }
        mgr->search.position[current]=-2;mgr->nav_expanded++;
        if(current==goal_wp){
            int node=current,length=0;
            /* Count first, then return a connected prefix if caller capacity is
             * smaller than the route. Never splice a truncated flank into a jump. */
            while(node>=0 && length<mgr->wp_count){length++;node=mgr->search.parent[node];}
            node=current;
            for(i=length-1;i>=0;i--){if(i<max_path)out_path[i]=node;node=mgr->search.parent[node];}
            return length<max_path?length:max_path;
        }
        for(i=0;i<mgr->waypoints[current].edge_count;i++){
            int next=mgr->waypoints[current].edges[i];float cost;
            if(next<0 || next>=mgr->wp_count || mgr->search.position[next]==-2 || mgr->waypoints[current].blocked_until[i]>mgr->now_ms)continue;
            cost=mgr->search.g[current]+mgr->waypoints[current].dists[i]*(1.f+(float)(((unsigned)current*73856093u ^ (unsigned)next*19349663u ^ mgr->route_seed)%101)*.0015f)+mgr->waypoints[next].danger;
            if(mgr->path_team>=0 && mgr->path_team<2)cost+=mgr->heat[mgr->path_team][next]*2.f;
            if(cost>=mgr->search.g[next])continue;
            mgr->search.g[next]=cost;mgr->search.f[next]=cost+vec3_dist(mgr->waypoints[next].origin,mgr->waypoints[goal_wp].origin);mgr->search.parent[next]=current;
            index=mgr->search.position[next];
            if(index<0){index=open_count++;mgr->search.heap[index]=next;mgr->search.position[next]=index;}
            zbot_heap_up(mgr,index);
        }
    }
    return count;
}

static void zbot_load_profiles(ZBotManager* mgr) {
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char line[256];
    const char* p;

    mgr->profile_count = 0;
    if (cod_bot_virtual_read("cod/bot/zbot_profiles.txt", &buf, &sz) && buf && sz > 0) {
        p = (const char*)buf;
        while (*p) {
            int li = 0;
            while (*p && *p != '\n' && *p != '\r' && li < (int)sizeof(line) - 1) {
                line[li++] = *p++;
            }
            line[li] = '\0';
            while (*p == '\n' || *p == '\r') p++;

            if (line[0] == '#' || line[0] == '\0') continue;
            if (strncmp(line, "profile", 7) == 0 && mgr->profile_count < 32) {
                char name[32], skill[16], pclass[16];
                float agg = 0.5f, team = 0.5f;
                int n = sscanf(line, "profile \"%31[^\"]\" %15s %15s %f %f", name, skill, pclass, &agg, &team);
                if (n >= 3) {
                    ZBotProfile* prof = &mgr->profiles[mgr->profile_count++];
                    snprintf(prof->name, sizeof(prof->name), "%s", name);
                    prof->base_skill = zbot_parse_skill(skill);
                    snprintf(prof->pref_class, sizeof(prof->pref_class), "%s", pclass);
                    prof->aggression = agg;
                    prof->teamwork = team;
                }
            }
        }
        free(buf);
    }

    if (mgr->profile_count == 0) {
        static const struct { const char* n; ZBotSkill s; const char* c; } defaults[] = {
            { "Phoenix", ZBOT_SKILL_PRO, "rifle" },
            { "Wolf", ZBOT_SKILL_PRO, "smg" },
            { "Maverick", ZBOT_SKILL_MID, "smg" },
            { "PanzerHans", ZBOT_SKILL_MID, "rifle" },
            { "SgtMiller", ZBOT_SKILL_MID, "support" },
            { "Gunther", ZBOT_SKILL_LOW, "smg" },
            { "PvtRyan", ZBOT_SKILL_LOW, "rifle" },
            { "Ivan", ZBOT_SKILL_LOW, "support" }
        };
        unsigned int i;
        for (i = 0; i < sizeof(defaults)/sizeof(defaults[0]); ++i) {
            ZBotProfile* prof = &mgr->profiles[mgr->profile_count++];
            snprintf(prof->name, sizeof(prof->name), "%s", defaults[i].n);
            prof->base_skill = defaults[i].s;
            snprintf(prof->pref_class, sizeof(prof->pref_class), "%s", defaults[i].c);
            prof->aggression = 0.7f;
            prof->teamwork = 0.8f;
        }
    }
}

static void zbot_load_chatter(ZBotManager* mgr) {
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char line[256];
    const char* p;

    mgr->kill_roast_count = 0;
    mgr->death_roast_count = 0;
    mgr->obj_chat_count = 0;
    memset(mgr->vsay_count, 0, sizeof(mgr->vsay_count));

    if (cod_bot_virtual_read("cod/bot/zbot_chatter.txt", &buf, &sz) && buf && sz > 0) {
        p = (const char*)buf;
        while (*p) {
            int li = 0;
            while (*p && *p != '\n' && *p != '\r' && li < (int)sizeof(line) - 1) {
                line[li++] = *p++;
            }
            line[li] = '\0';
            while (*p == '\n' || *p == '\r') p++;

            if (line[0] == '#' || line[0] == '\0') continue;
            if (strncmp(line, "roast_kill \"", 12) == 0 && mgr->kill_roast_count < ZBOT_MAX_ROASTS) {
                char text[128];
                if (sscanf(line, "roast_kill \"%127[^\"]\"", text) == 1) {
                    snprintf(mgr->kill_roasts[mgr->kill_roast_count++], 128, "%s", text);
                }
            } else if (strncmp(line, "roast_death \"", 13) == 0 && mgr->death_roast_count < ZBOT_MAX_ROASTS) {
                char text[128];
                if (sscanf(line, "roast_death \"%127[^\"]\"", text) == 1) {
                    snprintf(mgr->death_roasts[mgr->death_roast_count++], 128, "%s", text);
                }
            } else if (strncmp(line, "roast_objective \"", 17) == 0 && mgr->obj_chat_count < ZBOT_MAX_ROASTS) {
                char text[128];
                if (sscanf(line, "roast_objective \"%127[^\"]\"", text) == 1) {
                    snprintf(mgr->obj_chats[mgr->obj_chat_count++], 128, "%s", text);
                }
            } else if (strncmp(line, "vsay_", 5) == 0) {
                char kind_str[32], val[32];
                if (sscanf(line, "vsay_%31s %31s", kind_str, val) == 2) {
                    int k = -1;
                    if (!strcmp(kind_str, "spot") || !strcmp(kind_str, "sniper") || !strcmp(kind_str, "grenade")) k = ZBOT_VSAY_SPOT;
                    else if (!strcmp(kind_str, "kill")) k = ZBOT_VSAY_KILL;
                    else if (!strcmp(kind_str, "attack")) k = ZBOT_VSAY_ATTACK;
                    else if (!strcmp(kind_str, "defend")) k = ZBOT_VSAY_DEFEND;
                    else if (!strcmp(kind_str, "help")) k = ZBOT_VSAY_HELP;
                    else if (!strcmp(kind_str, "random")) k = ZBOT_VSAY_RANDOM;
                    if (k >= 0 && mgr->vsay_count[k] < ZBOT_VSAY_PER_KIND) {
                        snprintf(mgr->vsay[k][mgr->vsay_count[k]++], 32, "%s", val);
                    }
                }
            }
        }
        free(buf);
    }

    if (mgr->kill_roast_count == 0) {
        snprintf(mgr->kill_roasts[mgr->kill_roast_count++], 128, "%s", "Sit down %s, nice try though.");
        snprintf(mgr->kill_roasts[mgr->kill_roast_count++], 128, "%s", "Check your corners next time %s.");
        snprintf(mgr->kill_roasts[mgr->kill_roast_count++], 128, "%s", "Did your monitor turn off %s?");
    }
    if (mgr->death_roast_count == 0) {
        snprintf(mgr->death_roasts[mgr->death_roast_count++], 128, "%s", "Lucky shot %s, pure lag.");
        snprintf(mgr->death_roasts[mgr->death_roast_count++], 128, "%s", "Enjoy that one %s, I was reloading.");
    }
}

/* Build centre-position routes from real hull sweeps. BSP brush bounds alone
 * cannot establish walkability: overlapping boxes often straddle solid walls. */
static int zbot_ground(ZBotManager* mgr,const float* from,float* to){
    const float mins[3]={-15,-15,-24},maxs[3]={15,15,14},standing[3]={15,15,32};int attempt;
    for(attempt=0;attempt<2;attempt++){
        float top[3]={to[0],to[1],from[2]+(attempt?36:18)},bottom[3]={to[0],to[1],from[2]-64};hxSGTrace hit;
        if(!mgr->imp->trace_query(mgr->eng,top,bottom,mins,maxs,ZBOT_MOVE_MASK,-1,&hit) || hit.startsolid || hit.fraction>=1 || hit.normal[2]<.7f)continue;
        {float candidate[3];memcpy(candidate,hit.position,sizeof(candidate));candidate[2]+=.125f;
         if(!mgr->imp->trace_query(mgr->eng,candidate,candidate,mins,standing,ZBOT_MOVE_MASK,-1,&hit) || hit.startsolid || hit.allsolid)continue;
         memcpy(to,candidate,sizeof(candidate));return 1;}
    }
    return 0;
}
static int zbot_walk_sweep(ZBotManager* mgr,const float* a,const float* b){
    const float mins[3]={-15,-15,-24},maxs[3]={15,15,14};
    float start[3]={a[0],a[1],a[2]+18},end[3]={b[0],b[1],b[2]+18};hxSGTrace hit;
    return mgr->imp->trace_query(mgr->eng,start,end,mins,maxs,ZBOT_MOVE_MASK,-1,&hit) && !hit.startsolid && hit.fraction>=.999f;
}
static int zbot_walk_link(ZBotManager* mgr,const float* a,const float* b){
    float rise=b[2]-a[2];
    if(rise<=18.5f && rise>=-64)return zbot_walk_sweep(mgr,a,b);
    if(rise>36 || rise< -64)return 0;
    /* Several ordinary stair risers can exceed one PMove step over a whole
     * grid cell. Validate short steps rather than rejecting the entire flight. */
    {float previous[3];int j,parts=(int)ceilf(hypotf(b[0]-a[0],b[1]-a[1])/16.f);
     memcpy(previous,a,sizeof(previous));if(parts<2)parts=2;
     for(j=1;j<=parts;j++){
        float next[3]={a[0]+(b[0]-a[0])*j/parts,a[1]+(b[1]-a[1])*j/parts,previous[2]};
        if(!zbot_ground(mgr,previous,next) || next[2]-previous[2]>18.5f || next[2]-previous[2]< -64 || !zbot_walk_sweep(mgr,previous,next))return 0;
        memcpy(previous,next,sizeof(previous));
     }
     return fabsf(previous[2]-b[2])<2;
    }
}
typedef struct ZBotGridIndex {int head[65536],next[ZBOT_MAX_WAYPOINTS];} ZBotGridIndex;
static int zbot_grid_node(ZBotManager* mgr,ZBotGridIndex* index,const float* p){
    unsigned hash=((unsigned)(int)lroundf(p[0])*73856093u ^ (unsigned)(int)lroundf(p[1])*19349663u)&65535u;
    int i;
    for(i=index->head[hash]-1;i>=0;i=index->next[i]-1)
        if(fabsf(p[0]-mgr->waypoints[i].origin[0])<1 && fabsf(p[1]-mgr->waypoints[i].origin[1])<1 && fabsf(p[2]-mgr->waypoints[i].origin[2])<18)return i;
    if(mgr->wp_count>=ZBOT_MAX_WAYPOINTS)return -1;
    i=mgr->wp_count++;memset(&mgr->waypoints[i],0,sizeof(mgr->waypoints[i]));memcpy(mgr->waypoints[i].origin,p,3*sizeof(float));mgr->waypoints[i].entity_id=-1;
    index->next[i]=index->head[hash];index->head[hash]=i+1;return i;
}
static void zbot_build_walk_grid(ZBotManager* mgr){
    int ids[256],n,i,j,overflow=0;float step=20;ZBotGridIndex* index;
    if(!mgr->imp->trace_query || !mgr->imp->entity_find || !mgr->imp->entity_key)return;
    n=mgr->imp->entity_find(mgr->eng,"spawn_anchor","1",ids,256);if(!n)return;
    index=(ZBotGridIndex*)calloc(1,sizeof(*index));if(!index)return;
    do{
        overflow=0;mgr->wp_count=0;memset(index,0,sizeof(*index));
        for(i=0;i<n && i<256;i++){
            float p[3],q[3];const char* value=mgr->imp->entity_key(mgr->eng,ids[i],"origin");
            if(!value || sscanf(value,"%f %f %f",p,p+1,p+2)!=3)continue;
            q[0]=roundf(p[0]/step)*step;q[1]=roundf(p[1]/step)*step;q[2]=p[2];
            if(zbot_ground(mgr,p,q) && zbot_walk_link(mgr,p,q))zbot_grid_node(mgr,index,q);
        }
        for(i=0;i<mgr->wp_count && !overflow;i++){
            static const int dx[]={1,0,-1,0,1,-1,-1,1},dy[]={0,1,0,-1,1,1,-1,-1};ZBotWaypoint* w=&mgr->waypoints[i];
            for(j=0;j<8;j++){
                float q[3]={w->origin[0]+dx[j]*step,w->origin[1]+dy[j]*step,w->origin[2]};int next;
                if(!zbot_ground(mgr,w->origin,q) || !zbot_walk_link(mgr,w->origin,q))continue;
                next=zbot_grid_node(mgr,index,q);if(next<0){overflow=1;break;}
                w->edges[w->edge_count]=next;w->dists[w->edge_count++]=vec3_dist(w->origin,q);
            }
        }
        /* Do not publish a half-expanded graph. Large maps retry at coarser
         * spacing rather than leaving arbitrary dead ends at the node limit. */
        if(overflow)step*=2;
    }while(overflow && step<=320);
    free(index);
    if(overflow)mgr->wp_count=0;
    if(mgr->imp->print){char msg[128];snprintf(msg,sizeof(msg),"Bot collision navigation: %d walkable nodes spacing=%.0f complete=%d\n",mgr->wp_count,step,!overflow);mgr->imp->print(mgr->eng,msg);}
}

static void zbot_seed_map_waypoints(ZBotManager* mgr) {
    int i,j,n=0;
    /* Preserve the map's directed reachability instead of inventing paths
       through walls from proximity alone. Area ids map directly to waypoint ids. */
    if(mgr->imp->nav_area){
        n=mgr->imp->nav_area(mgr->eng,0,NULL,NULL,0);
        if(n>ZBOT_MAX_WAYPOINTS)n=ZBOT_MAX_WAYPOINTS;
        for(i=0;i<n;i++){
            ZBotWaypoint* w=&mgr->waypoints[i];int edges[ZBOT_MAX_EDGES],count;
            count=mgr->imp->nav_area(mgr->eng,i,w->origin,edges,ZBOT_MAX_EDGES);
            if(count<0)break;
            w->entity_id=-1;w->flags=ZBOT_WP_NORMAL;w->edge_count=0;
            for(j=0;j<count && j<ZBOT_MAX_EDGES;j++)if(edges[j]>=0 && edges[j]<n)
                w->edges[w->edge_count++]=edges[j];
        }
        mgr->wp_count=i;
        for(i=0;i<mgr->wp_count;i++){
            ZBotWaypoint* w=&mgr->waypoints[i];
            for(j=0;j<w->edge_count;j++)w->dists[j]=vec3_dist(w->origin,mgr->waypoints[w->edges[j]].origin);
        }
    }
    zbot_build_walk_grid(mgr);
    if(mgr->imp->entity_find && mgr->imp->entity_key){
        static const char* classes[]={
          "mp_dm_spawn","mp_tdm_spawn_allies","mp_tdm_spawn_axis",
          "mp_sd_spawn_attacker","mp_sd_spawn_defender","bombzone_A","bombzone_B","mp_hq_radio","mp_re_objective",
          "info_player_deathmatch","info_player_start",
          "info_bomb_target","func_bomb_target",
          "monster_hostage","hostage_entity","func_hostage_rescue","info_hostage_rescue",
          "func_vip_safetyzone","info_vip_safetyzone"
        };
        for(i=0;i<(int)(sizeof(classes)/sizeof(classes[0]));i++){
            int ids[256],count=mgr->imp->entity_find(mgr->eng,(i==5 || i==6)?"targetname":"classname",classes[i],ids,256);
            for(j=0;j<count && j<256;j++){
                float pos[3];const char* value=mgr->imp->entity_key(mgr->eng,ids[j],"origin");int wp;
                if(!value || sscanf(value,"%f %f %f",pos,pos+1,pos+2)!=3)continue;
                wp=find_nearest_waypoint(mgr,pos);
                if ((i==5 || i==6) && mgr->imp->trace_query) {
                    const char* model=mgr->imp->entity_key(mgr->eng,ids[j],"model");
                    int model_id=model && model[0]=='*'?atoi(model+1):0;
                    if(model_id>0){
                        int candidate;float best=1e30f;
                        float mins[3]={-15,-15,-24},maxs[3]={15,15,32};
                        for(candidate=0;candidate<mgr->wp_count;candidate++){
                            hxSGTrace hit;float d=vec3_dist_sq(pos,mgr->waypoints[candidate].origin);
                            if(d>=best)continue;
                            if(mgr->imp->trace_query(mgr->eng,mgr->waypoints[candidate].origin,mgr->waypoints[candidate].origin,
                               mins,maxs,5,-1024-model_id,&hit) && (hit.startsolid || hit.allsolid || hit.fraction<1)){
                                best=d;wp=candidate;
                            }
                        }
                    }
                }
                if(wp>=0){
                    unsigned flags = ZBOT_WP_NORMAL;
                    if (i == 2 || i == 9) flags = ZBOT_WP_SPAWN_AX; /* Axis / Terrorist */
                    else if (i == 1 || i == 10) flags = ZBOT_WP_SPAWN_ALL; /* Allies / Counter-Terrorist */
                    else if (i >= 5) flags = ZBOT_WP_OBJECTIVE; /* Bomb sites, Hostages, Rescue zones, VIP */
                    else flags = ZBOT_WP_SPAWN_ALL;
                    mgr->waypoints[wp].flags|=flags;
                    mgr->waypoints[wp].entity_id=ids[j];
                }
            }
        }
    }
}

void zbot_init(ZBotManager* mgr, hxSGEngine* eng, hxSGImports* imp,
               void* host_game, ZBotClientCmdFn client_cmd_cb,
               const char* map_name, const char* gametype,
               const char* allies_nation, const char* axis_nation) {
    const char* cv;
    if (!mgr) return;
    memset(mgr, 0, sizeof(*mgr));
    if(!imp || imp->api_version<13)return;
    mgr->initialized = 1;
    mgr->eng = eng;
    mgr->imp = imp;
    if (imp && imp->cvar_set && (!imp->cvar_string || !imp->cvar_string(eng, "bot_parallel") || !imp->cvar_string(eng, "bot_parallel")[0])) {
        imp->cvar_set(eng, "bot_parallel", "0");
    }
    mgr->host_game = host_game;
    mgr->client_cmd_cb = client_cmd_cb;
    mgr->rng_state = 0x1337c0de;

    snprintf(mgr->map_name, sizeof(mgr->map_name), "%s", map_name ? map_name : "");
    snprintf(mgr->gametype, sizeof(mgr->gametype), "%s", gametype ? gametype : "dm");
    snprintf(mgr->allies_nation, sizeof(mgr->allies_nation), "%s", allies_nation ? allies_nation : "american");
    snprintf(mgr->axis_nation, sizeof(mgr->axis_nation), "%s", axis_nation ? axis_nation : "german");

    mgr->configured_skill = ZBOT_SKILL_RANDOM;
    mgr->quota = 0;
    mgr->chat_enabled = 1;
    mgr->roast_enabled = 1;
    mgr->vsay_enabled = 1;

    if (imp && imp->cvar_string) {
        cv = imp->cvar_string(eng, "bot_skill");
        if (cv && cv[0]) mgr->configured_skill = zbot_parse_skill(cv);
        cv = imp->cvar_string(eng, "bot_quota");
        if (cv && cv[0]) mgr->quota = atoi(cv);
        cv = imp->cvar_string(eng, "bot_chat");
        if (cv && cv[0]) mgr->chat_enabled = atoi(cv) != 0;
        cv = imp->cvar_string(eng, "bot_roast");
        if (cv && cv[0]) mgr->roast_enabled = atoi(cv) != 0;
        cv = imp->cvar_string(eng, "bot_vsay");
        if (cv && cv[0]) mgr->vsay_enabled = atoi(cv) != 0;
    }

    zbot_load_profiles(mgr);
    zbot_load_chatter(mgr);
    mgr->attack_site=mgr->previous_site=mgr->planter=mgr->defuser=-1;
    mgr->route_seed=mgr->rng_state;
    zbot_seed_map_waypoints(mgr);
    for(int i=0;i<mgr->wp_count && mgr->site_count<8;i++)if(mgr->waypoints[i].flags&ZBOT_WP_OBJECTIVE)mgr->sites[mgr->site_count++]=i;
}

void zbot_restart_round(ZBotManager* mgr) {
    int i;for(i=0;i<mgr->wp_count;i++){mgr->heat[0][i]*=.8f;mgr->heat[1][i]*=.8f;}
    mgr->now_ms=mgr->last_frame_ms=mgr->next_quota_check_ms=0;mgr->bomb_planted=0;
    mgr->previous_site=mgr->attack_site;mgr->attack_site=mgr->planter=mgr->defuser=-1;
    mgr->next_plan_ms=0;mgr->round_number++;mgr->route_seed=(unsigned int)zbot_rand_int(mgr,2147483647);mgr->plan_revision++;
    memset(mgr->order_cooldown,0,sizeof(mgr->order_cooldown));
    for(i=0;i<ZBOT_MAX_CLIENTS;i++)if(mgr->bots[i].active){
        ZBotAgent old=mgr->bots[i],*b=&mgr->bots[i];memset(b,0,sizeof(*b));
        b->active=1;b->client_num=old.client_num;b->profile_idx=old.profile_idx;b->skill=old.skill;
        b->desired_team=old.desired_team;b->joined=1;b->current_wp=b->goal_wp=b->interact_wp=b->enemy_client=b->assigned_goal=b->flank_wp=-1;
        b->last_known_frags=old.last_known_frags;b->last_known_deaths=old.last_known_deaths;
        strcpy(b->name,old.name);strcpy(b->desired_weapon,old.desired_weapon);
    }
}

void zbot_shutdown(ZBotManager* mgr) {
    int i;
    if (!mgr || !mgr->initialized) return;
    for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
        if (mgr->bots[i].active) {
            zbot_client_disconnect(mgr, i);
        }
    }
    mgr->initialized = 0;
}

int zbot_is_bot(const ZBotManager* mgr, int client_num) {
    if (!mgr || client_num < 0 || client_num >= ZBOT_MAX_CLIENTS) return 0;
    return mgr->bots[client_num].active;
}

int zbot_count(const ZBotManager* mgr) {
    int i, c = 0;
    if (!mgr) return 0;
    for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
        if (mgr->bots[i].active) c++;
    }
    return c;
}

void zbot_client_disconnect(ZBotManager* mgr, int client_num) {
    if (!mgr || client_num < 0 || client_num >= ZBOT_MAX_CLIENTS) return;
    if (mgr->bots[client_num].active) {
        /* Clear first: the host synchronously calls client_disconnect back. */
        memset(&mgr->bots[client_num], 0, sizeof(mgr->bots[client_num]));
        if (mgr->imp && mgr->imp->bot_disconnect) {
            mgr->imp->bot_disconnect(mgr->eng, client_num);
        }
    }
}

int zbot_add_bot(ZBotManager* mgr, const char* skill_str, const char* team_str, const char* custom_name) {
    char name[32];
    int client_slot = -1;
    int prof_idx = 0;
    ZBotSkill skill;
    int desired_team = -1;
    ZBotAgent* b;

    if (!mgr || !mgr->initialized || !mgr->imp || !mgr->imp->bot_connect) return -1;

    prof_idx = zbot_rand_int(mgr, mgr->profile_count);
    if (custom_name && custom_name[0]) {
        snprintf(name, sizeof(name), "%s", custom_name);
    } else {
        snprintf(name, sizeof(name), "%s", mgr->profiles[prof_idx].name);
    }

    client_slot = mgr->imp->bot_connect(mgr->eng, name);
    if (client_slot < 0 || client_slot >= ZBOT_MAX_CLIENTS) return -1;

    b = &mgr->bots[client_slot];
    memset(b, 0, sizeof(*b));
    b->active = 1;
    b->client_num = client_slot;b->assigned_goal=b->flank_wp=-1;
    b->profile_idx = prof_idx;
    snprintf(b->name, sizeof(b->name), "%s", name);

    if (skill_str && skill_str[0]) {
        skill = zbot_parse_skill(skill_str);
    } else if (mgr->configured_skill != ZBOT_SKILL_RANDOM) {
        skill = mgr->configured_skill;
    } else {
        skill = mgr->profiles[prof_idx].base_skill;
    }
    if (skill == ZBOT_SKILL_RANDOM) {
        skill = (ZBotSkill)zbot_rand_int(mgr, 3);
    }
    b->skill = skill;

    if (team_str && team_str[0]) {
        if (!strcmp(team_str, "allies") || !strcmp(team_str, "0") ||
            !strcmp(team_str, "ct") || !strcmp(team_str, "counterterrorist") || !strcmp(team_str, "2")) desired_team = 0;
        else if (!strcmp(team_str, "axis") || !strcmp(team_str, "1") ||
                 !strcmp(team_str, "t") || !strcmp(team_str, "terrorist")) desired_team = 1;
    }
    b->desired_team = desired_team;

    {
        const char* nation = (desired_team == 1) ? mgr->axis_nation : mgr->allies_nation;
        const char* wpn = zbot_weapon_for(nation, mgr->profiles[prof_idx].pref_class);
        snprintf(b->desired_weapon, sizeof(b->desired_weapon), "%s", wpn);
    }

    b->next_join_ms = mgr->now_ms + 100 + zbot_rand_int(mgr, 200);
    b->current_wp = -1;
    b->goal_wp = -1;
    b->interact_wp = -1;
    b->next_random_vsay_ms = mgr->now_ms + 10000 + zbot_rand_int(mgr, 15000);
    return client_slot;
}

int zbot_kick_bot(ZBotManager* mgr, const char* target) {
    int i;
    if (!mgr || !mgr->initialized) return 0;
    if (!target || !target[0] || !strcmp(target, "all")) {
        int kicked = 0;
        for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
            if (mgr->bots[i].active) {
                zbot_client_disconnect(mgr, i);
                kicked++;
            }
        }
        return kicked;
    }

    for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
        if (mgr->bots[i].active && (!strcmp(mgr->bots[i].name, target) || atoi(target) == i)) {
            zbot_client_disconnect(mgr, i);
            return 1;
        }
    }
    return 0;
}

static void zbot_send_chat(ZBotManager* mgr, ZBotAgent* bot, int is_team, const char* text) {
    char cmd[256];
    if (!mgr || !bot || !mgr->imp || !mgr->imp->client_message || !text || !text[0]) return;
    if (!mgr->chat_enabled) return;
    if (mgr->now_ms < bot->next_chat_ms) return;
    snprintf(cmd, sizeof(cmd), "%s %s", is_team ? "say_team" : "say", text);
    mgr->imp->client_message(mgr->eng, bot->client_num, cmd);
    bot->next_chat_ms = mgr->now_ms + 2500 + zbot_rand_int(mgr, 2500);
}

static void zbot_send_vsay(ZBotManager* mgr, ZBotAgent* bot, ZBotVsayKind kind) {
    char cmd[64];
    int idx;
    if (!mgr || !bot || !mgr->vsay_enabled || kind < 0 || kind >= ZBOT_VSAY_KINDS) return;
    if (mgr->now_ms < bot->next_vsay_ms || mgr->vsay_count[kind] <= 0) return;
    idx = zbot_rand_int(mgr, mgr->vsay_count[kind]);
    snprintf(cmd, sizeof(cmd), "vsay %s", mgr->vsay[kind][idx]);
    if (mgr->imp && mgr->imp->client_message) {
        mgr->imp->client_message(mgr->eng, bot->client_num, cmd);
    }
    bot->next_vsay_ms = mgr->now_ms + 4000 + zbot_rand_int(mgr, 3000);
}


static void zbot_learn(ZBotManager*,int,const float*,float);
static void zbot_check_events(ZBotManager* mgr, ZBotAgent* bot, const hxSGBotState* st) {
    /* Kill detection */
    if (st->frags > bot->last_known_frags) {
        bot->last_known_frags = st->frags;
        if (mgr->roast_enabled && mgr->kill_roast_count > 0 && zbot_rand_float(mgr) < 0.65f) {
            char roasted[128];
            int r_idx = zbot_rand_int(mgr, mgr->kill_roast_count);
            char victim_name[32];
            snprintf(victim_name, sizeof(victim_name), "Player%d", st->last_victim >= 0 ? st->last_victim : 0);
            if (st->last_victim >= 0 && st->last_victim < ZBOT_MAX_CLIENTS && mgr->bots[st->last_victim].active) {
                snprintf(victim_name, sizeof(victim_name), "%s", mgr->bots[st->last_victim].name);
            }
            zbot_format_roast(mgr->kill_roasts[r_idx], victim_name, roasted, sizeof(roasted));
            zbot_send_chat(mgr, bot, 0, roasted);
        } else {
            zbot_send_vsay(mgr, bot, ZBOT_VSAY_KILL);
        }
    }

    /* Death detection */
    if (st->deaths > bot->last_known_deaths) {
        bot->last_known_deaths = st->deaths;
        zbot_learn(mgr,st->team,st->origin,30);
        if (mgr->roast_enabled && mgr->death_roast_count > 0 && zbot_rand_float(mgr) < 0.45f) {
            char roasted[128];
            int r_idx = zbot_rand_int(mgr, mgr->death_roast_count);
            char killer_name[32];
            snprintf(killer_name, sizeof(killer_name), "Player%d", st->last_attacker >= 0 ? st->last_attacker : 0);
            if (st->last_attacker >= 0 && st->last_attacker < ZBOT_MAX_CLIENTS && mgr->bots[st->last_attacker].active) {
                snprintf(killer_name, sizeof(killer_name), "%s", mgr->bots[st->last_attacker].name);
            }
            zbot_format_roast(mgr->death_roasts[r_idx], killer_name, roasted, sizeof(roasted));
            zbot_send_chat(mgr, bot, 0, roasted);
        }
    }
}

/* Decisions use team sightings, never hidden opponents' server positions. */
static float zbot_site_pressure(const ZBotManager* mgr,int team,int wp) {
    float score=0;int i;
    for(i=0;i<ZBOT_MAX_CLIENTS;i++){
        const ZBotAgent* b=&mgr->bots[i];hxSGBotState st;
        if(!b->active || b->memory_until_ms<mgr->now_ms || !mgr->imp->bot_state(mgr->eng,i,&st) || st.health<=0 || st.team!=team)continue;
        if(vec3_dist(b->enemy_last_pos,mgr->waypoints[wp].origin)<1300)score+=1;
    }
    if(team>=0 && team<2){float learned=0;
        for(i=0;i<mgr->wp_count;i++)if(vec3_dist_sq(mgr->waypoints[i].origin,mgr->waypoints[wp].origin)<1000000 && mgr->heat[team][i]>learned)learned=mgr->heat[team][i];
        score+=learned/40.f;
    }
    return score;
}
static int zbot_cover_point(ZBotManager* mgr,int anchor,int client,int revision) {
    int i,best=anchor;float score=-1e9f;
    if(anchor<0 || anchor>=mgr->wp_count)return anchor;
    for(i=0;i<mgr->wp_count;i++){
        float d=vec3_dist(mgr->waypoints[i].origin,mgr->waypoints[anchor].origin),candidate;
        if(d<160 || d>480 || fabsf(mgr->waypoints[i].origin[2]-mgr->waypoints[anchor].origin[2])>64)continue;
        candidate=(float)(((unsigned int)i*2654435761u ^ (unsigned int)(client+1)*19349663u ^ (unsigned int)revision*83492791u)%1000)-mgr->waypoints[i].danger;
        if(candidate<=score)continue;
        if(mgr->imp->trace_query){float a[3],b[3],zero[3]={0,0,0};hxSGTrace hit;
            memcpy(a,mgr->waypoints[i].origin,sizeof(a));memcpy(b,mgr->waypoints[anchor].origin,sizeof(b));a[2]+=24;b[2]+=24;
            if(!mgr->imp->trace_query(mgr->eng,a,b,zero,zero,1,-1,&hit) || hit.startsolid || hit.fraction<.99f)continue;
        }
        candidate=(float)(((unsigned int)i*2654435761u ^ (unsigned int)(client+1)*19349663u ^ (unsigned int)revision*83492791u)%1000)-mgr->waypoints[i].danger;
        if(candidate>score){score=candidate;best=i;}
    }
    return best;
}
static void zbot_plan_sd(ZBotManager* mgr) {
    int i,old_site=mgr->attack_site,old_planter=mgr->planter,old_defuser=mgr->defuser;
    float best=1e12f;int planter=-1,defuser=-1;
    if((strcmp(mgr->gametype,"sd") && strcmp(mgr->gametype,"de")) || !mgr->site_count || mgr->now_ms<mgr->next_plan_ms)return;
    mgr->next_plan_ms=mgr->now_ms+2500;
    if(mgr->attack_site<0){
        int pick=zbot_rand_int(mgr,mgr->site_count);
        if(mgr->site_count>1 && mgr->sites[pick]==mgr->previous_site)pick=(pick+1)%mgr->site_count;
        if(mgr->round_number>=2){float pressure=zbot_site_pressure(mgr,mgr->attacking_team,mgr->sites[pick]);
            for(i=0;i<mgr->site_count;i++){float candidate=zbot_site_pressure(mgr,mgr->attacking_team,mgr->sites[i]);if(candidate+.5f<pressure){pick=i;pressure=candidate;}}
        }
        mgr->attack_site=mgr->sites[pick];
    }else if(!mgr->bomb_planted && zbot_site_pressure(mgr,mgr->attacking_team,mgr->attack_site)>=1){
        float pressure=zbot_site_pressure(mgr,mgr->attacking_team,mgr->attack_site);
        for(i=0;i<mgr->site_count;i++)if(zbot_site_pressure(mgr,mgr->attacking_team,mgr->sites[i])+.75f<pressure){mgr->attack_site=mgr->sites[i];break;}
    }
    for(i=0;i<ZBOT_MAX_CLIENTS;i++){
        hxSGBotState st;float d;
        if(!mgr->bots[i].active || !mgr->imp->bot_state(mgr->eng,i,&st) || st.health<=0 || st.team<0)continue;
        if(st.team==mgr->attacking_team){if(planter<0 || i==old_planter)planter=i;}
        else if(mgr->bomb_planted){d=vec3_dist(st.origin,mgr->planted_pos);if(st.clip_ammo<=0 && st.reserve_ammo<=0)d+=300;if(i==old_defuser)d-=250;if(d<best){best=d;defuser=i;}}
    }
    mgr->planter=planter;mgr->defuser=defuser;
    if(old_site!=mgr->attack_site || old_planter!=planter || old_defuser!=defuser)mgr->plan_revision++;
}
/* Only tactical radio aliases create temporary, team-local orders. Urgent bomb
 * work takes priority; a new follow position is sampled without resetting routes
 * on every frame. */
int zbot_voice_order(ZBotManager* mgr,int sender,const char* alias) {
    hxSGBotState source;int kind=0,i;
    if(!mgr || !mgr->initialized || !alias || sender<0 || sender>=ZBOT_MAX_CLIENTS || !mgr->imp->bot_state || !mgr->imp->bot_state(mgr->eng,sender,&source) || source.health<=0 || source.team<0)return 0;
    if(!strcmp(mgr->gametype,"dm"))return 0;
    if(!strcmp(alias,"follow_me"))kind=1;
    else if(!strcmp(alias,"move_in"))kind=7;
    else if(!strcmp(alias,"hold_position") || !strcmp(alias,"defend"))kind=2;
    else if(!strcmp(alias,"fall_back") || !strcmp(alias,"regroup"))kind=3;
    else if(!strcmp(alias,"need_reinforcements") || !strcmp(alias,"cover_me"))kind=4;
    else if(!strcmp(alias,"attack_left_flank"))kind=5;
    else if(!strcmp(alias,"attack_right_flank"))kind=6;
    if(!kind || mgr->now_ms<mgr->order_cooldown[sender])return 0;
    mgr->order_cooldown[sender]=mgr->now_ms+1500;
    for(i=0;i<ZBOT_MAX_CLIENTS;i++){
        ZBotAgent* b=&mgr->bots[i];hxSGBotState st;
        if(i==sender || !b->active || !mgr->imp->bot_state(mgr->eng,i,&st) || st.health<=0 || st.team!=source.team)continue;
        if(mgr->bomb_planted && source.team!=mgr->attacking_team && i==mgr->defuser)continue;
        b->order_kind=kind;b->order_sender=sender;b->order_until_ms=mgr->now_ms+12000;
        b->has_target_pos=1;memcpy(b->target_pos,source.origin,sizeof(b->target_pos));b->watch_yaw=source.yaw;
        if(kind==7 || kind==5 || kind==6){
            if(mgr->bomb_planted)memcpy(b->target_pos,mgr->planted_pos,sizeof(b->target_pos));
            else if(source.team==mgr->attacking_team && mgr->attack_site>=0)memcpy(b->target_pos,mgr->waypoints[mgr->attack_site].origin,sizeof(b->target_pos));
            else {b->target_pos[0]+=cosf(DEG2RAD(source.yaw))*600;b->target_pos[1]+=sinf(DEG2RAD(source.yaw))*600;}
        }
        b->path_len=b->path_idx=0;b->next_goal_ms=b->next_path_ms=0;
    }
    return 1;
}
void zbot_set_objective(ZBotManager* mgr,int client,const ZBotObjectiveTask* task){
    ZBotAgent* bot;ZBotObjectiveTask empty={0};int changed;
    if(!mgr || client<0 || client>=ZBOT_MAX_CLIENTS)return;
    if(!task)task=&empty;
    if(task->kind<0 || task->kind>ZBOT_OBJECTIVE_RE_DELIVER || !isfinite(task->radius) || task->radius<0)return;
    if(!isfinite(task->origin[0]) || !isfinite(task->origin[1]) || !isfinite(task->origin[2]))return;
    bot=&mgr->bots[client];
    changed=bot->objective.kind!=task->kind || bot->objective.entity!=task->entity || bot->objective.model!=task->model ||
        vec3_dist_sq(bot->objective.origin,task->origin)>16.f*16.f;
    bot->objective=*task;
    if(changed){bot->objective_wp=-1;bot->path_len=bot->path_idx=0;bot->assigned_goal=-1;bot->next_goal_ms=bot->next_path_ms=0;bot->unstuck_until_ms=0;}
}
static int zbot_objective_anchor(ZBotManager* mgr,ZBotAgent* bot){
    const ZBotObjectiveTask* task=&bot->objective;int i,wp=-1;float best=1e30f;
    if(bot->objective_wp>=0)return bot->objective_wp;
    for(i=0;i<ZBOT_MAX_CLIENTS;++i){const ZBotAgent* other=&mgr->bots[i];
        if(other!=bot && other->active && other->objective.kind && other->objective_wp>=0 && other->objective.entity==task->entity &&
           other->objective.model==task->model && vec3_dist_sq(other->objective.origin,task->origin)<16.f*16.f)
            return bot->objective_wp=other->objective_wp;
    }
    for(i=0;i<mgr->wp_count;++i){
        const float* point=mgr->waypoints[i].origin;float distance=vec3_dist_sq(point,task->origin);
        if(distance>=best)continue;
        if(task->model>0 && mgr->imp->trace_query){
            const float mins[3]={-15,-15,-24},maxs[3]={15,15,32};hxSGTrace hit;
            if(!mgr->imp->trace_query(mgr->eng,point,point,mins,maxs,5,-1024-task->model,&hit) ||
               (!hit.startsolid && !hit.allsolid && hit.fraction>=1))continue;
        }else if(fabsf(point[2]-task->origin[2])>48)continue;
        wp=i;best=distance;
    }
    /* Tiny use triggers can fall between grid samples. Attach a grounded,
     * collision-checked endpoint rather than steering through nearby walls. */
    if(mgr->imp->trace_query && mgr->wp_count<ZBOT_MAX_WAYPOINTS){
        float ground[3];memcpy(ground,task->origin,sizeof(ground));
        if(zbot_ground(mgr,task->origin,ground)){
            hxSGTrace hit;const float mins[3]={-15,-15,-24},maxs[3]={15,15,32};
            int valid=task->model<=0 || (mgr->imp->trace_query(mgr->eng,ground,ground,mins,maxs,5,-1024-task->model,&hit) && (hit.startsolid || hit.allsolid || hit.fraction<1));
            if(valid){int node=mgr->wp_count,links=0;ZBotWaypoint* endpoint=&mgr->waypoints[node];memset(endpoint,0,sizeof(*endpoint));memcpy(endpoint->origin,ground,sizeof(ground));endpoint->entity_id=task->entity;
                for(i=0;i<node && links<ZBOT_MAX_EDGES;++i){ZBotWaypoint* neighbor=&mgr->waypoints[i];float d=vec3_dist(ground,neighbor->origin);
                    if(d>65 || neighbor->edge_count>=ZBOT_MAX_EDGES || !zbot_walk_link(mgr,neighbor->origin,ground) || !zbot_walk_link(mgr,ground,neighbor->origin))continue;
                    endpoint->edges[links]=i;endpoint->dists[links++]=d;neighbor->edges[neighbor->edge_count]=node;neighbor->dists[neighbor->edge_count++]=d;
                }
                if(links){endpoint->edge_count=links;++mgr->wp_count;wp=node;}
            }
        }
    }
    bot->objective_wp=wp;return wp;
}
static void zbot_decide_role_and_goal(ZBotManager* mgr, ZBotAgent* bot, const hxSGBotState* st) {
    int i;
    ZBotRole previous_role=bot->role;
    int is_sd = !strcmp(mgr->gametype, "sd");
    int is_hq = !strcmp(mgr->gametype, "hq");
    int is_re = !strcmp(mgr->gametype, "re");


    if(is_sd){
        if(bot->objective.kind){
            if(st->team==mgr->attacking_team)bot->role=mgr->bomb_planted?ZBOT_ROLE_DEFEND_OBJ:(bot->objective.kind==ZBOT_OBJECTIVE_HQ?ZBOT_ROLE_PLANT:ZBOT_ROLE_ESCORT);
            else bot->role=mgr->bomb_planted?ZBOT_ROLE_DEFUSE:ZBOT_ROLE_DEFEND_OBJ;
        }else if(st->team==mgr->attacking_team)bot->role=mgr->bomb_planted?ZBOT_ROLE_DEFEND_OBJ:(bot->client_num==mgr->planter?ZBOT_ROLE_PLANT:ZBOT_ROLE_ESCORT);
        else bot->role=mgr->bomb_planted?(bot->client_num==mgr->defuser?ZBOT_ROLE_DEFUSE:ZBOT_ROLE_DEFEND_OBJ):ZBOT_ROLE_DEFEND_OBJ;
    }else if(bot->objective.kind)bot->role=bot->objective.kind==ZBOT_OBJECTIVE_GUARD?ZBOT_ROLE_DEFEND_OBJ:ZBOT_ROLE_ATTACK_OBJ;
    else bot->role=ZBOT_ROLE_ROAM;
    if(bot->plan_revision!=mgr->plan_revision){bot->path_len=0;bot->assigned_goal=bot->flank_wp=-1;bot->plan_revision=mgr->plan_revision;}
    if(bot->order_kind && (mgr->now_ms>=bot->order_until_ms || bot->role==ZBOT_ROLE_DEFUSE)){
        bot->order_kind=0;bot->has_target_pos=0;bot->path_len=0;
    }
    if(bot->order_kind==1 && mgr->now_ms>=bot->next_goal_ms){hxSGBotState leader;
        if(mgr->imp->bot_state(mgr->eng,bot->order_sender,&leader) && leader.health>0 && leader.team==st->team){
            if(vec3_dist(leader.origin,bot->target_pos)>160){memcpy(bot->target_pos,leader.origin,sizeof(bot->target_pos));bot->path_len=0;}
        }else{bot->order_kind=0;bot->has_target_pos=0;bot->path_len=0;}
        bot->next_goal_ms=mgr->now_ms+1500;
    }
    if(previous_role==bot->role && bot->path_idx<bot->path_len)return;
    if(previous_role==bot->role && (bot->role==ZBOT_ROLE_DEFEND_OBJ || bot->role==ZBOT_ROLE_ESCORT) && bot->path_len && bot->path[bot->path_len-1]==bot->goal_wp && mgr->now_ms<bot->next_goal_ms)return;

    if(mgr->now_ms<bot->unstuck_until_ms || mgr->now_ms<bot->next_path_ms || mgr->nav_plans_this_frame>=2)return;
    mgr->nav_plans_this_frame++;bot->next_path_ms=mgr->now_ms+250;
    /* Pick goal waypoint based on role */
    bot->current_wp = zbot_route_start(mgr, st->origin);
    if(bot->current_wp<0){bot->path_len=bot->path_idx=0;bot->unstuck_until_ms=mgr->now_ms+400;bot->unstuck_dir=(int)bot->look_yaw+90;return;}

    bot->goal_wp = -1;
    if(bot->order_kind && bot->has_target_pos){
        int anchor=find_nearest_waypoint(mgr,bot->target_pos);
        bot->goal_wp=(bot->order_kind==1 || bot->order_kind==3 || bot->order_kind==7)?anchor:zbot_cover_point(mgr,anchor,bot->client_num,mgr->plan_revision);
    }else if(bot->objective.kind){
        int anchor=zbot_objective_anchor(mgr,bot);
        if(anchor<0)anchor=find_nearest_waypoint(mgr,bot->objective.origin);
        if(anchor<0){bot->path_len=bot->path_idx=0;bot->next_path_ms=mgr->now_ms+1000;return;}
        if(bot->role==ZBOT_ROLE_PLANT || bot->role==ZBOT_ROLE_DEFUSE || bot->role==ZBOT_ROLE_ATTACK_OBJ)bot->goal_wp=anchor;
        else{bot->goal_wp=zbot_cover_point(mgr,anchor,bot->client_num,mgr->plan_revision+mgr->now_ms/12000);bot->next_goal_ms=mgr->now_ms+12000;}
    }else if(is_sd && mgr->site_count){
        int ordinal=0,anchor;
        for(i=0;i<bot->client_num;i++){hxSGBotState teammate;if(mgr->bots[i].active && mgr->imp->bot_state(mgr->eng,i,&teammate) && teammate.health>0 && teammate.team==st->team)ordinal++;}
        anchor=mgr->bomb_planted?find_nearest_waypoint(mgr,mgr->planted_pos):st->team==mgr->attacking_team?mgr->attack_site:mgr->sites[ordinal%mgr->site_count];
        if(bot->role==ZBOT_ROLE_PLANT || bot->role==ZBOT_ROLE_DEFUSE)bot->goal_wp=anchor;
        else {bot->goal_wp=zbot_cover_point(mgr,anchor,bot->client_num,mgr->plan_revision+mgr->now_ms/12000);bot->next_goal_ms=mgr->now_ms+12000;}
    }else if(bot->role==ZBOT_ROLE_ATTACK_OBJ){
        for(i=0;i<mgr->wp_count;i++)if(mgr->waypoints[i].flags&ZBOT_WP_OBJECTIVE){bot->goal_wp=i;break;}
    }
    if (bot->goal_wp < 0) {
        int tries = 0;
        do {
            bot->goal_wp = zbot_rand_int(mgr, mgr->wp_count);
            tries++;
        } while (bot->goal_wp == bot->current_wp && tries < 10);
    }

    if(bot->assigned_goal!=bot->goal_wp)bot->flank_wp=-1;
    if(bot->flank_wp>=0 && vec3_dist(st->origin,mgr->waypoints[bot->flank_wp].origin)<80)bot->flank_wp=-2;
    mgr->path_team=st->team;
    mgr->route_seed^=(unsigned int)(bot->client_num+1)*83492791u;
    bot->path_len = zbot_find_path(mgr, bot->current_wp, bot->goal_wp, bot->path, ZBOT_MAX_PATH);
    mgr->route_seed^=(unsigned int)(bot->client_num+1)*83492791u;
    if(bot->flank_wp!=-2 && bot->path_len>2 && !mgr->bomb_planted && ((!bot->order_kind && is_sd && st->team==mgr->attacking_team && bot->role!=ZBOT_ROLE_PLANT) || bot->order_kind==5 || bot->order_kind==6)){
        const float* a=st->origin;const float* b=mgr->waypoints[bot->goal_wp].origin;
        float direct=vec3_dist(a,b),best=-1e9f;int via=bot->flank_wp>=0?bot->flank_wp:-1,j;
        for(j=0;bot->flank_wp<0 && j<80 && direct>500;j++){
            int node=zbot_rand_int(mgr,mgr->wp_count);const float* q=mgr->waypoints[node].origin;
            float da=vec3_dist(a,q),db=vec3_dist(q,b),side=((b[0]-a[0])*(q[1]-a[1])-(b[1]-a[1])*(q[0]-a[0]))/direct,score;
            if(da<250 || db<250 || da+db>direct*1.65f || fabsf(side)<160 || fabsf(q[2]-a[2])>96)continue;
            if((bot->order_kind==5 && side<0)||(bot->order_kind==6 && side>0))continue;
            score=fabsf(side)-fabsf(da-db)*.2f-mgr->waypoints[node].danger;
            if(score>best){via=node;best=score;}
        }
        if(via>=0){int left[ZBOT_MAX_PATH],right[ZBOT_MAX_PATH];int n=zbot_find_path(mgr,bot->current_wp,via,left,ZBOT_MAX_PATH),m=zbot_find_path(mgr,via,bot->goal_wp,right,ZBOT_MAX_PATH);
            if(n>1 && m>1 && left[n-1]==via && right[m-1]==bot->goal_wp && n+m-1<=ZBOT_MAX_PATH){memcpy(bot->path,left,n*sizeof(int));memcpy(bot->path+n,right+1,(m-1)*sizeof(int));bot->path_len=n+m-1;bot->flank_wp=via;}
        }
    }
    bot->assigned_goal=bot->goal_wp;
    if(bot->goal_wp>=0)bot->watch_yaw=RAD2DEG(atan2f(mgr->waypoints[bot->goal_wp].origin[1]-st->origin[1],mgr->waypoints[bot->goal_wp].origin[0]-st->origin[0]));
    if(bot->role==ZBOT_ROLE_DEFEND_OBJ && !bot->order_kind && bot->goal_wp>=0 && mgr->site_count){
        int anchor=-1;float closest=1e12f;
        if(mgr->bomb_planted)anchor=find_nearest_waypoint(mgr,mgr->planted_pos);
        else for(i=0;i<mgr->site_count;i++){float d=vec3_dist_sq(mgr->waypoints[bot->goal_wp].origin,mgr->waypoints[mgr->sites[i]].origin);if(d<closest){closest=d;anchor=mgr->sites[i];}}
        if(anchor>=0)bot->watch_yaw=RAD2DEG(atan2f(mgr->waypoints[anchor].origin[1]-mgr->waypoints[bot->goal_wp].origin[1],mgr->waypoints[anchor].origin[0]-mgr->waypoints[bot->goal_wp].origin[0]));
    }
    if(bot->path_len==0){
        int queue[ZBOT_MAX_WAYPOINTS],seen[ZBOT_MAX_WAYPOINTS]={0},head=0,tail=1;
        queue[0]=bot->current_wp;seen[bot->current_wp]=1;
        while(head<tail){int node=queue[head++],edge;
            for(edge=0;edge<mgr->waypoints[node].edge_count;edge++){
                int next=mgr->waypoints[node].edges[edge];
                if(next>=0 && next<mgr->wp_count && mgr->waypoints[node].blocked_until[edge]<=mgr->now_ms && !seen[next]){seen[next]=1;queue[tail++]=next;}
            }
        }
        if(tail>1){
            int approach=bot->current_wp;float closest=vec3_dist_sq(mgr->waypoints[approach].origin,mgr->waypoints[bot->goal_wp].origin);
            for(i=1;i<tail;i++){float distance=vec3_dist_sq(mgr->waypoints[queue[i]].origin,mgr->waypoints[bot->goal_wp].origin);if(distance<closest){closest=distance;approach=queue[i];}}
            if(approach!=bot->current_wp)bot->path_len=zbot_find_path(mgr,bot->current_wp,approach,bot->path,ZBOT_MAX_PATH);
        }
        /* Keep goal_wp as the real objective. A reachable staging point is
         * not a bombsite and must never start an endless fake planting hold. */
    }
    bot->path_idx = bot->path_len>1?0:bot->path_len;
    if(!bot->path_len)bot->next_path_ms=mgr->now_ms+1000;
}

/* An auditory clue is approximate and cannot authorize a shot or expose a target id. */
void zbot_hear_sound(ZBotManager* mgr,int source,const float* origin,float radius) {
    hxSGBotState emitter;int i;
    if(!mgr || !mgr->initialized || !origin || !mgr->imp->bot_state || radius<=0 ||
       !isfinite(origin[0]) || !isfinite(origin[1]) || !isfinite(origin[2]) ||
       !mgr->imp->bot_state(mgr->eng,source,&emitter) || emitter.health<=0)return;
    for(i=0;i<ZBOT_MAX_CLIENTS;i++){
        ZBotAgent* b=&mgr->bots[i];hxSGBotState listener;float range=radius,dsq,zero[3]={0,0,0};hxSGTrace hit;
        if(!b->active || i==source || b->enemy_client>=0 || !mgr->imp->bot_state(mgr->eng,i,&listener) || listener.health<=0 || listener.locked)continue;
        if(strcmp(mgr->gametype,"dm") && listener.team==emitter.team)continue;
        dsq=vec3_dist_sq(listener.eye,origin);if(dsq>range*range)continue;
        /* Inside the occluded radius the clue is audible either way. Bots
         * already tracking an enemy ignore clues, so neither case needs a ray. */
        if(dsq>(range*.35f)*(range*.35f) && mgr->imp->trace_query && mgr->imp->trace_query(mgr->eng,listener.eye,origin,zero,zero,1,-3-i,&hit) && (hit.startsolid || hit.fraction<.99f))range*=.35f;
        if(dsq>range*range)continue;
        b->heard_pos[0]=roundf(origin[0]/80.f)*80.f;b->heard_pos[1]=roundf(origin[1]/80.f)*80.f;b->heard_pos[2]=origin[2];
        b->heard_until_ms=mgr->now_ms+1800;
    }
}
static void zbot_learn(ZBotManager* mgr,int team,const float* position,float amount){
    int i;if(team<0 || team>1)return;
    for(i=0;i<mgr->wp_count;i++){
        float dsq=vec3_dist_sq(position,mgr->waypoints[i].origin);
        if(dsq<480*480){float distance=sqrtf(dsq);float* h=&mgr->heat[team][i];*h+=amount*(1-distance/480);if(*h>180)*h=180;}
    }
}

/* Stock MP grenades share speed 960, upward impulse 120 and radius 350.
 * Solve a low arc and sweep it with the real collision service before pulling
 * the pin. No bounce prediction, unseen-enemy query or synthetic damage. */
static int zbot_grenade_arc(ZBotManager* m,const hxSGBotState* st,const float* target,int client,float* pitch){
    float dx=target[0]-st->eye[0],dy=target[1]-st->eye[1],distance=hypotf(dx,dy),best=1e9f,angle=0;
    float gravity=800,previous[3],zero[3]={-2,-2,-2},maxs[3]={2,2,2};int a,j;
    if(distance<420 || distance>1000 || !m->imp->trace_query)return 0;
    if(m->imp->cvar_string){const char* value=m->imp->cvar_string(m->eng,"g_gravity");if(value && atof(value)>0)gravity=(float)atof(value);}
    for(a=-5;a<=60;a++){
        float r=DEG2RAD((float)a),t=distance/(960*cosf(r)),z=st->eye[2]+(960*sinf(r)+120)*t-.5f*gravity*t*t;
        float error=fabsf(z-target[2]);if(error<best){best=error;angle=(float)a;}
    }
    if(best>24)return 0;
    memcpy(previous,st->eye,sizeof(previous));
    for(j=1;j<=24;j++){
        float r=DEG2RAD(angle),t=distance/(960*cosf(r))*j/24.f;
        float point[3]={st->eye[0]+dx*j/24.f,st->eye[1]+dy*j/24.f,st->eye[2]+(960*sinf(r)+120)*t-.5f*gravity*t*t};hxSGTrace hit;
        if(!m->imp->trace_query(m->eng,previous,point,zero,maxs,1,-3-client,&hit) || hit.startsolid || hit.fraction<.99f)return 0;
        memcpy(previous,point,sizeof(previous));
    }
    *pitch=-angle;return 1;
}
static void zbot_grenade(ZBotManager* m,ZBotAgent* b,const hxSGBotState* st,int* fwd,int* right,int* buttons){
    const char* name;float pitch=0;int i;
    if(!m->imp->player_weapon || !m->imp->player_weapon_name || st->locked || st->health<=0 || b->interact_until_ms)return;
    name=m->imp->player_weapon_name(m->eng,b->client_num,3);
    if(!b->grenade_phase){
        float scores[4]={0,0,0,0};int candidates[4]={-1,-1,-1,-1},target=-1,j,k;
        if(m->now_ms<b->next_grenade_ms || m->now_ms<1200 || m->grenade_checks_this_frame>=1 || (b->enemy_client>=0 && vec3_dist(st->origin,b->enemy_last_pos)<500))return;
        b->next_grenade_ms=m->now_ms+2000+(b->client_num%5)*300;
        if(!name || (strcmp(name,"fraggrenade_mp") && strcmp(name,"stielhandgranate_mp") && strcmp(name,"mk1britishfrag_mp") && strcmp(name,"rgd-33russianfrag_mp")) || m->imp->player_weapon(m->eng,b->client_num,HX_WEAPON_GET_CLIP,3,name,0)<=0)return;
        if(st->team<0 || st->team>1)return;
        /* Rank with cheap math first. Occluded hot zones must not trigger a
         * full ballistic sweep for every heated grid cell on the same frame. */
        for(i=0;i<m->wp_count;i++)if(m->heat[st->team][i]>20){
            float dsq=vec3_dist_sq(st->eye,m->waypoints[i].origin),score=m->heat[st->team][i];
            if(dsq<420*420 || dsq>1000*1000)continue;
            for(j=0;j<4;j++)if(score>scores[j]){for(k=3;k>j;k--){scores[k]=scores[k-1];candidates[k]=candidates[k-1];}scores[j]=score;candidates[j]=i;break;}
        }
        if(candidates[0]<0)return;
        m->grenade_checks_this_frame++;
        for(j=0;j<4 && candidates[j]>=0;j++)if(zbot_grenade_arc(m,st,m->waypoints[candidates[j]].origin,b->client_num,&pitch)){target=candidates[j];break;}
        if(target<0)return;
        memcpy(b->grenade_target,m->waypoints[target].origin,sizeof(b->grenade_target));
        for(i=0;i<ZBOT_MAX_CLIENTS;i++){hxSGBotState friend;
            if(i!=b->client_num && m->imp->bot_state(m->eng,i,&friend) && friend.health>0 && friend.team==st->team && strcmp(m->gametype,"dm") && vec3_dist(friend.origin,b->grenade_target)<450)return;
        }
        b->grenade_phase=1;b->grenade_until_ms=m->now_ms+2200;
        m->imp->player_weapon(m->eng,b->client_num,HX_WEAPON_SWITCH,3,name,0);
    }
    *fwd=*right=0;*buttons&=~(ZBOT_BTN_ATTACK|ZBOT_BTN_ADS|ZBOT_BTN_RELOAD);
    if(b->grenade_phase==1){
        if(m->now_ms>=b->grenade_until_ms){b->grenade_phase=0;return;}
        if(!name || !st->weapon_name || strcmp(name,st->weapon_name))return;
        {hxSGAnimationInput state;if(m->imp->animation_input && m->imp->animation_input(m->eng,b->client_num,&state) && state.weapon_phase)return;}
        if(!zbot_grenade_arc(m,st,b->grenade_target,b->client_num,&pitch)){b->grenade_phase=0;return;}
        for(i=0;i<ZBOT_MAX_CLIENTS;i++){hxSGBotState friend;
            if(i!=b->client_num && strcmp(m->gametype,"dm") && m->imp->bot_state(m->eng,i,&friend) && friend.health>0 && friend.team==st->team && vec3_dist(friend.origin,b->grenade_target)<450){b->grenade_phase=0;return;}
        }
        b->grenade_pitch=pitch;b->look_pitch=pitch;b->look_yaw=RAD2DEG(atan2f(b->grenade_target[1]-st->eye[1],b->grenade_target[0]-st->eye[0]));
        b->grenade_phase=2;b->grenade_until_ms=m->now_ms+300;
    }
    b->look_pitch=b->grenade_pitch;b->look_yaw=RAD2DEG(atan2f(b->grenade_target[1]-st->eye[1],b->grenade_target[0]-st->eye[0]));
    if(b->grenade_phase==2){
        if(m->now_ms<b->grenade_until_ms)*buttons|=ZBOT_BTN_ATTACK;
        else {b->grenade_phase=3;b->grenade_until_ms=m->now_ms+700;b->next_grenade_ms=m->now_ms+12000;}
    }else if(b->grenade_phase==3 && m->now_ms>=b->grenade_until_ms)b->grenade_phase=0;
}

static void zbot_sensor_combat(ZBotManager* mgr, ZBotAgent* bot, const hxSGBotState* st) {
    int i,count=0,order[ZBOT_MAX_CLIENTS],clients[ZBOT_MAX_CLIENTS];
    int best_enemy = -1;
    float distances[ZBOT_MAX_CLIENTS];
    hxSGBotState candidates[ZBOT_MAX_CLIENTS],enemy_st;

    if (mgr->now_ms < bot->next_scan_ms) {
        if (bot->enemy_client >= 0 && mgr->imp->bot_state(mgr->eng, bot->enemy_client, &enemy_st) && enemy_st.health > 0 && enemy_st.team != st->team) {
            bot->enemy_seen_ms = mgr->now_ms;
        }
        return;
    }
    if (bot->enemy_client >= 0 && mgr->imp->bot_state(mgr->eng, bot->enemy_client, &enemy_st) && enemy_st.health > 0 && enemy_st.team != st->team) {
        bot->next_scan_ms = mgr->now_ms + 750 + (bot->client_num % 5) * 60;
    } else {
        bot->next_scan_ms = mgr->now_ms + 110 + (bot->client_num % 5) * 13;
    }

    /* Scan other clients */
    for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
        hxSGBotState other;
        float dsq;
        if (i == bot->client_num) continue;
        if (!mgr->imp->bot_state(mgr->eng, i, &other)) continue;
        if (other.health <= 0) continue;
        if (strcmp(mgr->gametype,"dm") && other.team == st->team && st->team >= 0) continue; /* Same team */

        dsq = vec3_dist_sq(st->origin, other.origin);
        if (!(dsq < 3000.0f * 3000.0f)) continue;
        {
            int at=count;
            candidates[count]=other;distances[count]=dsq;clients[count]=i;
            while(at>0 && distances[order[at-1]]>dsq){order[at]=order[at-1];at--;}
            order[at]=count++;
        }
    }
    /* The nearest visible candidate wins. Checking in distance order lets us
     * stop at the first visible enemy instead of tracing successive winners
     * in client-slot order. Equal distances retain the lower client slot. */
    for(i=0;i<count;i++){
        int candidate=order[i],client=clients[candidate];
        const hxSGBotState other=candidates[candidate];
        /* 110-degree horizontal cone, independent of skill. Hearing turns the
           view towards clues; it never bypasses this cone or the sight trace. */
        {float bearing=RAD2DEG(atan2f(other.eye[1]-st->eye[1],other.eye[0]-st->eye[0]));
         float elevation=-RAD2DEG(atan2f(other.eye[2]-st->eye[2],hypotf(other.eye[0]-st->eye[0],other.eye[1]-st->eye[1])+.01f));
         if(fabsf(angle_normalize180(bearing-bot->look_yaw))>55 || fabsf(elevation-bot->look_pitch)>50)continue;}
        {
            float zero[3]={0,0,0};hxSGTrace hit;
            if(!mgr->imp->trace_query || !mgr->imp->trace_query(mgr->eng,st->eye,other.eye,zero,zero,1,-3-bot->client_num,&hit) || hit.startsolid || (hit.fraction<0.99f && hit.entity!=-3-client))continue;
            best_enemy = client;
            enemy_st = other;
            break;
        }
    }

    if (best_enemy >= 0) {
        if (bot->enemy_client != best_enemy) {
            int react_delay = 250;
            if (bot->skill == ZBOT_SKILL_PRO) react_delay = 90 + zbot_rand_int(mgr, 50);
            else if (bot->skill == ZBOT_SKILL_LOW) react_delay = 420 + zbot_rand_int(mgr, 120);
            else react_delay = 220 + zbot_rand_int(mgr, 80);

            bot->enemy_client = best_enemy;
            bot->enemy_seen_ms = mgr->now_ms;
            bot->enemy_react_ms = mgr->now_ms + react_delay;
            zbot_send_vsay(mgr, bot, ZBOT_VSAY_SPOT);
        }
        bot->enemy_last_pos[0] = enemy_st.origin[0];
        bot->enemy_last_pos[1] = enemy_st.origin[1];
        bot->enemy_last_pos[2] = enemy_st.eye[2] - 8.0f; /* aim at chest/head */
        bot->memory_until_ms = mgr->now_ms + 2500;
        if(mgr->now_ms>=bot->next_sighting_ms){zbot_learn(mgr,st->team,enemy_st.origin,12);bot->next_sighting_ms=mgr->now_ms+1500;}
    } else {
        bot->enemy_client = -1;
    }
}

static void zbot_aim_and_steer(ZBotManager* mgr, ZBotAgent* bot, const hxSGBotState* st,
                               int* out_fwd, int* out_right, int* out_up,
                               int* out_pitch, int* out_yaw, int* out_buttons) {
    float eye[3];
    float aim_target[3];
    float dx, dy, dz, dist2d, desired_pitch, desired_yaw;
    float max_ang_step;
    float yaw_diff, pitch_diff;
    int is_combat = (bot->enemy_client >= 0 && mgr->now_ms >= bot->enemy_react_ms);

    eye[0] = st->origin[0];
    eye[1] = st->origin[1];
    eye[2] = st->eye[2];



    /* Update human aim error */
    if (mgr->now_ms >= bot->next_err_ms) {
        float err_scale = 1.0f;
        if (bot->skill == ZBOT_SKILL_PRO) err_scale = 0.35f;
        else if (bot->skill == ZBOT_SKILL_LOW) err_scale = 3.5f;

        bot->err_pitch = (zbot_rand_float(mgr) * 2.0f - 1.0f) * err_scale;
        bot->err_yaw = (zbot_rand_float(mgr) * 2.0f - 1.0f) * err_scale;
        bot->next_err_ms = mgr->now_ms + 150 + zbot_rand_int(mgr, 150);
    }

    if(bot->role==ZBOT_ROLE_DEFUSE && mgr->bomb_planted && vec3_dist(st->origin,mgr->planted_pos)<100){
        memcpy(aim_target,mgr->planted_pos,sizeof(aim_target));max_ang_step=24;is_combat=0;
    } else if (is_combat) {
        aim_target[0] = bot->enemy_last_pos[0];
        aim_target[1] = bot->enemy_last_pos[1];
        aim_target[2] = bot->enemy_last_pos[2];
        max_ang_step = (bot->skill == ZBOT_SKILL_PRO) ? 38.0f : (bot->skill == ZBOT_SKILL_LOW) ? 14.0f : 24.0f;
    } else if (mgr->now_ms<bot->heard_until_ms) {
        memcpy(aim_target,bot->heard_pos,sizeof(aim_target));max_ang_step=12;
    } else if (bot->path_len > 1 && bot->path_idx < bot->path_len) {
        int wp = bot->path[bot->path_idx];
        aim_target[0] = mgr->waypoints[wp].origin[0];
        aim_target[1] = mgr->waypoints[wp].origin[1];
        aim_target[2] = mgr->waypoints[wp].origin[2] + 48.0f;
        max_ang_step = 16.0f;
    } else if(bot->role==ZBOT_ROLE_DEFEND_OBJ && bot->goal_wp>=0){
        float scan=bot->watch_yaw+sinf((float)mgr->now_ms*.0004f+bot->client_num)*65;
        aim_target[0]=eye[0]+cosf(DEG2RAD(scan))*300;aim_target[1]=eye[1]+sinf(DEG2RAD(scan))*300;aim_target[2]=eye[2];max_ang_step=8;
    } else {
        aim_target[0] = eye[0] + cosf(DEG2RAD(bot->look_yaw)) * 200.0f;
        aim_target[1] = eye[1] + sinf(DEG2RAD(bot->look_yaw)) * 200.0f;
        aim_target[2] = eye[2];
        max_ang_step = 12.0f;
    }

    dx = aim_target[0] - eye[0];
    dy = aim_target[1] - eye[1];
    dz = aim_target[2] - eye[2];
    dist2d = sqrtf(dx * dx + dy * dy);

    desired_yaw = RAD2DEG(atan2f(dy, dx)) + bot->err_yaw;
    desired_pitch = -RAD2DEG(atan2f(dz, dist2d > 0.1f ? dist2d : 0.1f)) + bot->err_pitch;

    /* Clamp pitch */
    if (desired_pitch > 85.0f) desired_pitch = 85.0f;
    if (desired_pitch < -85.0f) desired_pitch = -85.0f;

    yaw_diff = angle_normalize180(desired_yaw - bot->look_yaw);
    pitch_diff = desired_pitch - bot->look_pitch;

    if (fabsf(yaw_diff) > max_ang_step) {
        bot->look_yaw += (yaw_diff > 0.0f ? max_ang_step : -max_ang_step);
    } else {
        bot->look_yaw = desired_yaw;
    }
    bot->look_yaw = angle_normalize180(bot->look_yaw);

    if (fabsf(pitch_diff) > max_ang_step) {
        bot->look_pitch += (pitch_diff > 0.0f ? max_ang_step : -max_ang_step);
    } else {
        bot->look_pitch = desired_pitch;
    }

    *out_pitch = (int)(bot->look_pitch * 100.0f);
    *out_yaw = (int)(bot->look_yaw * 100.0f);

    if(bot->path_len>1 && bot->path_idx>0){
        int skip;
        for(skip=0;skip<3 && bot->path_idx+1<bot->path_len;skip++){
            const float* a=mgr->waypoints[bot->path[bot->path_idx-1]].origin;
            const float* b=mgr->waypoints[bot->path[bot->path_idx]].origin;
            const float* c=mgr->waypoints[bot->path[bot->path_idx+1]].origin;
            float x=b[0]-a[0],y=b[1]-a[1],u=c[0]-b[0],v=c[1]-b[1],length=hypotf(x,y);
            if(length<1 || fabsf(x*v-y*u)>1 || x*u+y*v<=0 || fabsf(a[2]-c[2])>8 || fabsf((st->origin[0]-a[0])*y-(st->origin[1]-a[1])*x)>8*length)break;
            bot->path_idx++;
        }
    }

    /* Movement & Navigation */
    *out_fwd = 0;
    *out_right = 0;
    *out_up = 0;

    if (bot->path_len > 1 && bot->path_idx < bot->path_len) {
        int wp = bot->path[bot->path_idx];
        float wp_dist = vec3_dist(st->origin, mgr->waypoints[wp].origin);
        float wp_yaw = RAD2DEG(atan2f(mgr->waypoints[wp].origin[1] - st->origin[1],
                                      mgr->waypoints[wp].origin[0] - st->origin[0]));
        float move_ang = DEG2RAD(angle_normalize180(wp_yaw - bot->look_yaw));

        if (hypotf(st->origin[0]-mgr->waypoints[wp].origin[0],st->origin[1]-mgr->waypoints[wp].origin[1])<18 && fabsf(st->origin[2]-mgr->waypoints[wp].origin[2])<24) {
            bot->path_idx++;
        }

        if (mgr->waypoints[wp].flags & ZBOT_WP_LADDER) {
            *out_pitch = -6000; /* Look up ladder */
            *out_fwd = 127;
            *out_buttons |= ZBOT_BTN_USE;
        } else {
            *out_fwd = (int)(cosf(move_ang) * 127.0f);
            *out_right = (int)(-sinf(move_ang) * 127.0f);
        }
    }

    /* No valid path means stop and replan, not blindly rush a wall. */
    if(bot->order_kind==1 && vec3_dist(st->origin,bot->target_pos)<150)*out_fwd=*out_right=0;

    /* Combat movements & shooting */
    if (is_combat) {
        float f_diff = fabsf(yaw_diff);
        *out_buttons |= ZBOT_BTN_ADS;

        if (mgr->now_ms >= bot->next_strafe_ms) {
            bot->strafe_dir = (zbot_rand_int(mgr, 3) - 1); /* -1, 0, 1 */
            bot->next_strafe_ms = mgr->now_ms + 900 + zbot_rand_int(mgr, 1100);
        }
        if(!*out_fwd && !*out_right)*out_right = bot->strafe_dir * 110;

        /* Burst fire logic */
        if (mgr->now_ms >= bot->next_burst_ms) {
            bot->burst_until_ms = mgr->now_ms + 180 + zbot_rand_int(mgr, 220);
            bot->next_burst_ms = bot->burst_until_ms + 120 + zbot_rand_int(mgr, 200);
        }

        if (f_diff < 12.0f && mgr->now_ms < bot->burst_until_ms) {
            *out_buttons |= ZBOT_BTN_ATTACK;
        }
    }

    if(st->clip_ammo<=0 && st->reserve_ammo>0 && !st->reloading){
        *out_buttons &= ~ZBOT_BTN_ATTACK;
        *out_buttons |= ZBOT_BTN_RELOAD;
    }

    /* Objective Plant / Defuse interaction */
    if ((bot->role == ZBOT_ROLE_PLANT && (!bot->order_kind || bot->order_kind==7)) || bot->role == ZBOT_ROLE_DEFUSE) {
        float obj_dist = 1000.0f;
        if (bot->role == ZBOT_ROLE_DEFUSE && mgr->bomb_planted) {
            obj_dist = vec3_dist(st->origin, mgr->planted_pos);
        } else if (bot->goal_wp >= 0 && bot->goal_wp < mgr->wp_count) {
            obj_dist = vec3_dist(st->origin, mgr->waypoints[bot->goal_wp].origin);
        }

        int touching=obj_dist<80.0f;
        if(bot->role==ZBOT_ROLE_PLANT && bot->goal_wp>=0 && bot->goal_wp<mgr->wp_count && mgr->imp->entity_key && mgr->imp->trace_query){
            int entity=mgr->waypoints[bot->goal_wp].entity_id;
            const char* model=entity>=0?mgr->imp->entity_key(mgr->eng,entity,"model"):NULL;
            int model_id=model && model[0]=='*'?atoi(model+1):0;
            if(model_id>0){
                hxSGTrace hit;float mins[3]={-15,-15,-24},maxs[3]={15,15,32};
                touching=obj_dist<160 && mgr->imp->trace_query(mgr->eng,st->origin,st->origin,mins,maxs,5,-1024-model_id,&hit)
                    && (hit.startsolid || hit.allsolid || hit.fraction<1);
            }
        }
        if (touching) {
            *out_fwd = 0;
            *out_right = 0;
            *out_buttons &= ~(ZBOT_BTN_ATTACK|ZBOT_BTN_ADS);
            bot->unstuck_until_ms=0;
            *out_buttons |= ZBOT_BTN_CROUCH;
            *out_buttons |= ZBOT_BTN_USE;

            if (bot->interact_until_ms == 0) {
                bot->interact_until_ms = mgr->now_ms + (bot->role == ZBOT_ROLE_PLANT ? 3500 : 5000);
                if (bot->role == ZBOT_ROLE_PLANT) {
                    zbot_send_chat(mgr, bot, 1, "Planting bomb at objective, cover me!");
                } else {
                    zbot_send_chat(mgr, bot, 1, "Defusing bomb now, hold them off!");
                }
            }
            /* Hold use until the real GSC-created objective changes. No native
               timer may declare a bomb planted or defused on its own. */

        } else {
            bot->interact_until_ms = 0;
        }
    }

    if(mgr->now_ms<bot->unstuck_until_ms){
        float escape=DEG2RAD((float)bot->unstuck_dir-bot->look_yaw);
        *out_fwd=(int)(cosf(escape)*127);*out_right=(int)(-sinf(escape)*127);
    }

    /* Follow the route around local walls rather than repeatedly walking into
       the same blocked segment. Keep movement relative to the actual view. */
    if(mgr->now_ms<bot->unstuck_until_ms && (*out_fwd || *out_right) && mgr->imp->trace_query && !st->on_ladder){
        float desired=bot->look_yaw+RAD2DEG(atan2f((float)-*out_right,(float)*out_fwd));
        float mins[3]={-14,-14,-6},maxs[3]={14,14,28},end[3];hxSGTrace hit;
        int j;float best=-1,best_yaw=desired;
        static const float offsets[]={0,45,-45,90,-90,135,-135,180};
        for(j=0;j<8;j++){
            float angle=desired+offsets[j],score;
            end[0]=st->origin[0]+cosf(DEG2RAD(angle))*64;end[1]=st->origin[1]+sinf(DEG2RAD(angle))*64;end[2]=st->origin[2];
            if(!mgr->imp->trace_query(mgr->eng,st->origin,end,mins,maxs,ZBOT_MOVE_MASK,-3-bot->client_num,&hit) || hit.startsolid)continue;
            score=hit.fraction-fabsf(offsets[j])*.001f;
            if(score>best){best=score;best_yaw=angle;}
            if(j==0 && hit.fraction>.95f)break;
        }
        if(best>=0){float angle=DEG2RAD(best_yaw-bot->look_yaw);*out_fwd=(int)(cosf(angle)*127);*out_right=(int)(-sinf(angle)*127);}
    }

    if(!is_combat && !bot->order_kind && !strcmp(mgr->gametype,"sd") && !mgr->bomb_planted && st->team==mgr->attacking_team &&
       mgr->now_ms<1200+(bot->client_num%4)*850){*out_fwd=*out_right=0;}

    if(st->locked){*out_fwd=*out_right=*out_up=0;bot->unstuck_until_ms=0;}

    /* Stuck detection */
    if (mgr->now_ms >= bot->last_check_ms + 500) {
        float moved_sq = vec3_dist_sq(st->origin, bot->last_check_pos);
        if (!bot->grenade_phase && (*out_fwd != 0 || *out_right != 0) && moved_sq < 16.0f * 16.0f) {
            if(bot->path_idx>0 && bot->path_idx<bot->path_len){
                ZBotWaypoint* from=&mgr->waypoints[bot->path[bot->path_idx-1]];int edge;
                for(edge=0;edge<from->edge_count;edge++)if(from->edges[edge]==bot->path[bot->path_idx] && !zbot_walk_sweep(mgr,from->origin,mgr->waypoints[from->edges[edge]].origin))from->blocked_until[edge]=mgr->now_ms+10000;
            }
            bot->path_len=bot->path_idx=0;bot->next_goal_ms=0;
            bot->unstuck_until_ms = mgr->now_ms + 1500;
            bot->unstuck_dir = (int)bot->look_yaw + (zbot_rand_int(mgr, 2) == 0 ? -90 : 90);
        }
        bot->last_check_pos[0] = st->origin[0];
        bot->last_check_pos[1] = st->origin[1];
        bot->last_check_pos[2] = st->origin[2];
        bot->last_check_ms = mgr->now_ms;
    }

    if (mgr->now_ms < bot->unstuck_until_ms) {
        *out_buttons |= ZBOT_BTN_JUMP;
        /* Keep the collision-tested escape direction selected above. */
    }
}

static void zbot_objective_control(ZBotManager* mgr,ZBotAgent* bot,const hxSGBotState* st,int* fwd,int* right,int* up,int* buttons){
    const ZBotObjectiveTask* task=&bot->objective;int inside=0;
    if(!task->kind || bot->order_kind || st->locked)return;
    if(task->model>0 && mgr->imp->trace_query){
        const float mins[3]={-15,-15,-24},maxs[3]={15,15,32};hxSGTrace hit;
        inside=mgr->imp->trace_query(mgr->eng,st->origin,st->origin,mins,maxs,5,-1024-task->model,&hit) &&
            (hit.startsolid || hit.allsolid || hit.fraction<1);
    }else inside=vec3_dist_sq(st->origin,task->origin)<=task->radius*task->radius && fabsf(st->origin[2]-task->origin[2])<48;
    if(task->kind==ZBOT_OBJECTIVE_RE_DELIVER){*buttons&=~ZBOT_BTN_USE;bot->grenade_phase=0;}
    if(inside){
        *fwd=*right=*up=0;bot->unstuck_until_ms=0;*buttons&=~ZBOT_BTN_JUMP;
        if(task->kind==ZBOT_OBJECTIVE_RE_PICKUP){
            /* Trigger-use is edge based. Release between attempts and stop
             * using immediately after the script changes us into a carrier. */
            *buttons&=~ZBOT_BTN_ATTACK;
            if((mgr->now_ms/100)%3)*buttons|=ZBOT_BTN_USE;else *buttons&=~ZBOT_BTN_USE;
        }
    }
}
static void zbot_manage_quota(ZBotManager* mgr) {
    int active_bots, total_clients = 0;
    int i;
    if (mgr->now_ms < mgr->next_quota_check_ms) return;
    mgr->next_quota_check_ms = mgr->now_ms + 100;

    active_bots = zbot_count(mgr);

    for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
        hxSGBotState st;
        if (mgr->imp && mgr->imp->bot_state && mgr->imp->bot_state(mgr->eng, i, &st)) {
            total_clients++;
        }
    }

    if (mgr->quota > 0) {
        if (total_clients < mgr->quota && active_bots < ZBOT_MAX_CLIENTS) {
            /* Small bounded batches fill a full server promptly without
             * running all connection callbacks in the same frame. */
            for (i=0;i<4 && total_clients<mgr->quota && total_clients<ZBOT_MAX_CLIENTS;i++) {
                if (zbot_add_bot(mgr, NULL, NULL, NULL)<0) break;
                total_clients++;
            }
        } else if (total_clients > mgr->quota && active_bots > 0) {
            for (i = ZBOT_MAX_CLIENTS - 1; i >= 0; --i) {
                if (mgr->bots[i].active) {
                    zbot_client_disconnect(mgr, i);
                    break;
                }
            }
        }
    }
}

typedef struct {
    int ready;
    int fwd, right, up, pitch, yaw, buttons;
} ZBotFrameOut;

static void zbot_think_one(ZBotManager* mgr, int i, const hxSGBotState* st, ZBotFrameOut* out) {
    ZBotAgent* bot = &mgr->bots[i];
    int fwd = 0, right = 0, up = 0, pitch = 0, yaw = 0, buttons = 0;
    zbot_sensor_combat(mgr, bot, st);
    zbot_aim_and_steer(mgr, bot, st, &fwd, &right, &up, &pitch, &yaw, &buttons);
    if(bot->objective.kind!=ZBOT_OBJECTIVE_RE_DELIVER)zbot_grenade(mgr,bot,st,&fwd,&right,&buttons);
    else bot->grenade_phase=0;
    if(bot->grenade_phase){pitch=(int)(bot->look_pitch*100);yaw=(int)(bot->look_yaw*100);}
    out->fwd = fwd; out->right = right; out->up = up;
    out->pitch = pitch; out->yaw = yaw; out->buttons = buttons;
    out->ready = 1;
}

#ifdef _WIN32
#define ZBOT_POOL_THREADS 4
typedef struct {
    int initialized;
    HANDLE threads[ZBOT_POOL_THREADS];
    HANDLE start_events[ZBOT_POOL_THREADS];
    HANDLE done_events[ZBOT_POOL_THREADS];
    ZBotManager* mgr;
    const hxSGBotState* states;
    const int* indices;
    int count;
    volatile LONG next_idx;
    ZBotFrameOut* outputs;
} ZBotPool;
static ZBotPool g_zbot_pool = {0};

static DWORD WINAPI zbot_worker_proc(LPVOID param) {
    int tid = (int)(intptr_t)param;
    for (;;) {
        WaitForSingleObject(g_zbot_pool.start_events[tid], INFINITE);
        for (;;) {
            LONG k = InterlockedIncrement(&g_zbot_pool.next_idx) - 1;
            if (k >= g_zbot_pool.count) break;
            int i = g_zbot_pool.indices[k];
            zbot_think_one(g_zbot_pool.mgr, i, &g_zbot_pool.states[i], &g_zbot_pool.outputs[i]);
        }
        SetEvent(g_zbot_pool.done_events[tid]);
    }
    return 0;
}

static void zbot_pool_init(void) {
    int t;
    if (g_zbot_pool.initialized) return;
    g_zbot_pool.initialized = 1;
    for (t = 0; t < ZBOT_POOL_THREADS; ++t) {
        g_zbot_pool.start_events[t] = CreateEventA(NULL, FALSE, FALSE, NULL);
        g_zbot_pool.done_events[t] = CreateEventA(NULL, FALSE, FALSE, NULL);
        g_zbot_pool.threads[t] = CreateThread(NULL, 0, zbot_worker_proc, (LPVOID)(intptr_t)t, 0, NULL);
    }
}
#endif

void zbot_run_frame(ZBotManager* mgr, int level_time_ms) {
    int i;
    int dt_ms;
    if (!mgr || !mgr->initialized) return;

    if(mgr->imp->cvar_string){
        const char* value=mgr->imp->cvar_string(mgr->eng,"bot_quota");
        if(value && value[0]){
            int quota=atoi(value);if(quota<0)quota=0;if(quota>ZBOT_MAX_CLIENTS)quota=ZBOT_MAX_CLIENTS;
            if(quota!=mgr->quota){mgr->quota=quota;mgr->next_quota_check_ms=0;}
        }
    }

    if(mgr->imp->cvar_string){const char* value=mgr->imp->cvar_string(mgr->eng,"bot_skill");
        if(value && value[0]){ZBotSkill skill=zbot_parse_skill(value);if(skill!=mgr->configured_skill){
            mgr->configured_skill=skill;for(i=0;i<ZBOT_MAX_CLIENTS;i++)if(mgr->bots[i].active)mgr->bots[i].skill=skill==ZBOT_SKILL_RANDOM?mgr->profiles[mgr->bots[i].profile_idx].base_skill:skill;
        }}
    }
    mgr->now_ms = level_time_ms;mgr->nav_plans_this_frame=mgr->grenade_checks_this_frame=0;
    dt_ms = (mgr->last_frame_ms > 0) ? (level_time_ms - mgr->last_frame_ms) : 50;
    mgr->last_frame_ms = level_time_ms;

    if(dt_ms<1)dt_ms=1;if(dt_ms>250)dt_ms=250;
    /* Slow decay: observations survive several rounds rather than six seconds. */
    for(i=0;i<mgr->wp_count;i++){int t;for(t=0;t<2;t++){mgr->heat[t][i]-=dt_ms*.0002f;if(mgr->heat[t][i]<0)mgr->heat[t][i]=0;}}
    /* Decay waypoint danger */
    for (i = 0; i < mgr->wp_count; ++i) {
        if (mgr->waypoints[i].danger > 0.0f) {
            mgr->waypoints[i].danger -= (float)dt_ms * 0.0005f;
            if (mgr->waypoints[i].danger < 0.0f) mgr->waypoints[i].danger = 0.0f;
        }
    }

    { int was_planted=mgr->bomb_planted;
    if((!strcmp(mgr->gametype,"sd") || !strcmp(mgr->gametype,"de")) && mgr->imp->entity_find && mgr->imp->entity_key){
        int ids[16],n=mgr->imp->entity_find(mgr->eng,"model","xmodel/mp_bomb1_defuse",ids,16),j;
        if(!n)n=mgr->imp->entity_find(mgr->eng,"model","models/w_c4.obj",ids,16);
        mgr->bomb_planted=0;
        for(j=0;j<n && j<16;j++)if(!atoi(mgr->imp->entity_key(mgr->eng,ids[j],"_hx_deleted"))){
            const char* origin=mgr->imp->entity_key(mgr->eng,ids[j],"origin");
            if(origin && sscanf(origin,"%f %f %f",mgr->planted_pos,mgr->planted_pos+1,mgr->planted_pos+2)==3){mgr->bomb_planted=1;break;}
        }
    }
    if(was_planted!=mgr->bomb_planted){mgr->next_plan_ms=0;mgr->plan_revision++;}
    if(was_planted!=mgr->bomb_planted)for(i=0;i<ZBOT_MAX_CLIENTS;i++){mgr->bots[i].path_len=0;mgr->bots[i].role=ZBOT_ROLE_NONE;mgr->bots[i].interact_until_ms=0;}
    }
    zbot_manage_quota(mgr);
    zbot_plan_sd(mgr);

    static int s_frame = 0;
    int parallel = 0;
    if (mgr->imp->cvar_string) {
        const char* pv = mgr->imp->cvar_string(mgr->eng, "bot_parallel");
        if (pv && atoi(pv) != 0) parallel = 1;
    }
    s_frame++;
#ifdef _WIN32
    if (parallel) {
        hxSGBotState par_states[ZBOT_MAX_CLIENTS];
        ZBotFrameOut par_outputs[ZBOT_MAX_CLIENTS];
        int par_indices[ZBOT_MAX_CLIENTS];
        int par_count = 0, t;
        memset(par_outputs, 0, sizeof(par_outputs));
        for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
            ZBotAgent* bot = &mgr->bots[i];
            hxSGBotState* st = &par_states[i];
            if (!bot->active || !mgr->imp || !mgr->imp->bot_state || !mgr->imp->bot_state(mgr->eng, i, st)) continue;
            if (!bot->joined) {
                if (mgr->now_ms >= bot->next_join_ms && mgr->client_cmd_cb) {
                    bot->joined=mgr->client_cmd_cb(mgr->host_game,i,"botmenu select")==2;
                    bot->next_join_ms=mgr->now_ms+100;
                }
                continue;
            }
            if (st->health <= 0) {
                bot->grenade_phase=0;bot->heard_until_ms=0;bot->interact_until_ms=0;
                bot->was_alive = 0;bot->role=ZBOT_ROLE_NONE;bot->path_len=bot->path_idx=0;
                zbot_check_events(mgr, bot, st);
                if(mgr->imp->bot_command)mgr->imp->bot_command(mgr->eng,i,dt_ms,0,0,0,st->pitch,st->yaw,ZBOT_BTN_USE,st->weapon);
                continue;
            }
            if(!bot->was_alive){bot->look_yaw=st->yaw;bot->look_pitch=st->pitch;bot->path_len=bot->path_idx=0;bot->role=ZBOT_ROLE_NONE;}
            if(mgr->imp->player_weapon && mgr->imp->player_weapon_name && !st->locked && !bot->grenade_phase){
                const char* primary=mgr->imp->player_weapon_name(mgr->eng,i,0);
                if(primary && primary[0] && strcmp(primary,"none") && (!st->weapon_name || strcmp(primary,st->weapon_name)) &&
                   (mgr->imp->player_weapon(mgr->eng,i,HX_WEAPON_GET_CLIP,0,primary,0)>0 || mgr->imp->player_weapon(mgr->eng,i,HX_WEAPON_GET_STOCK,0,primary,0)>0))
                    mgr->imp->player_weapon(mgr->eng,i,HX_WEAPON_SWITCH,0,primary,0);
            }
            if (((i + s_frame) & 1) && bot->was_alive) {
                if (mgr->imp->bot_command)
                    mgr->imp->bot_command(mgr->eng, i, dt_ms, bot->last_fwd, bot->last_right, bot->last_up, bot->last_pitch*.01f, bot->last_yaw*.01f, bot->last_buttons, 0);
                continue;
            }
            bot->was_alive = 1;
            zbot_check_events(mgr, bot, st);
            zbot_decide_role_and_goal(mgr, bot, st);
            par_indices[par_count++] = i;
        }
        if (par_count > 0) {
            zbot_pool_init();
            g_zbot_pool.mgr = mgr;
            g_zbot_pool.states = par_states;
            g_zbot_pool.indices = par_indices;
            g_zbot_pool.count = par_count;
            g_zbot_pool.next_idx = 0;
            g_zbot_pool.outputs = par_outputs;
            for (t = 0; t < ZBOT_POOL_THREADS; ++t) SetEvent(g_zbot_pool.start_events[t]);
            WaitForMultipleObjects(ZBOT_POOL_THREADS, g_zbot_pool.done_events, TRUE, INFINITE);
            for (t = 0; t < par_count; ++t) {
                int idx = par_indices[t];
                ZBotAgent* bot = &mgr->bots[idx];
                if (mgr->now_ms >= bot->next_random_vsay_ms) {
                    zbot_send_vsay(mgr, bot, ZBOT_VSAY_RANDOM);
                    bot->next_random_vsay_ms = mgr->now_ms + 12000 + zbot_rand_int(mgr, 18000);
                }
                if (par_outputs[idx].ready && mgr->imp->bot_command) {
                    ZBotFrameOut* o = &par_outputs[idx];
                    zbot_objective_control(mgr, bot, &par_states[idx], &o->fwd, &o->right, &o->up, &o->buttons);
                    bot->last_fwd = o->fwd; bot->last_right = o->right; bot->last_up = o->up;
                    bot->last_pitch = o->pitch; bot->last_yaw = o->yaw; bot->last_buttons = o->buttons;
                    mgr->imp->bot_command(mgr->eng, idx, dt_ms, o->fwd, o->right, o->up, o->pitch*.01f, o->yaw*.01f, o->buttons, 0);
                }
            }
        }
        return;
    }
#endif
    for (i = 0; i < ZBOT_MAX_CLIENTS; ++i) {
        ZBotAgent* bot = &mgr->bots[i];
        hxSGBotState st;
        int fwd = 0, right = 0, up = 0, pitch = 0, yaw = 0, buttons = 0;

        if (!bot->active) continue;

        if (!mgr->imp || !mgr->imp->bot_state || !mgr->imp->bot_state(mgr->eng, i, &st)) {
            continue;
        }

        /* Drive the same GSC menu responses as a human, one visible menu at
         * a time. Only the script's accepted persistent loadout completes join. */
        if (!bot->joined) {
            if (mgr->now_ms >= bot->next_join_ms && mgr->client_cmd_cb) {
                bot->joined=mgr->client_cmd_cb(mgr->host_game,i,"botmenu select")==2;
                bot->next_join_ms=mgr->now_ms+100;
            }
            continue;
        }

        if (st.health <= 0) {
            bot->grenade_phase=0;bot->heard_until_ms=0;bot->interact_until_ms=0;
            bot->was_alive = 0;bot->role=ZBOT_ROLE_NONE;bot->path_len=bot->path_idx=0;
            zbot_check_events(mgr, bot, &st);
            if(mgr->imp->bot_command)mgr->imp->bot_command(mgr->eng,i,dt_ms,0,0,0,st.pitch,st.yaw,ZBOT_BTN_USE,st.weapon);
            continue;
        }

        if(!bot->was_alive){bot->look_yaw=st.yaw;bot->look_pitch=st.pitch;bot->path_len=bot->path_idx=0;bot->role=ZBOT_ROLE_NONE;}
        if(mgr->imp->player_weapon && mgr->imp->player_weapon_name && !st.locked && !bot->grenade_phase){
            const char* primary=mgr->imp->player_weapon_name(mgr->eng,i,0);
            if(primary && primary[0] && strcmp(primary,"none") && (!st.weapon_name || strcmp(primary,st.weapon_name)) &&
               (mgr->imp->player_weapon(mgr->eng,i,HX_WEAPON_GET_CLIP,0,primary,0)>0 || mgr->imp->player_weapon(mgr->eng,i,HX_WEAPON_GET_STOCK,0,primary,0)>0))
                mgr->imp->player_weapon(mgr->eng,i,HX_WEAPON_SWITCH,0,primary,0);
        }
        if (((i + s_frame) & 1) && bot->was_alive) {
            if (mgr->imp && mgr->imp->bot_command) {
                mgr->imp->bot_command(mgr->eng, i, dt_ms, bot->last_fwd, bot->last_right, bot->last_up, bot->last_pitch*.01f, bot->last_yaw*.01f, bot->last_buttons, 0);
            }
            continue;
        }
        bot->was_alive = 1;
        zbot_check_events(mgr, bot, &st);
        zbot_decide_role_and_goal(mgr, bot, &st);
        zbot_sensor_combat(mgr, bot, &st);
        zbot_aim_and_steer(mgr, bot, &st, &fwd, &right, &up, &pitch, &yaw, &buttons);

        if(bot->objective.kind!=ZBOT_OBJECTIVE_RE_DELIVER)zbot_grenade(mgr,bot,&st,&fwd,&right,&buttons);
        else bot->grenade_phase=0;
        if(bot->grenade_phase){pitch=(int)(bot->look_pitch*100);yaw=(int)(bot->look_yaw*100);}
        zbot_objective_control(mgr,bot,&st,&fwd,&right,&up,&buttons);

        /* Random ambient voice chatter */
        if (mgr->now_ms >= bot->next_random_vsay_ms) {
            zbot_send_vsay(mgr, bot, ZBOT_VSAY_RANDOM);
            bot->next_random_vsay_ms = mgr->now_ms + 12000 + zbot_rand_int(mgr, 18000);
        }

        bot->last_fwd = fwd; bot->last_right = right; bot->last_up = up;
        bot->last_pitch = pitch; bot->last_yaw = yaw; bot->last_buttons = buttons;
        if (mgr->imp && mgr->imp->bot_command) {
            mgr->imp->bot_command(mgr->eng, i, dt_ms, fwd, right, up, pitch*.01f, yaw*.01f, buttons, 0);
        }

    }
}

int zbot_console_command(ZBotManager* mgr, const char* cmd) {
    char verb[32], a1[64], a2[64], a3[64];
    int n;
    if (!mgr || !mgr->initialized || !cmd || !cmd[0]) return 0;

    n = sscanf(cmd, "%31s %63s %63s %63s", verb, a1, a2, a3);
    if (n <= 0) return 0;

    if(!strcmp(verb,"bot_status")){
        if(mgr->imp->print){char msg[256];int i;snprintf(msg,sizeof(msg),"BOT_PLAN round=%d site=%d planter=%d defuser=%d revision=%d\n",mgr->round_number,mgr->attack_site,mgr->planter,mgr->defuser,mgr->plan_revision);mgr->imp->print(mgr->eng,msg);
            snprintf(msg,sizeof(msg),"BOT_NAV nodes=%d searches=%u expanded=%u plans_frame=%d grenade_checks_frame=%d\n",mgr->wp_count,mgr->nav_searches,mgr->nav_expanded,mgr->nav_plans_this_frame,mgr->grenade_checks_this_frame);mgr->imp->print(mgr->eng,msg);
            {int team;for(team=0;team<2;team++){int hot=-1;float heat=0;for(i=0;i<mgr->wp_count;i++)if(mgr->heat[team][i]>heat){heat=mgr->heat[team][i];hot=i;}snprintf(msg,sizeof(msg),"BOT_HEAT team=%d node=%d heat=%.1f\n",team,hot,heat);mgr->imp->print(mgr->eng,msg);}}
            for(i=0;i<ZBOT_MAX_CLIENTS;i++)if(mgr->bots[i].active){ZBotAgent* b=&mgr->bots[i];unsigned int hash=2166136261u;int j;for(j=0;j<b->path_len;j++)hash=(hash^(unsigned int)b->path[j])*16777619u;
                snprintf(msg,sizeof(msg),"BOT_TASK client=%d skill=%s role=%d goal=%d flank=%d path=%d/%d hash=%u order=%d\n",i,zbot_skill_name(b->skill),b->role,b->goal_wp,b->flank_wp,b->path_idx,b->path_len,hash,b->order_kind);mgr->imp->print(mgr->eng,msg);
                snprintf(msg,sizeof(msg),"BOT_SENSE client=%d visible=%d hearing=%d grenade=%d\n",i,b->enemy_client,b->heard_until_ms>mgr->now_ms,b->grenade_phase);mgr->imp->print(mgr->eng,msg);
                if(b->goal_wp>=0 && b->goal_wp<mgr->wp_count){int next=b->path_idx<b->path_len?b->path[b->path_idx]:b->goal_wp;const float* goal=mgr->waypoints[b->goal_wp].origin;const float* point=mgr->waypoints[next].origin;
                    snprintf(msg,sizeof(msg),"BOT_ROUTE client=%d goal=%.1f,%.1f,%.1f next=%.1f,%.1f,%.1f recovery=%d\n",i,goal[0],goal[1],goal[2],point[0],point[1],point[2],mgr->now_ms<b->unstuck_until_ms);mgr->imp->print(mgr->eng,msg);}}
        }return 1;
    }
    if (!strcmp(verb, "bot_add")) {
        const char* skill_arg = (n >= 2) ? a1 : NULL;
        const char* team_arg = (n >= 3) ? a2 : NULL;
        const char* name_arg = (n >= 4) ? a3 : NULL;
        int slot = zbot_add_bot(mgr, skill_arg, team_arg, name_arg);
        if (slot >= 0 && mgr->imp && mgr->imp->print) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Bot %s added (slot %d, skill %s)\n",
                     mgr->bots[slot].name, slot, zbot_skill_name(mgr->bots[slot].skill));
            mgr->imp->print(mgr->eng, msg);
        }
        return 1;
    }

    if (!strcmp(verb, "bot_kick")) {
        const char* target = (n >= 2) ? a1 : NULL;
        int count = zbot_kick_bot(mgr, target);
        if (mgr->imp && mgr->imp->print) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Kicked %d bot(s)\n", count);
            mgr->imp->print(mgr->eng, msg);
        }
        return 1;
    }

    if (!strcmp(verb, "bot_quota")) {
        if (n >= 2) {
            mgr->quota = atoi(a1);
            if (mgr->imp && mgr->imp->cvar_set) {
                mgr->imp->cvar_set(mgr->eng, "bot_quota", a1);
            }
        }
        return 1;
    }

    if (!strcmp(verb, "bot_skill")) {
        if (n >= 2) {
            mgr->configured_skill = zbot_parse_skill(a1);
            for(int i=0;i<ZBOT_MAX_CLIENTS;i++)if(mgr->bots[i].active)mgr->bots[i].skill=mgr->configured_skill==ZBOT_SKILL_RANDOM?mgr->profiles[mgr->bots[i].profile_idx].base_skill:mgr->configured_skill;
            if (mgr->imp && mgr->imp->cvar_set) {
                mgr->imp->cvar_set(mgr->eng, "bot_skill", a1);
            }
        }
        return 1;
    }

    if (!strcmp(verb, "bot_chat")) {
        if (n >= 2) {
            mgr->chat_enabled = (atoi(a1) != 0);
            if (mgr->imp && mgr->imp->cvar_set) {
                mgr->imp->cvar_set(mgr->eng, "bot_chat", a1);
            }
        }
        return 1;
    }

    if (!strcmp(verb, "bot_roast")) {
        if (n >= 2) {
            mgr->roast_enabled = (atoi(a1) != 0);
            if (mgr->imp && mgr->imp->cvar_set) {
                mgr->imp->cvar_set(mgr->eng, "bot_roast", a1);
            }
        }
        return 1;
    }

    if (!strcmp(verb, "bot_vsay")) {
        if (n >= 2) {
            mgr->vsay_enabled = (atoi(a1) != 0);
            if (mgr->imp && mgr->imp->cvar_set) {
                mgr->imp->cvar_set(mgr->eng, "bot_vsay", a1);
            }
        }
        return 1;
    }

    return 0;
}
