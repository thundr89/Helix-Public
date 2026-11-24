/* GPL-2.0-or-later. Footstep timing and surface selection without engine/assets. */
#include "footsteps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static hxSGAnimationInput state[2];static int locked;static int active[2],flags=21<<20,water,now,sounds,errors,precache_count;static char last[160],previous[160];
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);errors++;}}while(0)
static int animation(hxSGEngine* e,int id,hxSGAnimationInput* out){(void)e;if(id<0||id>=2||!active[id])return 0;*out=state[id];return 1;}
static int view(hxSGEngine* e,int id,hxSGPlayerView* out){(void)e;if(id<0||id>=2||!active[id])return 0;memset(out,0,sizeof(*out));out->locked=locked;return 1;}
static int trace(hxSGEngine* e,const float* a,const float* b,const float* mi,const float* ma,int mask,int ignore,hxSGTrace* out){(void)e;(void)a;(void)b;(void)mi;(void)ma;(void)mask;(void)ignore;memset(out,0,sizeof(*out));out->fraction=.75f;out->normal[2]=1;out->surface_flags=flags;out->contents=water?8:1;return 1;}
static int exists(hxSGEngine* e,const char* name){const char* p=strstr(name,"__variant_");(void)e;return p && atoi(p+10)>=1 && atoi(p+10)<=4;}
static int precache(hxSGEngine* e,int kind,const char* name){(void)e;(void)name;CHECK(kind==HX_PRECACHE_SOUND);precache_count++;return 1;}
static void sound(hxSGEngine* e,const char* alias,float x,float y,float z){(void)e;(void)x;(void)y;CHECK(z==0);strcpy(previous,last);snprintf(last,sizeof(last),"%s",alias);sounds++;}
static hxSGImports imp;static CodFootsteps f;
static void advance(float distance,int frames){int i;for(i=0;i<frames;i++){now+=50;state[0].origin[0]+=distance;cod_footsteps_frame(&f,NULL,&imp,now);}}
static void reset(void){memset(state,0,sizeof(state));memset(active,0,sizeof(active));active[0]=1;state[0].health=100;state[0].state=1;state[0].grounded=1;state[0].origin[2]=24;state[0].velocity[0]=190;now=0;sounds=0;locked=0;last[0]=previous[0]=0;flags=21<<20;water=0;cod_footsteps_init(&f,NULL,&imp);advance(0,1);}
int main(void){int before;imp.api_version=HELIX_SGAME_API_VERSION;imp.animation_input=animation;imp.player_view=view;imp.trace_query=trace;imp.fx_sound=sound;imp.precache_asset=precache;imp.sound_alias_exists=exists;
 reset();CHECK(precache_count==3*22*4);CHECK(!strcmp(cod_surface_name(21<<20),"wood"));CHECK(!strcmp(cod_surface_name(13<<20),"metal"));CHECK(!strcmp(cod_surface_name(31<<20),"default"));
 advance(0,20);CHECK(sounds==0);advance(10,9);CHECK(sounds==1 && strstr(last,"step_run_wood__variant_"));advance(10,9);CHECK(sounds==2 && strcmp(last,previous));
 reset();state[0].velocity[0]=100;flags=13<<20;advance(10,9);CHECK(sounds==1 && strstr(last,"step_walk_metal"));
 reset();state[0].stance=2;state[0].velocity[0]=38;flags=19<<20;advance(5,11);CHECK(sounds==1 && strstr(last,"step_prone_snow"));
 reset();state[0].velocity[0]=0;advance(10,20);CHECK(sounds==0);
 reset();flags=(21<<20)|0x2000;advance(10,20);CHECK(sounds==0);
 reset();state[0].grounded=0;advance(10,20);CHECK(sounds==0);state[0].grounded=1;advance(0,1);advance(10,9);CHECK(sounds==1);
 reset();state[0].state=0;advance(10,20);CHECK(sounds==0);state[0].state=1;state[0].health=0;advance(10,20);CHECK(sounds==0);
 reset();advance(10,5);state[0].grounded=0;advance(10,2);CHECK(sounds==0);state[0].grounded=1;advance(10,4);CHECK(sounds==1);
 reset();state[0].on_ladder=1;advance(10,20);CHECK(sounds==0);
 reset();advance(250,1);CHECK(sounds==0);advance(0,1);CHECK(sounds==0);
 reset();active[1]=1;state[1]=state[0];advance(0,1);{int i;for(i=0;i<9;i++){state[1].origin[0]+=10;advance(0,1);}}CHECK(sounds==1); /* Same rules for another client, including a bot. */
 reset();locked=1;advance(10,20);CHECK(sounds==0);
 reset();flags=31<<20;advance(10,9);CHECK(sounds==1 && strstr(last,"step_run_concrete"));
 reset();f.variants[0][21]=0;advance(10,9);CHECK(sounds==1 && strstr(last,"step_run_concrete"));
 reset();water=1;advance(10,9);CHECK(sounds==1 && strstr(last,"step_run_water"));
 reset();advance(10,8);now+=1000;state[0].origin[0]+=100;cod_footsteps_frame(&f,NULL,&imp,now);CHECK(sounds==0);
 reset();before=sounds;cod_footsteps_frame(&f,NULL,&imp,now);CHECK(before==sounds);
 printf("Footsteps: %d failures\n",errors);return errors?1:0;
}
