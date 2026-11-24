
#include "helix/game/iserver_game.h"
#include "bot/zbot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int ai_trace_calls,ai_arc_calls;
static int ai_buttons,ai_visible=1,ai_same_team,ai_client_buttons[64];
static int ai_state(hxSGEngine* e,int id,hxSGBotState* out){(void)e;if(id>1)return 0;memset(out,0,sizeof(*out));out->health=100;out->team=ai_same_team?0:id;out->origin[0]=1000+id*300;out->origin[1]=2000;out->origin[2]=30;out->eye[0]=out->origin[0];out->eye[1]=2000;out->eye[2]=56;out->clip_ammo=0;out->reserve_ammo=10;return 1;}
static int ai_trace(hxSGEngine* e,const float* a,const float* b,const float* c,const float* d,int mask,int ignore,hxSGTrace* out){(void)e;(void)a;(void)b;(void)c;(void)d;(void)mask;(void)ignore;ai_trace_calls++;if(c[0]==-2 && d[0]==2)ai_arc_calls++;memset(out,0,sizeof(*out));out->fraction=ai_visible?1.f:.2f;out->entity=-1;return 1;}
static int ai_command(hxSGEngine* e,int id,int ms,int f,int r,int u,float p,float y,int buttons,int w){(void)e;(void)id;(void)ms;(void)f;(void)r;(void)u;(void)p;(void)y;(void)w;ai_buttons=buttons;if(id>=0&&id<64)ai_client_buttons[id]=buttons;return 1;}
static int crowded_state(hxSGEngine* e,int id,hxSGBotState* out){(void)e;memset(out,0,sizeof(*out));out->health=100;out->team=id?1:0;out->origin[0]=out->eye[0]=id?1000-id*10:0;out->origin[2]=24;out->eye[2]=50;return 1;}
static int grid_switches,grid_locked;
static int grid_find(hxSGEngine* e,const char* key,const char* value,int* ids,int cap){
 (void)e;if(cap && ((!strcmp(key,"spawn_anchor")&&!strcmp(value,"1"))||(!strcmp(key,"targetname")&&!strcmp(value,"bombzone_A")))){ids[0]=!strcmp(key,"targetname");return 1;}return 0;
}
static const char* grid_key(hxSGEngine* e,int id,const char* key){(void)e;return !strcmp(key,"origin")?(id?"320 0 24.125":"0 0 24.125"):"";}
static int grid_trace(hxSGEngine* e,const float* a,const float* b,const float* mins,const float* maxs,int mask,int ignore,hxSGTrace* out){
 (void)e;(void)mins;(void)maxs;(void)mask;(void)ignore;memset(out,0,sizeof(*out));out->fraction=1;out->entity=-1;memcpy(out->position,b,3*sizeof(float));
 if(a[0]==b[0]&&a[1]==b[1]&&a[2]>b[2]+1){
  if(b[0]<0||b[0]>320||b[1]<0||b[1]>320||(b[0]==160&&b[1]<160))return 1;
  out->fraction=.25f;out->normal[2]=1;out->position[2]=24;
 }else if(((a[0]<160&&b[0]>=160)||(b[0]<160&&a[0]>=160))&&(a[1]<160||b[1]<160))out->fraction=.1f;
 return 1;
}
/* A 48-unit doorway centred between the old 80-unit grid rows, under
 * a 70-unit lintel. The standing hull fits; an extra raised full hull does not. */
static int doorway_trace(hxSGEngine* e,const float* a,const float* b,const float* mins,const float* maxs,int mask,int ignore,hxSGTrace* out){
 (void)e;(void)mask;(void)ignore;memset(out,0,sizeof(*out));out->fraction=1;out->entity=-1;memcpy(out->position,b,3*sizeof(float));
 if(a[2]+maxs[2]>70){out->startsolid=1;out->fraction=0;return 1;}
 if(a[0]==b[0] && a[1]==b[1] && a[2]>b[2]+1){
  if(b[0]<0 || b[0]>320 || b[1]<0 || b[1]>160)return 1;
  if(fabsf(b[0]-160)<15 && (b[1]+mins[1]<16 || b[1]+maxs[1]>64)){out->startsolid=1;return 1;}
  out->fraction=.25f;out->normal[2]=1;out->position[2]=24;
 }else if((a[0]<160 && b[0]>=160)||(b[0]<160 && a[0]>=160)){
  if(a[1]+mins[1]<16 || a[1]+maxs[1]>64 || b[1]+mins[1]<16 || b[1]+maxs[1]>64)out->fraction=.1f;
 }
 return 1;
}
static const char* stairs_key(hxSGEngine* e,int id,const char* key){(void)e;return !strcmp(key,"origin")?(id?"320 0 168.125":"0 0 24.125"):"";}
static int stairs_trace(hxSGEngine* e,const float* a,const float* b,const float* mins,const float* maxs,int mask,int ignore,hxSGTrace* out){
 (void)e;(void)maxs;(void)mask;(void)ignore;memset(out,0,sizeof(*out));out->fraction=1;out->entity=-1;memcpy(out->position,b,3*sizeof(float));
 if(a[0]==b[0] && a[1]==b[1] && a[2]>b[2]+1){float floor=b[0]>80?(b[0]-80)*.6f:0;
  if(b[0]<0 || b[0]>320 || b[1]<0 || b[1]>80)return 1;
  if(a[2]+mins[2]<floor){out->startsolid=1;out->fraction=0;return 1;}
  out->fraction=.25f;out->normal[2]=.86f;out->position[2]=floor+24;
 }return 1;
}
static int clip_nav_wrong_mask;
static int playerclip_trace(hxSGEngine* e,const float* a,const float* b,const float* mins,const float* maxs,int mask,int ignore,hxSGTrace* out){
 (void)e;(void)mins;(void)maxs;(void)ignore;memset(out,0,sizeof(*out));out->fraction=1;out->entity=-1;memcpy(out->position,b,3*sizeof(float));if(mask!=17)clip_nav_wrong_mask++;
 if((mask&16) && ((fabsf(b[0]-160)<15 && b[1]<160)||(((a[0]<160&&b[0]>=160)||(b[0]<160&&a[0]>=160))&&(a[1]<160||b[1]<160)))){out->fraction=.1f;out->startsolid=a[0]==b[0];return 1;}
 if(a[0]==b[0]&&a[1]==b[1]&&a[2]>b[2]+1 && b[0]>=0&&b[0]<=320&&b[1]>=0&&b[1]<=320){out->fraction=.25f;out->normal[2]=1;out->position[2]=24;}return 1;
}
static int wide_size=4000;
static const char* wide_key(hxSGEngine* e,int id,const char* key){static char value[64];(void)e;if(strcmp(key,"origin"))return "";snprintf(value,sizeof(value),"%d %d 24.125",id?wide_size:0,id?wide_size:0);return value;}
static int wide_trace(hxSGEngine* e,const float* a,const float* b,const float* mins,const float* maxs,int mask,int ignore,hxSGTrace* out){
 (void)e;(void)mins;(void)maxs;(void)mask;(void)ignore;memset(out,0,sizeof(*out));out->fraction=1;out->entity=-1;memcpy(out->position,b,3*sizeof(float));
 if(a[0]==b[0]&&a[1]==b[1]&&a[2]>b[2]+1 && b[0]>=0&&b[0]<=wide_size&&b[1]>=0&&b[1]<=wide_size){out->fraction=.25f;out->normal[2]=1;out->position[2]=24;}return 1;
}
static int grid_state(hxSGEngine* e,int id,hxSGBotState* out){(void)e;if(id)return 0;memset(out,0,sizeof(*out));out->health=100;out->origin[2]=24.125f;out->eye[2]=50;out->weapon_name="pistol_mp";out->locked=grid_locked;return 1;}
static float objective_x;
static int objective_state(hxSGEngine* e,int id,hxSGBotState* out){int result=grid_state(e,id,out);if(result){out->origin[0]=out->eye[0]=objective_x;out->grounded=1;}return result;}
static const char* objective_key(hxSGEngine* e,int id,const char* key){return id==1 && !strcmp(key,"model")?"*7":grid_key(e,id,key);}
static int objective_trace(hxSGEngine* e,const float* a,const float* b,const float* mins,const float* maxs,int mask,int ignore,hxSGTrace* out){
 if(ignore!=-1031)return grid_trace(e,a,b,mins,maxs,mask,ignore,out);
 memset(out,0,sizeof(*out));out->fraction=1;out->entity=-1;out->startsolid=a[0]>=300;return 1;
}
static const char* grid_primary(hxSGEngine* e,int id,int slot){(void)e;(void)id;(void)slot;return "rifle_mp";}
static int grid_weapon(hxSGEngine* e,int id,int op,int slot,const char* name,int value){(void)e;(void)id;(void)slot;(void)name;(void)value;if(op==HX_WEAPON_SWITCH)grid_switches++;return 1;}
static hxSGBotState tactical_states[64];
static int tactical_state(hxSGEngine* e,int id,hxSGBotState* out){(void)e;if(id<0 || id>=64)return 0;*out=tactical_states[id];return 1;}
static int tactical_trace(hxSGEngine* e,const float* a,const float* b,const float* c,const float* d,int mask,int ignore,hxSGTrace* out){int saved=ai_visible;ai_visible=ignore==-1;int result=ai_trace(e,a,b,c,d,mask,ignore,out);ai_visible=saved;return result;}
static const char* tactical_skill="";
static const char* tactical_cvar(hxSGEngine* e,const char* key){(void)e;return !strcmp(key,"bot_skill")?tactical_skill:"";}
static int tactical_bomb;
static int tactical_find(hxSGEngine* e,const char* key,const char* value,int* ids,int cap){(void)e;if(tactical_bomb && cap && !strcmp(key,"model")&&!strcmp(value,"xmodel/mp_bomb1_defuse")){ids[0]=1;return 1;}return 0;}
static const char* tactical_key(hxSGEngine* e,int id,const char* key){(void)e;(void)id;return !strcmp(key,"origin")?"1600 0 24":"";}
static const char* hearing_weapon(hxSGEngine* e,int id,int slot){(void)e;(void)id;return slot==3?"fraggrenade_mp":"rifle_mp";}
static ZBotManager* disconnect_manager;
static int bot_disconnect_calls;
static int recursive_bot_disconnect(hxSGEngine* e,int client){
 (void)e;bot_disconnect_calls++;zbot_client_disconnect(disconnect_manager,client);return 1;
}
static int fixture_nav(hxSGEngine* e,int id,float* center,int* edges,int cap){
 (void)e;if(!center)return 3;if(id<0||id>=3)return -1;
 center[0]=1000.0f+id*300.0f;center[1]=2000.0f;center[2]=30.0f;
 if(id<2){if(edges&&cap>0)edges[0]=id+1;return 1;}return 0;
}
extern hxServerGame* helix_sgame_create(void);
static float fog_end,fog_red;
static int deleted_defined=-1,deleted_field=-1,reused_defined=-1,old_alias_defined=-1;
static int team_scores[2],script_score=-999;
static int score_service(hxSGEngine* e,int team,const int* value,int* result){(void)e;if(team<0||team>1||!result)return 0;if(value)team_scores[team]=*value;*result=team_scores[team];return 1;}
static int variant_grey,variant_british;
static int literal_sound;
static int icon_requests,enemy_viewmodel,foreign_viewmodel,look_mode,look_hits;
static int scoreboard_shader_requests,missing_shader_requests;
static int icon_prepare(hxSGEngine* e,int kind,const char* name){(void)e;if(kind==HX_PRECACHE_SHADER && (!strcmp(name,"hudScoreboard_mp") || !strcmp(name,"HUDSCOREBOARD_MP")))scoreboard_shader_requests++;if(kind==HX_PRECACHE_SHADER && !strcmp(name,"fixture_missing_shader"))missing_shader_requests++;if(kind==HX_PRECACHE_DATA && strstr(name,"/12/profile.txt"))variant_grey++;if(kind==HX_PRECACHE_DATA && strstr(name,"/10/profile.txt"))variant_british++;if(kind==HX_PRECACHE_DATA&&!strcmp(name,"cod/viewhands/mosin_nagant_mp/1/profile.txt"))foreign_viewmodel++;if(kind==HX_PRECACHE_DATA&&!strcmp(name,"cod/viewhands/mp44_mp/1/profile.txt"))enemy_viewmodel++;if(kind==HX_PRECACHE_SOUND&&!strcmp(name,"literal_sound_fixture"))literal_sound++;if(kind==HX_PRECACHE_SHADER&&!strcmp(name,"hud_status_dead"))icon_requests++;return 1;}
static int archive_enabled=-1,archive_calls;
static float archive_seconds;static int archive_field_value=-1;
static int archive_player(hxSGEngine* e,int client,const float* value,float* result){(void)e;if(client!=0 || !result)return 0;if(value)archive_seconds=*value>3?3:*value;*result=archive_seconds;return 1;}

static int archive_service(hxSGEngine* e,int enabled){(void)e;archive_enabled=enabled;archive_calls++;return 1;}
static char tenths_text[64];
static int tenths_hud(hxSGEngine* e,int id,const hxSGHud* value){(void)e;(void)id;if(value && value->text)snprintf(tenths_text,sizeof(tenths_text),"%s",value->text);return 1;}
static int plant_state,plant_frozen,plant_weapons,plant_huds,plant_hud_removed,no_turret;
static int plant_control(hxSGEngine* e,int client,int frozen,int weapons){(void)e;if(client!=0)return 0;if(frozen>=0)plant_frozen=frozen;if(weapons>=0)plant_weapons=weapons;return 1;}
static int plant_hud(hxSGEngine* e,int id,const hxSGHud* h){(void)e;(void)id;if(h)plant_huds++;else plant_hud_removed++;return 1;}
static int menu_calls,scoreboard_menu_available;
static int prepare_menu(hxSGEngine* e,const char* name){(void)e;menu_calls++;return !strcmp(name,"fixture_menu") || (scoreboard_menu_available && !strcmp(name,"scoreboard"));}
static int text_calls;static char text_key[128];
static int prepare_text(hxSGEngine* e,const char* key){(void)e;text_calls++;snprintf(text_key,sizeof(text_key),"%s",key);return 1;}
static int info_calls;static char info_name[64],info_default[64];
static int register_info(hxSGEngine* e,const char* name,const char* value){(void)e;info_calls++;snprintf(info_name,sizeof(info_name),"%s",name);snprintf(info_default,sizeof(info_default),"%s",value);return 1;}
static char ambient[64],allies[64],axis[64],use_default[16]; static int team=-1,reads=0,failures=0;
static char runtime_active[16];static int runtime_linked;
static void check(int,const char*);
static int announcement_format_seen,client_format_seen,carrier_updates;static float carrier_x;
static int loop_calls,loop_stops,objective_visible;
static int loop_probe(hxSGEngine* e,int id,const char* alias){(void)e;(void)id;loop_calls++;if(!alias)loop_stops++;return 1;}
static int objective_carrier_probe(hxSGEngine* e,int id,const hxSGObjective* value){(void)e;(void)id;objective_visible=value!=NULL;if(value){carrier_x=value->position[0];carrier_updates++;check(value->entity==-1,"player objective is copied as position without leaking CoD player encoding");}return 1;}
static int announcement_probe(hxSGEngine* e,int id,const hxSGHud* value){(void)e;(void)id;if(value && value->text){
 if(strstr(value->text,"OBJECTIVE_FORMAT") && strchr(value->text,'\x1f') && strstr(value->text,"OBJECTIVE_NAME"))announcement_format_seen=1;
 if(strstr(value->text,"CLIENT_FORMAT") && strchr(value->text,'\x1f') && strstr(value->text,"7"))client_format_seen=1;
 }return 1;}
static void check(int good,const char* name){printf("%s: %s\n",name,good?"PASS":"FAIL");if(!good)++failures;}
static int read_script(hxSGEngine* e,const char* path,unsigned char** out,unsigned int* size){
 const char* s=NULL;(void)e;++reads;
 if(!strcmp(path,"rawgsc/maps/mp/mp_variants.gsc"))s="main(){ game[\"allies\"]=\"british\";game[\"axis\"]=\"german\";game[\"british_soldiertype\"]=\"airborne\";game[\"german_soldiertype\"]=\"fallschirmjagergrey\"; }";
 if(!strcmp(path,"rawgsc/maps/mp/mp_link_bad.gsc"))s="main(){ambientPlay(\"must_not_start\");}unused(){mod_native_not_registered();}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_link_no_main.gsc"))s="utility(){return 1;}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_carrier.gsc"))s="main(){objective_add(0,\"current\");p=getent(\"native_player\",\"targetname\");objective_onEntity(0,p);announcement(&\"OBJECTIVE_FORMAT\",&\"OBJECTIVE_NAME\");clientAnnouncement(p,&\"CLIENT_FORMAT\",7);wait .05;objective_position(0,(9,8,7));}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_hq_metadata.gsc"))s="main(){objective_add(0,\"current\");objective_delete(0);objective_icon(0,\"fixture_icon\");objective_team(0,\"axis\");e=spawn(\"script_origin\",(0,0,0));e playLoopSound(\"fixture_loop\");e stopLoopSound(\"fixture_loop\");e stopLoopSound();ambientPlay(\"metadata_complete\");}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_look.gsc"))s="main(){e=getent(\"look_test\",\"targetname\");for(;;){e waittill(\"trigger\",p);if(p isLookingAt(e))setcvar(\"look_hits\",1);}}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_one.gsc"))s="main(){ game[\"allies\"]=\"american\"; game[\"axis\"]=\"german\"; ambientPlay(\"ambient_one\"); }";
 if(!strcmp(path,"rawgsc/maps/mp/mp_fog.gsc"))s="main(){ setCullFog(0,5000,0.32,0.36,0.4,0); wait 0.05; ambientPlay(\"ambient_delayed\"); wait 0.05; setCullFog(0,8000,0.5,0.5,0.5,0.1); }";
 if(!strcmp(path,"rawgsc/maps/mp/mp_plant.gsc"))s="main(){m=getent(\"minefield\",\"targetname\");m thread plant();}never_called(){self playLoopSound(\"literal_sound_fixture\");}plant(){for(;;){self waittill(\"trigger\",p);if(p.pers[\"team\"]!=\"allies\"||!p useButtonPressed())continue;if(!isDefined(p.h))p.h=newClientHudElem(p);p linkTo(self);p disableWeapon();setcvar(\"plant_state\",1);t=0;while(p useButtonPressed() && t<1){wait .05;t+=.05;}p.h destroy();p unlink();p enableWeapon();if(t>=1){setcvar(\"plant_state\",2);return;}setcvar(\"plant_state\",0);}}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_tenths.gsc"))s="main(){h=newHudElem();h setTenthsTimer(3.4);}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_archive_fields.gsc"))s="main(){p=getent(\"native_player\",\"targetname\");p.archivetime=9;setcvar(\"archive_field_value\",int(p.archivetime*100));p.archivetime=0;}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_archive.gsc"))s="main(){setArchive(true);wait 0.05;setArchive(false);ambientPlay(\"after_archive\");}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_icons.gsc"))s="main(){precacheStatusIcon(\"hud_status_dead\");precacheStatusIcon(\"HUD_STATUS_DEAD\");ambientPlay(\"after_icons\");}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_score.gsc"))s="main(){setTeamScore(\"allies\",7);setTeamScore(\"axis\",-2);setcvar(\"script_score\",getTeamScore(\"allies\"));ambientPlay(\"after_score\");}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_scoreboard_precache.gsc"))s="main(){precacheShader(\"hudScoreboard_mp\");precacheShader(\"HUDSCOREBOARD_MP\");precacheShader(\"fixture_missing_shader\");ambientPlay(\"after_scoreboard\");}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_info.gsc"))s="main(){makeCvarServerInfo(\"test_limit\",\"30\");precacheString(&\"ROUND_HINT\");precacheMenu(\"fixture_menu\");ambientPlay(\"after_info\");}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_deleted.gsc"))s="main(){e=spawn(\"script_origin\",(0,0,0));a=[];a[0]=e;e.marker=\"present\";e delete();setcvar(\"deleted_defined\",isdefined(a[0]));setcvar(\"deleted_field\",isdefined(a[0].marker));next=spawn(\"script_origin\",(1,2,3));setcvar(\"reused_defined\",isdefined(next));setcvar(\"old_alias_defined\",isdefined(a[0]));}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_two.gsc"))s="main(){ ambientPlay(\"ambient_two\"); }";
 if(!strcmp(path,"rawgsc/maps/mp/gametypes/dm.gsc"))s="main(){ thread helper(); } helper(){ wait 1; ambientPlay(\"must_not_run\"); }";
 if(!strcmp(path,"weapons/mp/mg42_test"))s="WEAPONFILE\\leftArc\\12\\rightArc\\23\\topArc\\11\\bottomArc\\18\\damage\\17\\fireTime\\0.2\\playerPositionDist\\40";
 if(!strcmp(path,"rawgsc/maps/mp/mp_native.gsc"))s="main(){ e=getent(\"misc_mg42\",\"classname\");e playSound(\"native_sound\");p=getent(\"native_player\",\"targetname\");p takeAllWeapons();p giveWeapon(\"native_weapon\");println(\"native_print\"); }";
 if(!strcmp(path,"rawgsc/maps/mp/mp_scene.gsc"))s="main(){fx=loadfx(\"scene_test\");e=spawn(\"script_origin\",(0,0,0));e.origin=(1,2,3);e moveTo((11,2,3),0.1);thread done(e);level.h=newHudElem();level.h.x=10;level.h.color=(1,0,0);level.h setText(\"Scene HUD\");level.h fadeOverTime(0.1);level.h.alpha=0;objective_add(0,\"current\",(1,2,3));objective_icon(0,\"scene_shader\");objective_onEntity(0,e);t=bullettrace((0,0,0),(20,0,0),0,undefined);setcvar(\"scene_hit\",int(t.fraction*100));playfx(fx,(3,4,5));}done(e){e waittill(\"movedone\");setcvar(\"scene_moved\",int(e.origin[0]));e delete();objective_delete(0);objective_delete(0);level.h destroy();}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_l10n.gsc"))s="main(){level.h=newHudElem();level.h setText(&\"SCORE\",&\"TEAM\",7);level.h setText(&\"TITLE\" + \" literal\");}";
 if(!strcmp(path,"fx/scene_test.efx"))s="Particle { count 2 life 500 velocity 10 gravity 0 shaders [ scene_shader ] size { start 2 end 5 } }";
 if(!strcmp(path,"rawgsc/maps/mp/mp_triggers.gsc"))s="main(){m=getent(\"minefield\",\"targetname\");u=getent(\"radio_trig\",\"targetname\");u setHintString(&\"RE_PRESS_TO_PICKUP\",&\"RE_OBJ_FIELD_RADIO\");thread watch_mine(m);thread watch_use(u);}watch_mine(m){m waittill(\"trigger\",other);if(other isTouching(m)){org=other getOrigin();s=spawn(\"script_model\",org);s playSound(\"minefield_click\");radiusDamage(org,100,120,10);s delete();}}watch_use(u){u waittill(\"trigger\",other);setcvar(\"use_triggered\",\"1\");}";
 if(!strcmp(path,"rawgsc/maps/mp/mp_missing_ops.gsc"))s="main(){setExpFog(0.0002,0.4,0.5,0.6,0);p=getent(\"native_player\",\"targetname\");clientAnnouncement(p,\"Player Announcement\");p autoDemoStart();p autoDemoStop();p autoScreenshot();p pingPlayer();m=spawn(\"script_origin\",(100,200,300));m rotateVelocity((10,20,30),5);m moveGravity((50,0,100),2);fx=loadfx(\"scene_test\");playfxontag(fx,m,\"tag_origin\");bot=addtestclient();setcvar(\"ops_tested\",\"1\");}";
 if(!s)return 0;*size=(unsigned int)strlen(s);*out=malloc(*size);memcpy(*out,s,*size);return 1;
}
static void release(hxSGEngine* e,unsigned char* p){(void)e;free(p);}
static const char* get(hxSGEngine* e,const char* n){(void)e;if(!strcmp(n,"g_gsc_runtime")){const char* mode=getenv("GSC_TEST_RUNTIME");return mode?mode:"";}return !strcmp(n,"g_gametype")?"dm":"";}
static int use_triggered,trig_use_avail,ops_tested;static char trig_use_text[128];
static void set(hxSGEngine* e,const char* n,const char* v){if(!strcmp(n,"g_gsc_runtime_active"))snprintf(runtime_active,sizeof(runtime_active),"%s",v);if(!strcmp(n,"g_gsc_linked"))runtime_linked=atoi(v);if(!strcmp(n,"ops_tested"))ops_tested=atoi(v);if(!strcmp(n,"archive_field_value"))archive_field_value=atoi(v);if(!strcmp(n,"look_hits"))look_hits=atoi(v);if(!strcmp(n,"plant_state"))plant_state=atoi(v);if(!strcmp(n,"use_triggered"))use_triggered=atoi(v);if(!strcmp(n,"ui_trigger_use_available"))trig_use_avail=atoi(v);if(!strcmp(n,"ui_trigger_use_text"))snprintf(trig_use_text,sizeof(trig_use_text),"%s",v);if(!strcmp(n,"script_score"))script_score=atoi(v);if(!strcmp(n,"reused_defined"))reused_defined=atoi(v);if(!strcmp(n,"old_alias_defined"))old_alias_defined=atoi(v);if(!strcmp(n,"deleted_defined"))deleted_defined=atoi(v);if(!strcmp(n,"deleted_field"))deleted_field=atoi(v);if(!strcmp(n,"r_fogEnd"))fog_end=(float)atof(v);if(!strcmp(n,"r_fogRed"))fog_red=(float)atof(v);if(!strcmp(n,"ui_default_use_key"))snprintf(use_default,sizeof(use_default),"%s",v);(void)e;if(!strcmp(n,"g_ambient"))snprintf(ambient,sizeof(ambient),"%s",v);if(!strcmp(n,"g_allies"))snprintf(allies,sizeof(allies),"%s",v);if(!strcmp(n,"g_axis"))snprintf(axis,sizeof(axis),"%s",v);}
static int teams[64],counts[64];static char inventories[64][3][64];
static void set_team(hxSGEngine* e,int c,int t){(void)e;team=t;teams[c]=t;}
static void take(hxSGEngine* e,int c){(void)e;counts[c]=0;}
static void give(hxSGEngine* e,int c,const char* name){(void)e;if(counts[c]<3)snprintf(inventories[c][counts[c]++],64,"%s",name);}

static hxSGPlayerView view;
static int locked, shots, shot_damage, last_hitscan_client, now;
static float joint_pitch,joint_yaw,rest_pitch;
static int joint(hxSGEngine* e,int id,const char* name,float p,float y){(void)e;(void)id;(void)name;joint_pitch=p;joint_yaw=y;return 1;}
static int find_gun(hxSGEngine* e,const char* k,const char* v,int* ids,int cap){(void)e;if(cap>0 && ((!strcmp(k,"classname")&&!strcmp(v,"trigger_lookat"))||(!strcmp(k,"targetname")&&!strcmp(v,"look_test")))){ids[0]=10;return 1;}if(!no_turret && cap>0 && !strcmp(k,"classname") && !strcmp(v,"misc_mg42")){ids[0]=7;return 1;}if(cap>0 && !strcmp(k,"classname") && !strcmp(v,"trigger_multiple")){ids[0]=8;return 1;}if(cap>0 && !strcmp(k,"classname") && !strcmp(v,"trigger_use")){ids[0]=9;return 1;}if(cap>0 && !strcmp(k,"targetname") && !strcmp(v,"minefield")){ids[0]=8;return 1;}if(cap>0 && !strcmp(k,"targetname") && !strcmp(v,"radio_trig")){ids[0]=9;return 1;}return 0;}
static const char* gun_key(hxSGEngine* e,int id,const char* key){(void)e;if(id==10){if(!strcmp(key,"model"))return "*4";if(!strcmp(key,"classname"))return "trigger_lookat";return "";}if(id==8){if(!strcmp(key,"classname"))return "trigger_multiple";if(!strcmp(key,"targetname"))return "minefield";if(!strcmp(key,"model"))return "*2";return "";}if(id==9){if(!strcmp(key,"classname"))return "trigger_use";if(!strcmp(key,"targetname"))return "radio_trig";if(!strcmp(key,"model"))return "*3";if(!strcmp(key,"origin"))return "0 0 30";return "";}if(!strcmp(key,"origin"))return "0 0 30";if(!strcmp(key,"angles"))return "0 0 0";if(!strcmp(key,"weaponinfo"))return "mg42_test";return "";}
static int player_view(hxSGEngine* e,int c,hxSGPlayerView* out){(void)e;if(c)return 0;*out=view;out->locked=locked;return 1;}
static int player_lock(hxSGEngine* e,int c,const float* o,float p,float y){(void)e;(void)c;locked=o!=NULL;if(o){memcpy(view.origin,o,sizeof(view.origin));view.pitch=p;view.yaw=y;}return 1;}
static int trace_floor(hxSGEngine* e,float sx,float sy,float sz,float ex,float ey,float ez,float mx,float my,float mz,float ax,float ay,float az,int mask,float* f,float* nx,float* ny,float* nz){(void)e;(void)sx;(void)sy;(void)ex;(void)ey;(void)mx;(void)my;(void)mz;(void)ax;(void)ay;(void)az;(void)mask;*nx=*ny=0;*nz=1;*f=ez<sz && ez<24?(sz-24)/(sz-ez):1;return 0;}
static int fire(hxSGEngine* e,int c,const float* start,float p,float y,int damage,float range){(void)e;(void)start;(void)p;(void)y;(void)range;shots++;shot_damage=damage;last_hitscan_client=c;return 1;}
static int clock_ms(hxSGEngine* e){(void)e;return now;}
static void think(hxServerGame* g,int b,int p,int y){g->client_think(g,0,50,0,0,0,p*100,y*100,0,b,1);}
static int native_sounds,native_prints,mine_sounds;
static float scene_position[3],scene_hud_alpha;static int scene_live,scene_deleted,scene_hud,scene_objective,scene_particles,scene_moved,scene_hit;
static int scene_spawn(hxSGEngine* e,const char* name,const float* pos){(void)e;check(!strcmp(name,"script_origin")||!strcmp(name,"script_model"),"GSC spawn class reaches host");memcpy(scene_position,pos,sizeof(scene_position));scene_live=1;return 100;}
static int scene_remove(hxSGEngine* e,int id){(void)e;check(id==100&&scene_live,"GSC delete addresses spawned entity");scene_live=0;scene_deleted++;return 1;}
static int scene_transform(hxSGEngine* e,int id,const float* origin,const float* angles,float* out_origin,float* out_angles){(void)e;(void)angles;if(id==8 && !origin && !angles){if(out_origin)memset(out_origin,0,3*sizeof(float));return 1;}if(id!=100||!scene_live)return 0;if(origin)memcpy(scene_position,origin,sizeof(scene_position));if(out_origin)memcpy(out_origin,scene_position,sizeof(scene_position));if(out_angles)memset(out_angles,0,3*sizeof(float));return 1;}
static int scene_trace(hxSGEngine* e,const float* start,const float* end,const float* mins,const float* maxs,int mask,int ignore,hxSGTrace* out){(void)e;(void)mins;(void)maxs;memset(out,0,sizeof(*out));if(ignore==-1028){out->fraction=look_mode?.01f:1.f;return 1;}if(ignore==-3 && mask==1){out->fraction=look_mode==2?.005f:1.f;return 1;}if(ignore==-1024-2||ignore==-1024-3){out->fraction=1.f;out->startsolid=(start[0]>=-16.f&&start[0]<=16.f);return 1;}check(start[0]==0&&end[0]==20&&mask==129&&ignore==-1,"GSC bullet trace arguments and mask");out->fraction=.5f;out->position[0]=10;out->normal[0]=-1;out->entity=100;return 1;}
static int localized_format_seen,localized_concat_seen;
static int localized_hud(hxSGEngine* e,int id,const hxSGHud* value){(void)e;(void)id;if(value&&value->text){
 const char formatted[]="SCORE\037TEAM\0377",joined[]="\036TITLE\036 literal";
 if(!strcmp(value->text,formatted))localized_format_seen=1;
 if(!strcmp(value->text,joined))localized_concat_seen=1;
 }return 1;}
static int scene_hud_update(hxSGEngine* e,int id,const hxSGHud* value){(void)e;check(id==0,"GSC HUD stable handle");if(value&&value->text&&!strcmp(value->text,"Scene HUD")){check(value->x==10&&value->color[0]==1&&value->color[1]==0,"GSC HUD fields update host");scene_hud=1;scene_hud_alpha=value->color[3];}if(!value)scene_hud=2;return 1;}
static int scene_objective_update(hxSGEngine* e,int id,const hxSGObjective* value){(void)e;check(id==0,"GSC objective index reaches host");if(value&&value->entity==100)scene_objective=1;if(!value)scene_objective=2;return 1;}
static int scene_emit(hxSGEngine* e,const float* origin,const float* forward,const hxSGParticle* particles,int count){(void)e;(void)forward;check(origin[0]==3&&origin[1]==4&&origin[2]==5&&count==1&&particles[0].count==2&&particles[0].lifetime==.5f&&!strcmp(particles[0].material,"scene_shader"),"GSC FX emits decoded precached particle descriptor");scene_particles++;return 1;}
static void scene_cvar(hxSGEngine* e,const char* name,const char* value){set(e,name,value);if(!strcmp(name,"scene_hit"))scene_hit=atoi(value);if(!strcmp(name,"scene_moved"))scene_moved=atoi(value);}
static int missing_ops_emitted;
static int ops_emit(hxSGEngine* e,const float* origin,const float* forward,const hxSGParticle* p,int count){(void)e;(void)origin;(void)forward;(void)p;(void)count;missing_ops_emitted++;return 1;}

static int native_find(hxSGEngine* e,const char* key,const char* value,int* ids,int cap){
 if(cap>0&&!strcmp(key,"targetname")&&!strcmp(value,"native_player")){ids[0]=-3;return 1;}
 return find_gun(e,key,value,ids,cap);
}
static void native_sound(hxSGEngine* e,const char* name,float x,float y,float z){
 (void)e;if(!strcmp(name,"minefield_click")&&x==0&&y==0&&z==0){mine_sounds++;return;}check(!strcmp(name,"native_sound")&&x==0&&y==0&&z==30,"GSC sound alias and entity position reach effects import");native_sounds++;}
static void native_print(hxSGEngine* e,const char* text){(void)e;if(!strcmp(text,"native_print"))native_prints++;}
int main(void){hxSGImports imp={0};hxServerGame* g=helix_sgame_create();imp.api_version=HELIX_SGAME_API_VERSION;imp.fs_read=read_script;imp.fs_free=release;imp.cvar_string=get;imp.cvar_set=set;imp.player_set_team=set_team;imp.player_take_weapons=take;imp.player_give_weapon=give;imp.precache_asset=icon_prepare;
 check(g && g->api_version==HELIX_SGAME_API_VERSION,"SDK matches module");if(!g)return 1;
 /* The engine runs menu frames after DLL creation, before map init/imports. */
 g->run_frame(g,0);g->run_frame(g,16);g->client_disconnect(g,0);g->shutdown(g,0);
 check(reads==0,"created module survives menu frames before map init");
 g->init(g,NULL,&imp,"",0);check(reads==0,"menu does not execute empty map script");
 g->init(g,NULL,&imp,"mp_variants",0);check(variant_grey>0 && variant_british>0,"native soldier type names precache the exact view variants");
 g->init(g,NULL,&imp,"mp_one",0);check(!strcmp(runtime_active,get(NULL,"g_gsc_runtime")[0]?get(NULL,"g_gsc_runtime"):"legacy"),"requested runtime profile is active");check(runtime_linked==!strcmp(get(NULL,"g_gsc_runtime"),"linked"),"sealed link status matches requested profile");check(!strcmp(allies,"american")&&!strcmp(axis,"german"),"map GSC team host callbacks");check(!strcmp(ambient,"ambient_one"),"ambient host callback and stopped wait branch");check(enemy_viewmodel>0,"map precache includes enemy MP44 with allied viewhands");check(foreign_viewmodel==0,"map precache excludes inactive nations unless their weapons are placed");
 check(g->client_connect(g,0,"player")==0,"client accepted");g->client_begin(g,0);check(team==-1&&counts[0]==0,"client begins as unarmed spectator");
 check(g->console_command(g,"team axis"),"module handles command");check(team==1,"team command invokes engine import");
 check(counts[0]==3&&!strcmp(inventories[0][1],"mp40_mp"),"axis gets national primary pistol and grenade");
 g->client_begin(g,1);g->client_command(g,1,"team allies");g->client_command(g,1,"weapon thompson");
 check(!strcmp(inventories[1][1],"thompson_mp")&&!strcmp(inventories[0][1],"mp40_mp"),"client weapon choices remain independent");
 g->client_command(g,1,"weapon mp44");check(!strcmp(inventories[1][1],"thompson_mp"),"enemy weapon selection rejected");
 g->client_command(g,0,"team spectator");check(teams[0]==-1&&counts[0]==0,"spectator takes all weapons");
 g->client_command(g,0,"weapon mp44");check(counts[0]==0,"spectator weapon request rejected");
 g->client_command(g,0,"team axis");check(teams[0]==1&&counts[0]==3,"rejoining restores a playable loadout");
 g->init(g,NULL,&imp,"mp_two",1);check(!strcmp(ambient,"ambient_two")&&!allies[0]&&!axis[0],"map restart resets metadata and executes new main");g->run_frame(g,16);g->client_disconnect(g,0);g->shutdown(g,0);
 imp.entity_find=find_gun;imp.entity_key=gun_key;imp.trace=trace_floor;imp.player_view=player_view;imp.player_view_lock=player_lock;imp.player_hitscan=fire;imp.level_time_ms=clock_ms;imp.model_joint_angles=joint;
 view.health=100;view.grounded=1;view.origin[0]=view.eye[0]=-60;view.origin[2]=24;view.eye[2]=50;
 g->init(g,NULL,&imp,"mp_one",0);int load_reads=reads;check(!strcmp(use_default,"F"),"CoD requests native F use default");rest_pitch=joint_pitch;check(rest_pitch<0,"unoccupied gun returns to collision-derived rest pitch");
 think(g,8,0,0);check(locked && view.origin[0]==-40,"MG42 use applies native player spacing");
 think(g,8,85,150);check(locked && view.pitch==18 && view.yaw==23,"held use and native asymmetric aim bounds");check(joint_pitch==18 && joint_yaw==23,"aim controls moving gun joint");
 think(g,1,0,0);check(shots==1 && shot_damage==17,"native damage and mounted fire");
 think(g,1,0,0);check(shots==1,"fire cadence holds");now=199;think(g,1,0,0);check(shots==1,"native 200ms cadence before deadline");now=200;think(g,1,0,0);check(shots==2,"native cadence resumes at deadline");
 think(g,0,0,0);think(g,8,0,0);check(!locked,"second use press releases");check(joint_pitch==rest_pitch && joint_yaw==0,"dismount restores rest joint");
 check(reads==load_reads,"use and fire do not reopen VFS files");
 think(g,0,0,0);think(g,8,0,0);check(locked,"mount again");view.health=0;g->run_frame(g,250);check(!locked,"dead player releases stationary interaction");
 view.health=100;think(g,0,0,0);think(g,8,0,0);g->client_disconnect(g,0);check(!locked,"disconnect releases ownership");
 imp.fx_sound=native_sound;imp.print=native_print;imp.entity_find=native_find;
 g->init(g,NULL,&imp,"mp_native",0);
 check(counts[0]==1&&!strcmp(inventories[0][0],"native_weapon"),"GSC receiver weapon methods affect only target client inventory");
 check(native_sounds==1&&native_prints==1,"module executes real native sound and print imports");

 imp.entity_spawn=scene_spawn;imp.entity_remove=scene_remove;imp.entity_transform=scene_transform;
 imp.trace_query=scene_trace;imp.hud_update=scene_hud_update;imp.objective_update=scene_objective_update;imp.particle_emit=scene_emit;imp.cvar_set=scene_cvar;
 g->init(g,NULL,&imp,"mp_scene",0);
 check(scene_hud==1&&scene_objective==1&&scene_particles==1&&scene_hit==50,"script scene native operations execute during map initialization");
 {int before=reads;g->run_frame(g,0);g->run_frame(g,50);check(scene_hud_alpha==.5f,"GSC HUD fade interpolates actual host alpha");check(scene_position[0]==6&&scene_moved==0,"GSC mover interpolates before completion");g->run_frame(g,100);
 check(scene_moved==11&&scene_deleted==1&&!scene_live&&scene_hud==2&&scene_objective==2,"movedone wakes script then deletes actual entity HUD and objective");
 check(reads==before,"movement and FX playback do not reopen VFS files");}
 {imp.cvar_set=set;imp.precache_text=prepare_text;shots=0;shot_damage=0;last_hitscan_client=99;mine_sounds=0;use_triggered=0;trig_use_avail=0;trig_use_text[0]=0;
  view.health=100;view.origin[0]=view.eye[0]=0;view.origin[1]=view.eye[1]=0;view.origin[2]=24;view.eye[2]=50;view.pitch=0;view.yaw=0;
  g->init(g,NULL,&imp,"mp_triggers",0);g->client_begin(g,0);g->client_command(g,0,"team axis");
  g->run_frame(g,50);
  check(mine_sounds==1&&shots==1&&last_hitscan_client==-3&&shot_damage>=100,"trigger_multiple overlap wakes minefield script, plays sound on spawned model, and applies radiusDamage to player");
  think(g,0,0,0);check(trig_use_avail==1&&!strcmp(trig_use_text,"RE_PRESS_TO_PICKUP\tRE_OBJ_FIELD_RADIO"),"trigger_use publishes localized hint string and argument when player is in range");
  think(g,8,0,0);g->run_frame(g,100);
  check(use_triggered==1,"trigger_use wakes waittill(trigger) when player presses use");}
 imp.cvar_set=set;now=0;g->init(g,NULL,&imp,"mp_fog",0);
 check(fog_end==5000&&fog_red>.319f&&fog_red<.321f,"setCullFog reaches renderer state before ambient");
 check(ambient[0]==0,"wait delays ambient until runtime tick");
 g->run_frame(g,0);g->run_frame(g,50);check(!strcmp(ambient,"ambient_delayed"),"delayed ambient publishes host cvar during runtime");
 g->run_frame(g,100);g->run_frame(g,150);check(fog_end==6500,"fog transition interpolates renderer state");
 g->run_frame(g,200);check(fog_end==8000,"fog transition reaches final distance");
 imp.cvar_set=set;g->init(g,NULL,&imp,"mp_deleted",0);check(reused_defined==1&&old_alias_defined==0,"reused host entity ID gets fresh script object without reviving old aliases");check(deleted_defined==0&&deleted_field==0,"delete invalidates aliased entity references and cached fields");
 text_calls=0;imp.precache_menu=prepare_menu;imp.precache_text=prepare_text;imp.cvar_serverinfo=register_info;g->init(g,NULL,&imp,"mp_info",0);
 check(info_calls==1&&!strcmp(info_name,"test_limit")&&!strcmp(info_default,"30")&&!strcmp(ambient,"after_info"),"makeCvarServerInfo invokes real registration then continues script");
 check(text_calls==1&&!strcmp(text_key,"ROUND_HINT"),"precacheString forwards localized key to retained host text cache");
 check(menu_calls==1,"precacheMenu invokes real host menu preparation");
 imp.precache_asset=icon_prepare;scoreboard_menu_available=1;menu_calls=scoreboard_shader_requests=missing_shader_requests=0;
 g->init(g,NULL,&imp,"mp_scoreboard_precache",0);
 check(menu_calls==2 && scoreboard_shader_requests==0,"scoreboard builtin prepares its actual HUD menu, case insensitive");
 check(missing_shader_requests==1 && !strcmp(ambient,"after_scoreboard"),"unrelated shader requests remain visible and script continues");
 scoreboard_menu_available=0;menu_calls=scoreboard_shader_requests=missing_shader_requests=0;
 g->init(g,NULL,&imp,"mp_scoreboard_precache",0);
 check(menu_calls==2 && scoreboard_shader_requests==2,"missing scoreboard menu preserves original shader failures");
 imp.precache_menu=NULL;menu_calls=scoreboard_shader_requests=0;
 g->init(g,NULL,&imp,"mp_scoreboard_precache",0);
 check(menu_calls==0 && scoreboard_shader_requests==2,"hosts without menu precache retain normal shader reporting");
 imp.precache_menu=prepare_menu;
 imp.snapshot_archive=archive_service;g->init(g,NULL,&imp,"mp_archive",0);
 check(archive_enabled==1&&archive_calls==1,"setArchive enables actual host recording service");g->run_frame(g,0);g->run_frame(g,50);
 check(archive_enabled==0&&archive_calls==2&&!strcmp(ambient,"after_archive"),"setArchive disables service after scheduled wait");
 imp.snapshot_archive=NULL;g->init(g,NULL,&imp,"mp_archive",0);check(ambient[0]==0&&archive_calls==2,"missing archive service rejects script");
 imp.precache_asset=icon_prepare;g->init(g,NULL,&imp,"mp_icons",0);
 check(icon_requests==1&&!strcmp(ambient,"after_icons"),"status icon precache queues shader dependency once, case insensitive");
 g->init(g,NULL,&imp,"mp_icons",0);check(icon_requests==2,"status icon registry resets for each map");
 imp.precache_asset=NULL;g->init(g,NULL,&imp,"mp_icons",0);check(ambient[0]==0,"missing icon asset preparation rejects script");
 imp.team_score=score_service;g->init(g,NULL,&imp,"mp_score",0);
 check(team_scores[0]==7&&team_scores[1]==-2&&script_score==7&&!strcmp(ambient,"after_score"),"GSC set/getTeamScore uses authoritative host service for both teams");
 imp.team_score=NULL;g->init(g,NULL,&imp,"mp_score",0);check(ambient[0]==0,"missing score host service stops script");
 imp.cvar_serverinfo=NULL;g->init(g,NULL,&imp,"mp_info",0);check(info_calls==1&&ambient[0]==0,"unsupported serverinfo registration stops script rather than fake success");
 no_turret=1;locked=0;view.health=100;view.grounded=1;view.origin[0]=view.origin[1]=0;view.origin[2]=24;
 imp.player_script_control=plant_control;imp.hud_update=plant_hud;imp.precache_asset=icon_prepare;plant_state=plant_frozen=plant_weapons=plant_huds=plant_hud_removed=0;
 g->init(g,NULL,&imp,"mp_plant",0);check(literal_sound==1,"compiled literal loop-sound dependency is prepared even in an uncalled function");
 g->client_begin(g,0);g->client_command(g,0,"team allies");think(g,8,0,0);g->run_frame(g,0);
 check(plant_state==1&&plant_frozen&&plant_weapons,"native selected team reaches GSC pers and held use starts plant with independent locks");
 think(g,0,0,0);g->run_frame(g,50);check(plant_state==0&&!plant_frozen&&!plant_weapons&&plant_hud_removed>0,"use release cancels plant and removes locks and HUD");
 think(g,8,0,0);for(now=100;now<=1300;now+=50)g->run_frame(g,now);
 check(plant_state==2&&!plant_frozen&&!plant_weapons&&plant_huds>=2,"retry after destroyed HUD completes held-use plant and releases controls");

 g->init(g,NULL,&imp,"mp_look",0);g->client_begin(g,0);g->client_command(g,0,"team axis");
 look_mode=0;look_hits=0;g->run_frame(g,0);g->run_frame(g,50);check(!look_hits,"trigger_lookat ignores a missed ray");
 look_mode=2;g->run_frame(g,100);check(!look_hits,"trigger_lookat is blocked by nearer world geometry");
 look_mode=1;g->run_frame(g,150);check(look_hits==1,"visible look trigger wakes script and isLookingAt agrees without pressing use");
 {
     extern ZBotSkill zbot_parse_skill(const char* str);
     extern const char* zbot_skill_name(ZBotSkill skill);
     extern const char* zbot_weapon_for(const char* nation, const char* pref_class);
     extern int zbot_format_roast(const char* templ, const char* name, char* out, int cap);
     extern int zbot_add_waypoint(ZBotManager* mgr, const float feet[3], unsigned int flags, int entity_id);
     extern void zbot_link_waypoints(ZBotManager* mgr);
     extern int zbot_find_path(ZBotManager* mgr, int start_wp, int goal_wp, int* out_path, int max_path);

     {
         static ZBotManager mgr;hxSGImports api;int path[8];
         memset(&api,0,sizeof(api));api.api_version=HELIX_SGAME_API_VERSION;
         api.nav_area=fixture_nav;api.bot_disconnect=recursive_bot_disconnect;
         zbot_init(&mgr,NULL,&api,NULL,NULL,"fixture","dm","american","german");
         check(mgr.wp_count==3 && mgr.waypoints[0].origin[0]==1000.0f,"bots consume actual map navigation positions");
         check(zbot_find_path(&mgr,0,2,path,8)==3,"bots follow directed map reaches");
         check(zbot_find_path(&mgr,2,0,path,8)==0,"bots do not invent reverse navigation reaches");
         api.bot_state=ai_state;api.bot_command=ai_command;api.trace_query=ai_trace;
         mgr.bots[0].active=1;mgr.bots[0].joined=1;mgr.bots[0].enemy_client=-1;
         ai_visible=0;zbot_run_frame(&mgr,1000);
         check(mgr.bots[0].enemy_client==-1,"occluded enemies do not engage combat or override navigation");
         check((ai_buttons&ZBOT_BTN_RELOAD)!=0 && !(ai_buttons&ZBOT_BTN_ATTACK),"empty bot clip requests reload without firing");
         ai_visible=1;zbot_run_frame(&mgr,1050);
         check(mgr.bots[0].enemy_client==1 && mgr.bots[0].enemy_last_pos[2]==48.f,"visible target uses host eye height");
         ai_same_team=1;zbot_run_frame(&mgr,1100);check(mgr.bots[0].enemy_client==1,"deathmatch treats same-team slot values as opponents");
         strcpy(mgr.gametype,"tdm");zbot_run_frame(&mgr,1150);check(mgr.bots[0].enemy_client==-1,"team deathmatch excludes teammates");ai_same_team=0;
         api.bot_state=crowded_state;mgr.wp_count=0;mgr.bots[0].look_yaw=mgr.bots[0].look_pitch=0;ai_trace_calls=0;
         mgr.bots[0].path_len=0;mgr.bots[0].unstuck_until_ms=0;mgr.bots[0].last_check_ms=1200;
         zbot_run_frame(&mgr,1200);
         check(mgr.bots[0].enemy_client==63,"crowded server chooses nearest visible enemy in slot 63");
         check(ai_trace_calls==1,"nearest-first sensing uses one sight trace when nearest enemy is visible");
         mgr.bots[0].active=0;
         disconnect_manager=&mgr;bot_disconnect_calls=0;mgr.bots[1].active=1;
         zbot_client_disconnect(&mgr,1);
         check(bot_disconnect_calls==1 && !mgr.bots[1].active,"synchronous disconnect callback cannot recurse");
         zbot_shutdown(&mgr);
     }
     {
         ZBotManager* mgr=(ZBotManager*)calloc(1,sizeof(*mgr));hxSGImports api={0};int goal=-1,i,path[128],n,detour=0;
         api.api_version=HELIX_SGAME_API_VERSION;api.entity_find=grid_find;api.entity_key=grid_key;api.trace_query=grid_trace;
         zbot_init(mgr,NULL,&api,NULL,NULL,"fixture","sd","american","german");
         for(i=0;i<mgr->wp_count;i++)if(mgr->waypoints[i].flags&ZBOT_WP_OBJECTIVE)goal=i;
         check(mgr->wp_count>10 && goal>=0,"collision grid expands from spawn and recognizes stock bombzone targetname");
         n=zbot_find_path(mgr,0,goal,path,128);for(i=0;i<n;i++)if(mgr->waypoints[path[i]].origin[1]>=160)detour=1;
         check(n>4 && detour,"bot route goes around wall rather than connecting nearby points through it");
         api.bot_state=grid_state;api.bot_command=ai_command;api.player_weapon_name=grid_primary;api.player_weapon=grid_weapon;
         mgr->bots[0].active=mgr->bots[0].joined=1;mgr->bots[0].enemy_client=-1;
         grid_switches=0;zbot_run_frame(mgr,1000);check(grid_switches==1,"armed bot switches from pistol to carried primary");
         check(mgr->bots[0].role==ZBOT_ROLE_PLANT && mgr->bots[0].goal_wp==goal,"SD attacker routes to actual bombzone");
         mgr->bots[0].last_check_ms=5000;zbot_run_frame(mgr,5000);
         check(mgr->bots[0].goal_wp==goal && mgr->bots[0].path_len>0 && mgr->bots[0].path[mgr->bots[0].path_len-1]==goal,"ongoing objective route survives periodic goal timer");
         grid_locked=1;zbot_run_frame(mgr,5050);check(grid_switches==2,"scripted objective lock prevents weapon switching");grid_locked=0;
         api.entity_key=objective_key;api.trace_query=objective_trace;api.bot_state=objective_state;
         objective_x=260;zbot_run_frame(mgr,5100);
         check(!(ai_client_buttons[0]&ZBOT_BTN_USE),"nearby planter outside actual bomb trigger keeps approaching");
         objective_x=310;zbot_run_frame(mgr,5150);
         check(ai_client_buttons[0]&ZBOT_BTN_USE,"planter holds use after entering actual bomb trigger");
         free(mgr);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int i,goal=-1,path[256],n;
         api.api_version=HELIX_SGAME_API_VERSION;api.entity_find=grid_find;api.entity_key=grid_key;api.trace_query=doorway_trace;
         zbot_init(m,NULL,&api,NULL,NULL,"doorway","sd","american","german");
         for(i=0;i<m->wp_count;i++)if(m->waypoints[i].flags&ZBOT_WP_OBJECTIVE)goal=i;
         n=zbot_find_path(m,0,goal,path,256);
         check(goal>=0 && n>0 && m->waypoints[path[n-1]].origin[0]>160,"navigation reaches indoor objective through a narrow offset doorway and low lintel");free(m);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int i,goal=-1,path[256],n;
         api.api_version=HELIX_SGAME_API_VERSION;api.entity_find=grid_find;api.entity_key=stairs_key;api.trace_query=stairs_trace;
         zbot_init(m,NULL,&api,NULL,NULL,"stairs","sd","american","german");
         for(i=0;i<m->wp_count;i++)if(m->waypoints[i].flags&ZBOT_WP_OBJECTIVE)goal=i;
         n=zbot_find_path(m,0,goal,path,256);check(n>0 && m->waypoints[path[n-1]].origin[0]>300,"navigation climbs multiple legal steps within one grid cell");free(m);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int i,goal=-1,path[256],n,detour=0;
         api.api_version=HELIX_SGAME_API_VERSION;api.entity_find=grid_find;api.entity_key=grid_key;api.trace_query=playerclip_trace;clip_nav_wrong_mask=0;
         zbot_init(m,NULL,&api,NULL,NULL,"playerclip","sd","american","german");
         for(i=0;i<m->wp_count;i++)if(m->waypoints[i].flags&ZBOT_WP_OBJECTIVE)goal=i;
         n=zbot_find_path(m,0,goal,path,256);for(i=0;i<n;i++)if(m->waypoints[path[i]].origin[1]>=160)detour=1;
         check(n>0 && detour && !clip_nav_wrong_mask,"navigation respects invisible playerclip walls with the same static collision mask as player movement");free(m);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int x,y,i,site,first_defuser;
         api.api_version=HELIX_SGAME_API_VERSION;api.bot_state=tactical_state;api.bot_command=ai_command;api.trace_query=tactical_trace;api.entity_find=tactical_find;api.entity_key=tactical_key;
         zbot_init(m,NULL,&api,NULL,NULL,"tactical","sd","russian","german");
         memset(tactical_states,0,sizeof(tactical_states));ai_visible=0;
         for(y=0;y<5;y++)for(x=0;x<5;x++){float p[3]={x*400.f,y*400.f,24};zbot_add_waypoint(m,p,(x==4&&(y==0||y==4))?ZBOT_WP_OBJECTIVE:0,-1);}
         zbot_link_waypoints(m);m->sites[0]=4;m->sites[1]=24;m->site_count=2;
         for(i=0;i<6;i++){tactical_states[i].health=100;tactical_states[i].team=i<2||i==5?0:1;tactical_states[i].clip_ammo=30;tactical_states[i].origin[2]=24;tactical_states[i].eye[2]=50;}
         tactical_states[4].origin[0]=1500;
         for(i=0;i<5;i++){m->bots[i].active=m->bots[i].joined=1;m->bots[i].client_num=i;m->bots[i].enemy_client=-1;}
         zbot_run_frame(m,1000);zbot_run_frame(m,1050);zbot_run_frame(m,1100);site=m->attack_site;
         check(m->bots[0].role==ZBOT_ROLE_PLANT && m->bots[1].role==ZBOT_ROLE_ESCORT,"SD team assigns one planter and a supporting attacker");
         check(m->bots[2].goal_wp!=m->bots[3].goal_wp,"defenders split their guarded approaches across both sites");
         m->bots[0].enemy_client=2;m->bots[0].memory_until_ms=10000;memcpy(m->bots[0].enemy_last_pos,m->waypoints[site].origin,sizeof(m->bots[0].enemy_last_pos));
         zbot_run_frame(m,4000);check(m->attack_site!=site,"observed resistance redirects attack to less threatened site");
         site=m->attack_site;zbot_restart_round(m);zbot_run_frame(m,1000);check(m->attack_site!=site,"new round varies selected site without replaying previous plan");
         check(zbot_voice_order(m,5,"follow_me") && m->bots[0].order_kind==1 && m->bots[2].order_kind==0,"follow radio orders affect living teammates only");
         zbot_run_frame(m,1100);check(!(ai_client_buttons[0]&ZBOT_BTN_USE),"following a leader does not mistake leader position for a planting objective");
         check(!zbot_voice_order(m,5,"follow_me"),"radio order cooldown rejects repeated spam");
         tactical_bomb=1;m->next_plan_ms=0;zbot_run_frame(m,5000);zbot_run_frame(m,5050);zbot_run_frame(m,5100);first_defuser=m->defuser;
         check(first_defuser==4 && m->bots[4].role==ZBOT_ROLE_DEFUSE,"nearest available defender receives sole defuse assignment");
         check(m->bots[2].role==ZBOT_ROLE_DEFEND_OBJ && m->bots[3].role==ZBOT_ROLE_DEFEND_OBJ && m->bots[2].goal_wp!=m->bots[4].goal_wp,"other defenders cover approaches rather than crowd bomb");
         check(zbot_voice_order(m,2,"fall_back") && m->bots[4].order_kind==0,"urgent defuse overrides conflicting retreat radio order");
         tactical_states[4].health=0;m->next_plan_ms=0;zbot_run_frame(m,8000);check(m->defuser>=0 && m->defuser!=first_defuser,"dead defuser is replaced by a living teammate");
         tactical_states[0].health=100;zbot_run_frame(m,20000);check(m->bots[0].order_kind==0,"temporary radio order expires and returns to objective plan");
         api.cvar_string=tactical_cvar;tactical_skill="high";zbot_run_frame(m,20050);
         check(m->bots[0].skill==ZBOT_SKILL_PRO && m->bots[2].skill==ZBOT_SKILL_PRO,"live skill cvar updates already connected bots");
         strcpy(m->gametype,"dm");check(!zbot_voice_order(m,5,"follow_me"),"deathmatch radio does not command opposing players as teammates");
         tactical_skill="";tactical_bomb=0;ai_visible=1;free(m);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int i,path[8];float sound[3]={-600,0,24};
         api.api_version=HELIX_SGAME_API_VERSION;api.bot_state=tactical_state;api.bot_command=ai_command;api.trace_query=ai_trace;
         zbot_init(m,NULL,&api,NULL,NULL,"hearing","tdm","american","german");
         memset(tactical_states,0,sizeof(tactical_states));ai_visible=1;
         tactical_states[0].health=tactical_states[63].health=100;tactical_states[63].team=1;
         tactical_states[63].origin[0]=tactical_states[63].eye[0]=-600;
         tactical_states[0].origin[2]=tactical_states[63].origin[2]=24;tactical_states[0].eye[2]=tactical_states[63].eye[2]=50;
         m->bots[0].active=m->bots[0].joined=m->bots[0].was_alive=1;m->bots[0].enemy_client=-1;
         zbot_run_frame(m,1000);check(m->bots[0].enemy_client==-1,"rear enemy in slot 63 is outside the vision cone");
         zbot_hear_sound(m,63,sound,900);zbot_run_frame(m,1050);
         check(m->bots[0].heard_until_ms>1050 && m->bots[0].enemy_client==-1 && !(ai_buttons&ZBOT_BTN_ATTACK),"near rear sound turns attention without identifying or shooting an unseen enemy");
         m->bots[0].heard_until_ms=0;m->bots[0].look_yaw=180;tactical_states[63].eye[2]=2000;zbot_run_frame(m,1075);
         check(m->bots[0].enemy_client==-1,"vision cone also excludes enemies far above the view");tactical_states[63].eye[2]=50;
         m->bots[0].look_yaw=180;zbot_run_frame(m,1100);check(m->bots[0].enemy_client==63,"turning to a visible enemy acquires high client slots");
         {float p[3]={-600,0,24};zbot_add_waypoint(m,p,0,-1);}
         m->bots[0].next_sighting_ms=0;zbot_run_frame(m,1150);check(m->heat[0][0]>0 && m->heat[1][0]==0,"only observing team learns an enemy hotspot");
         {float heat=m->heat[0][0];zbot_restart_round(m);check(m->heat[0][0]>heat*.79f,"hotspot evidence survives round restart");}
         m->bots[0].enemy_client=-1;m->bots[0].heard_until_ms=0;ai_visible=0;
         zbot_hear_sound(m,63,sound,900);check(m->bots[0].heard_until_ms==0,"wall attenuation prevents distant sound detection");
         ai_visible=1;sound[0]=-1500;zbot_hear_sound(m,63,sound,900);check(m->bots[0].heard_until_ms==0,"sounds outside hearing radius reveal no information");
         sound[0]=-200;tactical_states[63].team=0;zbot_hear_sound(m,63,sound,900);check(m->bots[0].heard_until_ms==0,"teammate footsteps do not create enemy clues");
         memset(tactical_states,0,sizeof(tactical_states));tactical_states[0].health=100;tactical_states[0].eye[2]=50;
         m->wp_count=0;m->bots[0].heard_until_ms=0;m->bots[0].was_alive=1;m->bots[0].look_yaw=0;
         for(i=0;i<3;i++){float p[3]={i*300.f,0,24};zbot_add_waypoint(m,p,0,-1);}
         zbot_link_waypoints(m);m->heat[0][2]=80;api.player_weapon=grid_weapon;api.player_weapon_name=hearing_weapon;
         zbot_run_frame(m,3000);check(m->bots[0].grenade_phase==1,"learned hotspot selects an opening grenade without a visible enemy");
         tactical_states[0].weapon_name="fraggrenade_mp";zbot_run_frame(m,3050);
         check(m->bots[0].grenade_phase==2 && (ai_buttons&ZBOT_BTN_ATTACK),"selected grenade pulls its pin through real attack input");
         zbot_run_frame(m,3400);check(m->bots[0].grenade_phase==3 && !(ai_buttons&ZBOT_BTN_ATTACK),"grenade release clears attack before returning to primary");
         m->bots[0].grenade_phase=0;m->bots[0].next_grenade_ms=0;tactical_states[1].health=100;tactical_states[1].origin[0]=600;
         zbot_run_frame(m,5000);check(m->bots[0].grenade_phase==0,"nearby teammate vetoes hotspot grenade");
         tactical_states[1].health=0;ai_visible=0;m->bots[0].next_grenade_ms=0;zbot_run_frame(m,8000);
         check(m->bots[0].grenade_phase==0,"blocked grenade arc vetoes throw");
         m->waypoints[0].edge_count=1;m->waypoints[0].edges[0]=1;m->waypoints[0].blocked_until[0]=10000;m->now_ms=9000;
         check(zbot_find_path(m,0,2,path,8)==0,"temporarily blocked navigation edge is not retried immediately");
         m->now_ms=11000;check(zbot_find_path(m,0,2,path,8)>0,"blocked edge is reconsidered after cooldown");
         ai_visible=1;free(m);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int goal=-1,i,n,path[256];
         api.api_version=HELIX_SGAME_API_VERSION;api.entity_find=grid_find;api.entity_key=wide_key;api.trace_query=wide_trace;
         for(wide_size=4000;wide_size<=8000;wide_size*=2){
             zbot_init(m,NULL,&api,NULL,NULL,"wide","sd","american","german");goal=-1;
             for(i=0;i<m->wp_count;i++)if(m->waypoints[i].flags&ZBOT_WP_OBJECTIVE)goal=i;
             n=zbot_find_path(m,0,goal,path,256);
             check(m->wp_count>8192 && n>0 && path[n-1]==goal && m->waypoints[goal].origin[0]==wide_size,"large navigation graph finishes expansion and reaches far objective beyond old node cap");
             {int valid=1,j;for(i=1;i<n;i++){int linked=0;for(j=0;j<m->waypoints[path[i-1]].edge_count;j++)if(m->waypoints[path[i-1]].edges[j]==path[i])linked=1;if(!linked)valid=0;}check(valid,"heap A* reconstructs connected routes on large and coarsened graphs");}
         }
         free(m);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int i;
         api.api_version=HELIX_SGAME_API_VERSION;api.bot_state=tactical_state;api.bot_command=ai_command;api.trace_query=ai_trace;
         zbot_init(m,NULL,&api,NULL,NULL,"unreachable","sd","american","german");memset(tactical_states,0,sizeof(tactical_states));ai_visible=1;
         tactical_states[0].health=100;tactical_states[0].origin[2]=24;tactical_states[0].eye[2]=50;
         for(i=0;i<3;i++){float p[3]={i==2?2000.f:i*400.f,0,24};zbot_add_waypoint(m,p,i==2?ZBOT_WP_OBJECTIVE:0,-1);}
         m->waypoints[0].edge_count=1;m->waypoints[0].edges[0]=1;m->waypoints[0].dists[0]=400;m->sites[0]=2;m->site_count=1;
         m->bots[0].active=m->bots[0].joined=m->bots[0].was_alive=1;m->bots[0].enemy_client=-1;
         zbot_run_frame(m,1000);check(m->bots[0].goal_wp==2 && m->bots[0].path_len==2 && m->bots[0].path[1]==1,"unreachable objective uses reachable staging point without replacing real bombsite");
         m->bots[0].path_idx=m->bots[0].path_len;tactical_states[0].origin[0]=400;tactical_states[0].eye[0]=400;zbot_run_frame(m,2000);
         check(!(ai_client_buttons[0]&ZBOT_BTN_USE),"attacker at fallback endpoint does not stop forever planting outside bombzone");
         tactical_states[0].origin[0]=tactical_states[0].eye[0]=0;m->bots[0].path_len=2;m->bots[0].path_idx=1;m->bots[0].path[0]=0;m->bots[0].path[1]=1;m->bots[0].last_check_ms=2000;memset(m->bots[0].last_check_pos,0,sizeof(m->bots[0].last_check_pos));m->bots[0].last_check_pos[2]=24;
         zbot_run_frame(m,2500);check(m->waypoints[0].blocked_until[0]==0,"a stalled bot does not globally blacklist a statically clear route segment");
         memset(tactical_states,0,sizeof(tactical_states));tactical_states[0].health=tactical_states[1].health=100;tactical_states[0].eye[2]=tactical_states[1].eye[2]=50;
         strcpy(m->gametype,"tdm");m->site_count=0;m->wp_count=0;memset(m->bots,0,sizeof(m->bots));
         m->wp_count=2000;memset(m->waypoints,0,2000*sizeof(m->waypoints[0]));for(i=0;i<2000;i++){m->waypoints[i].origin[0]=600+(i%40)*4;m->waypoints[i].origin[1]=(i/40)*4;m->waypoints[i].origin[2]=24;m->heat[0][i]=80;}
         for(i=0;i<2;i++){m->bots[i].active=m->bots[i].joined=m->bots[i].was_alive=1;m->bots[i].client_num=i;m->bots[i].enemy_client=-1;}
         api.player_weapon=grid_weapon;api.player_weapon_name=hearing_weapon;ai_visible=0;ai_trace_calls=ai_arc_calls=0;
         zbot_run_frame(m,5000);check(ai_arc_calls<=4 && m->grenade_checks_this_frame==1,"thousands of occluded hot cells and multiple bots have a bounded grenade trace budget");
         check(m->nav_plans_this_frame<=2,"route planning is limited per simulation frame");ai_visible=1;free(m);
     }
     {
         ZBotManager* m=(ZBotManager*)calloc(1,sizeof(*m));hxSGImports api={0};int i,path[16],n;unsigned searches;
         api.api_version=HELIX_SGAME_API_VERSION;api.bot_state=tactical_state;api.bot_command=ai_command;
         zbot_init(m,NULL,&api,NULL,NULL,"long_route","sd","american","german");memset(tactical_states,0,sizeof(tactical_states));tactical_states[0].health=100;tactical_states[0].team=1;tactical_states[0].origin[2]=24;tactical_states[0].eye[2]=50;
         for(i=0;i<600;i++){float p[3]={i*80.f,0,24};zbot_add_waypoint(m,p,i==599?ZBOT_WP_OBJECTIVE:0,-1);if(i){m->waypoints[i-1].edge_count=1;m->waypoints[i-1].edges[0]=i;m->waypoints[i-1].dists[0]=80;}}
         n=zbot_find_path(m,0,599,path,16);check(n==16 && path[0]==0 && path[15]==15,"small route buffer returns a connected prefix without inventing a final goal jump");
         m->sites[0]=599;m->site_count=1;m->bots[0].active=m->bots[0].joined=m->bots[0].was_alive=1;m->bots[0].enemy_client=-1;
         zbot_run_frame(m,1000);searches=m->nav_searches;
         tactical_states[0].origin[0]=tactical_states[0].eye[0]=m->waypoints[m->bots[0].path[m->bots[0].path_len-1]].origin[0];m->bots[0].path_idx=m->bots[0].path_len;
         zbot_run_frame(m,2000);check(m->nav_searches>searches && m->bots[0].path_len>1,"defender continues a truncated route instead of treating its prefix endpoint as an arrived objective");free(m);
     }
     check(zbot_parse_skill("high")==ZBOT_SKILL_PRO,"high skill alias selects pro rather than silently falling back to mid");
     check(zbot_parse_skill("low") == ZBOT_SKILL_LOW, "zbot skill low parse");
     check(zbot_parse_skill("mid") == ZBOT_SKILL_MID, "zbot skill mid parse");
     check(zbot_parse_skill("pro") == ZBOT_SKILL_PRO, "zbot skill pro parse");
     check(zbot_parse_skill("random") == ZBOT_SKILL_RANDOM, "zbot skill random parse");
     check(!strcmp(zbot_skill_name(ZBOT_SKILL_PRO), "pro"), "zbot skill pro name");

     check(!strcmp(zbot_weapon_for("german", "rifle"), "kar98k_mp"), "german rifle loadout");
     check(!strcmp(zbot_weapon_for("german", "smg"), "mp40_mp"), "german smg loadout");
     check(!strcmp(zbot_weapon_for("american", "smg"), "thompson_mp"), "american smg loadout");
     check(!strcmp(zbot_weapon_for("russian", "sniper"), "mosin_nagant_sniper_mp"), "russian sniper loadout");
     check(!strcmp(zbot_weapon_for("british", "smg"), "sten_mp"), "british smg loadout");

     {
         char out[128];
         zbot_format_roast("Sit down %s, nice try.", "Player1", out, sizeof(out));
         check(!strcmp(out, "Sit down Player1, nice try."), "roast format with %s");
         zbot_format_roast("Nice try {name}!", "Enemy", out, sizeof(out));
         check(!strcmp(out, "Nice try Enemy!"), "roast format with {name}");
     }

     {
         static ZBotManager mgr;
         float p0[3] = { 0, 0, 0 }, p1[3] = { 300, 0, 0 }, p2[3] = { 600, 0, 0 };
         int path[16];
         memset(&mgr, 0, sizeof(mgr));
         zbot_add_waypoint(&mgr, p0, 0, 0);
         zbot_add_waypoint(&mgr, p1, 0, 1);
         zbot_add_waypoint(&mgr, p2, 0, 2);
         zbot_link_waypoints(&mgr);
         int plen = zbot_find_path(&mgr, 0, 2, path, 16);
         check(plen == 3 && path[0] == 0 && path[1] == 1 && path[2] == 2, "A* pathfinding connects chained waypoints");
         {ZBotObjectiveTask task={0};ZBotAgent* bot=&mgr.bots[1];
          task.kind=ZBOT_OBJECTIVE_RE_PICKUP;task.entity=7;task.radius=64;task.origin[0]=600;
          bot->path_len=3;bot->objective_wp=2;zbot_set_objective(&mgr,1,&task);
          check(bot->path_len==0 && bot->objective_wp==-1,"new objective invalidates obsolete route");
          bot->path_len=3;bot->objective_wp=2;zbot_set_objective(&mgr,1,&task);
          check(bot->path_len==3 && bot->objective_wp==2,"stable objective preserves route and cached anchor");
          task.kind=ZBOT_OBJECTIVE_RE_DELIVER;task.entity=8;zbot_set_objective(&mgr,1,&task);
          check(bot->path_len==0 && bot->objective.kind==ZBOT_OBJECTIVE_RE_DELIVER,"pickup-to-carrier transition replans delivery");
          zbot_set_objective(&mgr,1,NULL);
          check(bot->objective.kind==ZBOT_OBJECTIVE_NONE && bot->objective_wp==-1,"clearing objective removes stale task");
         }
     }

 }
 g->shutdown(g,0);imp.hud_update=tenths_hud;g->init(g,NULL,&imp,"mp_tenths",0);
 g->run_frame(g,0);check(!strcmp(tenths_text,"3.4"),"script tenths timer publishes fractional initial value");
 g->run_frame(g,1000);check(!strcmp(tenths_text,"2.4"),"script tenths timer counts simulation time in tenths");
 g->shutdown(g,0);imp.entity_find=native_find;imp.player_archive=archive_player;g->init(g,NULL,&imp,"mp_archive_fields",0);
 check(archive_field_value==300 && archive_seconds==0,"script archivetime uses host-clamped history and clears playback");
 g->shutdown(g,0);imp.hud_update=localized_hud;g->init(g,NULL,&imp,"mp_l10n",0);g->run_frame(g,0);
 check(localized_format_seen,"HUD setText preserves localized format arguments and numeric values");
 check(localized_concat_seen,"localized string concatenation retains deferred translation markers");
 g->shutdown(g,0);imp.particle_emit=ops_emit;g->init(g,NULL,&imp,"mp_missing_ops",0);g->run_frame(g,0);
 check(ops_tested==1,"all newly confirmed MP native operations execute successfully in sgame");
 check(fog_end>0,"setExpFog computes valid positive r_fogEnd");
 check(missing_ops_emitted==1,"playfxontag emits particles at target entity tag");
 g->shutdown(g,0);imp.objective_update=objective_carrier_probe;imp.entity_loop_sound=loop_probe;g->init(g,NULL,&imp,"mp_hq_metadata",0);check(!strcmp(ambient,"metadata_complete") && !objective_visible,"HQ metadata updates after objective deletion do not reactivate the slot");check(loop_calls==3 && loop_stops==2,"loop sound stops accept both stock alias argument and zero-argument form");
 g->shutdown(g,0);imp.entity_find=native_find;imp.player_view=player_view;imp.objective_update=objective_carrier_probe;imp.hud_update=announcement_probe;view.origin[0]=123;g->init(g,NULL,&imp,"mp_carrier",0);check(announcement_format_seen && client_format_seen,"announcements retain localized replacement arguments");check(carrier_updates>0 && carrier_x==123,"objective_onEntity accepts a player and publishes its position");view.origin[0]=456;g->run_frame(g,0);check(carrier_x==456,"player objective follows movement on the next frame");g->run_frame(g,50);check(carrier_x==9,"objective_position detaches player tracking");view.origin[0]=789;g->run_frame(g,100);check(carrier_x==9,"detached objective retains its explicit position");
 if(!strcmp(get(NULL,"g_gsc_runtime"),"linked")){g->shutdown(g,0);g->init(g,NULL,&imp,"mp_link_bad",0);check(!strcmp(runtime_active,"failed") && !runtime_linked && !ambient[0],"unbound unused mod call blocks both map and gametype execution");check(g->client_connect(g,0,"blocked")!=0,"failed linked preparation rejects clients without legacy fallback");g->shutdown(g,0);g->init(g,NULL,&imp,"mp_link_no_main",0);check(!strcmp(runtime_active,"failed") && !runtime_linked && !ambient[0],"missing map main blocks gametype startup before any script runs");}
 g->shutdown(g,0);g->destroy(g);return failures?1:0;}

