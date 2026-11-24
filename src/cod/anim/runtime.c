/* GPL-2.0-or-later. Prepared native player animation selection. */
#include "anim/runtime.h"
#include "anim/playeranim.h"
#include "anim/notes.h"
#include "anim/controls.h"
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CLIENTS 64
#define CLIPS 2048
typedef struct PreparedClip {char name[128],path[192];float duration,speed;int valid,loop,branch,nnotes;hxSGAnimationNote* notes;} PreparedClip;
typedef struct AnimClient {CodPlayerControls controls;int known,event_until,event_mask,event_once,event_serial;char event_paths[3][192];struct {const PreparedClip* clip;CodNoteCursor cursor;float rate;int loop;} tracks[3];int last_time;hxSGAnimationInput previous;char previous_weapon[128];} AnimClient;
struct CodNativeAnimation {CodAnimProgram* program;hxSGEngine* eng;hxSGImports* imp;PreparedClip clips[CLIPS];int count;void (*emit)(void*,int,const char*,const char*,int);void* emit_user;struct{char name[128];CodAnimProgram* program;} trees[16];int ntrees;AnimClient clients[CLIENTS];};
static int equal(const char* a,const char* b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return *a==*b;}
static int read_text(hxSGEngine* eng,hxSGImports* imp,const char* path,char** out){unsigned char* raw=NULL;unsigned bytes=0;char* text;if(!imp->fs_read(eng,path,&raw,&bytes))return 0;
    if(bytes>4*1024*1024){imp->fs_free(eng,raw);return 0;}text=(char*)malloc((size_t)bytes+1);if(!text){imp->fs_free(eng,raw);return 0;}memcpy(text,raw,bytes);text[bytes]=0;imp->fs_free(eng,raw);*out=text;return 1;}
static void load_notes(CodNativeAnimation* a,PreparedClip* c){
    int i,count;free(c->notes);c->notes=NULL;c->nnotes=0;
    if(a->imp->api_version<18 || !a->imp->animation_note)return;
    count=a->imp->animation_note(a->eng,c->path,-1,NULL);if(count<=0 || count>256)return;
    c->notes=(hxSGAnimationNote*)calloc((size_t)count,sizeof(*c->notes));if(!c->notes)return;
    for(i=0;i<count;i++)if(!a->imp->animation_note(a->eng,c->path,i,c->notes+i)){free(c->notes);c->notes=NULL;return;}
    c->nnotes=count;
}
CodNativeAnimation* cod_animation_init(CodNativeAnimation* a,hxSGEngine* eng,hxSGImports* imp){
    char *script=NULL,*tree=NULL,error[192]={0};int i,missing=0,branches=0;
    if(!imp || imp->api_version<17 || !imp->animation_info || !imp->animation_input || !imp->entity_animation || !imp->fs_read || !imp->fs_free)return a;
    if(!a){a=(CodNativeAnimation*)calloc(1,sizeof(*a));if(!a)return NULL;
        if(!read_text(eng,imp,"mp/playeranim.script",&script)||!read_text(eng,imp,"animtrees/multiplayer.atr",&tree) || !(a->program=cod_anim_parse(script,tree,error,sizeof(error)))){
            if(imp->print){char msg[256];snprintf(msg,sizeof(msg),"Native animation data unavailable: %s",*error?error:"player script/tree missing");imp->print(eng,msg);}free(script);free(tree);free(a);return NULL;
        }
        free(script);free(tree);a->count=cod_anim_clip_count(a->program);
        if(a->count>CLIPS){cod_anim_free(a->program);free(a);return NULL;}
        for(i=0;i<a->count;i++){PreparedClip* c=a->clips+i;snprintf(c->name,sizeof(c->name),"%s",cod_anim_clip(a->program,i));c->branch=cod_anim_node(a->program,c->name)!=1;snprintf(c->path,sizeof(c->path),"cod/animations/%s.hxa",c->name);}
    }
    a->eng=eng;a->imp=imp;memset(a->clients,0,sizeof(a->clients));
    for(i=0;i<a->count;i++){
        PreparedClip* c=a->clips+i;c->valid=0;
        if(c->branch){branches++;continue;}
        if(imp->precache_asset)imp->precache_asset(eng,HX_PRECACHE_ANIMATION,c->path);
        if(imp->animation_info(eng,c->path,&c->duration,&c->loop,&c->speed)){c->valid=1;load_notes(a,c);}else missing++;
    }
    if(imp->cvar_set){char text[32];snprintf(text,sizeof(text),"%d",a->count-missing-branches);imp->cvar_set(eng,"g_anim_prepared",text);snprintf(text,sizeof(text),"%d",missing);imp->cvar_set(eng,"g_anim_missing",text);snprintf(text,sizeof(text),"%d",branches);imp->cvar_set(eng,"g_anim_branches",text);}
    return a;
}
void cod_animation_free(CodNativeAnimation* a){if(a){int i;for(i=0;i<a->count;i++)free(a->clips[i].notes);for(i=0;i<a->ntrees;i++)cod_anim_free(a->trees[i].program);cod_anim_free(a->program);free(a);}}
void cod_animation_reset_client(CodNativeAnimation* a,int client){if(a&&client>=0&&client<CLIENTS)memset(a->clients+client,0,sizeof(a->clients[client]));}
static const PreparedClip* prepared(const CodNativeAnimation* a,const char* name){int i;for(i=0;i<a->count;i++)if(equal(a->clips[i].name,name))return a->clips[i].valid?a->clips+i:NULL;return NULL;}
static CodAnimProgram* reference_tree(CodNativeAnimation* a,const char* tree){
    int i;char path[256],error[160],*text=NULL;
    if(!a || !tree || !*tree || strlen(tree)>127 || tree[0]=='/' || strstr(tree,"..") || strpbrk(tree,":\\\""))return NULL;
    if(equal(tree,"multiplayer"))return a->program;
    for(i=0;i<a->ntrees;i++)if(equal(a->trees[i].name,tree))return a->trees[i].program;
    if(a->ntrees>=16 || !a->imp->precache_asset)return NULL;
    snprintf(path,sizeof(path),"animtrees/%s.atr",tree);
    if(!a->imp->precache_asset(a->eng,HX_PRECACHE_DATA,path) || !read_text(a->eng,a->imp,path,&text))return NULL;
    {CodAnimProgram* program=cod_anim_parse("",text,error,sizeof(error));free(text);if(!program)return NULL;
     snprintf(a->trees[a->ntrees].name,sizeof(a->trees[a->ntrees].name),"%s",tree);a->trees[a->ntrees++].program=program;return program;}
}
void cod_animation_reference(CodNativeAnimation* a,const char* tree,const char* clip){
    CodAnimProgram* program=reference_tree(a,tree);PreparedClip* item;int i;
    if(!program || !clip || cod_anim_node(program,clip)!=1 || strlen(clip)>127 || strspn(clip,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(clip))return;
    for(i=0;i<a->count;i++)if(equal(a->clips[i].name,clip))return;
    if(a->count>=CLIPS)return;
    item=a->clips+a->count++;snprintf(item->name,sizeof(item->name),"%s",clip);snprintf(item->path,sizeof(item->path),"cod/animations/%s.hxa",clip);
    if(a->imp->precache_asset && a->imp->precache_asset(a->eng,HX_PRECACHE_ANIMATION,item->path))item->valid=a->imp->animation_info(a->eng,item->path,&item->duration,&item->loop,&item->speed);
    if(item->valid)load_notes(a,item);
}
int cod_animation_length(CodNativeAnimation* a,const char* tree,const char* clip,float* result){
    const PreparedClip* c;CodAnimProgram* program;int i;
    if(!a || !tree || !clip || !result)return 0;
    program=equal(tree,"multiplayer")?a->program:NULL;
    for(i=0;!program && i<a->ntrees;i++)if(equal(a->trees[i].name,tree))program=a->trees[i].program;
    if(!program || cod_anim_node(program,clip)!=1)return 0;
    c=prepared(a,clip);if(!c)return 0;*result=c->duration;return 1;
}
int cod_animation_has_note(CodNativeAnimation* a,const char* tree,const char* clip,const char* note,int* found){
    float seconds;const PreparedClip* c;int i;if(!note || !found || !cod_animation_length(a,tree,clip,&seconds) || a->imp->api_version<18)return 0;
    c=prepared(a,clip);*found=0;for(i=0;i<c->nnotes;i++)if(!strcmp(c->notes[i].name,note)){*found=1;break;}return 1;
}
void cod_animation_note_callback(CodNativeAnimation* a,void (*emit)(void*,int,const char*,const char*,int),void* user){if(a){a->emit=emit;a->emit_user=user;}}
typedef struct NoteTarget {CodNativeAnimation* a;int client,layer;const char* clip;} NoteTarget;
static void emit_note(void* user,const char* name){NoteTarget* t=(NoteTarget*)user;if(t->a->emit)t->a->emit(t->a->emit_user,t->client,t->clip,name,t->layer);}
static void update_notes(CodNativeAnimation* a,int client,int now,const hxSGAnimation* command){
    AnimClient* c=a->clients+client;int layer;double dt=c->known && now>=c->last_time?(now-c->last_time)*.001:0;
    for(layer=0;layer<3;layer++){
        const PreparedClip* next=NULL;int i;NoteTarget target={a,client,layer,NULL};
        if(c->tracks[layer].clip){const PreparedClip* previous=c->tracks[layer].clip;target.clip=previous->name;
            if(cod_notes_advance(&c->tracks[layer].cursor,previous->notes,previous->nnotes,previous->duration,c->tracks[layer].loop,dt,c->tracks[layer].rate,emit_note,&target)<0 && a->imp->print)a->imp->print(a->eng,"Animation notetrack event budget exceeded");}
        for(i=0;command->clips[layer] && i<a->count;i++)if(!strcmp(a->clips[i].path,command->clips[layer])){next=a->clips+i;break;}
        if(next!=c->tracks[layer].clip || (command->restart_mask&(1u<<layer))){
            c->tracks[layer].clip=next;if(next){cod_notes_reset(&c->tracks[layer].cursor,next->duration,command->rates[layer]);target.clip=next->name;
                cod_notes_advance(&c->tracks[layer].cursor,next->notes,next->nnotes,next->duration,next->loop && !(command->once_mask&(1u<<layer)),0,command->rates[layer],emit_note,&target);}
        }
        c->tracks[layer].rate=command->rates[layer];c->tracks[layer].loop=next && next->loop && !(command->once_mask&(1u<<layer));
    }
    c->last_time=now;
}
static const char* move_type(const hxSGAnimationInput* s,char* buffer,const char** strafe){
    const float speed=sqrtf(s->velocity[0]*s->velocity[0]+s->velocity[1]*s->velocity[1]);
    const float yaw=s->yaw*.01745329252f,forward=cosf(yaw)*s->velocity[0]+sinf(yaw)*s->velocity[1],side=-sinf(yaw)*s->velocity[0]+cosf(yaw)*s->velocity[1];
    const int backwards=forward < -speed*.25f;const char* stance=s->stance==2?"prone":s->stance==1?"cr":"";
    *strafe=!backwards && fabsf(side)>speed*.5f?(side>0?"left":"right"):"not";
    if(speed<5){snprintf(buffer,32,"idle%s",stance);return buffer;}
    snprintf(buffer,32,"%s%s%s",speed<140 || s->ads || s->stance==2?"walk":"run",stance,backwards?"bk":"");return buffer;
}
void cod_animation_frame(CodNativeAnimation* a,int client,int now,int mounted){
    hxSGAnimationInput state;AnimClient* c;CodAnimInput input;CodAnimAction base,event;char move[32],context[64];const char* event_name=NULL;const char* strafe;
    hxSGAnimation command;hxSGJointPose joints[7];int has_controls=0;int i,mask=0;float speed;
    if(!a || client<0 || client>=CLIENTS || !a->imp->animation_input(a->eng,client,&state))return;
    c=a->clients+client;
    if(!state.state){cod_animation_reset_client(a,client);return;}
    if(a->imp->api_version>=19 && a->imp->animation_pose_input && a->imp->entity_pose){hxSGPoseInput pose;
        if(a->imp->animation_pose_input(a->eng,client,&pose)){cod_player_controls(&c->controls,&state,&pose,now,mounted,joints);has_controls=1;}}
    input.state=&state;input.move=move_type(&state,move,&strafe);input.strafing=strafe;input.mounted=mounted;
    memset(&base,0,sizeof(base));memset(&event,0,sizeof(event));memset(&command,0,sizeof(command));command.rates[0]=command.rates[1]=command.rates[2]=1;command.fade_seconds=.1f;command.held_tag=mounted?"":"tag_weapon_right";
    snprintf(context,sizeof(context),"combat/%s",input.move);cod_anim_select(a->program,context,&input,(unsigned)client,&base);
    if(has_controls && c->controls.turn && !strncmp(input.move,"idle",4) && !mounted && !state.on_ladder && state.health>0){
        CodAnimAction turn;snprintf(context,sizeof(context),"combat/turn%s",c->controls.turn>0?"left":"right");
        if(cod_anim_select(a->program,context,&input,(unsigned)client,&turn) && *turn.clips[1]){snprintf(base.clips[1],sizeof(base.clips[1]),"%s",turn.clips[1]);}}
    if(state.health<=0){if(!c->known || c->previous.health>0)event_name="death";}
    else if(c->known){
        if(c->previous.health<=0){c->event_until=0;c->event_mask=0;command.fade_seconds=0;}
        else if(state.reloading && !c->previous.reloading)event_name="reload";
        else if(state.firing && state.event_time!=c->previous.event_time)event_name=state.firing==2?"meleeattack":"fireweapon";
        else if(state.weapon_phase==1 && c->previous.weapon_phase!=1)event_name="dropweapon";
        else if(state.weapon_phase==2 && c->previous.weapon_phase!=2)event_name="raiseweapon";
        else if(!state.grounded && c->previous.grounded && state.velocity[2]>0)event_name=strstr(move,"bk")?"jumpbk":"jump";
        else if(state.grounded && !c->previous.grounded && c->previous.velocity[2]<-10)event_name="land";
    }
    if(state.on_ladder && (!c->known || !c->previous.on_ladder || now>=c->event_until))event_name=state.velocity[2]<0?"climbdown":"climbup";
    if(event_name && cod_anim_select(a->program,event_name,&input,(unsigned)(client+(++c->event_serial)),&event)){
        int duration=0;c->event_mask=0;c->event_once=0;
        for(i=0;i<3;i++)if(*event.clips[i]){
            const PreparedClip* clip=prepared(a,event.clips[i]);if(!clip)continue;
            c->event_mask|=1<<i;c->event_once|=1<<i;snprintf(c->event_paths[i],sizeof(c->event_paths[i]),"%s",clip->path);
            if((int)(clip->duration*1000)>duration)duration=(int)(clip->duration*1000);
            if(event.duration[i]>duration)duration=event.duration[i];
            if(event.blend[i]>=0)command.fade_seconds=(float)event.blend[i]*.001f;
        }
        c->event_until=state.health<=0?2147483647:now+duration;
        if(*event.sound && a->imp->fx_sound)a->imp->fx_sound(a->eng,event.sound,state.origin[0],state.origin[1],state.origin[2]);
    }
    speed=sqrtf(state.velocity[0]*state.velocity[0]+state.velocity[1]*state.velocity[1]);
    for(i=0;i<3;i++)if(*base.clips[i]){
        const PreparedClip* clip=prepared(a,base.clips[i]);if(clip){command.clips[i]=clip->path;if(i!=2 && clip->speed>1 && speed>5)command.rates[i]=fmaxf(.25f,fminf(3.f,speed/clip->speed));}
    }
    if(c->event_until>now){
        mask=c->event_mask;
        if(mask&1){for(i=0;i<3;i++)command.clips[i]=NULL;}
        for(i=0;i<3;i++)if(mask&(1<<i))command.clips[i]=c->event_paths[i];
        command.once_mask=(unsigned)c->event_once;
        if(event_name)command.restart_mask=(unsigned)mask;
    }
    a->imp->entity_animation(a->eng,-3-client,&command);
    /* Upright controllers replace spine/pelvis transforms; preserve the authored
       lying pose rather than forcing standing joint offsets onto prone clips. */
    if(has_controls)a->imp->entity_pose(a->eng,-3-client,joints,state.stance==2?0:7,.05f);
    update_notes(a,client,now,&command);
    c->previous=state;snprintf(c->previous_weapon,sizeof(c->previous_weapon),"%s",state.weapon_name?state.weapon_name:"");c->previous.weapon_name=c->previous_weapon;c->known=1;
}
