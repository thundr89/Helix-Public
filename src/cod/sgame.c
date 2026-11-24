/*
 * CoD1 server game module. Runs map and gametype GSC through the small VM.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "helix/game/iserver_game.h"
#include "gsc/vm.h"
#include "gsc/linked.h"
#include "turret.h"
#include "anim/runtime.h"
#include "bot/zbot.h"
#include "footsteps.h"


#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define SG_EXPORT __declspec(dllexport)
#else
#define SG_EXPORT __attribute__((visibility("default")))
#endif

typedef struct CodHud {
    int active, handle, timer, timer_end, timer_last, retired_owner;hxSGHud value;int fade_start,fade_end,move_start,move_end,scale_start,scale_end;float fade_from[4],move_from[2],scale_from[2];
    char text[1024],shader[256];
} CodHud;
typedef struct CodObjective {int active,player;hxSGObjective value;char icon[256];} CodObjective;
typedef struct CodFx {int active,count;char name[256];hxSGParticle parts[16];char shaders[16][192];} CodFx;

typedef struct CodMotion {int active,id,start,end,rotation,gravity;float from[3],to[3],velocity[3],accel,decel;} CodMotion;
typedef struct CodGame {
    hxServerGame api;
    GscRuntime* runtime;
    CodTurrets turrets;
    CodNativeAnimation* animations;
    int hint_available,last_hint,radius_ignore_players;
    int clock_origin;
    int clock_started;
    int preserving,reconnecting[COD_CLIENTS];
    int connecting[COD_CLIENTS],begin_pending[COD_CLIENTS],response_count[COD_CLIENTS];
    struct {int kind,deadline;char menu[128],response[128];} responses[COD_CLIENTS][8];
    int last_threads, last_errors, last_start;
    char origin_text[96];
    hxSGEngine* eng;
    hxSGImports* imp;
    char map[64];
    char gametype[32];
    char weapons[16][64];
    int nweapons;
    char status_icons[16][256],head_icons[15][256];int status_icon_count,head_icon_count;
    char allies[64];
    char axis[64];
    char ambient[96];
    struct {int active,team,linked,link_entity,state,maxhealth;char weapon[64],sessionteam[16],body[256],viewmodel[256],current_menu[128],headicon[256],statusicon[256],headiconteam[16];int attach_count;struct{char model[256],tag[96];int ignore;} attachments[16];} players[COD_CLIENTS];
    struct {int id,visible;char allow[96];} placed[256];
    int placed_count,placed_poll;
    struct {int id,model,is_use;char hint[256];} triggers[256];
    int trigger_count,trig_use_buttons[COD_CLIENTS],trig_use_available;
    char trig_use_text[256];
    float fog_from[5],fog_target[5];int fog_start,fog_end;
    CodMotion motions[256];GscNativeField trace_fields[8];
    CodHud hud[128];int hud_next;unsigned char hud_used[128];CodObjective objectives[32];CodFx effects[64];int scene_now;int announcement_id,announcement_end;
    int client_announcement_id[COD_CLIENTS], client_announcement_end[COD_CLIENTS];
    ZBotManager zbot;
    CodFootsteps footsteps;
    int retired_players[COD_CLIENTS*2];
    int linked_mode,linked_prepared,linked_callback,script_failed;
} CodGame;

static void bot_movement_sound(void* context,int client,const float* origin,float radius){zbot_hear_sound((ZBotManager*)context,client,origin,radius);}

static int handle_cmd(CodGame* g, int client, const char* cmd);
static int bot_client_command(void* game,int client,const char* cmd){return handle_cmd((CodGame*)game,client,cmd);}


static void add_weapon(CodGame* g, const char* name) {
    int i;
    if (!g || !name || !name[0] || g->nweapons >= 16) return;
    for (i = 0; i < g->nweapons; i++) {
        if (strcmp(g->weapons[i], name) == 0) return;
    }
    snprintf(g->weapons[g->nweapons], 64, "%s", name);
    g->nweapons++;
}

static void on_weapon(void* user, const char* name) { add_weapon((CodGame*)user, name); }
static void on_take(void* user) { ((CodGame*)user)->nweapons = 0; }
static void on_ambient(void* user, const char* name) {
    CodGame* g=(CodGame*)user;
    snprintf(g->ambient,sizeof(g->ambient),"%s",name?name:"");
    if(g->imp && g->imp->cvar_set)g->imp->cvar_set(g->eng,"g_ambient",g->ambient);
}
static void on_teams(void* user, const char* allies, const char* axis) {
    CodGame* g = (CodGame*)user;
    if (allies) snprintf(g->allies, sizeof(g->allies), "%s", allies);
    if (axis) snprintf(g->axis, sizeof(g->axis), "%s", axis);
}
static void on_gt(void* user, const char* name) {
    if (name && name[0]) snprintf(((CodGame*)user)->gametype, sizeof(((CodGame*)user)->gametype), "%s", name);
}

static int native_name(const char*,const char*);
static void scene_fx_prepare(CodGame*,const char*);
static void on_precache(void* user, int kind, const char* name) {
    CodGame* g = (CodGame*)user;
    char path[512];
    if (!name || !name[0]) return;
    /* Retail precaches an engine-owned scoreboard shader, not an archive image.
     * Our native scoreboard is a translated HUD menu: prepare its actual images.
     * Keep the shader request on failure so missing host UI is still reported. */
    if(kind==GSC_PC_SHADER && native_name(name,"hudScoreboard_mp") && g && g->imp &&
       g->imp->api_version>=9 && g->imp->precache_menu && g->imp->precache_menu(g->eng,"scoreboard"))return;
    if (kind == GSC_PC_MODEL && strncmp(name,"xmodel/",7)!=0 && strncmp(name,"models/",7)!=0 && strncmp(name,"cod/",4)!=0)
        snprintf(path,sizeof(path),"xmodel/%s",name);
    else if (kind == GSC_PC_WEAPON && strncmp(name,"weapons/",8)!=0)
        snprintf(path,sizeof(path),"weapons/mp/%s",name);
    else if (kind == GSC_PC_FX) {
        size_t n=strlen(name);int suffix=n>=4 && strcmp(name+n-4,".efx")==0;
        snprintf(path,sizeof(path),"%s%s%s",strncmp(name,"fx/",3)==0?"":"fx/",name,suffix?"":".efx");
    } else snprintf(path,sizeof(path),"%s",name);
    if (kind == GSC_PC_FX) scene_fx_prepare(g,name);
    if (g && g->imp && g->imp->api_version >= 3 && g->imp->precache_asset)
        g->imp->precache_asset(g->eng, kind, path);
}
static void on_cvar(void* user, const char* name, const char* value) {
    CodGame* g = (CodGame*)user;
    if (g && g->imp && g->imp->cvar_set && name && value) g->imp->cvar_set(g->eng, name, value);
}
static const char* host_key(void* user,int id,const char* key);
static unsigned nation_asset_mask(const char* nation) {
    return !strcmp(nation,"german")?8u:!strcmp(nation,"russian")?4u:!strcmp(nation,"british")?2u:1u;
}
static void precache_view_weapon(CodGame* g,const char* weapon,const char* nation) {
        {int nation_id=!strcmp(nation,"german")?4:!strcmp(nation,"british")?2:!strcmp(nation,"russian")?3:1;
         char key[96];const char *type,*variation;snprintf(key,sizeof(key),"%s_soldiertype",nation);type=gsc_runtime_game_string(g->runtime,key);
         snprintf(key,sizeof(key),"%s_soldiervariation",nation);variation=gsc_runtime_game_string(g->runtime,key);
         if(!strcmp(nation,"american")&&!strcmp(variation,"winter"))nation_id=5;
         if(!strcmp(nation,"german")){if(!strcmp(type,"waffen"))nation_id=!strcmp(variation,"winter")?8:6;else if(!strcmp(type,"fallschirmjagergrey"))nation_id=12;else if(!strcmp(type,"fallschirmjagercamo"))nation_id=11;else if(!strcmp(type,"fallschirmjager"))nation_id=!strcmp(variation,"grey")?12:11;else if(!strcmp(type,"kriegsmarine"))nation_id=13;else if(!strcmp(variation,"winter"))nation_id=7;}
         if(!strcmp(nation,"russian")&&!strcmp(type,"veteran"))nation_id=9;
         if(!strcmp(nation,"british")&&!strcmp(type,"airborne"))nation_id=10;
         char path[192];snprintf(path,sizeof(path),"cod/viewhands/%s/%d/hands.obj",weapon,nation_id);on_precache(g,GSC_PC_MODEL,path);
         snprintf(path,sizeof(path),"cod/viewhands/%s/%d/gun.obj",weapon,nation_id);on_precache(g,GSC_PC_MODEL,path);
         snprintf(path,sizeof(path),"cod/viewhands/%s/%d/profile.txt",weapon,nation_id);on_precache(g,HX_PRECACHE_DATA,path);
         snprintf(path,sizeof(path),"cod/viewhands/%s/%d/motion.bin",weapon,nation_id);on_precache(g,HX_PRECACHE_DATA,path);}
}
static void precache_nation(CodGame* g, const char* nation) {
    on_precache(g,GSC_PC_MODEL,"xmodel/health_small");
    static const char* german[] = {"kar98k_mp", "mp40_mp", "mp44_mp", "kar98k_sniper_mp", "luger_mp", "stielhandgranate_mp", "panzerfaust_mp", "fg42_mp", NULL};
    static const char* american[] = {"m1garand_mp", "m1carbine_mp", "thompson_mp", "bar_mp", "springfield_mp", "colt_mp", "fraggrenade_mp", "panzerfaust_mp", "fg42_mp", NULL};
    static const char* russian[] = {"mosin_nagant_mp", "ppsh_mp", "mosin_nagant_sniper_mp", "luger_mp", "rgd-33russianfrag_mp", "panzerfaust_mp", "fg42_mp", NULL};
    static const char* british[] = {"enfield_mp", "sten_mp", "bren_mp", "springfield_mp", "colt_mp", "mk1britishfrag_mp", "panzerfaust_mp", "fg42_mp", NULL};
    const char** weapons = strcmp(nation, "german") == 0 ? german : strcmp(nation, "russian") == 0 ? russian : strcmp(nation, "british") == 0 ? british : american;
    int i;
    for (i = 0; weapons[i]; ++i) {
        char key[96], short_name[64]; const char* allow;
        snprintf(short_name, sizeof(short_name), "%s", weapons[i]);
        if (strlen(short_name) > 3) short_name[strlen(short_name)-3] = '\0';
        if (strcmp(short_name, "kar98k_sniper") == 0) snprintf(short_name, sizeof(short_name), "kar98ksniper");
        if (strcmp(short_name, "mosin_nagant") == 0) snprintf(short_name, sizeof(short_name), "mosinnagant");
        if (strcmp(short_name, "mosin_nagant_sniper") == 0) snprintf(short_name, sizeof(short_name), "mosinnagantsniper");
        if(!strcmp(short_name,"springfield")&&!strcmp(nation,"british"))strcpy(short_name,"enfieldsniper");
        snprintf(key, sizeof(key), "scr_allow_%s", short_name);
        allow = g->imp && g->imp->cvar_string ? g->imp->cvar_string(g->eng, key) : NULL;
        if (allow && allow[0] && atoi(allow) == 0 && strcmp(short_name,"fg42") && strcmp(short_name,"panzerfaust")) continue;
        on_precache(g, GSC_PC_WEAPON, weapons[i]);
        precache_view_weapon(g,weapons[i],nation);

    }
    {
        static const char* pickups[]={"kar98k_mp","mp40_mp","mp44_mp","kar98k_sniper_mp",
            "m1garand_mp","m1carbine_mp","thompson_mp","bar_mp","springfield_mp",
            "mosin_nagant_mp","ppsh_mp","mosin_nagant_sniper_mp","enfield_mp","sten_mp","bren_mp",
            "luger_mp","colt_mp","stielhandgranate_mp","fraggrenade_mp","rgd-33russianfrag_mp",
            "mk1britishfrag_mp","panzerfaust_mp","fg42_mp"};
        static const unsigned nations[]={8,8,8,8,1,1,1,1,3,4,4,4,2,2,2,12,3,8,1,4,2,15,15};
        const unsigned map_nations=nation_asset_mask(g->allies[0]?g->allies:"american") |
                                  nation_asset_mask(g->axis[0]?g->axis:"german");
        unsigned w;
        for(w=0;w<sizeof(pickups)/sizeof(pickups[0]);++w){
            int placed=0,j;
            for(j=0;j<g->placed_count && !placed;++j)
                placed=!strcmp(host_key(g,g->placed[j].id,"weapon"),pickups[w]);
            if(!(nations[w]&map_nations) && !placed)continue;
            on_precache(g,GSC_PC_WEAPON,pickups[w]);
            precache_view_weapon(g,pickups[w],nation);
        }
    }

}

static int host_read(void* user, const char* path, char** text, size_t* size) {
    CodGame* g = (CodGame*)user;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char virt[256];
    if (!g || !g->imp || !g->imp->fs_read || !path || !text || !size) return 0;
    snprintf(virt, sizeof(virt), "rawgsc/%s", path);
    if (!g->imp->fs_read(g->eng, virt, &buf, &sz) && !g->imp->fs_read(g->eng, path, &buf, &sz)) return 0;
    *text = (char*)malloc((size_t)sz + 1);
    if (!*text) {
        if (g->imp->fs_free) g->imp->fs_free(g->eng, buf);
        return 0;
    }
    memcpy(*text, buf, sz);
    (*text)[sz] = '\0';
    *size = sz;
    if (g->imp->fs_free) g->imp->fs_free(g->eng, buf);
    return 1;
}

static const char* host_cvar(void* user,const char* name) {
    CodGame* g=(CodGame*)user;
    return g->imp && g->imp->cvar_string ? g->imp->cvar_string(g->eng,name):"";
}
static int compare_id(const void* a,const void* b) {
    int x=*(const int*)a,y=*(const int*)b;return (x>y)-(x<y);
}
static int host_find(void* user,const char* key,const char* value,int* ids,int cap) {
    CodGame* g=(CodGame*)user;int count=0,n,i,tmp[4096],matches[4096];
    if(!g->imp || g->imp->api_version<4 || !g->imp->entity_find || !key || !value || cap<0 || (cap && !ids))return 0;
    if(!strcmp(key,"classname") && !strcmp(value,"player")) {
        for(i=0;i<COD_CLIENTS && count<cap;i++)if(g->players[i].active)ids[count++]=-3-i;return count;
    }
    if(strcmp(key,"classname")!=0 || !g->imp->entity_key)return g->imp->entity_find(g->eng,key,value,ids,cap);
    n=g->imp->entity_find(g->eng,"cod_classname",value,tmp,4096);
    for(i=0;i<n && count<4096;i++)matches[count++]=tmp[i];
    n=g->imp->entity_find(g->eng,"classname",value,tmp,4096);
    for(i=0;i<n && count<4096;i++) {
        const char* original=g->imp->entity_key?g->imp->entity_key(g->eng,tmp[i],"cod_classname"):"";
        if(!original || !original[0])matches[count++]=tmp[i];
    }
    if(count>1)qsort(matches,(size_t)count,sizeof(int),compare_id);
    if(count>cap)count=cap;
    for(i=0;i<count;i++)ids[i]=matches[i];
    return count;
}
static const char* host_key(void* user,int id,const char* key) {
    CodGame* g=(CodGame*)user;const char* original;
    if(id<=-3 && -3-id<COD_CLIENTS && g->players[-3-id].active && !strcmp(key,"classname"))return "player";
    if(!g->imp || g->imp->api_version<4 || !g->imp->entity_key)return "";
    if(strcmp(key,"classname")==0) {
        original=g->imp->entity_key(g->eng,id,"cod_classname");if(original && original[0])return original;
    }
    if(strcmp(key,"origin")==0) {
        const char* placed=g->imp->entity_key(g->eng,id,"spawn_placed");
        float feet[3];
        if(placed && atoi(placed) && g->imp->api_version>=5 && g->imp->spawn_anchor_origin && g->imp->spawn_anchor_origin(g->eng,id,NULL,feet)) {
            snprintf(g->origin_text,sizeof(g->origin_text),"%.9g %.9g %.9g",feet[0],feet[1],feet[2]);return g->origin_text;
        }
        original=g->imp->entity_key(g->eng,id,"cod_origin");if(original && original[0])return original;
        if(g->imp->api_version>=5 && g->imp->spawn_anchor_origin && g->imp->spawn_anchor_origin(g->eng,id,NULL,feet)) {
            snprintf(g->origin_text,sizeof(g->origin_text),"%.9g %.9g %.9g",feet[0],feet[1],feet[2]);return g->origin_text;
        }
    }
    return g->imp->entity_key(g->eng,id,key);
}
static int host_place_spawnpoint(void* user,int id) {
    CodGame* g=(CodGame*)user;float start[3],end[3],feet[3],f=1,nx=0,ny=0,nz=0;int i,solid;
    const char* raw;
    if(!g->imp || g->imp->api_version<5 || !g->imp->spawn_anchor_origin || !g->imp->trace)return 0;
    raw=host_key(g,id,"origin");
    if(!raw || sscanf(raw,"%f %f %f",&start[0],&start[1],&start[2])!=3)return 0;
    for(i=0;i<3;i++)if(!isfinite(start[i]))return 0;
    for(i=0;i<3;i++)end[i]=start[i];
    end[2]+=128.f;
    /* CoD spawn positions are feet; the native hull is 30 wide and 70 high.
     * The generic trace API currently performs a box sweep, not a capsule. */
    g->imp->trace(g->eng,start[0],start[1],start[2],end[0],end[1],end[2],-15,-15,0,15,15,70,19,&f,&nx,&ny,&nz);
    if(!isfinite(f) || f<0 || f>1)return 0;
    for(i=0;i<3;i++)start[i]+=(end[i]-start[i])*f;
    for(i=0;i<3;i++)end[i]=start[i];
    end[2]-=262144.f;
    g->imp->trace(g->eng,start[0],start[1],start[2],end[0],end[1],end[2],-15,-15,0,15,15,70,19,&f,&nx,&ny,&nz);
    if(!isfinite(f) || f<0 || f>=1 || nz<0.55f)return 0;
    for(i=0;i<3;i++)feet[i]=start[i]+(end[i]-start[i])*f;
    solid=g->imp->trace(g->eng,feet[0],feet[1],feet[2],feet[0],feet[1],feet[2],-15,-15,0,15,15,70,19,&f,&nx,&ny,&nz);
    if(solid)return 0; // Leave the previous source unchanged when placement is unsafe.
    return g->imp->spawn_anchor_origin(g->eng,id,feet,NULL);
}
static int host_state(void* user,int id,int visible,int linked) {
    CodGame* g=(CodGame*)user;
    return g->imp && g->imp->api_version>=4 && g->imp->entity_state ? g->imp->entity_state(g->eng,id,visible,linked):0;
}

static void placed_weapons(CodGame* g,int prepare) {
    int ids[4096],n,i;
    if(prepare){
        g->placed_count=0;g->placed_poll=-1;n=host_find(g,"","",ids,4096);
        for(i=0;i<n && g->placed_count<256;i++){
            const char* cls=host_key(g,ids[i],"classname");int slot;
            if(strncmp(cls,"mpweapon_",9))continue;
            slot=g->placed_count++;g->placed[slot].id=ids[i];g->placed[slot].visible=-1;
            snprintf(g->placed[slot].allow,96,"scr_allow_%s",cls+9);
            on_precache(g,GSC_PC_MODEL,host_key(g,ids[i],"model"));
            on_precache(g,GSC_PC_WEAPON,host_key(g,ids[i],"weapon"));
        }
    }
    for(i=0;i<g->placed_count;i++){
        const char* value=host_cvar(g,g->placed[i].allow);
        int allowed=value&&*value?atoi(value)!=0:strcmp(g->placed[i].allow,"scr_allow_fg42")!=0;
        if(allowed!=g->placed[i].visible){host_state(g,g->placed[i].id,allowed,allowed);g->placed[i].visible=allowed;}
    }
}
static void host_error(void* user,const char* message) {
    CodGame* g=(CodGame*)user;
    if(g->imp && g->imp->print)g->imp->print(g->eng,message);
}

static int find_or_add_trigger(CodGame* g,int id,int is_use) {
    int i;const char* model;
    for(i=0;i<g->trigger_count;i++)if(g->triggers[i].id==id)return i;
    if(g->trigger_count>=256)return -1;
    i=g->trigger_count++;
    memset(&g->triggers[i],0,sizeof(g->triggers[i]));
    g->triggers[i].id=id;
    g->triggers[i].is_use=is_use;
    model=host_key(g,id,"model");
    g->triggers[i].model=(model && model[0]=='*')?atoi(model+1):0;
    return i;
}
static void triggers_init(CodGame* g) {
    static const char* classes[]={"trigger_multiple","trigger_use","trigger_use_touch","trigger_lookat"};
    int ids[256],c,n,i;
    g->trigger_count=0;g->trig_use_available=0;g->trig_use_text[0]=0;
    memset(g->trig_use_buttons,0,sizeof(g->trig_use_buttons));
    if(g->imp && g->imp->cvar_set){
        g->imp->cvar_set(g->eng,"ui_trigger_use_available","0");
        g->imp->cvar_set(g->eng,"ui_trigger_use_text","");
    }
    for(c=0;c<4;c++){
        n=host_find(g,"classname",classes[c],ids,256);
        for(i=0;i<n;i++)find_or_add_trigger(g,ids[i],c==3?2:c>0);
    }
}
static int entity_player_touching(CodGame* g,int ent_id,int model_id,const hxSGPlayerView* v) {
    hxSGTrace hit;
    float mins[3]={-15.f,-15.f,-24.f},maxs[3]={15.f,15.f,32.f};
    const char* linked=host_key(g,ent_id,"linked");
    if(linked && *linked && !atoi(linked))return 0;
    if(model_id>0 && g->imp && g->imp->api_version>=8 && g->imp->trace_query) {
        memset(&hit,0,sizeof(hit));
        if(g->imp->trace_query(g->eng,v->origin,v->origin,mins,maxs,4|1,-1024-model_id,&hit))
            return hit.startsolid || hit.allsolid || hit.fraction<1.f;
    }
    return 0;
}
static int entity_player_looking(CodGame* g,int entity,int model,int client,const hxSGPlayerView* view) {
    float end[3],zero[3]={0,0,0};hxSGTrace target,wall;
    float pitch=view->pitch*.01745329252f,yaw=view->yaw*.01745329252f;
    const char* linked=host_key(g,entity,"linked");
    if(model<=0 || atoi(host_key(g,entity,"_hx_deleted")) || (linked && *linked && !atoi(linked)) ||
       g->imp->api_version<8 || !g->imp->trace_query)return 0;
    end[0]=view->eye[0]+8192*cosf(pitch)*cosf(yaw);
    end[1]=view->eye[1]+8192*cosf(pitch)*sinf(yaw);
    end[2]=view->eye[2]-8192*sinf(pitch);
    if(!g->imp->trace_query(g->eng,view->eye,end,zero,zero,4|1,-1024-model,&target) ||
       (!target.startsolid && target.fraction>=1))return 0;
    if(!g->imp->trace_query(g->eng,view->eye,end,zero,zero,1,-3-client,&wall))return 0;
    return target.startsolid || wall.fraction+.0001f>=target.fraction;
}
static void triggers_frame(CodGame* g) {
    int i,c;
    if(!g->runtime || !g->imp || g->imp->api_version<6 || !g->imp->player_view)return;
    for(i=0;i<g->trigger_count;i++) {
        if(g->triggers[i].is_use==1 || g->triggers[i].model<=0)continue;
        for(c=0;c<COD_CLIENTS;c++) {
            hxSGPlayerView v;
            if(!g->players[c].active || g->players[c].team<0)continue;
            if(!g->imp->player_view(g->eng,c,&v) || v.health<=0)continue;
            if(g->triggers[i].is_use==2 ? entity_player_looking(g,g->triggers[i].id,g->triggers[i].model,c,&v) :
               entity_player_touching(g,g->triggers[i].id,g->triggers[i].model,&v)) {
                GscArgument actor;memset(&actor,0,sizeof(actor));
                actor.kind=4;actor.integer=-3-c;
                gsc_runtime_notify(g->runtime,g->triggers[i].id,"trigger",&actor,1);
            }
        }
    }
}
static void trigger_use_think(CodGame* g,int client,int buttons) {
    hxSGPlayerView v;int pressed,i,found=-1;
    if(client<0 || client>=COD_CLIENTS || !g->imp || g->imp->api_version<6 || !g->imp->player_view)return;
    pressed=buttons&~g->trig_use_buttons[client];g->trig_use_buttons[client]=buttons;
    if(client==0){g->trig_use_available=0;g->trig_use_text[0]=0;}
    if(g->players[client].team<0 || !g->imp->player_view(g->eng,client,&v) || v.health<=0 || v.locked){
        if(client==0 && g->imp->cvar_set){g->imp->cvar_set(g->eng,"ui_trigger_use_available","0");g->imp->cvar_set(g->eng,"ui_trigger_use_text","");}
        return;
    }
    for(i=0;i<g->trigger_count;i++){
        float ox,oy,oz;const char* raw;
        if(g->triggers[i].is_use!=1)continue;
        if(entity_player_touching(g,g->triggers[i].id,g->triggers[i].model,&v)){found=i;break;}
        raw=host_key(g,g->triggers[i].id,"origin");
        if(raw && sscanf(raw,"%f %f %f",&ox,&oy,&oz)==3){
            float dx=ox-v.origin[0],dy=oy-v.origin[1],dz=oz-v.origin[2];
            if(dx*dx+dy*dy+dz*dz<=96.f*96.f){found=i;break;}
        }
    }
    if(found>=0){
        if(client==0){
            g->trig_use_available=1;
            snprintf(g->trig_use_text,sizeof(g->trig_use_text),"%s",g->triggers[found].hint);
            if(g->imp->cvar_set){
                g->imp->cvar_set(g->eng,"ui_trigger_use_available","1");
                g->imp->cvar_set(g->eng,"ui_trigger_use_text",g->trig_use_text);
            }
        }
        if(pressed&8){
            GscArgument actor;memset(&actor,0,sizeof(actor));
            actor.kind=4;actor.integer=-3-client;
            gsc_runtime_notify(g->runtime,g->triggers[found].id,"trigger",&actor,1);
        }
    }else if(client==0 && g->imp->cvar_set){
        g->imp->cvar_set(g->eng,"ui_trigger_use_available","0");
        g->imp->cvar_set(g->eng,"ui_trigger_use_text","");
    }
}


static float turret_angle(float x) { x=fmodf(x+180.f,360.f);if(x<0)x+=360.f;return x-180.f; }
static float turret_clamp(float x,float lo,float hi) { return x<lo?lo:x>hi?hi:x; }
static float turret_number(const char* data,const char* key,float fallback) {
    char needle[80];const char* p;char* end;float value;
    snprintf(needle,sizeof(needle),"\\%s\\",key);p=strstr(data,needle);
    if(!p)return fallback;
    value=strtof(p+strlen(needle),&end);
    return end!=p+strlen(needle) && (*end=='\\' || !*end) && isfinite(value)?value:fallback;
}
static void turret_init(CodGame* g) {
    int ids[COD_TURRETS],i;CodTurrets* ts=&g->turrets;
    g->hint_available=0;g->last_hint=-1;
    if(g->imp && g->imp->cvar_set){g->imp->cvar_set(g->eng,"ui_default_use_key","F");g->imp->cvar_set(g->eng,"ui_default_crouch_key","C");g->imp->cvar_set(g->eng,"ui_default_prone_key","CTRL");g->imp->cvar_set(g->eng,"ui_default_melee_key","SHIFT");g->imp->cvar_set(g->eng,"ui_use_available","0");g->imp->cvar_set(g->eng,"ui_turret_use_available","0");g->imp->cvar_set(g->eng,"ui_turret_use_text","use MG42");g->imp->cvar_set(g->eng,"ui_use_icon","gfx/icons/hint_usable.dds");}
    memset(ts,0,sizeof(*ts));for(i=0;i<COD_CLIENTS;i++)ts->clients[i].mounted=-1;
    if(!g->imp || g->imp->api_version<6 || !g->imp->player_view || !g->imp->player_view_lock || !g->imp->entity_find)return;
    ts->count=host_find(g,"classname","misc_mg42",ids,COD_TURRETS);
    for(i=0;i<ts->count;i++) {
        CodTurret* t=&ts->guns[i];const char* value;char path[160];unsigned char* data=NULL;unsigned int size=0;
        t->id=ids[i];t->owner=-1;t->damage=60;t->delay=50;t->left=t->right=45;t->top=t->bottom=40;t->spacing=46;
        value=host_key(g,t->id,"origin");if(sscanf(value,"%f %f %f",&t->origin[0],&t->origin[1],&t->origin[2])!=3){t->owner=-2;continue;}
        value=host_key(g,t->id,"angles");sscanf(value,"%f %f",&t->pitch,&t->yaw);
        value=host_key(g,t->id,"weaponinfo");snprintf(path,sizeof(path),"weapons/mp/%s",value&&*value?value:"mg42_bipod_stand_mp");
        if(g->imp->fs_read && g->imp->fs_read(g->eng,path,&data,&size)) {
            char* text=(char*)malloc((size_t)size+1);
            if(text) {memcpy(text,data,size);text[size]=0;
                t->left=turret_clamp(turret_number(text,"leftArc",45),0,180);t->right=turret_clamp(turret_number(text,"rightArc",45),0,180);
                t->top=turret_clamp(turret_number(text,"topArc",40),0,89);t->bottom=turret_clamp(turret_number(text,"bottomArc",40),0,89);
                t->spacing=turret_clamp(turret_number(text,"playerPositionDist",46),16,96);
                t->damage=(int)turret_clamp(turret_number(text,"damage",60),1,1000);
                t->delay=(int)turret_clamp(turret_number(text,"fireTime",.05f)*1000,10,5000);free(text);
            }if(g->imp->fs_free)g->imp->fs_free(g->eng,data);
        }

        {const char* model=host_key(g,t->id,"model");char tags[256];unsigned char* raw=NULL;unsigned int bytes=0;
         t->aim[2]=13;memcpy(t->butt,t->aim,sizeof(t->aim));t->butt[0]-=46;memcpy(t->flash,t->aim,sizeof(t->aim));t->flash[0]+=11;
         if(model && *model){snprintf(tags,sizeof(tags),"%s.hxtags",model);
          if(g->imp->fs_read(g->eng,tags,&raw,&bytes)) {char* txt=(char*)malloc(bytes+1);if(txt){char* line;memcpy(txt,raw,bytes);txt[bytes]=0;
           line=strstr(txt,"tag_aim ");if(line)sscanf(line+8,"%f %f %f",&t->aim[0],&t->aim[1],&t->aim[2]);
           line=strstr(txt,"tag_butt ");if(line)sscanf(line+9,"%f %f %f",&t->butt[0],&t->butt[1],&t->butt[2]);free(txt);}g->imp->fs_free(g->eng,raw);}
         }
        }

        {float rad=t->yaw*.01745329252f,start[3],end[3],f=1,nx,ny,nz;int step;
         start[0]=t->origin[0]+cosf(rad)*t->aim[0]-sinf(rad)*t->aim[1];start[1]=t->origin[1]+sinf(rad)*t->aim[0]+cosf(rad)*t->aim[1];start[2]=t->origin[2]+t->aim[2];t->rest=-90;
         if(g->imp->trace)for(step=0;step<=30;step++){float pitch=-3.f*step*.01745329252f,dx=t->butt[0]-t->aim[0],dy=t->butt[1]-t->aim[1],dz=t->butt[2]-t->aim[2];float x=cosf(pitch)*dx+sinf(pitch)*dz,z=-sinf(pitch)*dx+cosf(pitch)*dz;
           end[0]=start[0]+cosf(rad)*x-sinf(rad)*dy;end[1]=start[1]+sinf(rad)*x+cosf(rad)*dy;end[2]=start[2]+z;
           g->imp->trace(g->eng,start[0],start[1],start[2],end[0],end[1],end[2],0,0,0,0,0,0,17,&f,&nx,&ny,&nz);
           if(f<1){t->rest=-3.f*step;break;}
         }
         if(g->imp->api_version>=7 && g->imp->model_joint_angles)g->imp->model_joint_angles(g->eng,t->id,"mg01",t->rest,0);
        }
        on_precache(g,GSC_PC_WEAPON,path);
        value=host_key(g,t->id,"model");if(value&&*value)on_precache(g,GSC_PC_MODEL,value);
    }
    if(g->imp->cvar_set){char n[24];snprintf(n,sizeof(n),"%d",ts->count);g->imp->cvar_set(g->eng,"g_mg42_count",n);g->imp->cvar_set(g->eng,"ui_mg42_mounted","0");}
}
static void turret_release(CodGame* g,int client) {
    CodTurretClient* c;CodTurret* t;
    if(client<0 || client>=COD_CLIENTS)return;
    c=&g->turrets.clients[client];if(c->mounted<0)return;
    if(client==0)g->hint_available=0;
    t=&g->turrets.guns[c->mounted];t->owner=-1;c->mounted=-1;c->next_fire=0;
    if(g->imp && g->imp->api_version>=6 && g->imp->player_view_lock)g->imp->player_view_lock(g->eng,client,NULL,0,0);
    if(g->imp && g->imp->api_version>=7 && g->imp->model_joint_angles)g->imp->model_joint_angles(g->eng,t->id,"mg01",t->rest,0);
    gsc_runtime_notify(g->runtime,t->id,"deactivated",NULL,0);
    if(client==0 && g->imp && g->imp->cvar_set)g->imp->cvar_set(g->eng,"ui_mg42_mounted","0");
}
static void turret_think(CodGame* g,int client,int buttons,float pitch,float yaw) {
    CodTurretClient* c;hxSGPlayerView v;CodTurret* t=NULL;int pressed,i,time;
    if(client<0 || client>=COD_CLIENTS || !g->imp || g->imp->api_version<6 || !g->imp->player_view || !g->imp->player_view_lock)return;
    if(client==0)g->hint_available=0;
    c=&g->turrets.clients[client];pressed=buttons&~c->buttons;c->buttons=buttons;
    if(!g->imp->player_view(g->eng,client,&v) || v.health<=0){turret_release(g,client);return;}
    if(c->mounted>=0) {
        if(!v.locked){turret_release(g,client);return;}
        if(pressed&8){turret_release(g,client);return;}
        t=&g->turrets.guns[c->mounted];
    } else if(v.grounded && g->imp->trace) {
        float best=96.f*96.f;
        for(i=0;i<g->turrets.count;i++) {
            CodTurret* candidate=&g->turrets.guns[i];float dx=candidate->origin[0]-v.origin[0],dy=candidate->origin[1]-v.origin[1],dz=candidate->origin[2]-v.eye[2];
            float d=dx*dx+dy*dy+dz*dz,f=1,nx,ny,nz,rad=candidate->yaw*.01745329252f;
            float target[3]={candidate->origin[0]+cosf(rad)*candidate->aim[0]-sinf(rad)*candidate->aim[1],candidate->origin[1]+sinf(rad)*candidate->aim[0]+cosf(rad)*candidate->aim[1],candidate->origin[2]+candidate->aim[2]};
            const char* visible=host_key(g,candidate->id,"visible");
            if(candidate->owner!=-1 || (*visible && !atoi(visible)) || d>=best || dx*cosf(rad)+dy*sinf(rad)<=0)continue;
            /* Interact from behind and while looking toward the gun, not through walls. */
            if(dx*cosf(yaw*.01745329252f)+dy*sinf(yaw*.01745329252f)<.5f*sqrtf(dx*dx+dy*dy))continue;
            g->imp->trace(g->eng,v.eye[0],v.eye[1],v.eye[2],target[0],target[1],target[2],0,0,0,0,0,0,17,&f,&nx,&ny,&nz);
            if(f<.999f)continue;
            t=candidate;best=d;
        }
        if(t) {
            float rad=t->yaw*.01745329252f,end[3],f=1,nx,ny,nz;int solid;
            c->anchor[0]=t->origin[0]-cosf(rad)*t->spacing;c->anchor[1]=t->origin[1]-sinf(rad)*t->spacing;c->anchor[2]=v.origin[2];
            solid=g->imp->trace(g->eng,v.origin[0],v.origin[1],v.origin[2],c->anchor[0],c->anchor[1],c->anchor[2],-15,-15,-24,15,15,32,17,&f,&nx,&ny,&nz);
            if(solid || f<.999f)return;
            memcpy(end,c->anchor,sizeof(end));end[2]-=48;
            solid=g->imp->trace(g->eng,c->anchor[0],c->anchor[1],c->anchor[2],end[0],end[1],end[2],-15,-15,-24,15,15,32,17,&f,&nx,&ny,&nz);
            if(solid || f>=1 || nz<.55f)return;
            c->anchor[2]+=(end[2]-c->anchor[2])*f;
            if(client==0)g->hint_available=1;
            if(!(pressed&8))return;
            c->mounted=(int)(t-g->turrets.guns);t->owner=client;
            if(client==0 && g->imp && g->imp->cvar_set)g->imp->cvar_set(g->eng,"ui_mg42_mounted","1");
            {GscArgument actor;memset(&actor,0,sizeof(actor));actor.kind=4;actor.integer=-3-client;gsc_runtime_notify(g->runtime,t->id,"activated",&actor,1);}
        }
    }
    if(!t)return;
    if(client==0)g->hint_available=1;
    pitch=t->pitch+turret_clamp(turret_angle(pitch-t->pitch),-t->top,t->bottom);
    yaw=t->yaw+turret_clamp(turret_angle(yaw-t->yaw),-t->left,t->right);
    if(g->imp->api_version>=7 && g->imp->model_joint_angles)g->imp->model_joint_angles(g->eng,t->id,"mg01",pitch-t->pitch,turret_angle(yaw-t->yaw));
    if(!g->imp->player_view_lock(g->eng,client,c->anchor,pitch,yaw)){turret_release(g,client);return;}
    time=g->imp->level_time_ms?g->imp->level_time_ms(g->eng):0;
    if((buttons&1) && time>=c->next_fire && g->imp->player_hitscan) {
        float eye[3];for(i=0;i<3;i++)eye[i]=c->anchor[i]+v.eye[i]-v.origin[i];
        /* Camera ray fallback until animated native muzzle tags are available. */
        if(g->imp->player_hitscan(g->eng,client,eye,pitch,yaw,t->damage,8192)) {
            c->next_fire=time+t->delay;gsc_runtime_notify(g->runtime,t->id,"fire",NULL,0);
        }
    }
}

static int native_name(const char* a,const char* b) {
    while(*a&&*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;}
    return *a==*b;
}
static int scene_vec(const GscNativeValue *value, float out[3]) {
    int i;
    if (value->kind != GSC_NATIVE_VECTOR)
        return 0;
    for (i = 0; i < 3; i++) {
        if (!isfinite(value->vector[i]) || fabs(value->vector[i]) > 1e7)
            return 0;
        out[i] = (float)value->vector[i];
    }
    return 1;
}
static float scene_fx_number(const char *text, const char *key, float fallback) {
    const char *p = text;
    size_t n = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        if ((p == text || isspace((unsigned char)p[-1])) && isspace((unsigned char)p[n])) {
            char *end;
            float f = strtof(p + n, &end);
            if (end != p + n && isfinite(f))
                return f;
        }
        p += n;
    }
    return fallback;
}
static int scene_fx_path(const char *name, char path[256]) {
    size_t n = strlen(name);
    int suffix = n >= 4 && !strcmp(name + n - 4, ".efx");
    if (strstr(name, "..") || strchr(name, ':'))
        return 0;
    return snprintf(path, 256, "%s%s%s", strncmp(name, "fx/", 3) ? "fx/" : "", name,
                    suffix ? "" : ".efx") < 256;
}
static void scene_fx_prepare(CodGame *g, const char *name) {
    char path[256], *text = NULL, *p;
    size_t size;
    int id;
    if (!scene_fx_path(name, path))
        return;
    for (id = 0; id < 64; id++)
        if (g->effects[id].active && !strcmp(g->effects[id].name, path))
            return;
    if (g->clock_started) {
        host_error(g, "GSC FX must be precached before gameplay");
        return;
    }
    for (id = 0; id < 64; id++)
        if (!g->effects[id].active)
            break;
    if (id == 64 || !host_read(g, path, &text, &size) || size > 1024 * 1024) {
        free(text);
        host_error(g, "GSC FX precache failed");
        return;
    }
    {
        CodFx *fx = &g->effects[id];
        memset(fx, 0, sizeof(*fx));
        strcpy(fx->name, path);
        p = text;
        while ((p = strstr(p, "Particle")) != NULL) {
            char *begin = strchr(p, '{');
            char *end;
            char *shaders;
            char *sizeblock;
            int depth = 0, part;
            if (!begin)
                break;
            for (end = begin; *end; end++) {
                if (*end == '{')
                    depth++;
                else if (*end == '}' && !--depth)
                    break;
            }
            if (!*end)
                break;
            *end = 0;
            if (fx->count == 16) {
                fx->count = 0;
                break;
            }
            part = fx->count;
            shaders = strstr(begin, "shaders");
            sizeblock = strstr(begin, "size");
            if (shaders && (shaders = strchr(shaders, '[')) != NULL) {
                char *q = shaders + 1;
                size_t n = 0;
                while (isspace((unsigned char)*q))
                    q++;
                if (*q == '"')
                    q++;
                while (q[n] && !isspace((unsigned char)q[n]) && q[n] != ']' && q[n] != '"' &&
                       n < 191)
                    n++;
                if (n) {
                    hxSGParticle *def = &fx->parts[part];
                    memcpy(fx->shaders[part], q, n);
                    fx->shaders[part][n] = 0;
                    def->material = fx->shaders[part];
                    def->count = (int)scene_fx_number(begin, "count", 1);
                    def->lifetime = scene_fx_number(begin, "life", 300) * .001f;
                    def->speed = scene_fx_number(begin, "velocity", 40);
                    def->gravity = scene_fx_number(begin, "gravity", 0);
                    def->start_size = sizeblock ? scene_fx_number(sizeblock, "start", 10) : 10;
                    def->end_size = sizeblock ? scene_fx_number(sizeblock, "end", 40) : 40;
                    if (def->count < 1 || def->count > 256 || def->lifetime <= 0 ||
                        def->lifetime > 600) {
                        fx->count = 0;
                        break;
                    }
                    on_precache(g, GSC_PC_SHADER, def->material);
                    fx->count++;
                }
            }
            p = end + 1;
        }
        fx->active = fx->count > 0;
        if (!fx->active)
            host_error(g, "GSC FX has no supported particle elements");
    }
    free(text);
}
static int scene_fx_native(CodGame *g, const char *name, int receiver, const GscNativeValue *args,
                           int argc) {
    char path[256];
    float origin[3], forward[3] = {0, 0, 1};
    int i;
    if (receiver || (!native_name(name, "playFx") && !native_name(name, "playFxOnTag")))
        return 0;
    if (native_name(name, "playFxOnTag")) {
        int ent_id;
        if (argc != 3 || (args[0].kind != GSC_NATIVE_STRING && args[0].kind != GSC_NATIVE_INT) ||
            args[1].kind != GSC_NATIVE_ENTITY || args[2].kind != GSC_NATIVE_STRING ||
            !g->imp->particle_emit)
            return -1;
        if (args[0].kind == GSC_NATIVE_STRING) {
            if (!scene_fx_path(args[0].text, path)) return -1;
        } else {
            path[0] = 0;
        }
        ent_id = args[1].integer;
        if (ent_id <= -3) {
            hxSGPlayerView view;
            if (!g->imp->player_view || !g->imp->player_view(g->eng, -3 - ent_id, &view)) return -1;
            memcpy(origin, view.origin, sizeof(origin));
        } else if (ent_id >= 0) {
            const char* raw;
            if (!g->imp->entity_transform || !g->imp->entity_transform(g->eng, ent_id, NULL, NULL, origin, NULL)) {
                raw = host_key(g, ent_id, "origin");
                if (!raw || sscanf(raw, "%f %f %f", &origin[0], &origin[1], &origin[2]) != 3) return -1;
            }
        } else return -1;
        for (i = 0; i < 64; i++)
            if (g->effects[i].active && ((path[0] && !strcmp(g->effects[i].name, path)) || (args[0].kind == GSC_NATIVE_INT && i == args[0].integer)))
                return g->imp->particle_emit(g->eng, origin, forward, g->effects[i].parts,
                                             g->effects[i].count)
                           ? 1
                           : -1;
        return 1;
    }
    if (argc < 2 || argc > 4 || args[0].kind != GSC_NATIVE_STRING ||
        !scene_fx_path(args[0].text, path) || !scene_vec(args + 1, origin) ||
        !g->imp->particle_emit)
        return -1;
    if (argc >= 3 && !scene_vec(args + 2, forward))
        return -1;
    if (argc == 4) {
        float up[3];
        if (!scene_vec(args + 3, up))
            return -1;
    }
    for (i = 0; i < 64; i++)
        if (g->effects[i].active && !strcmp(g->effects[i].name, path))
            return g->imp->particle_emit(g->eng, origin, forward, g->effects[i].parts,
                                         g->effects[i].count)
                       ? 1
                       : -1;
    return -1;
}

#define COD_HUD_BASE 1073741824
static float presentation_fraction(int now, int start, int end) {
    return end <= start || now >= end ? 1 : now <= start ? 0 : (float)(now - start) / (end - start);
}
static hxSGHud presentation_current(CodGame *g, CodHud *h) {
    hxSGHud value = h->value;
    float t;
    int i;
    if (h->fade_end) {
        t = presentation_fraction(g->scene_now, h->fade_start, h->fade_end);
        for (i = 0; i < 4; i++)
            value.color[i] = h->fade_from[i] + (value.color[i] - h->fade_from[i]) * t;
    }
    if (h->move_end) {
        t = presentation_fraction(g->scene_now, h->move_start, h->move_end);
        value.x = h->move_from[0] + (value.x - h->move_from[0]) * t;
        value.y = h->move_from[1] + (value.y - h->move_from[1]) * t;
    }
    if (h->scale_end) {
        t = presentation_fraction(g->scene_now, h->scale_start, h->scale_end);
        value.width = h->scale_from[0] + (value.width - h->scale_from[0]) * t;
        value.height = h->scale_from[1] + (value.height - h->scale_from[1]) * t;
    }
    return value;
}
static int presentation_sync(CodGame *g, int id) {
    if(g->hud[id].retired_owner)return 1;
    CodHud *h = &g->hud[id];
    hxSGHud value = presentation_current(g, h);
    value.text = h->text;
    value.shader = h->shader;
    return g->imp->hud_update && g->imp->hud_update(g->eng, id, h->active ? &value : NULL);
}
static int presentation_number(const GscNativeValue *v, double *n) {
    if (v->kind == GSC_NATIVE_INT)
        *n = v->integer;
    else if (v->kind == GSC_NATIVE_FLOAT)
        *n = v->number;
    else
        return 0;
    return isfinite(*n);
}
static int presentation_field(CodGame *g, int entity, const char *name, const GscNativeValue *value,
                              GscNativeValue *out) {
    int id, i;
    double number;
    CodHud *h;
    float *target = NULL;
    if (entity < COD_HUD_BASE)
        return 0;
    for (id = 0; id < 128; id++)
        if (g->hud[id].active && g->hud[id].handle == entity - COD_HUD_BASE)
            break;
    if (id == 128)
        return -1;
    h = &g->hud[id];
    if (native_name(name, "x"))
        target = &h->value.x;
    else if (native_name(name, "y"))
        target = &h->value.y;
    else if (native_name(name, "alpha"))
        target = &h->value.color[3];
    else if (native_name(name, "fontScale"))
        target = &h->value.scale;
    if (target) {
        if (value) {
            if (!presentation_number(value, &number))
                return -1;
            *target = (float)number;
            return presentation_sync(g, id) ? 1 : -1;
        }
        out->kind = GSC_NATIVE_FLOAT;
        out->number = *target;
        return 1;
    }
    if (native_name(name, "color")) {
        if (value) {
            if (value->kind != GSC_NATIVE_VECTOR)
                return -1;
            for (i = 0; i < 3; i++) {
                if (value->vector[i] < 0 || value->vector[i] > 1 || !isfinite(value->vector[i]))
                    return -1;
            }
            for (i = 0; i < 3; i++)
                h->value.color[i] = (float)value->vector[i];
            return presentation_sync(g, id) ? 1 : -1;
        }
        out->kind = GSC_NATIVE_VECTOR;
        for (i = 0; i < 3; i++)
            out->vector[i] = h->value.color[i];
        return 1;
    }
    if (native_name(name, "alignX") || native_name(name, "alignY") ||
        native_name(name, "horzAlign") || native_name(name, "vertAlign")) {
        int horizontal = native_name(name, "alignX") || native_name(name, "horzAlign");
        int *align = horizontal ? &h->value.align_x : &h->value.align_y;
        if (value) {
            const char *s;
            if (value->kind != GSC_NATIVE_STRING)
                return -1;
            s = value->text;
            if (native_name(s, "left") || native_name(s, "top"))
                *align = 0;
            else if (native_name(s, "center") || native_name(s, "middle"))
                *align = 1;
            else if (native_name(s, "right") || native_name(s, "bottom"))
                *align = 2;
            else
                return -1;
            return presentation_sync(g, id) ? 1 : -1;
        }
        out->kind = GSC_NATIVE_STRING;
        out->text = *align == 1   ? "center"
                    : *align == 2 ? (horizontal ? "right" : "bottom")
                                  : (horizontal ? "left" : "top");
        return 1;
    }
    return 0;
}
static int objective_sync(CodGame *g, int id) {
    CodObjective *o = &g->objectives[id];
    if(o->active && o->player>=0){
        hxSGPlayerView view;if(!g->imp->player_view || !g->imp->player_view(g->eng,o->player,&view))return 0;
        memcpy(o->value.position,view.origin,sizeof(o->value.position));
    }
    o->value.icon = o->icon;
    return g->imp->objective_update &&
           g->imp->objective_update(g->eng, id, o->active ? &o->value : NULL);
}
static int format_native_text(const GscNativeValue* args,int argc,char* out,size_t capacity){
    int i;size_t used=0;
    if(argc<1 || argc>16 || args[0].kind!=GSC_NATIVE_STRING)return 0;
    for(i=0;i<argc;++i){
        char number[64];const char* part;size_t length;
        if(args[i].kind==GSC_NATIVE_STRING)part=args[i].text;
        else if(args[i].kind==GSC_NATIVE_INT){snprintf(number,sizeof(number),"%d",args[i].integer);part=number;}
        else if(args[i].kind==GSC_NATIVE_FLOAT){snprintf(number,sizeof(number),"%g",args[i].number);part=number;}
        else return 0;
        if(!part)return 0;length=strlen(part);
        if(used+length+(i?1:0)>=capacity)return 0;
        if(i)out[used++]='\x1f';memcpy(out+used,part,length);used+=length;
    }
    out[used]=0;return 1;
}
static int presentation_native(CodGame *g, const char *name, int receiver, int entity,
                               const GscNativeValue *args, int argc, GscNativeValue *out) {
    int id, i;
    double number;
    if (g->imp->api_version < 8)
        return 0;
    if (!receiver && (native_name(name, "newHudElem") || native_name(name, "newClientHudElem") ||
                      native_name(name, "newTeamHudElem"))) {
        int client = -1, team = -1;
        if (!g->imp->hud_update)
            return -1;
        if (native_name(name, "newClientHudElem")) {
            if (argc != 1 || args[0].kind != GSC_NATIVE_ENTITY || args[0].integer > -3)
                return -1;
            client = -3 - args[0].integer;
            if (client >= COD_CLIENTS)
                return -1;
        } else if (native_name(name, "newTeamHudElem")) {
            if (argc != 1 || args[0].kind != GSC_NATIVE_STRING)
                return -1;
            team = native_name(args[0].text, "allies") ? 0
                   : native_name(args[0].text, "axis") ? 1
                                                       : -1;
            if (team < 0)
                return -1;
        } else if (argc)
            return -1;
        for (id = 0; id < 128; id++) {
            int reserved = (id == g->announcement_id), k;
            for (k = 0; k < COD_CLIENTS && !reserved; k++) if (id == g->client_announcement_id[k]) reserved = 1;
            if (!g->hud[id].active && !reserved)
                break;
        }
        if (id == 128)
            return -1;
        {
            CodHud *h = &g->hud[id];
            memset(h, 0, sizeof(*h));
            if (g->hud_next >= INT_MAX - COD_HUD_BASE)
                return -1;
            h->handle = ++g->hud_next;
            h->active = 1;
            h->value.client = client;
            h->value.team = team;
            h->value.scale = 1;
            for (i = 0; i < 4; i++)
                h->value.color[i] = 1;
            if (!presentation_sync(g, id))
                return -1;
        }
        out->kind = GSC_NATIVE_ENTITY;
        out->integer = COD_HUD_BASE + g->hud[id].handle;
        return 1;
    }
    if (receiver && entity >= COD_HUD_BASE) {
        CodHud *h;
        for (id = 0; id < 128; id++)
            if (g->hud[id].active && g->hud[id].handle == entity - COD_HUD_BASE)
                break;
        if (id == 128)
            return -1;
        h = &g->hud[id];
        if (native_name(name, "fadeOverTime") || native_name(name, "moveOverTime") ||
            native_name(name, "scaleOverTime")) {
            hxSGHud current = presentation_current(g, h);
            int duration;
            int scaling = native_name(name, "scaleOverTime");
            if (argc != (scaling ? 3 : 1) || !presentation_number(args, &number) || number < 0 ||
                number > 86400 || g->scene_now > INT_MAX - (int)(number * 1000))
                return -1;
            duration = (int)(number * 1000);
            if (native_name(name, "fadeOverTime")) {
                h->fade_start = g->scene_now;
                h->fade_end = g->scene_now + duration;
                for (i = 0; i < 4; i++)
                    h->fade_from[i] = current.color[i];
            } else if (scaling) {
                double width, height;
                if (!presentation_number(args + 1, &width) ||
                    !presentation_number(args + 2, &height) || width < 0 || height < 0)
                    return -1;
                h->scale_start = g->scene_now;
                h->scale_end = g->scene_now + duration;
                h->scale_from[0] = current.width;
                h->scale_from[1] = current.height;
                h->value.width = (float)width;
                h->value.height = (float)height;
            } else {
                h->move_start = g->scene_now;
                h->move_end = g->scene_now + duration;
                h->move_from[0] = current.x;
                h->move_from[1] = current.y;
            }
            return presentation_sync(g, id) ? 1 : -1;
        }
        if (native_name(name, "destroy")) {
            if (argc)
                return -1;
            h->active = 0;
            return presentation_sync(g, id) ? 1 : -1;
        }
        if (native_name(name, "setText")) {
            int i;size_t used=0;
            if(argc<1 || argc>16 || args[0].kind!=GSC_NATIVE_STRING)return -1;
            for(i=0;i<argc;i++){
                char number[64];const char* part;size_t length;
                if(args[i].kind==GSC_NATIVE_STRING)part=args[i].text;
                else if(args[i].kind==GSC_NATIVE_INT){snprintf(number,sizeof(number),"%d",args[i].integer);part=number;}
                else if(args[i].kind==GSC_NATIVE_FLOAT){snprintf(number,sizeof(number),"%g",args[i].number);part=number;}
                else return -1;
                length=strlen(part);if(used+length+(i?1:0)>=sizeof(h->text))return -1;
                if(i)h->text[used++]='\x1f';memcpy(h->text+used,part,length);used+=length;
            }
            h->text[used]=0;h->shader[0]=0;h->timer=0;
            return presentation_sync(g,id)?1:-1;
        }
        if (native_name(name, "setValue")) {
            if (argc != 1 || !presentation_number(args, &number))
                return -1;
            snprintf(h->text, sizeof(h->text), "%g", number);
            h->shader[0] = 0;
            h->timer = 0;
            return presentation_sync(g, id) ? 1 : -1;
        }
        if (native_name(name, "setShader")) {
            if (argc != 3 || args[0].kind != GSC_NATIVE_STRING ||
                strlen(args[0].text) >= sizeof(h->shader))
                return -1;
            strcpy(h->shader, args[0].text);
            if (!presentation_number(args + 1, &number))
                return -1;
            h->value.width = (float)number;
            if (!presentation_number(args + 2, &number))
                return -1;
            h->value.height = (float)number;
            h->timer = 0;
            return presentation_sync(g, id) ? 1 : -1;
        }
        if (native_name(name, "setTimer") || native_name(name, "setTimerUp") || native_name(name,"setTenthsTimer") || native_name(name,"setTenthsTimerUp")) {
            if (argc != 1 || !presentation_number(args, &number) || number < 0 || number > 86400)
                return -1;
            h->timer = native_name(name,"setTenthsTimer")?3:native_name(name,"setTenthsTimerUp")?4:native_name(name,"setTimer")?1:2;
            if (g->scene_now > INT_MAX - (int)(number * 1000))
                return -1;
            h->timer_end = g->scene_now + (h->timer==1 || h->timer==3 ? 1 : -1) * (int)(number * 1000);
            h->timer_last = -1;
            return 1;
        }
        return 0;
    }
    if (!receiver && !strncmp(name, "objective_", 10)) {
        CodObjective *o;
        if (argc < 1 || args[0].kind != GSC_NATIVE_INT || (id = args[0].integer) < 0 || id >= 32 ||
            !g->imp->objective_update)
            return -1;
        o = &g->objectives[id];
        if (native_name(name, "objective_add")) {
            if (argc != 2 && argc != 3 && argc != 4)
                return -1;
            if (args[1].kind != GSC_NATIVE_STRING)
                return -1;
            if (o->active)
                return -1;
            memset(o, 0, sizeof(*o));
            o->player=-1;
            o->active = 1;
            o->value.team = -1;
            o->value.entity = -1;
            if (native_name(args[1].text, "current"))
                o->value.state = 1;
            else if (native_name(args[1].text, "done"))
                o->value.state = 2;
            else if (!native_name(args[1].text, "invisible"))
                return -1;
            if (argc >= 3 && !scene_vec(args + 2, o->value.position))
                return -1;
            if (argc == 4) {
                if (args[3].kind != GSC_NATIVE_STRING || strlen(args[3].text) >= sizeof(o->icon))
                    return -1;
                strcpy(o->icon, args[3].text);
            }
        } else {
            /* HQ updates icon/team metadata after deleting the visible slot.
             * Preserve those properties without reactivating the objective. */
            if (!o->active && !native_name(name, "objective_delete") &&
                !native_name(name,"objective_icon") && !native_name(name,"objective_team"))
                return -1;
            if (native_name(name, "objective_delete")) {
                if (argc != 1)
                    return -1;
                o->active = 0;
            } else if (native_name(name, "objective_position")) {
                if (argc != 2 || !scene_vec(args + 1, o->value.position))
                    return -1;
                o->value.entity = -1;
                o->player=-1;
            } else if (native_name(name, "objective_icon")) {
                if (argc != 2 || args[1].kind != GSC_NATIVE_STRING ||
                    strlen(args[1].text) >= sizeof(o->icon))
                    return -1;
                strcpy(o->icon, args[1].text);
            } else if (native_name(name, "objective_onEntity")) {
                if (argc != 2 || args[1].kind != GSC_NATIVE_ENTITY || args[1].integer==-1 || args[1].integer==-2 || args[1].integer < -3-(COD_CLIENTS-1))
                    return -1;
                o->player=args[1].integer<=-3?-3-args[1].integer:-1;
                o->value.entity = o->player>=0?-1:args[1].integer;
            } else if (native_name(name, "objective_team")) {
                if (argc != 2 || args[1].kind != GSC_NATIVE_STRING)
                    return -1;
                o->value.team = native_name(args[1].text, "allies") ? 0
                                : native_name(args[1].text, "axis") ? 1
                                : native_name(args[1].text, "none") ? -1
                                                                    : -2;
                if (o->value.team == -2)
                    return -1;
            } else if (native_name(name, "objective_state")) {
                if (argc != 2 || args[1].kind != GSC_NATIVE_STRING)
                    return -1;
                o->value.state = native_name(args[1].text, "current")     ? 1
                                 : native_name(args[1].text, "done")      ? 2
                                 : native_name(args[1].text, "invisible") ? 0
                                                                          : -1;
                if (o->value.state < 0)
                    return -1;
            } else
                return 0;
        }
        return objective_sync(g, id) ? 1 : -1;
    }
    return 0;
}
static void presentation_tick(CodGame *g, int now) {
    int objective;
    for(objective=0;objective<32;++objective)if(g->objectives[objective].active && g->objectives[objective].player>=0)objective_sync(g,objective);
    int animated;
    for (animated = 0; animated < 128; animated++) {
        CodHud *h = &g->hud[animated];
        if (!h->active || (!h->fade_end && !h->move_end && !h->scale_end))
            continue;
        presentation_sync(g, animated);
        if (h->fade_end && now >= h->fade_end)
            h->fade_end = 0;
        if (h->move_end && now >= h->move_end)
            h->move_end = 0;
        if (h->scale_end && now >= h->scale_end)
            h->scale_end = 0;
    }

    int id;
    for (id = 0; id < 128; id++)
        if (g->hud[id].active && g->hud[id].timer) {
            CodHud *h = &g->hud[id];
            const int divisor=h->timer>=3?100:1000;
            const int delta=h->timer==1 || h->timer==3?h->timer_end-now:now-h->timer_end;
            int seconds=delta>0?(delta+(h->timer==1 || h->timer==3?divisor-1:0))/divisor:0;
            if (seconds != h->timer_last) {
                h->timer_last = seconds;
                if(h->timer>=3)snprintf(h->text,sizeof(h->text),"%d.%d",seconds/10,seconds%10);
                else snprintf(h->text, sizeof(h->text), "%d:%02d", seconds / 60, seconds % 60);
                presentation_sync(g, id);
            }
        }
}

static int scene_field(void *user, int entity, const char *name, const GscNativeValue *value,
                       GscNativeValue *out) {
    CodGame *g = (CodGame *)user;
    float v[3];
    int rotation = native_name(name, "angles"), i;
    {
        int status = presentation_field(g, entity, name, value, out);
        if (status)
            return status;
    }

    if(entity<=-3 && native_name(name,"archivetime")){
        float seconds=0;double number=0;const int client=-3-entity;
        if(!g->imp || g->imp->api_version<16 || !g->imp->player_archive || client<0 || client>=COD_CLIENTS)return -1;
        if(value){if(!presentation_number(value,&number) || number<0 || number>60)return -1;seconds=(float)number;}
        if(!g->imp->player_archive(g->eng,client,value?&seconds:NULL,&seconds))return -1;
        if(!value){out->kind=GSC_NATIVE_FLOAT;out->number=seconds;}return 1;
    }

    if(entity<=-3 && (native_name(name,"headicon") || native_name(name,"headiconteam") || native_name(name,"statusicon"))){
        const int client=-3-entity,team_field=native_name(name,"headiconteam"),head=native_name(name,"headicon");char* field;
        if(client<0 || client>=COD_CLIENTS)return -1;
        field=team_field?g->players[client].headiconteam:head?g->players[client].headicon:g->players[client].statusicon;
        if(value){
            int team=-1;
            if(value->kind!=GSC_NATIVE_STRING || !g->imp || g->imp->api_version<15 || !g->imp->player_icon)return -1;
            if(team_field){
                if(strlen(value->text)>=16)return -1;team=native_name(value->text,"allies")?0:native_name(value->text,"axis")?1:-1;
                if(team<0 && !native_name(value->text,"none") && value->text[0])return -1;
            }else if(strlen(value->text)>=256)return -1;
            if(!g->imp->player_icon(g->eng,client,team_field?2:head?0:1,team_field?NULL:value->text,team))return -1;
            strcpy(field,value->text);
        }else{out->kind=GSC_NATIVE_STRING;out->text=field;}
        return 1;
    }

    if(entity<=-3 && g->imp && g->imp->api_version>=12 && g->imp->player_state) {
        int client=-3-entity;hxSGPlayerState state={0};
        int property=native_name(name,"health")?1:native_name(name,"score")?2:native_name(name,"deaths")?3:native_name(name,"sessionteam")?4:native_name(name,"sessionstate")?5:native_name(name,"spectatorclient")?6:0;
        if(client<0 || client>=COD_CLIENTS)return -1;
        if(native_name(name,"maxhealth")) {
            if(value){if(value->kind!=GSC_NATIVE_INT || value->integer<1 || value->integer>1000000)return -1;g->players[client].maxhealth=value->integer;}
            else {out->kind=GSC_NATIVE_INT;out->integer=g->players[client].maxhealth?g->players[client].maxhealth:100;}
            return 1;
        }
        if(property) {
            if(value){
                if(property<=3 || property==6){
                    if(value->kind!=GSC_NATIVE_INT)return -1;
                    state.mask=property==1?HX_PLAYER_HEALTH:property==2?HX_PLAYER_SCORE:property==3?HX_PLAYER_DEATHS:HX_PLAYER_SPECTATE;
                    state.health=value->integer;state.score=value->integer;state.deaths=value->integer;state.spectator=value->integer;
                }else{
                    if(value->kind!=GSC_NATIVE_STRING)return -1;
                    if(property==4){
                        int team=native_name(value->text,"axis")?1:native_name(value->text,"allies")?0:-1;
                        GscArgument pers={0};
                        if(native_name(value->text,"none") && gsc_runtime_get_entity_value(g->runtime,entity,"pers","team",&pers) && pers.kind==3)team=native_name(pers.text,"axis")?1:native_name(pers.text,"allies")?0:-1;
                        g->players[client].team=team;snprintf(g->players[client].sessionteam,sizeof(g->players[client].sessionteam),"%s",value->text);
                        if(g->imp->player_set_team)g->imp->player_set_team(g->eng,client,team);
                        state.mask=HX_PLAYER_TEAM;state.team=team;
                    }else{
                        int mode=native_name(value->text,"playing")?1:native_name(value->text,"dead")?2:native_name(value->text,"intermission")?3:native_name(value->text,"spectator")?0:-1;
                        if(mode<0)return -1;g->players[client].state=mode;state.mask=HX_PLAYER_STATE;state.state=mode;
                    }
                }
                return g->imp->player_state(g->eng,client,&state,NULL)?1:-1;
            }
            if(!g->imp->player_state(g->eng,client,NULL,&state))return -1;
            if(property==4 || property==5){out->kind=GSC_NATIVE_STRING;out->text=property==4?g->players[client].sessionteam:g->players[client].state==0?"spectator":g->players[client].state==1?"playing":g->players[client].state==2?"dead":"intermission";}
            else{out->kind=GSC_NATIVE_INT;out->integer=property==1?state.health:property==2?state.score:property==3?state.deaths:state.spectator;}
            return 1;
        }
    }


    if(entity<=-3 && native_name(name,"model")) {
        int client=-3-entity;if(client<0 || client>=COD_CLIENTS)return -1;
        if(value){if(value->kind!=GSC_NATIVE_STRING || !g->imp || g->imp->api_version<12 || !g->imp->player_model || strlen(value->text)>=256 || !g->imp->player_model(g->eng,client,0,value->text,""))return -1;strcpy(g->players[client].body,value->text);}
        else{out->kind=GSC_NATIVE_STRING;out->text=g->players[client].body;}return 1;
    }

    if(entity<=-3 && native_name(name,"name")) {
        const char* name;
        if(value || !g->imp || g->imp->api_version<11 || !g->imp->player_name || !(name=g->imp->player_name(g->eng,-3-entity)))return -1;
        out->kind=GSC_NATIVE_STRING;out->text=name;return 1;
    }
    if (!rotation && !native_name(name, "origin"))
        return 0;
    if (entity <= -3) {
        int client = -3 - entity;
        hxSGPlayerView view;
        if (value || client < 0 || client >= COD_CLIENTS || !g->imp || g->imp->api_version < 6 ||
            !g->imp->player_view || !g->imp->player_view(g->eng, client, &view))
            return -1;
        out->kind = GSC_NATIVE_VECTOR;
        if (rotation) {
            out->vector[0] = view.pitch;
            out->vector[1] = view.yaw;
            out->vector[2] = 0;
        } else {
            out->vector[0] = view.origin[0];
            out->vector[1] = view.origin[1];
            out->vector[2] = view.origin[2] - 24.f;
        }
        return 1;
    }
    if (entity < 0)
        return 0;
    if (!g->imp || g->imp->api_version < 8 || !g->imp->entity_transform)
        return -1;
    if (!rotation && g->imp->spawn_anchor_origin && atoi(host_key(g, entity, "spawn_anchor"))) {
        if (value) {
            if (!scene_vec(value, v))
                return -1;
            return g->imp->spawn_anchor_origin(g->eng, entity, v, NULL) ? 1 : -1;
        }
        if (!g->imp->spawn_anchor_origin(g->eng, entity, NULL, v))
            return -1;
        out->kind = GSC_NATIVE_VECTOR;
        for (i = 0; i < 3; i++)
            out->vector[i] = v[i];
        return 1;
    }
    if (value) {
        if (!scene_vec(value, v))
            return -1;
        for (i = 0; i < 256; i++)
            if (g->motions[i].active && g->motions[i].id == entity &&
                g->motions[i].rotation == rotation)
                g->motions[i].active = 0;
        return g->imp->entity_transform(g->eng, entity, rotation ? NULL : v, rotation ? v : NULL,
                                        NULL, NULL)
                   ? 1
                   : -1;
    }
    if (!g->imp->entity_transform(g->eng, entity, NULL, NULL, rotation ? NULL : v,
                                  rotation ? v : NULL))
        return -1;
    out->kind = GSC_NATIVE_VECTOR;
    for (i = 0; i < 3; i++)
        out->vector[i] = v[i];
    return 1;
}
static void scene_motion_tick(CodGame *g, int now) {
    int i, k;
    for (i = 0; i < 256; i++)
        if (g->motions[i].active) {
            CodMotion *m = &g->motions[i];
            float v[3];
            double t = now >= m->end     ? 1.0
                       : now <= m->start ? 0
                                         : (double)(now - m->start) / (m->end - m->start);
            if (t > 0 && t < 1 && (m->accel > 0 || m->decel > 0)) {
                const double duration = m->end - m->start, elapsed = now - m->start,
                             remaining = m->end - now;
                const double denominator = duration - .5 * (m->accel + m->decel);
                t = m->accel > 0 && elapsed < m->accel
                        ? .5 * elapsed * elapsed / m->accel / denominator
                    : m->decel > 0 && remaining < m->decel
                        ? 1 - .5 * remaining * remaining / m->decel / denominator
                        : (elapsed - .5 * m->accel) / denominator;
            }
            if (m->gravity) {
                double elapsed_sec = (double)(now - m->start) * 0.001;
                double max_sec = (double)(m->end - m->start) * 0.001;
                if (elapsed_sec > max_sec) elapsed_sec = max_sec;
                v[0] = (float)(m->from[0] + m->velocity[0] * elapsed_sec);
                v[1] = (float)(m->from[1] + m->velocity[1] * elapsed_sec);
                v[2] = (float)(m->from[2] + m->velocity[2] * elapsed_sec - 0.5 * 800.0 * elapsed_sec * elapsed_sec);
            } else {
                for (k = 0; k < 3; k++)
                    v[k] = (float)(m->from[k] + (m->to[k] - m->from[k]) * t);
            }
            if (!g->imp->entity_transform(g->eng, m->id, m->rotation ? NULL : v,
                                          m->rotation ? v : NULL, NULL, NULL)) {
                m->active = 0;
                host_error(g, "GSC mover transform rejected");
                continue;
            }
            if (t >= 1) {
                m->active = 0;
                gsc_runtime_notify(g->runtime, m->id, m->rotation ? "rotatedone" : "movedone", NULL,
                                   0);
            }
        }
}
static void fog_publish(CodGame* g,int now) {
    static const char* names[5]={"r_fogStart","r_fogEnd","r_fogRed","r_fogGreen","r_fogBlue"};
    int i;float t=g->fog_end>g->fog_start?(float)(now-g->fog_start)/(g->fog_end-g->fog_start):1.f;
    if(t<0)t=0;if(t>1)t=1;
    for(i=0;i<5;i++){char text[48];snprintf(text,sizeof(text),"%.9g",g->fog_from[i]+(g->fog_target[i]-g->fog_from[i])*t);g->imp->cvar_set(g->eng,names[i],text);}
}
static int scene_native(CodGame *g, const char *name, int receiver, int entity,
                        const GscNativeValue *args, int argc, GscNativeValue *out) {
    hxSGImports *imp = g->imp;
    float v[3], a[3];
    int i;
    if (imp->api_version < 8)
        return 0;
    {
        int status = presentation_native(g, name, receiver, entity, args, argc, out);
        if (status)
            return status;
    }
    {
        int status = scene_fx_native(g, name, receiver, args, argc);
        if (status)
            return status;
    }
    if(!receiver && native_name(name,"setCullFog")) {
        double values[6];float t;int k;
        if(argc!=6 || !imp->cvar_set)return -1;
        for(k=0;k<6;k++)if(!presentation_number(args+k,values+k))return -1;
        if(values[0]<0 || values[1]<=values[0] || values[1]>1e8 || values[5]<0 || values[5]>86400)return -1;
        for(k=2;k<5;k++)if(values[k]<0 || values[k]>1)return -1;
        if(g->scene_now>INT_MAX-(int)(values[5]*1000))return -1;
        t=g->fog_end>g->fog_start?(float)(g->scene_now-g->fog_start)/(g->fog_end-g->fog_start):1.f;
        if(t<0)t=0;if(t>1)t=1;
        for(k=0;k<5;k++){g->fog_from[k]+=(g->fog_target[k]-g->fog_from[k])*t;g->fog_target[k]=(float)values[k];}
        g->fog_start=g->scene_now;g->fog_end=g->scene_now+(int)(values[5]*1000);
        fog_publish(g,g->scene_now);return 1;
    }
    if(!receiver && native_name(name,"setExpFog")) {
        double density, colors[3], trans; float t, target[5]; int k;
        if(argc!=5 || !imp->cvar_set)return -1;
        if(!presentation_number(args,&density) || density<=0 || density>=1)return -1;
        for(k=0;k<3;k++)if(!presentation_number(args+1+k,colors+k) || colors[k]<0 || colors[k]>1)return -1;
        if(!presentation_number(args+4,&trans) || trans<0 || trans>86400)return -1;
        if(g->scene_now>INT_MAX-(int)(trans*1000))return -1;
        target[0]=0.f; target[1]=(float)(3.0/density);
        for(k=0;k<3;k++)target[2+k]=(float)colors[k];
        t=g->fog_end>g->fog_start?(float)(g->scene_now-g->fog_start)/(g->fog_end-g->fog_start):1.f;
        if(t<0)t=0;if(t>1)t=1;
        for(k=0;k<5;k++){g->fog_from[k]+=(g->fog_target[k]-g->fog_from[k])*t;g->fog_target[k]=target[k];}
        g->fog_start=g->scene_now;g->fog_end=g->scene_now+(int)(trans*1000);
        fog_publish(g,g->scene_now);return 1;
    }
    if (!receiver && native_name(name, "spawn")) {
        int id;
        if (argc != 2 || args[0].kind != GSC_NATIVE_STRING || !scene_vec(args + 1, v) ||
            !imp->entity_spawn)
            return -1;
        if (strcmp(args[0].text, "script_origin") && strcmp(args[0].text, "script_model") && strcmp(args[0].text, "item_health") && strcmp(args[0].text, "item_armor"))
            return -1;
        id = imp->entity_spawn(g->eng, args[0].text, v);
        if (id < 0)
            return -1;
        out->kind = GSC_NATIVE_ENTITY;
        out->integer = id;
        return 1;
    }
    if (receiver && native_name(name, "delete")) {
        if (argc || entity < 0 || !imp->entity_remove || !imp->entity_remove(g->eng, entity))
            return -1;
        if(g->imp->api_version>=11 && g->imp->entity_loop_sound)g->imp->entity_loop_sound(g->eng,entity,NULL);
        for (i = 0; i < 256; i++)
            if (g->motions[i].id == entity)
                g->motions[i].active = 0;
        for(i=0;i<COD_CLIENTS;i++)if(g->players[i].linked && g->players[i].link_entity==entity){
            if(g->imp->api_version>=11 && g->imp->player_script_control)g->imp->player_script_control(g->eng,i,0,-1);
            g->players[i].linked=0;
        }
        gsc_runtime_notify(g->runtime, entity, "death", NULL, 0);
        return 1;
    }
    if (receiver && native_name(name, "setModel")) {
        char path[256];
        if (argc != 1 || args[0].kind != GSC_NATIVE_STRING || !imp->entity_model)
            return -1;
        if (snprintf(path, sizeof(path), "%s%s",
                     strncmp(args[0].text, "xmodel/", 7) ? "xmodel/" : "",
                     args[0].text) >= (int)sizeof(path))
            return -1;
        return imp->entity_model(g->eng, entity, path) ? 1 : -1;
    }
    if (receiver && (native_name(name, "getOrigin") || native_name(name, "getAngles"))) {
        if (argc)
            return -1;
        return scene_field(g, entity, native_name(name, "getOrigin") ? "origin" : "angles", NULL,
                           out);
    }
    if (receiver && (native_name(name, "moveTo") || native_name(name, "moveX") ||
                     native_name(name, "moveY") || native_name(name, "moveZ") ||
                     native_name(name, "rotateTo") || native_name(name, "rotateX") ||
                     native_name(name, "rotateY") || native_name(name, "rotateZ") ||
                     native_name(name, "rotateVelocity") || native_name(name, "moveGravity"))) {
        int is_rot_vel = native_name(name, "rotateVelocity");
        int is_move_grav = native_name(name, "moveGravity");
        int rotation = is_rot_vel || native_name(name, "rotateTo") || native_name(name, "rotateX") ||
                       native_name(name, "rotateY") || native_name(name, "rotateZ");
        int axis = native_name(name, "moveX") || native_name(name, "rotateX")   ? 0
                   : native_name(name, "moveY") || native_name(name, "rotateY") ? 1
                                                                                : 2;
        int absolute = native_name(name, "moveTo") || native_name(name, "rotateTo"), slot = -1;
        double seconds, accel = 0, decel = 0;
        float vel[3] = {0};
        if (rotation && !absolute && !is_rot_vel)
            axis = native_name(name, "rotateX") ? 2 : native_name(name, "rotateY") ? 0 : 1;
        if (argc < 2 || argc > 4 || !imp->entity_transform ||
            (args[1].kind != GSC_NATIVE_INT && args[1].kind != GSC_NATIVE_FLOAT))
            return -1;
        seconds = args[1].kind == GSC_NATIVE_INT ? args[1].integer : args[1].number;
        if (!isfinite(seconds) || seconds < 0 || seconds > 86400)
            return -1;
        if (argc >= 3 && !presentation_number(args + 2, &accel))
            return -1;
        if (argc == 4 && !presentation_number(args + 3, &decel))
            return -1;
        if (accel < 0 || decel < 0 || accel + decel > seconds ||
            g->scene_now > INT_MAX - (int)(seconds * 1000))
            return -1;
        if (!imp->entity_transform(g->eng, entity, NULL, NULL, rotation ? NULL : v,
                                   rotation ? v : NULL))
            return -1;
        memcpy(a, v, sizeof(a));
        if (is_rot_vel || is_move_grav) {
            if (!scene_vec(args, vel))
                return -1;
            if (is_rot_vel) {
                for (i = 0; i < 3; i++) a[i] = (float)(v[i] + vel[i] * seconds);
            }
        } else if (absolute) {
            if (!scene_vec(args, a))
                return -1;
        } else {
            if (args[0].kind != GSC_NATIVE_INT && args[0].kind != GSC_NATIVE_FLOAT)
                return -1;
            a[axis] += (float)(args[0].kind == GSC_NATIVE_INT ? args[0].integer : args[0].number);
        }
        for (i = 0; i < 256; i++)
            if (g->motions[i].active && g->motions[i].id == entity &&
                g->motions[i].rotation == rotation) {
                slot = i;
                break;
            }
        if (slot < 0)
            for (i = 0; i < 256; i++)
                if (!g->motions[i].active) {
                    slot = i;
                    break;
                }
        if (slot < 0)
            return -1;
        {
            CodMotion *m = &g->motions[slot];
            m->active = 1;
            m->id = entity;
            m->rotation = rotation;
            m->gravity = is_move_grav;
            m->start = g->scene_now;
            m->end = m->start + (int)(seconds * 1000);
            m->accel = (float)(accel * 1000);
            m->decel = (float)(decel * 1000);
            memcpy(m->from, v, sizeof(v));
            memcpy(m->to, a, sizeof(a));
            if (is_move_grav) memcpy(m->velocity, vel, sizeof(vel));
        }
        return 1;
    }
    if (!receiver && native_name(name, "bulletTrace")) {
        hxSGTrace hit;
        float zero[3] = {0};
        int mask = 1 | 128, ignore = -1;
        if (argc != 4 || !scene_vec(args, v) || !scene_vec(args + 1, a) ||
            args[2].kind != GSC_NATIVE_INT || !imp->trace_query)
            return -1;
        if (args[2].integer)
            mask |= 2;
        if (args[3].kind == GSC_NATIVE_ENTITY)
            ignore = args[3].integer;
        else if (args[3].kind != GSC_NATIVE_UNDEFINED)
            return -1;
        if (!imp->trace_query(g->eng, v, a, zero, zero, mask, ignore, &hit))
            return -1;
        memset(g->trace_fields, 0, sizeof(g->trace_fields));
        g->trace_fields[0].name = "fraction";
        g->trace_fields[0].value.kind = GSC_NATIVE_FLOAT;
        g->trace_fields[0].value.number = hit.fraction;
        g->trace_fields[1].name = "position";
        g->trace_fields[1].value.kind = GSC_NATIVE_VECTOR;
        g->trace_fields[2].name = "normal";
        g->trace_fields[2].value.kind = GSC_NATIVE_VECTOR;
        for (i = 0; i < 3; i++) {
            g->trace_fields[1].value.vector[i] = hit.position[i];
            g->trace_fields[2].value.vector[i] = hit.normal[i];
        }
        g->trace_fields[3].name = "entity";
        if (hit.entity != -1) {
            g->trace_fields[3].value.kind = GSC_NATIVE_ENTITY;
            g->trace_fields[3].value.integer = hit.entity;
        }
        g->trace_fields[4].name = "surfacetype";
        g->trace_fields[4].value.kind = GSC_NATIVE_STRING;
        g->trace_fields[4].value.text = cod_surface_name(hit.surface_flags);
        out->kind = GSC_NATIVE_RECORD;
        out->fields = g->trace_fields;
        out->field_count = 5;
        return 1;
    }
    return 0;
}

static int host_native(void* user,const char* name,int receiver,int entity,
                       const GscNativeValue* args,int argc,GscNativeValue* out) {
    CodGame* g=(CodGame*)user;
    if(!receiver && native_name(name,"animHasNotetrack")){
        int found;if(argc!=2 || args[0].kind!=GSC_NATIVE_ANIMATION || args[1].kind!=GSC_NATIVE_STRING || !cod_animation_has_note(g->animations,args[0].tree,args[0].text,args[1].text,&found))return -1;
        out->kind=GSC_NATIVE_INT;out->integer=found;return 1;
    }
    if(!receiver && native_name(name,"getAnimLength")){
        float seconds;if(argc!=1 || args[0].kind!=GSC_NATIVE_ANIMATION || !cod_animation_length(g->animations,args[0].tree,args[0].text,&seconds))return -1;
        out->kind=GSC_NATIVE_FLOAT;out->number=seconds;return 1;
    }
hxSGImports* imp=g->imp;
    int client=entity<=-3?-3-entity:-1;
    if(!imp)return -1;

    if(!receiver && native_name(name,"exitLevel")){
        char rotation[4096],key[64],value[128];const char* raw;char* cursor;int consumed;
        if(argc!=1 || args[0].kind!=GSC_NATIVE_INT || imp->api_version<12 || !imp->request_map || (args[0].integer && (imp->api_version<15 || !imp->request_transition)))return -1;
        raw=host_cvar(g,"sv_mapRotationCurrent");if(!raw || !*raw)raw=host_cvar(g,"sv_mapRotation");
        if(!raw || !*raw)return (args[0].integer?imp->request_transition(g->eng,NULL,1):imp->request_map(g->eng,NULL))?1:-1;
        if(strlen(raw)>=sizeof(rotation))return -1;strcpy(rotation,raw);cursor=rotation;
        while(sscanf(cursor," %63s %127s %n",key,value,&consumed)==2){
            cursor+=consumed;
            if(native_name(key,"gametype"))on_cvar(g,"g_gametype",value);
            else if(native_name(key,"map")){
                if(!(args[0].integer?imp->request_transition(g->eng,value,1):imp->request_map(g->eng,value)))return -1;
                on_cvar(g,"sv_mapRotationCurrent",cursor);return 1;
            }else return -1;
        }
        return -1;
    }


    if(receiver && native_name(name,"attackButtonPressed")){
        if(argc || client<0 || client>=COD_CLIENTS)return -1;out->kind=GSC_NATIVE_INT;out->integer=(g->trig_use_buttons[client]&1)!=0;return 1;
    }
    if(receiver && native_name(name,"playLocalSound")){
        if(argc!=1 || args[0].kind!=GSC_NATIVE_STRING || client<0 || client>=COD_CLIENTS || imp->api_version<12 || !imp->player_ui)return -1;
        return imp->player_ui(g->eng,client,6,args[0].text,"")?1:-1;
    }


    if(receiver && native_name(name,"allowSpectateTeam")) {
        int category;if(argc!=2 || args[0].kind!=GSC_NATIVE_STRING || args[1].kind!=GSC_NATIVE_INT || client<0 || client>=COD_CLIENTS || imp->api_version<12 || !imp->player_spectate_permission)return -1;
        category=native_name(args[0].text,"allies")?0:native_name(args[0].text,"axis")?1:native_name(args[0].text,"freelook")?2:native_name(args[0].text,"none")?3:-1;
        if(category<0)return -1;return imp->player_spectate_permission(g->eng,client,category,args[1].integer!=0)?1:-1;
    }


    if(!receiver && native_name(name,"map_restart")){
        if(argc!=1 || args[0].kind!=GSC_NATIVE_INT || imp->api_version<12 || !imp->request_restart)return -1;
        return imp->request_restart(g->eng,args[0].integer!=0)?1:-1;
    }
    if(!receiver && native_name(name,"obituary")){
        int victim,attacker=-1;if(argc!=4 || args[0].kind!=GSC_NATIVE_ENTITY || args[0].integer>-3 || args[2].kind!=GSC_NATIVE_STRING || imp->api_version<12 || !imp->player_obituary)return -1;
        victim=-3-args[0].integer;if(args[1].kind==GSC_NATIVE_ENTITY && args[1].integer<=-3)attacker=-3-args[1].integer;
        return imp->player_obituary(g->eng,victim,attacker,args[2].text)?1:-1;
    }
    if(receiver && client>=0 && client<COD_CLIENTS && imp->api_version>=12 && (native_name(name,"dropItem") || native_name(name,"dropHealth") || native_name(name,"clonePlayer"))){
        int item=native_name(name,"dropItem"),clone=native_name(name,"clonePlayer"),id;
        if(argc!=(item?1:0) || (item && args[0].kind!=GSC_NATIVE_STRING))return -1;
        if(item && (!strcmp(args[0].text,"none") || !args[0].text[0]))return 1;
        if(clone){if(!imp->player_clone)return -1;id=imp->player_clone(g->eng,client);}
        else {if(!imp->player_drop_item)return -1;id=imp->player_drop_item(g->eng,client,item?args[0].text:NULL);}
        if(id<0)return -1;out->kind=GSC_NATIVE_ENTITY;out->integer=id;return 1;
    }


    if(receiver && client>=0 && client<COD_CLIENTS && imp->api_version>=12) {
        if(native_name(name,"setModel") || native_name(name,"attach") || native_name(name,"detachAll")) {
            int detach=native_name(name,"detachAll"),body=native_name(name,"setModel"),index=g->players[client].attach_count;
            const char* tag="";int ignore=0;
            if(!imp->player_model || argc<(detach?0:1) || argc>(detach?0:body?1:3) || (!detach && (args[0].kind!=GSC_NATIVE_STRING || strlen(args[0].text)>=256)))return -1;
            if(!detach && !body){if(index>=16)return -1;if(argc>1){if(args[1].kind!=GSC_NATIVE_STRING || strlen(args[1].text)>=96)return -1;tag=args[1].text;}if(argc>2){if(args[2].kind!=GSC_NATIVE_INT)return -1;ignore=args[2].integer!=0;}}
            if(!imp->player_model(g->eng,client,detach?2:body?0:1,detach?"":args[0].text,tag))return -1;
            if(detach)g->players[client].attach_count=0;
            else if(body)strcpy(g->players[client].body,args[0].text);
            else{strcpy(g->players[client].attachments[index].model,args[0].text);strcpy(g->players[client].attachments[index].tag,tag);g->players[client].attachments[index].ignore=ignore;g->players[client].attach_count++;}return 1;
        }
        if(native_name(name,"getAttachSize")){if(argc)return -1;out->kind=GSC_NATIVE_INT;out->integer=g->players[client].attach_count;return 1;}
        if(native_name(name,"getAttachModelName") || native_name(name,"getAttachTagName") || native_name(name,"getAttachIgnoreCollision")){
            int index;if(argc!=1 || args[0].kind!=GSC_NATIVE_INT || (index=args[0].integer)<0 || index>=g->players[client].attach_count)return -1;
            if(native_name(name,"getAttachIgnoreCollision")){out->kind=GSC_NATIVE_INT;out->integer=g->players[client].attachments[index].ignore;}
            else{out->kind=GSC_NATIVE_STRING;out->text=native_name(name,"getAttachModelName")?g->players[client].attachments[index].model:g->players[client].attachments[index].tag;}return 1;
        }
        if(native_name(name,"getViewModel")){if(argc)return -1;out->kind=GSC_NATIVE_STRING;out->text=g->players[client].viewmodel;return 1;}
        if(native_name(name,"setViewModel")){
            hxSGPlayerState state={0};const char* model;
            if(argc!=1 || args[0].kind!=GSC_NATIVE_STRING || !imp->player_state || strlen(args[0].text)>=256)return -1;model=args[0].text;
            if(strncmp(model,"xmodel/viewmodel_hands_",23))return -1;
            model+=23;
            state.mask=HX_PLAYER_VIEW_VARIANT;state.view_variant=0;
            {static const char* variants[]={"us","british","russian","whermact","uswinter","waffen","whermact_winter","waffen_winter","russian_vetrn","british_air","fallschirmjager","fallschirmjager_grey","kriegsmarine"};int variant;
             for(variant=0;variant<13;variant++)if(!strcmp(model,variants[variant])){state.view_variant=variant+1;break;}
}
            if(!state.view_variant || !imp->player_state(g->eng,client,&state,NULL))return -1;
            strcpy(g->players[client].viewmodel,args[0].text);return 1;
        }
    }


    if(native_name(name,"iprintln") || native_name(name,"iprintlnBold")) {
        int i,first=receiver?client:0,last=receiver?client:COD_CLIENTS-1;char argument[256];const char* value="";
        if(imp->api_version<12 || !imp->player_ui || argc<1 || argc>2 || args[0].kind!=GSC_NATIVE_STRING || (receiver && (client<0 || client>=COD_CLIENTS)))return -1;
        if(argc==2){
            if(args[1].kind==GSC_NATIVE_STRING)value=args[1].text;
            else if(args[1].kind==GSC_NATIVE_ENTITY && args[1].integer<=-3 && imp->player_name)value=imp->player_name(g->eng,-3-args[1].integer);
            else if(args[1].kind==GSC_NATIVE_INT){snprintf(argument,sizeof(argument),"%d",args[1].integer);value=argument;}
            else return -1;
            if(!value)return -1;
        }
        for(i=first;i<=last;i++)if(g->players[i].active && !imp->player_ui(g->eng,i,native_name(name,"iprintlnBold")?5:4,args[0].text,value))return -1;
        return 1;
    }


    if(imp->api_version>=12 && !receiver && native_name(name,"positionWouldTelefrag")) {
        int i,j;double point[3];if(argc!=1 || args[0].kind!=GSC_NATIVE_VECTOR)return -1;
        memcpy(point,args[0].vector,sizeof(point));out->kind=GSC_NATIVE_INT;out->integer=0;
        for(i=0;i<COD_CLIENTS;i++)if(g->players[i].active && g->players[i].state==1){
            hxSGPlayerState state={0};if(!imp->player_state || !imp->player_state(g->eng,i,NULL,&state))continue;
            if(state.health<=0)continue;
            for(j=0;j<3;j++)if(fabs(point[j]-state.origin[j])>=(j==2?70:30))break;
            if(j==3){out->integer=1;break;}
        }return 1;
    }
    if(!receiver && native_name(name,"resetTimeout")) {
        /* There is no wall-clock watchdog to reset: the VM is preempted by its
         * bounded instruction slices even within an uninterrupted script call. */
        return argc==0?1:-1;
    }
    if(receiver && client>=0 && client<COD_CLIENTS && imp->api_version>=12) {
        if(native_name(name,"getEntityNumber") || native_name(name,"getGuid")){
            if(argc)return -1;out->kind=GSC_NATIVE_INT;out->integer=native_name(name,"getGuid")?0:client;return 1;
        }
        if(native_name(name,"setClientCvar") || native_name(name,"openMenu") || native_name(name,"closeMenu") || native_name(name,"closeInGameMenu")) {
            int set=native_name(name,"setClientCvar"),closeall=native_name(name,"closeInGameMenu") || (native_name(name,"closeMenu") && argc==0);char number[512];const char* value="",*key=closeall?"":argc?args[0].text:"";
            if(!imp->player_ui || (set?(argc!=2 && argc!=3):argc!=(closeall?0:1)) || (!closeall && args[0].kind!=GSC_NATIVE_STRING))return -1;
            if(set){
                if(args[1].kind==GSC_NATIVE_STRING)value=args[1].text;
                else if(args[1].kind==GSC_NATIVE_INT){snprintf(number,sizeof(number),"%d",args[1].integer);value=number;}
                else if(args[1].kind==GSC_NATIVE_FLOAT){snprintf(number,sizeof(number),"%.9g",args[1].number);value=number;}else return -1;

                if(argc==3){
                    const char* argument;char argnumber[64];const char* format=value;
                    if(args[2].kind==GSC_NATIVE_STRING)argument=args[2].text;
                    else if(args[2].kind==GSC_NATIVE_INT){snprintf(argnumber,sizeof(argnumber),"%d",args[2].integer);argument=argnumber;}
                    else return -1;
                    if(format==number){char copy[512];snprintf(copy,sizeof(copy),"%s",format);snprintf(number,sizeof(number),"%s\t%s",copy,argument);}
                    else if(snprintf(number,sizeof(number),"%s\t%s",format,argument)>=(int)sizeof(number))return -1;
                    value=number;
                }
                if(native_name(key,"g_scriptMainMenu"))key="ui_menu_session";
            }
            if(!set && !closeall && native_name(name,"openMenu"))snprintf(g->players[client].current_menu,sizeof(g->players[client].current_menu),"%s",key);
            return imp->player_ui(g->eng,client,set?(argc==3?7:0):closeall?3:native_name(name,"openMenu")?1:2,key,value)?1:-1;
        }
        if(native_name(name,"spawn") || native_name(name,"setOrigin") || native_name(name,"setPlayerAngles")) {
            float origin[3],angles[3];int i;hxSGPlayerState state={0};int spawn=native_name(name,"spawn"),rotate=native_name(name,"setPlayerAngles");
            if(argc!=(spawn?2:1) || args[0].kind!=GSC_NATIVE_VECTOR || (spawn && args[1].kind!=GSC_NATIVE_VECTOR) || !imp->player_state)return -1;
            for(i=0;i<3;i++){origin[i]=(float)args[0].vector[i];angles[i]=(float)(spawn?args[1].vector[i]:args[0].vector[i]);}
            if(spawn){if(!imp->player_spawn || !imp->player_spawn(g->eng,client,origin,angles))return -1;g->players[client].linked=0;return 1;}
            state.mask=rotate?HX_PLAYER_ANGLES:HX_PLAYER_ORIGIN;memcpy(state.origin,origin,sizeof(origin));memcpy(state.angles,angles,sizeof(angles));
            return imp->player_state(g->eng,client,&state,NULL)?1:-1;
        }
        if(native_name(name,"suicide") || native_name(name,"finishPlayerDamage")) {
            int suicide=native_name(name,"suicide"),attacker=-1,damage;
            if(!imp->player_finish_damage || argc!=(suicide?0:9))return -1;
            if(suicide){damage=1000000;attacker=client;}
            else{if(args[2].kind!=GSC_NATIVE_INT)return -1;damage=args[2].integer;if(args[1].kind==GSC_NATIVE_ENTITY && args[1].integer<=-3)attacker=-3-args[1].integer;}
            if(imp->api_version>=15 && imp->player_damage_event){
                hxSGDamage d={0};int i;d.victim=client;d.attacker=attacker;d.inflictor=-1;d.amount=damage;
                d.weapon="none";d.means=suicide?"MOD_SUICIDE":"MOD_UNKNOWN";d.hit_location="none";
                if(!suicide){
                    if(args[3].kind!=GSC_NATIVE_INT || args[4].kind!=GSC_NATIVE_STRING || args[5].kind!=GSC_NATIVE_STRING || args[8].kind!=GSC_NATIVE_STRING)return -1;
                    d.flags=args[3].integer;d.means=args[4].text;d.weapon=args[5].text;d.hit_location=args[8].text;
                    if(args[0].kind==GSC_NATIVE_ENTITY)d.inflictor=args[0].integer;
                    if(args[6].kind==GSC_NATIVE_VECTOR){d.has_point=1;for(i=0;i<3;i++)d.point[i]=(float)args[6].vector[i];}
                    if(args[7].kind==GSC_NATIVE_VECTOR){d.has_direction=1;for(i=0;i<3;i++)d.direction[i]=(float)args[7].vector[i];}
                }
                return imp->player_damage_event(g->eng,&d,1)?1:-1;
            }
            return imp->player_finish_damage(g->eng,client,attacker,damage)?1:-1;
        }
        if(native_name(name,"getCurrentWeapon") || native_name(name,"getWeaponSlotWeapon")) {
            int slot=-1;if(!imp->player_weapon_name)return -1;
            if(native_name(name,"getCurrentWeapon")){if(argc)return -1;}
            else {if(argc!=1 || args[0].kind!=GSC_NATIVE_STRING)return -1;slot=native_name(args[0].text,"primary")?0:native_name(args[0].text,"primaryb")?1:native_name(args[0].text,"pistol")?2:native_name(args[0].text,"grenade")?3:-2;if(slot<0)return -1;}
            out->text=imp->player_weapon_name(g->eng,client,slot);if(!out->text)return -1;out->kind=GSC_NATIVE_STRING;return 1;
        }
        {
            int operation=native_name(name,"takeWeapon")?HX_WEAPON_TAKE:native_name(name,"giveMaxAmmo")?HX_WEAPON_MAX_AMMO:native_name(name,"setSpawnWeapon") || native_name(name,"switchToWeapon")?HX_WEAPON_SWITCH:native_name(name,"hasWeapon")?HX_WEAPON_HAS:native_name(name,"setWeaponSlotWeapon")?HX_WEAPON_SLOT_SET:native_name(name,"getWeaponSlotClipAmmo")?HX_WEAPON_GET_CLIP:native_name(name,"setWeaponSlotClipAmmo")?HX_WEAPON_SET_CLIP:native_name(name,"getWeaponSlotAmmo")?HX_WEAPON_GET_STOCK:native_name(name,"setWeaponSlotAmmo")?HX_WEAPON_SET_STOCK:0;
            if(operation){
                int slot=0,value=0,result,slotop=operation>=HX_WEAPON_GET_CLIP && operation<=HX_WEAPON_SLOT_SET;const char* weapon=NULL;
                int write=operation==HX_WEAPON_SLOT_SET || operation==HX_WEAPON_SET_CLIP || operation==HX_WEAPON_SET_STOCK;
                if(!imp->player_weapon || argc!=(write?2:1) || args[0].kind!=GSC_NATIVE_STRING)return -1;
                if(slotop){slot=native_name(args[0].text,"primary")?0:native_name(args[0].text,"primaryb")?1:native_name(args[0].text,"pistol")?2:native_name(args[0].text,"grenade")?3:-1;if(slot<0)return -1;}else weapon=args[0].text;
                if(operation==HX_WEAPON_SLOT_SET){if(args[1].kind!=GSC_NATIVE_STRING)return -1;weapon=args[1].text;}
                else if(write){if(args[1].kind!=GSC_NATIVE_INT)return -1;value=args[1].integer;}
                result=imp->player_weapon(g->eng,client,operation,slot,weapon,value);if(result<0)return -1;
                if(operation==HX_WEAPON_HAS || operation==HX_WEAPON_GET_CLIP || operation==HX_WEAPON_GET_STOCK){out->kind=GSC_NATIVE_INT;out->integer=result;}
                return 1;
            }
        }
    }

    if(!receiver && native_name(name,"setArchive")) {
        if(argc!=1 || args[0].kind!=GSC_NATIVE_INT || imp->api_version<10 || !imp->snapshot_archive)return -1;
        return imp->snapshot_archive(g->eng,args[0].integer!=0)?1:-1;
    }
    if(!receiver && (native_name(name,"precacheStatusIcon") || native_name(name,"precacheHeadIcon"))) {
        int i,head=native_name(name,"precacheHeadIcon"),*count=head?&g->head_icon_count:&g->status_icon_count;
        char (*icons)[256]=head?g->head_icons:g->status_icons;
        if(argc!=1 || args[0].kind!=GSC_NATIVE_STRING || !args[0].text || !args[0].text[0] || strlen(args[0].text)>=256 || imp->api_version<3 || !imp->precache_asset)return -1;
        for(i=0;i<*count;i++)if(native_name(icons[i],args[0].text))return 1;
        if(*count>=(head?15:16) || !imp->precache_asset(g->eng,HX_PRECACHE_SHADER,args[0].text))return -1;
        strcpy(icons[(*count)++],args[0].text);return 1;
    }
    if(!receiver && (native_name(name,"setTeamScore") || native_name(name,"getTeamScore"))) {
        int set=native_name(name,"setTeamScore"),team,result,value;
        if(argc!=(set?2:1) || args[0].kind!=GSC_NATIVE_STRING || imp->api_version<10 || !imp->team_score)return -1;
        team=native_name(args[0].text,"allies")?0:native_name(args[0].text,"axis")?1:-1;
        if(team<0 || (set && args[1].kind!=GSC_NATIVE_INT))return -1;
        value=set?args[1].integer:0;
        if(!imp->team_score(g->eng,team,set?&value:NULL,&result))return -1;
        if(!set){out->kind=GSC_NATIVE_INT;out->integer=result;}
        return 1;
    }
    if(!receiver && native_name(name,"precacheMenu")) {
        if(argc!=1 || args[0].kind!=GSC_NATIVE_STRING || imp->api_version<9 || !imp->precache_menu)return -1;
        return imp->precache_menu(g->eng,args[0].text)?1:-1;
    }
    if(!receiver && native_name(name,"precacheString")) {
        if(argc!=1 || args[0].kind!=GSC_NATIVE_STRING || imp->api_version<9 || !imp->precache_text)return -1;
        return imp->precache_text(g->eng,args[0].text)?1:-1;
    }
    if(!receiver && native_name(name,"makeCvarServerInfo")) {
        if(argc!=2 || args[0].kind!=GSC_NATIVE_STRING || args[1].kind!=GSC_NATIVE_STRING || imp->api_version<9 || !imp->cvar_serverinfo)return -1;
        return imp->cvar_serverinfo(g->eng,args[0].text,args[1].text)?1:-1;
    }
    if(!receiver && native_name(name,"setClientNameMode")) {
        int mode;
        if(argc!=1 || args[0].kind!=GSC_NATIVE_STRING || imp->api_version<15 || !imp->client_name_mode)return -1;
        mode=native_name(args[0].text,"auto_change")?0:native_name(args[0].text,"manual_change")?1:-1;
        return mode>=0 && imp->client_name_mode(g->eng,mode)?1:-1;
    }
    if(!receiver && native_name(name,"updateClientNames")){
        if(argc || imp->api_version<15 || !imp->client_name_mode)return -1;
        return imp->client_name_mode(g->eng,2)?1:-1;
    }
    if(receiver && (native_name(name,"sayAll") || native_name(name,"sayTeam") || native_name(name,"vsay"))){
        char command[512];
        if(client<0 || client>=COD_CLIENTS || argc!=1 || args[0].kind!=GSC_NATIVE_STRING || imp->api_version<13 || !imp->client_message)return -1;
        if(snprintf(command,sizeof(command),"%s %s",native_name(name,"vsay")?"vsay":native_name(name,"sayTeam")?"say_team":"say",args[0].text)>=(int)sizeof(command))return -1;
        return imp->client_message(g->eng,client,command)?1:-1;
    }
    if(receiver && (native_name(name,"autoDemoStart") || native_name(name,"autoDemoStop") || native_name(name,"autoScreenshot"))) {
        int is_shot = native_name(name,"autoScreenshot");
        int is_stop = native_name(name,"autoDemoStop");
        const char* cvar_name = is_shot ? "g_autoscreenshot" : "g_autodemo";
        const char* cmd = is_shot ? "E" : (is_stop ? "D" : "C");
        if(argc || client<0 || client>=COD_CLIENTS)return -1;
        if(imp->cvar_int && imp->cvar_int(g->eng, cvar_name)) {
            if(imp->api_version>=13 && imp->client_message) imp->client_message(g->eng, client, cmd);
            out->kind = GSC_NATIVE_INT; out->integer = 1;
        }
        return 1;
    }
    if(receiver && native_name(name,"pingPlayer")) {
        if(argc || client<0 || client>=COD_CLIENTS)return -1;
        return 1;
    }
    if(receiver && native_name(name,"setHintString")) {
        int slot;
        if(entity<0 || (argc!=1 && argc!=2) || args[0].kind!=GSC_NATIVE_STRING)return -1;
        if(argc==2 && args[1].kind!=GSC_NATIVE_STRING)return -1;
        if(!g->clock_started && imp->api_version>=9 && imp->precache_text) {
            if(args[0].text[0])imp->precache_text(g->eng,args[0].text);
            if(argc==2 && args[1].text[0])imp->precache_text(g->eng,args[1].text);
        }
        slot=find_or_add_trigger(g,entity,1);
        if(slot>=0) {
            if(argc==2 && args[1].text[0])
                snprintf(g->triggers[slot].hint,sizeof(g->triggers[slot].hint),"%s\t%s",args[0].text,args[1].text);
            else
                snprintf(g->triggers[slot].hint,sizeof(g->triggers[slot].hint),"%s",args[0].text);
        }
        return 1;
    }
    if(!receiver && native_name(name,"announcement")) {
        int id=g->announcement_id,i;CodHud* h;char formatted[1024];
        if(!format_native_text(args,argc,formatted,sizeof(formatted)) || imp->api_version<8 || !imp->hud_update)return -1;
        if(id<0){for(id=0;id<128 && g->hud[id].active;id++);if(id>=128)return -1;g->announcement_id=id;}
        h=&g->hud[id];memset(h,0,sizeof(*h));h->active=1;h->value.client=h->value.team=-1;h->value.x=320;h->value.y=100;h->value.align_x=1;h->value.scale=1;
        for(i=0;i<4;i++)h->value.color[i]=1;
        strcpy(h->text,formatted);g->announcement_end=g->scene_now+5000;
        return presentation_sync(g,id)?1:-1;
    }
    if(!receiver && native_name(name,"clientAnnouncement")) {
        int target_client, id, i; CodHud* h;char formatted[1024];
        if(argc<2 || args[0].kind!=GSC_NATIVE_ENTITY || !format_native_text(args+1,argc-1,formatted,sizeof(formatted)) || imp->api_version<8 || !imp->hud_update)return -1;
        target_client = args[0].integer<=-3 ? -3-args[0].integer : args[0].integer;
        if(target_client<0 || target_client>=COD_CLIENTS)return -1;
        id = g->client_announcement_id[target_client];
        if(id<0){for(id=0;id<128 && g->hud[id].active;id++);if(id>=128)return -1;g->client_announcement_id[target_client]=id;}
        h=&g->hud[id];memset(h,0,sizeof(*h));h->active=1;h->value.client=target_client;h->value.team=-1;h->value.x=320;h->value.y=100;h->value.align_x=1;h->value.scale=1;
        for(i=0;i<4;i++)h->value.color[i]=1;
        strcpy(h->text,formatted);g->client_announcement_end[target_client]=g->scene_now+5000;
        return presentation_sync(g,id)?1:-1;
    }
    if(receiver && (native_name(name,"playLoopSound") || native_name(name,"stopLoopSound"))) {
        int stop=native_name(name,"stopLoopSound");
        if(entity<0 || (stop?(argc<0 || argc>1):argc!=1) || (argc && args[0].kind!=GSC_NATIVE_STRING) || imp->api_version<11 || !imp->entity_loop_sound)return -1;
        return imp->entity_loop_sound(g->eng,entity,stop?NULL:args[0].text)?1:-1;
    }
    if(receiver && native_name(name,"useButtonPressed")) {
        if(argc || client<0 || client>=COD_CLIENTS)return -1;
        out->kind=GSC_NATIVE_INT;out->integer=(g->trig_use_buttons[client]&8)!=0;return 1;
    }
    if(receiver && (native_name(name,"linkTo") || native_name(name,"unlink") || native_name(name,"disableWeapon") || native_name(name,"enableWeapon"))) {
        int attach=native_name(name,"linkTo"),detach=native_name(name,"unlink");float origin[3];
        if(client<0 || client>=COD_CLIENTS || imp->api_version<11 || !imp->player_script_control)return -1;
        if(attach){
            if(argc!=1 || args[0].kind!=GSC_NATIVE_ENTITY || args[0].integer<0 || !imp->entity_transform || !imp->entity_transform(g->eng,args[0].integer,NULL,NULL,origin,NULL))return -1;
            if(!imp->player_script_control(g->eng,client,1,-1))return -1;
            g->players[client].linked=1;g->players[client].link_entity=args[0].integer;
        }else if(detach){
            if(argc || !imp->player_script_control(g->eng,client,0,-1))return -1;
            g->players[client].linked=0;
        }else if(argc || !imp->player_script_control(g->eng,client,-1,native_name(name,"disableWeapon")))return -1;
        return 1;
    }
    if(receiver && native_name(name,"getEntityNumber")) {if(argc)return -1;out->kind=GSC_NATIVE_INT;out->integer=client>=0?client:entity;return 1;}
    if(receiver && native_name(name,"getGuid")) {if(argc || client<0 || client>=COD_CLIENTS)return -1;out->kind=GSC_NATIVE_STRING;out->text=host_key(g,entity,"guid");return 1;}
    if(receiver && native_name(name,"isLookingAt")) {
        hxSGPlayerView view;const char* model;
        if(argc!=1 || client<0 || client>=COD_CLIENTS || args[0].kind!=GSC_NATIVE_ENTITY ||
           args[0].integer<0 || !imp->player_view || !imp->player_view(g->eng,client,&view))return -1;
        model=host_key(g,args[0].integer,"model");
        out->kind=GSC_NATIVE_INT;
        out->integer=entity_player_looking(g,args[0].integer,model && model[0]=='*'?atoi(model+1):0,client,&view);
        return 1;
    }
    if(receiver && native_name(name,"isTouching")) {
        int trig_ent=-1,cl=-1,model_id;const char* model;hxSGPlayerView v;
        if(argc!=1)return -1;
        out->kind=GSC_NATIVE_INT;out->integer=0;
        if(args[0].kind!=GSC_NATIVE_ENTITY)return 1;
        if(entity<=-3 && args[0].integer>=0){trig_ent=args[0].integer;cl=-3-entity;}
        else if(entity>=0 && args[0].integer<=-3){trig_ent=entity;cl=-3-args[0].integer;}
        if(trig_ent<0 || cl<0 || cl>=COD_CLIENTS || imp->api_version<6 || !imp->player_view || !imp->player_view(g->eng,cl,&v))return 1;
        model=host_key(g,trig_ent,"model");
        model_id=(model && model[0]=='*')?atoi(model+1):0;
        out->integer=entity_player_touching(g,trig_ent,model_id,&v);
        return 1;
    }
    if(!receiver && native_name(name,"setPlayerIgnoreRadiusDamage")){
        if(argc!=1 || args[0].kind!=GSC_NATIVE_INT)return -1;g->radius_ignore_players=args[0].integer!=0;return 1;
    }
    if(!receiver && native_name(name,"radiusDamage")) {
        float org[3];double range,max_dmg,min_dmg;int c;
        if(argc<4 || !scene_vec(args,org) || !presentation_number(args+1,&range) ||
           !presentation_number(args+2,&max_dmg) || !presentation_number(args+3,&min_dmg))return -1;
        if(range<=0 || max_dmg<0 || max_dmg>1000000 || min_dmg<0 || min_dmg>max_dmg || imp->api_version<6 || !imp->player_view || !imp->player_hitscan)return -1;
        if(argc>6)return -1;
        if(imp->api_version>=15 && imp->radius_damage){
            int attacker=-1,inflictor=-1;
            if(argc>4){if(args[4].kind!=GSC_NATIVE_ENTITY && args[4].kind!=GSC_NATIVE_UNDEFINED)return -1;if(args[4].kind==GSC_NATIVE_ENTITY)attacker=args[4].integer;}
            if(argc>5){if(args[5].kind!=GSC_NATIVE_ENTITY && args[5].kind!=GSC_NATIVE_UNDEFINED)return -1;if(args[5].kind==GSC_NATIVE_ENTITY)inflictor=args[5].integer;}
            return imp->radius_damage(g->eng,org,(float)range,(int)max_dmg,(int)min_dmg,attacker,inflictor,g->radius_ignore_players)?1:-1;
        }
        if(g->radius_ignore_players)return 1;
        for(c=0;c<COD_CLIENTS;c++) {
            hxSGPlayerView view;float dx,dy,dz,dist;int dmg;
            if(!g->players[c].active || g->players[c].team<0)continue;
            if(!imp->player_view(g->eng,c,&view) || view.health<=0)continue;
            dx=view.origin[0]-org[0];dy=view.origin[1]-org[1];dz=(view.origin[2]-24.f)-org[2];
            dist=sqrtf(dx*dx+dy*dy+dz*dz);
            if(dist>(float)range)continue;
            dmg=(int)(max_dmg-(max_dmg-min_dmg)*(dist/range)+0.5);
            if(dmg>0 && !imp->player_hitscan(g->eng,-3-c,org,0,0,dmg,(float)range))return -1;
        }
        return 1;
    }
    {int status=scene_native(g,name,receiver,entity,args,argc,out);if(status)return status;}
    if(receiver && (native_name(name,"giveWeapon")||native_name(name,"takeAllWeapons"))) {
        if(client<0||client>=COD_CLIENTS||imp->api_version<2)return -1;
        if(native_name(name,"giveWeapon")) {
            if(argc!=1||args[0].kind!=GSC_NATIVE_STRING||!imp->player_give_weapon)return -1;
            imp->player_give_weapon(g->eng,client,args[0].text);
        } else {
            if(argc||!imp->player_take_weapons)return -1;
            imp->player_take_weapons(g->eng,client);
        }
        return 1;
    }
    if(receiver && (native_name(name,"getPlayerAngles")||
                    native_name(name,"isOnGround"))) {
        hxSGPlayerView view;
        if(argc||client<0||client>=COD_CLIENTS||imp->api_version<6||!imp->player_view||
           !imp->player_view(g->eng,client,&view))return -1;
        if(native_name(name,"isOnGround")){out->kind=GSC_NATIVE_INT;out->integer=view.grounded;}
        else {out->kind=GSC_NATIVE_VECTOR;out->vector[0]=view.pitch;out->vector[1]=view.yaw;out->vector[2]=0;}
        return 1;
    }
    if(!receiver && native_name(name,"addtestclient")) {
        int slot = -1;
        if(argc) return -1;
        if(g->zbot.initialized) slot = zbot_add_bot(&g->zbot, NULL, NULL, "testclient");
        else if(imp->api_version>=13 && imp->bot_connect) slot = imp->bot_connect(g->eng, "testclient");
        if(slot >= 0 && slot < COD_CLIENTS) {
            out->kind = GSC_NATIVE_ENTITY;
            out->integer = -3 - slot;
        } else {
            out->kind = GSC_NATIVE_UNDEFINED;
        }
        return 1;
    }
    if(!receiver && (native_name(name,"isPlayer")||native_name(name,"isAlive"))) {
        hxSGPlayerView view;int id;
        if(argc!=1)return -1;
        out->kind=GSC_NATIVE_INT;out->integer=0;
        if(args[0].kind!=GSC_NATIVE_ENTITY||args[0].integer>-3)return 1;
        id=-3-args[0].integer;
        if(id<0||id>=COD_CLIENTS||!g->players[id].active)return 1;
        if(native_name(name,"isPlayer"))out->integer=1;
        else {
            if(g->players[id].team<0)return 1;
            if(imp->api_version<6||!imp->player_view||!imp->player_view(g->eng,id,&view))return -1;
            out->integer=view.health>0;
        }
        return 1;
    }
    if(native_name(name,"playSound")) {
        float origin[3];hxSGPlayerView view;const char* raw;
        if(!receiver||argc!=1||args[0].kind!=GSC_NATIVE_STRING||!imp->fx_sound)return -1;
        if(client>=0&&client<COD_CLIENTS){
            if(imp->api_version<6||!imp->player_view||!imp->player_view(g->eng,client,&view))return -1;
            memcpy(origin,view.origin,sizeof(origin));
        }else if(imp->api_version<8||!imp->entity_transform||!imp->entity_transform(g->eng,entity,NULL,NULL,origin,NULL)){
            raw=host_key(g,entity,"origin");
            if(!raw||sscanf(raw,"%f %f %f",origin,origin+1,origin+2)!=3)return -1;
        }
        imp->fx_sound(g->eng,args[0].text,origin[0],origin[1],origin[2]);return 1;
    }
    if(!receiver && (native_name(name,"print")||native_name(name,"println")||native_name(name,"logPrint"))) {
        char message[2048];size_t used=0;int i;
        if(!imp->print)return -1;
        for(i=0;i<argc;i++) {
            int n;
            if(args[i].kind==GSC_NATIVE_STRING)n=snprintf(message+used,sizeof(message)-used,"%s",args[i].text);
            else if(args[i].kind==GSC_NATIVE_INT)n=snprintf(message+used,sizeof(message)-used,"%d",args[i].integer);
            else if(args[i].kind==GSC_NATIVE_FLOAT)n=snprintf(message+used,sizeof(message)-used,"%g",args[i].number);
            else return -1;
            if(n<0||(size_t)n>=sizeof(message)-used)return -1;used+=(size_t)n;
        }
        message[used]=0;imp->print(g->eng,message);return 1;
    }
    return 0;
}

#define COD_NATIVE(name, flags) \
static int bound_##name(void* user,int receiver,int entity,const GscNativeValue* args,int argc,GscNativeValue* out){ \
    return host_native(user,#name,receiver,entity,args,argc,out); \
}
#include "gsc/cod1_natives.inc"
#undef COD_NATIVE
static int bound_placeSpawnpoint(void* user,int receiver,int entity,const GscNativeValue* args,int argc,GscNativeValue* out){
    (void)args;(void)out;return receiver && !argc && entity>=0 && host_place_spawnpoint(user,entity)?1:-1;
}
static int bound_world_state(void* user,int entity,int argc,int visible,int linked){
    return !argc && entity>=0 && host_state(user,entity,visible,linked)?1:-1;
}
#define COD_STATE(name, visible, linked) \
static int bound_##name(void* user,int receiver,int entity,const GscNativeValue* args,int argc,GscNativeValue* out){ \
    (void)args;(void)out;return receiver?bound_world_state(user,entity,argc,visible,linked):-1; \
}
COD_STATE(hide,0,-1) COD_STATE(show,1,-1) COD_STATE(notSolid,-1,0) COD_STATE(solid,-1,1)
COD_STATE(unlinkFromWorld,-1,0) COD_STATE(linkIntoWorld,-1,1)
#undef COD_STATE
static GscRuntime* create_linked_runtime(CodGame* g,const GscHost* host){
    GscNativeBinding entries[]={
#define COD_NATIVE(name, flags) {#name,flags,bound_##name,NULL},
#include "gsc/cod1_natives.inc"
#undef COD_NATIVE
        {"placeSpawnpoint",GSC_BIND_METHOD,bound_placeSpawnpoint,NULL},
        {"hide",GSC_BIND_METHOD,bound_hide,NULL},{"show",GSC_BIND_METHOD,bound_show,NULL},
        {"notSolid",GSC_BIND_METHOD,bound_notSolid,NULL},{"solid",GSC_BIND_METHOD,bound_solid,NULL},
        {"unlinkFromWorld",GSC_BIND_METHOD,bound_unlinkFromWorld,NULL},{"linkIntoWorld",GSC_BIND_METHOD,bound_linkIntoWorld,NULL}
    };
    int i;for(i=0;i<(int)(sizeof(entries)/sizeof(*entries));++i)entries[i].user=g;
    return gsc_runtime_create_linked(host,entries,(int)(sizeof(entries)/sizeof(*entries)));
}
static int host_sound_alias_exists(void* user,const char* name){CodGame* g=(CodGame*)user;return g->imp && g->imp->api_version>=12 && g->imp->sound_alias_exists && g->imp->sound_alias_exists(g->eng,name);}
/* Opt-in mod callback, not an invented stock CoD1 animation setter. */
static void on_animation_note(void* user,int client,const char* clip,const char* note,int layer){
    CodGame* g=(CodGame*)user;GscArgument args[3]={{0}};if(!g->runtime || !g->players[client].active)return;
    args[0].kind=3;args[0].text=note;args[1].kind=3;args[1].text=clip;args[2].kind=1;args[2].integer=layer;
    gsc_runtime_call_level(g->runtime,"callbackAnimationNotetrack",-3-client,args,3);
}
static void on_animation_reference(void* user,const char* tree,const char* clip){CodGame* g=(CodGame*)user;cod_animation_reference(g->animations,tree,clip);}
static int run_script(CodGame* g, const char* path) {
    char* text = NULL;
    size_t sz = 0;
    char names[16][64];
    int n, i, ok;
    GscHost host;
    if (!host_read(g, path, &text, &sz)) {
        if (g->imp->print) {
            char msg[256];
            snprintf(msg, sizeof(msg), "GSC missing: %s", path);
            g->imp->print(g->eng, msg);
        }
        return 0;
    }
    memset(&host, 0, sizeof(host));
    host.user = g;
    host.animation_reference=on_animation_reference;host.native_call = host_native;host.native_field=scene_field;host.sound_alias_exists=host_sound_alias_exists;
    host.read_file = host_read;
    host.ambient = on_ambient;
    host.give_weapon = on_weapon;
    host.take_weapons = on_take;
    host.set_team_names = on_teams;
    host.set_gametype = on_gt;
    host.precache = on_precache;
    host.set_cvar = on_cvar;
    host.get_cvar=host_cvar;host.entity_find=host_find;host.entity_key=host_key;
    host.entity_state=host_state;host.error=host_error;host.place_spawnpoint=host_place_spawnpoint;
    if(!g->runtime)g->runtime=g->linked_mode?create_linked_runtime(g,&host):gsc_runtime_create(&host);
    ok=g->runtime && (g->linked_mode?(g->linked_prepared || gsc_runtime_add_source(g->runtime,path,text,sz)):gsc_runtime_start(g->runtime,path,text,sz,"main",-1));
    if(g->linked_mode && !ok)g->script_failed=1;
    if (g->imp->print) {
        char msg[256];
        snprintf(msg, sizeof(msg), "GSC main: %s (%s)", path, ok ? (g->linked_mode?"prepared":"runtime scheduled") : "runtime failed");
        g->imp->print(g->eng, msg);
    }
    n = g->linked_mode?0:gsc_vm_scan_weapons(text, sz, names, 16);
    for (i = 0; i < n; i++) add_weapon(g, names[i]);
    free(text);
    return ok;
}

static void scene_reset(CodGame* g) {
    int id;
    if(g->imp&&g->imp->api_version>=8){
        for(id=0;id<128;id++)if(g->hud[id].active&&g->imp->hud_update)g->imp->hud_update(g->eng,id,NULL);
        for(id=0;id<32;id++)if(g->objectives[id].active&&g->imp->objective_update)g->imp->objective_update(g->eng,id,NULL);
    }
    memset(g->hud,0,sizeof(g->hud));memset(g->objectives,0,sizeof(g->objectives));memset(g->motions,0,sizeof(g->motions));
    for(id=0;id<COD_CLIENTS;id++){g->client_announcement_id[id]=-1;g->client_announcement_end[id]=0;}
}
static void sg_init(hxServerGame* self, hxSGEngine* eng, hxSGImports* imp, const char* map_name, int restart) {
    CodGame* g = (CodGame*)self;
    char path[160],map_path[160],gametype_path[160];
    const char* gt;
    const char* requested=imp && imp->cvar_string?imp->cvar_string(eng,"g_gsc_runtime"):"";
    const int linked=requested && native_name(requested,"linked");
    const int invalid=requested && requested[0] && !linked && !native_name(requested,"legacy");
    int map_ok=0,gametype_ok=0;
    (void)restart;
    scene_reset(g);
    gt=imp && imp->cvar_string?imp->cvar_string(eng,"g_gametype"):"";
    if(!gt || !gt[0])gt="dm";
    if(!restart || !g->preserving || linked!=g->linked_mode ||
       (linked && (strcmp(g->map,map_name?map_name:"") || strcmp(g->gametype,gt)))){
        gsc_runtime_destroy(g->runtime);g->runtime=NULL;g->linked_prepared=g->linked_callback=0;memset(g->reconnecting,0,sizeof(g->reconnecting));
    }
    g->linked_mode=linked;g->script_failed=invalid;
    g->preserving=0;g->clock_started=0;g->radius_ignore_players=0;
    g->last_threads=g->last_errors=g->last_start=-1;
    g->eng = eng;
    g->imp = imp;
    memset(g->motions,0,sizeof(g->motions));
    memset(g->hud,0,sizeof(g->hud));memset(g->hud_used,0,sizeof(g->hud_used));memset(g->objectives,0,sizeof(g->objectives));g->scene_now=0;g->hud_next=0;g->announcement_id=-1;g->announcement_end=0;memset(g->fog_from,0,sizeof(g->fog_from));memset(g->fog_target,0,sizeof(g->fog_target));g->fog_start=g->fog_end=0;memset(g->effects,0,sizeof(g->effects));
    g->nweapons = 0;
    memset(g->status_icons,0,sizeof(g->status_icons));g->status_icon_count=0;memset(g->head_icons,0,sizeof(g->head_icons));g->head_icon_count=0;
    memset(g->players,0,sizeof(g->players));
    memset(g->connecting,0,sizeof(g->connecting));memset(g->begin_pending,0,sizeof(g->begin_pending));memset(g->response_count,0,sizeof(g->response_count));
    g->allies[0] = g->axis[0] = g->ambient[0] = '\0';
    snprintf(g->map, sizeof(g->map), "%s", map_name ? map_name : "");
    snprintf(g->gametype, sizeof(g->gametype), "dm");
    if (imp && imp->cvar_string) {
        gt = imp->cvar_string(eng, "g_gametype");
        if (gt && gt[0]) snprintf(g->gametype, sizeof(g->gametype), "%s", gt);
    }
    if (!g->map[0]) return;
    cod_footsteps_init(&g->footsteps,eng,imp);
    g->footsteps.on_sound=bot_movement_sound;g->footsteps.sound_context=&g->zbot;
    g->animations=cod_animation_init(g->animations,eng,imp);
    cod_animation_note_callback(g->animations,on_animation_note,g);
    triggers_init(g);
    snprintf(map_path, sizeof(map_path), "maps/mp/%s.gsc", g->map);
    snprintf(gametype_path, sizeof(gametype_path), "maps/mp/gametypes/%s.gsc", g->gametype);
    if(invalid)host_error(g,"GSC unknown g_gsc_runtime selector; expected legacy or linked");
    else {map_ok=run_script(g,map_path);gametype_ok=run_script(g,gametype_path);}
    if(linked){
        if(!gametype_ok)g->script_failed=1;
        if(!g->script_failed && !g->linked_prepared){
            char* text=NULL;size_t size=0;
            if(host_read(g,"maps/mp/gametypes/_callbacksetup.gsc",&text,&size)){
                g->linked_callback=1;
                if(!gsc_runtime_add_source(g->runtime,"maps/mp/gametypes/_callbacksetup.gsc",text,size))g->script_failed=1;
                free(text);
            }
            if(map_ok && !gsc_runtime_require_entry(g->runtime,map_path,"main"))g->script_failed=1;
            if(!gsc_runtime_require_entry(g->runtime,gametype_path,"main"))g->script_failed=1;
            if(g->linked_callback && !gsc_runtime_require_entry(g->runtime,"maps/mp/gametypes/_callbacksetup.gsc","CodeCallback_StartGameType"))g->script_failed=1;
            if(!g->script_failed && !gsc_runtime_link(g->runtime))g->script_failed=1;
            if(!g->script_failed)g->linked_prepared=1;
        }
        if(!g->script_failed){
            if(map_ok && !gsc_runtime_start(g->runtime,map_path,NULL,0,"main",-1))g->script_failed=1;
            if(!gsc_runtime_start(g->runtime,gametype_path,NULL,0,"main",-1))g->script_failed=1;
            if(g->linked_callback && !gsc_runtime_start_gametype(g->runtime))g->script_failed=1;
        }
    }else if(!invalid)gsc_runtime_start_gametype(g->runtime);
    if(imp && imp->api_version>=12 && imp->sound_alias_exists)gsc_runtime_precache_dependencies(g->runtime);
    {
        int pass;
        for(pass=0;pass<32;pass++){
            int used=gsc_runtime_tick(g->runtime,0,200000);
            if(!gsc_runtime_loading_pending(g->runtime) || !used)break;
        }
        if(gsc_runtime_loading_pending(g->runtime))host_error(g,"GSC loading barrier has unfinished runnable/startup work");
    }
    if(imp && imp->cvar_set) {
        char value[32];
        g->last_threads=gsc_runtime_threads(g->runtime);g->last_errors=gsc_runtime_errors(g->runtime);
        if(g->script_failed && !g->last_errors)g->last_errors=1;
        imp->cvar_set(eng,"g_gsc_runtime_active",g->script_failed?"failed":linked?"linked":"legacy");
        imp->cvar_set(eng,"g_gsc_linked",linked && g->linked_prepared && !g->script_failed?"1":"0");
        snprintf(value,sizeof(value),"%d",g->last_threads);imp->cvar_set(eng,"g_gsc_threads",value);
        snprintf(value,sizeof(value),"%d",g->last_errors);imp->cvar_set(eng,"g_gsc_errors",value);
        imp->cvar_set(eng,"g_gsc_start_callback",gsc_runtime_level_function(g->runtime,"callbackStartGameType"));
        g->last_start=gsc_runtime_start_dispatched(g->runtime);
        snprintf(value,sizeof(value),"%d",g->last_start);imp->cvar_set(eng,"g_gsc_start_dispatched",value);
    }
    turret_init(g);
    placed_weapons(g,1);
    precache_nation(g, g->allies[0] ? g->allies : "american");
    precache_nation(g, g->axis[0] ? g->axis : "german");
    if (imp && imp->cvar_set) {
        const char* layout=imp->cvar_string?imp->cvar_string(eng,"scr_layoutimage"):NULL;
        if (!layout || !layout[0]) {
            snprintf(path,sizeof(path),"levelshots/layouts/hud@layout_%s.dds",g->map);
            imp->cvar_set(eng,"scr_layoutimage",path);
        }
        imp->cvar_set(eng, "g_ambient", g->ambient);
        imp->cvar_set(eng, "g_allies", g->allies);
        imp->cvar_set(eng, "g_axis", g->axis);
        imp->cvar_set(eng,"ui_scoreboard_teamplay",strcmp(g->gametype,"dm")?"1":"0");
        imp->cvar_set(eng,"vote_gametypes","dm tdm sd re bel hq");
        imp->cvar_set(eng,"vote_error_invalid","GAME_INVALIDVOTESTRING");
        {const char* labels[][2]={{"passed","GAME_VOTEPASSED"},{"failed","GAME_VOTEFAILED"},{"yes","MENU_VOTE_YES"},{"no","MENU_VOTE_NO"},{"map","GAME_VOTE_MAP"},{"map_restart","GAME_VOTE_MAPRESTART"},{"map_rotate","GAME_VOTE_NEXTMAP"},{"typemap","GAME_VOTE_GAMETYPE"},{"g_gametype","GAME_VOTE_GAMETYPE"},{"clientkick","GAME_VOTE_KICK"},{"kick","GAME_VOTE_KICK"},{"friendlyfire","PATCH_1_5_VOTE_FRIENDLYFIRE"},{"killcam","PATCH_1_5_VOTE_KILLCAM"},{"drawfriend","PATCH_1_5_VOTE_DRAWFRIEND"}};unsigned k;
         for(k=0;k<sizeof(labels)/sizeof(labels[0]);++k){char key[96];snprintf(key,sizeof(key),"vote_label_%s",labels[k][0]);imp->cvar_set(eng,key,labels[k][1]);}}

        imp->cvar_set(eng,"vote_cvar_friendlyfire","scr_friendlyfire");imp->cvar_set(eng,"vote_max_friendlyfire","3");
        imp->cvar_set(eng,"vote_cvar_killcam","scr_killcam");imp->cvar_set(eng,"vote_max_killcam","1");
        imp->cvar_set(eng,"vote_cvar_drawfriend","scr_drawfriend");imp->cvar_set(eng,"vote_max_drawfriend","1");
        imp->cvar_set(eng,"ui_scoreboard_sort_team_score","1");
        imp->cvar_set(eng,"ui_scoreboard_player_singular","CGAME_SB_PLAYER");
        imp->cvar_set(eng,"ui_scoreboard_player_plural","CGAME_SB_PLAYERS");
        imp->cvar_set(eng,"ui_scoreboard_team_0_label","@GAME_ALLIES");
        imp->cvar_set(eng,"ui_scoreboard_team_1_label","@GAME_AXIS");
        imp->cvar_set(eng,"ui_scoreboard_team_-1_label","@CGAME_SPECTATORS");
        snprintf(path,sizeof(path),"gfx/hud/hud@mpflag_%s.dds",g->allies[0]?g->allies:"american");
        imp->cvar_set(eng,"ui_scoreboard_team_0_icon",path);
        imp->cvar_set(eng,"ui_scoreboard_team_1_icon","gfx/hud/hud@mpflag_german.tga");
        imp->cvar_set(eng,"ui_scoreboard_team_-1_icon","gfx/hud/hud@mpflag_spectator.dds");
        imp->cvar_set(eng,"ui_scoreboard_gametype",!strcmp(g->gametype,"sd")?"@SD_SEARCH_AND_DESTROY":!strcmp(g->gametype,"tdm")?"@TDM_TEAM_DEATHMATCH":!strcmp(g->gametype,"re")?"@RE_RETRIEVAL":!strcmp(g->gametype,"bel")?"@BEL_BEHIND_ENEMY_LINES":!strcmp(g->gametype,"hq")?"@HQ_HEADQUARTERS":"@DM_DEATHMATCH");
        imp->cvar_set(eng,"ui_spectator_next_text","CGAME_FOLLOWNEXTPLAYER\037{+attack}");
        imp->cvar_set(eng,"ui_spectator_previous_text","CGAME_FOLLOWPREVIOUSPLAYER\037{+ads}");
        imp->cvar_set(eng,"ui_spectator_stop_text","CGAME_FOLLOWSTOP\037{+activate}");

        snprintf(path, sizeof(path), "serverinfo_%s", g->gametype);
        imp->cvar_set(eng, "ui_start_menu", path);
        snprintf(path, sizeof(path), "team_%sgerman", g->allies[0] ? g->allies : "american");
        imp->cvar_set(eng, "ui_menu_team", path);
        imp->cvar_set(eng, "ui_menu_session", path);
        imp->cvar_set(eng, "ui_menu_viewmap", "viewmap");
        imp->cvar_set(eng, "ui_menu_callvote", "callvote");
        snprintf(path, sizeof(path), "weapon_%s", g->allies[0] ? g->allies : "american");
        imp->cvar_set(eng, "ui_menu_weapon", path);
        imp->cvar_set(eng,"ui_voice_spatial","1");imp->cvar_set(eng,"ui_voice_teamtext","1");
        imp->cvar_set(eng, "ui_voice_prefix", g->allies[0] ? g->allies : "american");
        imp->cvar_set(eng, "ui_voice_allies", g->allies[0] ? g->allies : "american");
        imp->cvar_set(eng, "ui_voice_axis", g->axis[0] ? g->axis : "german");
        imp->cvar_set(eng, "ui_sound_alias_german_youre_crazy", "german_are_you_crazy");
        {
            static const char* weapons[] = { "kar98k", "mp40", "mp44", "kar98ksniper", "m1garand", "m1carbine", "thompson", "bar", "springfield", "enfield", "sten", "bren", "enfieldsniper", "mosinnagant", "svt40", "ppsh", "mosinnagantsniper", "panzerfaust", "fg42" };
            unsigned i;
            for (i = 0; i < sizeof(weapons)/sizeof(weapons[0]); ++i) {
                char key[96], value[32]; const char* configured;
                snprintf(key, sizeof(key), "scr_allow_%s", weapons[i]);
                configured = imp->cvar_string ? imp->cvar_string(eng, key) : NULL;
                snprintf(value, sizeof(value), "%s", configured && configured[0] ? configured : (strcmp(weapons[i], "fg42") == 0 ? "0" : "1"));
                snprintf(key, sizeof(key), "ui_allow_%s", weapons[i]);
                imp->cvar_set(eng, key, value);
            }
            const char* allow_vote = imp->cvar_string ? imp->cvar_string(eng, "g_allowVote") : NULL;
            char vote[32]; snprintf(vote, sizeof(vote), "%s", allow_vote && allow_vote[0] ? allow_vote : "1");
            imp->cvar_set(eng, "ui_allowvote", vote);
            {const char* names[]={"maprestart","maprotate","map","typemap","kick","tempbanuser","drawfriend","friendlyfire","killcam"};unsigned k;
             for(k=0;k<sizeof(names)/sizeof(names[0]);++k){char key[96],ui[96];const char* current;
                snprintf(key,sizeof(key),"g_allowVote%s",!strcmp(names[k],"typemap")?"gametype":names[k]);current=imp->cvar_string?imp->cvar_string(eng,key):NULL;
                snprintf(ui,sizeof(ui),"ui_allowvote%s",names[k]);imp->cvar_set(eng,ui,current && current[0]?current:"1");}}

        }
    }
    if(!restart || !g->zbot.initialized)zbot_init(&g->zbot, eng, imp, g, bot_client_command, g->map, g->gametype, g->allies, g->axis);
    else zbot_restart_round(&g->zbot);
    g->zbot.attacking_team=!strcmp(gsc_runtime_game_string(g->runtime,"attackers"),"axis")?1:0;
}


static const char* player_nation(const CodGame* g,int team){return team==1?"german":g->allies[0]?g->allies:"american";}
static const char* default_primary(const char* nation){return !strcmp(nation,"german")?"mp40_mp":!strcmp(nation,"russian")?"mosin_nagant_mp":!strcmp(nation,"british")?"enfield_mp":"m1garand_mp";}
static int weapon_allowed(CodGame* g,int team,const char* name) {
    static const char* german[]={"kar98k_mp","mp40_mp","mp44_mp","kar98k_sniper_mp","panzerfaust_mp","fg42_mp",NULL};
    static const char* american[]={"m1garand_mp","m1carbine_mp","thompson_mp","bar_mp","springfield_mp",NULL};
    static const char* british[]={"enfield_mp","sten_mp","bren_mp","springfield_mp",NULL};
    static const char* russian[]={"mosin_nagant_mp","ppsh_mp","mosin_nagant_sniper_mp",NULL};
    const char* nation=player_nation(g,team);const char** names=!strcmp(nation,"german")?german:!strcmp(nation,"russian")?russian:!strcmp(nation,"british")?british:american;
    int i;if(team<0)return 0;
    for(i=0;names[i];i++)if(!strcmp(names[i],name)){
        char key[96],short_name[64];const char* allow;
        snprintf(short_name,sizeof(short_name),"%s",name);short_name[strlen(short_name)-3]=0;
        if(!strcmp(short_name,"kar98k_sniper"))strcpy(short_name,"kar98ksniper");
        if(!strcmp(short_name,"mosin_nagant"))strcpy(short_name,"mosinnagant");
        if(!strcmp(short_name,"mosin_nagant_sniper"))strcpy(short_name,"mosinnagantsniper");
        if(!strcmp(short_name,"springfield")&&!strcmp(nation,"british"))strcpy(short_name,"enfieldsniper");
        snprintf(key,sizeof(key),"scr_allow_%s",short_name);allow=g->imp&&g->imp->cvar_string?g->imp->cvar_string(g->eng,key):NULL;
        if(allow&&*allow)return atoi(allow)!=0;
        return strcmp(name,"fg42_mp")!=0;
    }
    return 0;
}
static void give_player_loadout(CodGame* g,int client) {
    const char *nation,*pistol,*grenade;int team;
    if(!g || !g->imp || client<0 || client>=COD_CLIENTS || g->players[client].team<0)return;
    team=g->players[client].team;nation=player_nation(g,team);
    pistol=team==1||!strcmp(nation,"russian")?"luger_mp":"colt_mp";
    grenade=team==1?"stielhandgranate_mp":!strcmp(nation,"russian")?"rgd-33russianfrag_mp":!strcmp(nation,"british")?"mk1britishfrag_mp":"fraggrenade_mp";
    if(g->imp->player_take_weapons)g->imp->player_take_weapons(g->eng,client);
    if(g->imp->player_give_weapon){g->imp->player_give_weapon(g->eng,client,pistol);g->imp->player_give_weapon(g->eng,client,g->players[client].weapon);g->imp->player_give_weapon(g->eng,client,grenade);}
}
static void sync_player_script(CodGame* g,int client) {
    GscArgument value;const char* team=g->players[client].team<0?"spectator":g->players[client].team==1?"axis":"allies";
    memset(&value,0,sizeof(value));value.kind=3;value.text=team;
    gsc_runtime_set_entity_value(g->runtime,-3-client,"sessionteam",NULL,&value);
    gsc_runtime_set_entity_value(g->runtime,-3-client,"pers","team",&value);
    value.text=g->players[client].weapon;gsc_runtime_set_entity_value(g->runtime,-3-client,"pers","weapon",&value);
    value.text=g->players[client].team<0?"spectator":"playing";gsc_runtime_set_entity_value(g->runtime,-3-client,"sessionstate",NULL,&value);
}
static void apply_team(CodGame* g,int client,int team) {
    const char* nation;char menu[96];
    if(!g || !g->imp || client<0 || client>=COD_CLIENTS)return;
    turret_release(g,client);g->players[client].active=1;g->players[client].team=team;
    nation=player_nation(g,team);
    if(team<0)g->players[client].weapon[0]=0;
    else if(!weapon_allowed(g,team,g->players[client].weapon))snprintf(g->players[client].weapon,sizeof(g->players[client].weapon),"%s",default_primary(nation));
    if(team>=0 && !weapon_allowed(g,team,g->players[client].weapon)){
        static const char* choices[]={"m1garand_mp","m1carbine_mp","thompson_mp","bar_mp","springfield_mp","kar98k_mp","mp40_mp","mp44_mp","kar98k_sniper_mp","enfield_mp","sten_mp","bren_mp","mosin_nagant_mp","ppsh_mp","mosin_nagant_sniper_mp","panzerfaust_mp","fg42_mp"};unsigned i;
        g->players[client].weapon[0]=0;for(i=0;i<sizeof(choices)/sizeof(choices[0]);i++)if(weapon_allowed(g,team,choices[i])){snprintf(g->players[client].weapon,sizeof(g->players[client].weapon),"%s",choices[i]);break;}
    }

    if(client==0 && g->imp->cvar_set){
        g->imp->cvar_set(g->eng,"g_team",team<0?"spectator":team==1?"axis":"allies");
        snprintf(menu,sizeof(menu),"weapon_%s",nation);g->imp->cvar_set(g->eng,"ui_menu_weapon",menu);
        g->imp->cvar_set(g->eng,"ui_voice_prefix",nation);
    }
    if(g->imp->player_set_team)g->imp->player_set_team(g->eng,client,team);
    if(team>=0)give_player_loadout(g,client);else if(g->imp->player_take_weapons)g->imp->player_take_weapons(g->eng,client);
    g->players[client].linked=0;g->trig_use_buttons[client]=0;sync_player_script(g,client);
}
static int auto_team(CodGame* g,int client){int allies=0,axis=0,i;for(i=0;i<COD_CLIENTS;i++)if(i!=client&&g->players[i].active){allies+=g->players[i].team==0;axis+=g->players[i].team==1;}return allies<=axis?0:1;}


static int script_available(CodGame* g) {
    return g->runtime && g->imp && g->imp->api_version>=12 && g->imp->script_player_rules && g->imp->player_state && gsc_runtime_level_function(g->runtime,"callbackPlayerConnect")[0];
}
static int lifecycle_ready(CodGame* g) {return script_available(g) && gsc_runtime_start_complete(g->runtime);}
static void lifecycle_tick(CodGame* g) {gsc_runtime_tick(g->runtime,g->scene_now,20000);}
static int pump_player_event(CodGame* g,int client) {
    GscArgument event[2]={{0}};int i;
    if(!lifecycle_ready(g) || !g->players[client].active)return 0;
    if(g->connecting[client]){
        g->connecting[client]=0;gsc_runtime_call_level(g->runtime,"callbackPlayerConnect",-3-client,NULL,0);return 1;
    }
    if(g->begin_pending[client] && gsc_runtime_is_waiting(g->runtime,-3-client,"begin")){
        g->begin_pending[client]=0;gsc_runtime_notify(g->runtime,-3-client,"begin",NULL,0);return 1;
    }
    while(g->response_count[client] && g->responses[client][0].deadline<g->scene_now){
        for(i=1;i<g->response_count[client];i++)g->responses[client][i-1]=g->responses[client][i];g->response_count[client]--;
    }
    if(!g->response_count[client] || !gsc_runtime_is_waiting(g->runtime,-3-client,"menuresponse"))return 0;
    event[0].kind=event[1].kind=3;event[0].text=g->responses[client][0].menu;event[1].text=g->responses[client][0].response;
    if(g->responses[client][0].kind==1)event[0].text=gsc_runtime_game_string(g->runtime,"menu_team");
    if(g->responses[client][0].kind==2)event[0].text=g->players[client].current_menu;
    gsc_runtime_notify(g->runtime,-3-client,"menuresponse",event,2);
    for(i=1;i<g->response_count[client];i++)g->responses[client][i-1]=g->responses[client][i];g->response_count[client]--;return 1;
}
static int queue_player_event(CodGame* g,int client,int kind,const char* menu,const char* response){
    int slot=g->response_count[client];if(slot>=8){host_error(g,"GSC client response queue full");return 1;}
    g->responses[client][slot].kind=kind;g->responses[client][slot].deadline=g->scene_now+2000;
    snprintf(g->responses[client][slot].menu,128,"%s",menu);snprintf(g->responses[client][slot].response,128,"%s",response);
    g->response_count[client]++;if(pump_player_event(g,client))lifecycle_tick(g);return 1;
}
static int sg_damage(hxServerGame* self,int victim,int attacker,int damage) {
    CodGame* g=(CodGame*)self;GscArgument args[9]={{0}};
    if(!lifecycle_ready(g))return 0;
    if(attacker>=0){args[0].kind=args[1].kind=4;args[0].integer=args[1].integer=-3-attacker;}
    args[2].kind=1;args[2].integer=damage;args[3].kind=1;
    args[4].kind=3;args[4].text="MOD_UNKNOWN";
    args[5].kind=3;args[5].text=g->imp->player_weapon_name && attacker>=0?g->imp->player_weapon_name(g->eng,attacker,-1):"none";
    args[8].kind=3;args[8].text="none";
    return gsc_runtime_call_level(g->runtime,"callbackPlayerDamage",-3-victim,args,9);
}
static const char* damage_means(const hxSGDamage* d) {
    static const char* means[]={"MOD_UNKNOWN","MOD_RIFLE_BULLET","MOD_PROJECTILE","MOD_PROJECTILE_SPLASH","MOD_GRENADE_SPLASH","MOD_FALLING","MOD_TRIGGER_HURT","MOD_MELEE"};
    if(d->means && *d->means)return d->means;
    if(d->cause==1 && d->weapon_class){
        if(native_name(d->weapon_class,"pistol"))return "MOD_PISTOL_BULLET";
        if(native_name(d->weapon_class,"smg"))return "MOD_SMG_BULLET";
        if(native_name(d->weapon_class,"mg"))return "MOD_MG_BULLET";
    }
    if(d->cause==3 && (!d->weapon || !strcmp(d->weapon,"none")))return "MOD_EXPLOSIVE";
    return means[d->cause>=0 && d->cause<8?d->cause:0];
}
static const char* damage_location(const hxSGDamage* d){
    static const char* hit[]={"none","head","torso_upper","right_hand","right_leg_upper"};
    return d->hit_location && *d->hit_location?d->hit_location:hit[d->hit_group>=0 && d->hit_group<5?d->hit_group:0];
}
static void sg_killed_detail(hxServerGame* self,const hxSGDamage* d){
    CodGame* g=(CodGame*)self;GscArgument args[7]={{0}};int i;
    if(!d || !lifecycle_ready(g) || d->victim<0 || d->victim>=COD_CLIENTS)return;
    if(g->imp && g->imp->level_time_ms)cod_animation_frame(g->animations,d->victim,g->imp->level_time_ms(g->eng),0);
    if(d->inflictor!=-1){args[0].kind=4;args[0].integer=d->inflictor;}
    if(d->attacker>=0){args[1].kind=4;args[1].integer=-3-d->attacker;}
    args[2].kind=1;args[2].integer=d->amount;args[3].kind=3;args[3].text=damage_means(d);
    args[4].kind=3;args[4].text=d->weapon?d->weapon:"none";
    if(d->has_direction){args[5].kind=5;for(i=0;i<3;i++)args[5].vector[i]=d->direction[i];}
    args[6].kind=3;args[6].text=damage_location(d);
    gsc_runtime_call_level(g->runtime,"callbackPlayerKilled",-3-d->victim,args,7);
    /* Stock DM/TDM own their bounded health queues in GSC. Round-based
     * SD/RE omit that drop, so provide the requested death pickup there. */
    if((!strcmp(g->gametype,"sd") || !strcmp(g->gametype,"re")) && g->imp->api_version>=12 && g->imp->player_drop_item)g->imp->player_drop_item(g->eng,d->victim,NULL);
}

static int sg_damage_detail(hxServerGame* self,const hxSGDamage* d) {
    CodGame* g=(CodGame*)self;GscArgument args[9]={{0}};int i;
    static const char* means[]={"MOD_UNKNOWN","MOD_RIFLE_BULLET","MOD_PROJECTILE","MOD_PROJECTILE_SPLASH","MOD_GRENADE_SPLASH","MOD_FALLING","MOD_TRIGGER_HURT","MOD_MELEE"};
    static const char* hit[]={"none","head","torso_upper","right_hand","right_leg_upper"};
    if(!d || d->victim<0 || d->victim>=COD_CLIENTS || !lifecycle_ready(g))return 0;
    if(d->inflictor!=-1){args[0].kind=4;args[0].integer=d->inflictor;}
    if(d->attacker>=0){args[1].kind=4;args[1].integer=-3-d->attacker;}
    args[2].kind=1;args[2].integer=d->amount;args[3].kind=1;args[3].integer=d->flags;
    args[4].kind=3;args[4].text=damage_means(d);
    args[5].kind=3;args[5].text=d->weapon?d->weapon:"none";
    if(d->has_point){args[6].kind=5;for(i=0;i<3;i++)args[6].vector[i]=d->point[i];}
    if(d->has_direction){args[7].kind=5;for(i=0;i<3;i++)args[7].vector[i]=d->direction[i];}
    args[8].kind=3;args[8].text=damage_location(d);
    return gsc_runtime_call_level(g->runtime,"callbackPlayerDamage",-3-d->victim,args,9);
}

static void sg_killed(hxServerGame* self,int victim,int attacker,int damage) {
    CodGame* g=(CodGame*)self;GscArgument args[7]={{0}};if(!lifecycle_ready(g))return;
    if(attacker>=0){args[0].kind=args[1].kind=4;args[0].integer=args[1].integer=-3-attacker;}
    args[2].kind=1;args[2].integer=damage;args[3].kind=3;args[3].text=attacker==victim?"MOD_SUICIDE":"MOD_UNKNOWN";
    args[4].kind=3;args[4].text=g->imp->player_weapon_name && attacker>=0?g->imp->player_weapon_name(g->eng,attacker,-1):"none";
    args[6].kind=3;args[6].text="none";
    gsc_runtime_call_level(g->runtime,"callbackPlayerKilled",-3-victim,args,7);
    if((!strcmp(g->gametype,"sd") || !strcmp(g->gametype,"re")) && g->imp->api_version>=12 && g->imp->player_drop_item)g->imp->player_drop_item(g->eng,victim,NULL);
}

static int handle_cmd(CodGame* g,int client,const char* cmd) {
    char verb[32],arg[64],extra[2];
    if(!g || !cmd || client<0 || client>=COD_CLIENTS)return 0;
    if(sscanf(cmd,"%31s %63s",verb,arg)!=2)return 0;
    if(strcmp(verb,"menuresponse") && sscanf(cmd,"%31s %63s %1s",verb,arg,extra)!=2)return 0;
    if(!strcmp(verb,"vsay")){hxSGBotState source;if(g->imp->bot_state && g->imp->bot_state(g->eng,client,&source))zbot_hear_sound(&g->zbot,client,source.origin,900);zbot_voice_order(&g->zbot,client,arg);return 1;}
    if(!strcmp(verb,"botmenu") && zbot_is_bot(&g->zbot,client) && lifecycle_ready(g)){
        GscArgument value={0};ZBotAgent* bot=&g->zbot.bots[client];
        const char* menu=g->players[client].current_menu;const char* response=NULL;
        if(gsc_runtime_get_entity_value(g->runtime,-3-client,"pers","weapon",&value) && value.kind==3 && value.text && value.text[0])return 2;
        if(!gsc_runtime_is_waiting(g->runtime,-3-client,"menuresponse") || g->response_count[client])return 1;
        if(!strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_serverinfo")))response="close";
        else if(!strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_team")))response=!strcmp(g->gametype,"bel")?"axis":bot->desired_team==1?"axis":bot->desired_team==0?"allies":"autoassign";
        else if(menu[0] && (!strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_weapon_allies")) || !strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_weapon_axis")) || !strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_weapon_all")) || !strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_weapon_allies_only")) || !strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_weapon_axis_only")))){
            int team=!strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_weapon_axis")) || !strcmp(menu,gsc_runtime_game_string(g->runtime,"menu_weapon_axis_only"));
            const char* nation=player_nation(g,team);
            const char* weapon=zbot_weapon_for(nation,g->zbot.profiles[bot->profile_idx].pref_class);
            if(!weapon_allowed(g,team,weapon))weapon=default_primary(nation);
            if(!weapon_allowed(g,team,weapon))return 1;
            snprintf(bot->desired_weapon,sizeof(bot->desired_weapon),"%s",weapon);response=bot->desired_weapon;
        }
        if(response && menu[0])return queue_player_event(g,client,0,menu,response);
        return 1;
    }
    if(script_available(g)) {
        if(!strcmp(verb,"menuresponse")){
            char menu[128],response[128];if(sscanf(cmd,"%*s %127s %127s",menu,response)!=2)return 1;
            if(!strncmp(menu,"quick",5))return 0;
            return queue_player_event(g,client,0,menu,response);
        }
        if(!strcmp(verb,"team") || !strcmp(verb,"weapon"))return queue_player_event(g,client,!strcmp(verb,"team")?1:2,"",arg);
    }
    if(!strcmp(verb,"team")){
        if(!strcmp(arg,"axis")||!strcmp(arg,"german"))apply_team(g,client,1);
        else if(!strcmp(arg,"allies")||!strcmp(arg,player_nation(g,0)))apply_team(g,client,0);
        else if(!strcmp(arg,"autoassign"))apply_team(g,client,auto_team(g,client));
        else if(!strcmp(arg,"spectator"))apply_team(g,client,-1);
        return 1;
    }
    if(!strcmp(verb,"weapon")) {
        char name[64];size_t n=strlen(arg);snprintf(name,sizeof(name),"%s",arg);
        if(!strcmp(arg,"kar98ksniper"))strcpy(name,"kar98k_sniper");
        if(!strcmp(arg,"mosinnagant"))strcpy(name,"mosin_nagant");
        if(!strcmp(arg,"mosinnagantsniper"))strcpy(name,"mosin_nagant_sniper");
        if(!strcmp(arg,"enfieldsniper"))strcpy(name,"springfield");
        n=strlen(name);if(n<3||strcmp(name+n-3,"_mp")){if(n+3>=sizeof(name))return 1;strcat(name,"_mp");}
        if(weapon_allowed(g,g->players[client].team,name)){
            snprintf(g->players[client].weapon,sizeof(g->players[client].weapon),"%s",name);give_player_loadout(g,client);sync_player_script(g,client);
        }else if(g->imp && g->imp->print)g->imp->print(g->eng,"Weapon selection unavailable for this team.\n");
        return 1;
    }
    return 0;
}
static void sg_begin(hxServerGame* self,int client) {
    CodGame* g=(CodGame*)self;
    if(!g->imp || client<0 || client>=COD_CLIENTS || g->script_failed)return;
    if(script_available(g)){g->begin_pending[client]=1;if(pump_player_event(g,client))lifecycle_tick(g);}
    else apply_team(g,client,-1);
}

static int sg_drain_disconnects(hxServerGame* self,int max_virtual_ms) {
    CodGame* g=(CodGame*)self;int elapsed,i,pending=0;
    if(!g->runtime)return 0;
    if(max_virtual_ms<0)max_virtual_ms=0;if(max_virtual_ms>5000)max_virtual_ms=5000;
    for(elapsed=0;elapsed<=max_virtual_ms;elapsed+=10){
        pending=0;
        for(i=0;i<COD_CLIENTS*2;i++)if(g->retired_players[i] && gsc_runtime_entity_busy(g->runtime,g->retired_players[i]))pending++;
        if(!pending)break;
        gsc_runtime_tick(g->runtime,g->scene_now+elapsed,20000);
    }
    return pending;
}

static void sg_shutdown(hxServerGame* self, int restart) {
    CodGame* g=(CodGame*)self;
    int i;for(i=0;i<COD_CLIENTS;i++)turret_release(g,i);
    if(!restart){zbot_shutdown(&g->zbot);cod_animation_free(g->animations);g->animations=NULL;}
    if(g->imp && g->imp->api_version>=12 && g->imp->script_player_rules)g->imp->script_player_rules(g->eng,0);
    scene_reset(g);memset(g->retired_players,0,sizeof(g->retired_players));
    if(restart && g->runtime && gsc_runtime_reset_map(g->runtime)){
        g->preserving=1;for(i=0;i<COD_CLIENTS;i++)g->reconnecting[i]=g->players[i].active;
    }else{gsc_runtime_destroy(g->runtime);g->runtime=NULL;g->preserving=0;memset(g->reconnecting,0,sizeof(g->reconnecting));}
    if(g->imp && g->imp->cvar_set){g->imp->cvar_set(g->eng,"g_gsc_threads","0");g->imp->cvar_set(g->eng,"g_gsc_errors","0");}
    if(g->imp && g->imp->cvar_set){g->imp->cvar_set(g->eng,"g_gsc_start_callback","");g->imp->cvar_set(g->eng,"g_gsc_start_dispatched","0");}
}
static int bot_script_integer(CodGame* g,int entity,const char* field,int* out){
    GscArgument value={0};if(!gsc_runtime_get_entity_value(g->runtime,entity,field,NULL,&value))return 0;
    if(value.kind==1){*out=value.integer;return 1;}return 0;
}
static int bot_script_position(CodGame* g,int entity,float* out){
    GscArgument value={0};int i;if(!gsc_runtime_get_entity_value(g->runtime,entity,"origin",NULL,&value) || value.kind!=5)return 0;
    for(i=0;i<3;++i)out[i]=(float)value.vector[i];return 1;
}
static int bot_script_entity(CodGame* g,int entity,const char* field,int* out){
    GscArgument value={0};if(!gsc_runtime_get_entity_value(g->runtime,entity,field,NULL,&value) || value.kind!=4 || value.integer<0)return 0;
    *out=value.integer;return 1;
}
static int bot_trigger_model(CodGame* g,int entity){const char* model=host_key(g,entity,"model");return model && model[0]=='*'?atoi(model+1):0;}
static void bot_objectives(CodGame* g){
    struct Objective {int id,owner,carrier;ZBotObjectiveTask pickup,goal;} objectives[32];
    int ids[32],count=0,n,i,client,hq=!strcmp(g->gametype,"hq"),re=!strcmp(g->gametype,"re");
    if(!g->runtime || !g->zbot.initialized || (!hq && !re))return;
    n=gsc_runtime_find_entities(g->runtime,"targetname",hq?"hqradio":"retrieval_objective",ids,32);
    if(re)g->zbot.attacking_team=native_name(gsc_runtime_game_string(g->runtime,"re_attackers"),"axis")?1:0;
    for(i=0;i<n && count<32;++i){
        struct Objective* objective=&objectives[count];int hidden=1,trigger,goal,num;
        GscArgument value={0};memset(objective,0,sizeof(*objective));objective->id=ids[i];objective->owner=objective->carrier=-1;
        if(hq){
            if(!bot_script_integer(g,ids[i],"hidden",&hidden) || hidden || !bot_script_position(g,ids[i],objective->pickup.origin))continue;
            if(gsc_runtime_get_entity_value(g->runtime,ids[i],"team",NULL,&value) && value.kind==3)
                objective->owner=native_name(value.text,"allies")?0:native_name(value.text,"axis")?1:-1;
            objective->pickup.kind=ZBOT_OBJECTIVE_HQ;objective->pickup.entity=ids[i];objective->pickup.radius=75;objective->pickup.origin[2]+=24;
            if(bot_script_integer(g,ids[i],"radius",&num) && num>0 && num<115)objective->pickup.radius=num*.65f;
        }else{
            if(!bot_script_entity(g,ids[i],"trigger",&trigger) || !bot_script_entity(g,ids[i],"goal",&goal) ||
               !bot_script_position(g,trigger,objective->pickup.origin) || !bot_script_position(g,goal,objective->goal.origin))continue;
            objective->pickup.kind=ZBOT_OBJECTIVE_RE_PICKUP;objective->pickup.entity=trigger;objective->pickup.radius=64;objective->pickup.origin[2]+=24;
            objective->goal.kind=ZBOT_OBJECTIVE_RE_DELIVER;objective->goal.entity=goal;objective->goal.model=bot_trigger_model(g,goal);objective->goal.radius=64;objective->goal.origin[2]+=24;
            if(bot_script_integer(g,ids[i],"objnum",&num))for(client=0;client<COD_CLIENTS;++client)if(g->players[client].active &&
                gsc_runtime_get_entity_index(g->runtime,-3-client,"hasobj",num,&value) && value.kind==4 && value.integer==ids[i]){objective->carrier=client;break;}
        }
        ++count;
    }
    for(client=0;client<COD_CLIENTS;++client)if(zbot_is_bot(&g->zbot,client)){
        hxSGBotState state;ZBotObjectiveTask task={0};float best=1e30f;
        if(!g->imp->bot_state || !g->imp->bot_state(g->eng,client,&state) || state.health<=0){zbot_set_objective(&g->zbot,client,NULL);continue;}
        for(i=0;i<count;++i){
            struct Objective* objective=&objectives[i];ZBotObjectiveTask candidate=objective->pickup;float score=0;int axis;
            if(hq){if(objective->owner==state.team){candidate.kind=ZBOT_OBJECTIVE_GUARD;score=-1e12f;}}
            else if(objective->carrier==client) {task=objective->goal;break;}
            else if(state.team!=g->zbot.attacking_team || objective->carrier>=0){candidate=objective->carrier>=0?objective->goal:objective->pickup;candidate.kind=ZBOT_OBJECTIVE_GUARD;}
            else score=-1e11f;
            for(axis=0;axis<3;++axis){float delta=state.origin[axis]-candidate.origin[axis];score+=delta*delta;}
            if(score<best){best=score;task=candidate;}
        }
        zbot_set_objective(&g->zbot,client,task.kind?&task:NULL);
    }
}
static void sg_frame(hxServerGame* self, int time_ms) {
    CodGame* g=(CodGame*)self;int i;
    if(time_ms>=g->placed_poll){placed_weapons(g,0);g->placed_poll=time_ms+500;}
    for(i=0;i<COD_CLIENTS;i++)if(g->turrets.clients[i].mounted>=0) {
        hxSGPlayerView v;
        if(!g->imp || g->imp->api_version<6 || !g->imp->player_view || !g->imp->player_view(g->eng,i,&v) || v.health<=0 || !v.locked)turret_release(g,i);
    }
    if(g->imp && g->imp->cvar_set && g->last_hint!=g->hint_available){g->last_hint=g->hint_available;g->imp->cvar_set(g->eng,"ui_use_available",g->hint_available?"1":"0");g->imp->cvar_set(g->eng,"ui_turret_use_available",g->hint_available?"1":"0");}
    if(g->imp && g->imp->cvar_set)g->imp->cvar_set(g->eng,"ui_turret_use_text",g->turrets.clients[0].mounted>=0?"leave MG42":"use MG42");
    if(!g->clock_started){g->clock_origin=time_ms;g->clock_started=1;}
    g->scene_now=time_ms-g->clock_origin;
    if(g->announcement_id>=0 && g->announcement_end && g->scene_now>=g->announcement_end){
        if(g->imp->hud_update)g->imp->hud_update(g->eng,g->announcement_id,NULL);
        g->hud[g->announcement_id].active=0;g->announcement_end=0;
    }
    for(i=0;i<COD_CLIENTS;i++)if(g->client_announcement_id[i]>=0 && g->client_announcement_end[i] && g->scene_now>=g->client_announcement_end[i]){
        if(g->imp->hud_update)g->imp->hud_update(g->eng,g->client_announcement_id[i],NULL);
        g->hud[g->client_announcement_id[i]].active=0;g->client_announcement_end[i]=0;
    }
    if(g->fog_end>g->fog_start){fog_publish(g,g->scene_now);if(g->scene_now>=g->fog_end)g->fog_start=g->fog_end;}
    scene_motion_tick(g,g->scene_now);presentation_tick(g,g->scene_now);
    triggers_frame(g);
    for(i=0;i<COD_CLIENTS;i++)pump_player_event(g,i);
    gsc_runtime_tick(g->runtime,time_ms-g->clock_origin,20000);
    for(i=0;i<COD_CLIENTS*2;i++)if(g->retired_players[i] && !gsc_runtime_entity_busy(g->runtime,g->retired_players[i])){
        int id;for(id=0;id<128;id++)if(g->hud[id].active && g->hud[id].retired_owner==g->retired_players[i]){
            gsc_runtime_forget_entity(g->runtime,COD_HUD_BASE+g->hud[id].handle);g->hud[id].active=0;
        }
        gsc_runtime_forget_entity(g->runtime,g->retired_players[i]);g->retired_players[i]=0;
    }
    for(i=0;i<COD_CLIENTS;i++)cod_animation_frame(g->animations,i,time_ms,g->turrets.clients[i].mounted>=0);
    if(g->imp && g->imp->cvar_set && g->imp->cvar_string){const char* names[]={"friendlyfire","killcam","drawfriend"};int n;
        for(n=0;n<3;++n){char source[64],dest[64];const char* current;snprintf(source,sizeof(source),"scr_%s",names[n]);snprintf(dest,sizeof(dest),"ui_%s",names[n]);current=g->imp->cvar_string(g->eng,source);g->imp->cvar_set(g->eng,dest,current && current[0]?current:"0");}}
    bot_objectives(g);
    zbot_run_frame(&g->zbot,time_ms);
    cod_footsteps_frame(&g->footsteps,g->eng,g->imp,time_ms);
    if(g->imp && g->imp->cvar_set) {
        char value[32];int threads=gsc_runtime_threads(g->runtime),errors=gsc_runtime_errors(g->runtime);
        if(g->script_failed && !errors)errors=1;
        if(threads!=g->last_threads){snprintf(value,sizeof(value),"%d",threads);g->imp->cvar_set(g->eng,"g_gsc_threads",value);g->last_threads=threads;}
        if(errors!=g->last_errors){snprintf(value,sizeof(value),"%d",errors);g->imp->cvar_set(g->eng,"g_gsc_errors",value);g->last_errors=errors;}
        if(gsc_runtime_start_dispatched(g->runtime)!=g->last_start) {
            g->last_start=gsc_runtime_start_dispatched(g->runtime);snprintf(value,sizeof(value),"%d",g->last_start);
            g->imp->cvar_set(g->eng,"g_gsc_start_dispatched",value);
            g->imp->cvar_set(g->eng,"g_gsc_start_callback",gsc_runtime_level_function(g->runtime,"callbackStartGameType"));
        }
    }
}
static int sg_connect(hxServerGame* self, int client, const char* info) {
    CodGame* g=(CodGame*)self;(void)info;
    if(client<0 || client>=COD_CLIENTS || g->script_failed)return 1;
    memset(&g->footsteps.clients[client],0,sizeof(g->footsteps.clients[client]));
    cod_animation_reset_client(g->animations,client);
    if(g->imp && g->imp->api_version>=17 && g->imp->entity_animation)g->imp->entity_animation(g->eng,-3-client,NULL);
    if(script_available(g)) {
        if(!g->reconnecting[client])gsc_runtime_forget_entity(g->runtime,-3-client);
        g->reconnecting[client]=0;
        memset(&g->players[client],0,sizeof(g->players[client]));g->players[client].active=1;g->players[client].team=-1;
        strcpy(g->players[client].sessionteam,"spectator");
        g->imp->script_player_rules(g->eng,1);g->imp->player_set_team(g->eng,client,-1);
        {hxSGPlayerState initial={0};initial.mask=HX_PLAYER_STATE;initial.state=0;g->imp->player_state(g->eng,client,&initial,NULL);}
        if(g->imp->player_take_weapons)g->imp->player_take_weapons(g->eng,client);
        g->response_count[client]=g->begin_pending[client]=0;g->connecting[client]=1;
        if(pump_player_event(g,client))lifecycle_tick(g);
    }
    return 0;
}
static void sg_disconnect(hxServerGame* self, int client) {
    CodGame* g=(CodGame*)self;turret_release(g,client);
    {int id;for(id=0;id<32;++id)if(g->objectives[id].active && g->objectives[id].player==client)g->objectives[id].player=-1;}
    if(client>=0 && client<COD_CLIENTS)memset(&g->footsteps.clients[client],0,sizeof(g->footsteps.clients[client]));
    zbot_client_disconnect(&g->zbot,client);
    cod_animation_reset_client(g->animations,client);
    if(g->imp && g->imp->api_version>=17 && g->imp->entity_animation)g->imp->entity_animation(g->eng,-3-client,NULL);
    if(lifecycle_ready(g) && client>=0 && client<COD_CLIENTS){
        int retired,slot,id;
        gsc_runtime_notify(g->runtime,-3-client,"disconnect",NULL,0);
        gsc_runtime_cancel_entity_threads(g->runtime,-3-client);
        gsc_runtime_call_level(g->runtime,"callbackPlayerDisconnect",-3-client,NULL,0);lifecycle_tick(g);
        retired=gsc_runtime_retire_entity(g->runtime,-3-client);
        for(slot=0;slot<COD_CLIENTS*2 && g->retired_players[slot];slot++);
        if(slot<COD_CLIENTS*2)g->retired_players[slot]=retired;
        else{host_error(g,"GSC pending disconnect limit");gsc_runtime_forget_entity(g->runtime,retired);}
        for(id=0;id<128;id++)if(g->hud[id].active && g->hud[id].value.client==client){
            g->hud[id].retired_owner=retired;if(g->imp->hud_update)g->imp->hud_update(g->eng,id,NULL);
        }
    }
    if(client>=0&&client<COD_CLIENTS){memset(&g->players[client],0,sizeof(g->players[client]));g->connecting[client]=g->begin_pending[client]=g->response_count[client]=0;}
}
static int sg_command(hxServerGame* self, int client, const char* cmd) {
    CodGame* g = (CodGame*)self;
    return handle_cmd(g, client, cmd);
}
static void sg_think(hxServerGame* self, int client, int msec, int f, int r, int u, int p, int y, int ro, int b,
                     int w) {
    (void)msec;(void)f;(void)r;(void)u;(void)ro;(void)w;
    if(client>=0 && client<COD_CLIENTS && ((CodGame*)self)->players[client].team>=0) {
        turret_think((CodGame*)self,client,b,p*.01f,y*.01f);
        trigger_use_think((CodGame*)self,client,b);
    }
}
static int sg_console(hxServerGame* self, const char* cmd) {
    CodGame* g = (CodGame*)self;
    if(!strcmp(cmd,"bot_status") && g->imp && g->imp->print){
        int client;for(client=0;client<COD_CLIENTS;++client)if(zbot_is_bot(&g->zbot,client)){
            char message[512];GscArgument value={0};const char* weapon="";
            if(gsc_runtime_get_entity_value(g->runtime,-3-client,"pers","weapon",&value) && value.kind==3)weapon=value.text;
            snprintf(message,sizeof(message),"BOT_SCRIPT client=%d menu=%s pers_weapon=%s waiting=%d pending=%d joined=%d\n",client,g->players[client].current_menu,weapon?weapon:"",gsc_runtime_is_waiting(g->runtime,-3-client,"menuresponse"),g->response_count[client],g->zbot.bots[client].joined);
            g->imp->print(g->eng,message);
            {hxSGBotState state;ZBotAgent* b=&g->zbot.bots[client];memset(&state,0,sizeof(state));g->imp->bot_state(g->eng,client,&state);
            snprintf(message,sizeof(message),"BOT_OBJECTIVE client=%d kind=%d entity=%d model=%d anchor=%d pos=%g,%g,%g target=%g,%g,%g radius=%g\n",client,b->objective.kind,b->objective.entity,b->objective.model,b->objective_wp,state.origin[0],state.origin[1],state.origin[2],b->objective.origin[0],b->objective.origin[1],b->objective.origin[2],b->objective.radius);g->imp->print(g->eng,message);}
        }
    }
    if (zbot_console_command(&g->zbot, cmd)) return 1;
    return handle_cmd(g, 0, cmd);
}
static void sg_destroy(hxServerGame* self) {
    CodGame* g = (CodGame*)self;
    if (g) {
        zbot_shutdown(&g->zbot);
        cod_animation_free(g->animations);
        gsc_runtime_destroy(g->runtime);
        free(g);
    }
}


SG_EXPORT hxServerGame* helix_sgame_create(void) {
    CodGame* g = (CodGame*)calloc(1, sizeof(CodGame));
    if (!g) return NULL;
    {int i;for(i=0;i<COD_CLIENTS;i++)g->turrets.clients[i].mounted=-1;}
    g->api.api_version = HELIX_SGAME_API_VERSION;
    g->api.init = sg_init;
    g->api.shutdown = sg_shutdown;
    g->api.drain_disconnects = sg_drain_disconnects;
    g->api.run_frame = sg_frame;
    g->api.player_killed_detail=sg_killed_detail;g->api.player_damage_detail=sg_damage_detail;g->api.player_damage=sg_damage;g->api.player_killed=sg_killed;
    g->api.client_connect = sg_connect;
    g->api.client_begin = sg_begin;
    g->api.client_disconnect = sg_disconnect;
    g->api.client_command = sg_command;
    g->api.client_think = sg_think;
    g->api.console_command = sg_console;
    g->api.destroy = sg_destroy;
    return &g->api;
}
