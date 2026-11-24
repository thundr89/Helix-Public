/* GPL-2.0-or-later. CoD1 BSP surface types occupy bits 20..24.
 * Native alias spelling is "asphault". Material names are never inferred from textures. */
#include "footsteps.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const char* surfaces[]={"default","bark","brick","carpet","cloth","concrete","dirt","flesh","foliage","glass","grass","gravel","ice","metal","mud","paper","plaster","rock","sand","snow","water","wood","asphault"};
static const char* modes[]={"run","walk","prone"};
const char* cod_surface_name(int flags){unsigned type=((unsigned)flags>>20)&31u;return type<23?surfaces[type]:"default";}
void cod_footsteps_init(CodFootsteps* f,hxSGEngine* eng,const hxSGImports* imp){
    int mode,type,variant;memset(f,0,sizeof(*f));f->last_time=-1;f->rng=0x12345678u;
    if(!imp || imp->api_version<17 || !imp->sound_alias_exists || !imp->precache_asset)return;
    for(mode=0;mode<3;mode++)for(type=1;type<23;type++){
        char alias[128];
        for(variant=1;variant<=8;variant++){
            snprintf(alias,sizeof(alias),"step_%s_%s__variant_%d",modes[mode],surfaces[type],variant);
            if(imp->sound_alias_exists(eng,alias) && imp->precache_asset(eng,HX_PRECACHE_SOUND,alias))f->variants[mode][type]|=1u<<(variant-1);
        }
        if(!f->variants[mode][type]){
            snprintf(alias,sizeof(alias),"step_%s_%s",modes[mode],surfaces[type]);
            if(imp->sound_alias_exists(eng,alias) && imp->precache_asset(eng,HX_PRECACHE_SOUND,alias))f->fallback[mode][type]=1;
        }
    }
}
void cod_footsteps_frame(CodFootsteps* f,hxSGEngine* eng,const hxSGImports* imp,int now){
    int client,dt=now-f->last_time;
    if(!imp || imp->api_version<17 || !imp->animation_input || !imp->player_view || !imp->trace_query || !imp->fx_sound)return;
    if(f->last_time<0 || dt<=0 || dt>250){for(client=0;client<64;client++)f->clients[client].known=0;}
    if(now==f->last_time)return;f->last_time=now;
    for(client=0;client<64;client++){
        hxSGAnimationInput state;hxSGPlayerView view;hxSGTrace hit;float dx,dy,distance,stride,start[3],end[3],zero[3]={0,0,0};
        int mode,type,variant=0,choices[8],count=0,i;unsigned mask;char alias[160];
        if(!imp->animation_input(eng,client,&state) || !imp->player_view(eng,client,&view) || state.state!=1 || state.health<=0 || state.on_ladder || view.locked){f->clients[client].known=0;continue;}
        if(!state.grounded){
            /* Thin seams/stair transitions may lose contact for a tick. Preserve
             * cadence briefly, but never accumulate airborne travel or emit in air. */
            if(now-f->clients[client].last_ground>150)f->clients[client].known=0;
            memcpy(f->clients[client].origin,state.origin,sizeof(state.origin));continue;
        }
        f->clients[client].last_ground=now;
        mode=state.stance==2?2:state.stance==1 || hypotf(state.velocity[0],state.velocity[1])<140?1:0;
        dx=state.origin[0]-f->clients[client].origin[0];dy=state.origin[1]-f->clients[client].origin[1];distance=hypotf(dx,dy);
        memcpy(f->clients[client].origin,state.origin,sizeof(state.origin));
        if(!f->clients[client].known || f->clients[client].mode!=mode || distance>192 || !isfinite(distance)){
            f->clients[client].known=1;f->clients[client].mode=mode;f->clients[client].distance=0;continue;
        }
        if(distance<.05f || hypotf(state.velocity[0],state.velocity[1])<5){f->clients[client].distance=0;continue;}
        stride=mode==0?90.f:mode==1?70.f:45.f;f->clients[client].distance+=distance;
        if(f->clients[client].distance<stride)continue;
        f->clients[client].distance=fmodf(f->clients[client].distance,stride);
        memcpy(start,state.origin,sizeof(start));memcpy(end,start,sizeof(end));end[2]-=32;
        /* Solid and water, excluding invisible player-only clips. */
        if(!imp->trace_query(eng,start,end,zero,zero,9,-3-client,&hit) || hit.fraction>=1 || hit.allsolid)continue;
        if(hit.contents&8)type=20;
        else{if(hit.startsolid || hit.normal[2]<.55f)continue;type=((unsigned)hit.surface_flags>>20)&31u;}
        if(hit.surface_flags&0x2000)continue; /* native noSteps */
        if(type<=0 || type>=23)type=5;
        if(!f->variants[mode][type] && !f->fallback[mode][type])type=5;
        mask=f->variants[mode][type];
        for(i=0;i<8;i++)if(mask&(1u<<i))choices[count++]=i+1;
        if(count){f->rng=f->rng*1664525u+1013904223u;variant=choices[(f->rng>>8)%(unsigned)count];
            if(count>1 && variant==f->clients[client].last_variant){for(i=0;i<count;i++)if(choices[i]!=variant){variant=choices[i];break;}}
            snprintf(alias,sizeof(alias),"voice/step_%s_%s__variant_%d",modes[mode],surfaces[type],variant);
        }else if(f->fallback[mode][type])snprintf(alias,sizeof(alias),"voice/step_%s_%s",modes[mode],surfaces[type]);else continue;
        f->clients[client].last_variant=variant;
        if(f->on_sound)f->on_sound(f->sound_context,client,state.origin,mode==0?700.f:mode==1?280.f:120.f);
        imp->fx_sound(eng,alias,state.origin[0],state.origin[1],state.origin[2]-24);
    }
}
