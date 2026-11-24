#include "anim/playeranim.h"
#include "anim/notes.h"
#include "anim/controls.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int errors;
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m);errors++;}}while(0)
static char received[4096];
static void note_emit(void* user,const char* name){(void)user;strcat(received,name);strcat(received,",");}
static void note_tests(void){
    hxSGAnimationNote notes[3]={{0,"start"},{.5f,"step"},{1,"end"}};CodNoteCursor cursor;int count;
    received[0]=0;cod_notes_reset(&cursor,1,1);count=cod_notes_advance(&cursor,notes,3,1,0,0,1,note_emit,NULL);CHECK(count==1&&!strcmp(received,"start,"),"frame zero fires once");
    received[0]=0;cod_notes_advance(&cursor,notes,3,1,0,.5,1,note_emit,NULL);CHECK(!strcmp(received,"step,"),"crossing exact note time emits");
    received[0]=0;cod_notes_advance(&cursor,notes,3,1,0,.5,0,note_emit,NULL);CHECK(!*received,"pause emits nothing");
    cod_notes_advance(&cursor,notes,3,1,0,1,1,note_emit,NULL);CHECK(!strcmp(received,"end,"),"nonlooping end fires once");
    received[0]=0;cod_notes_advance(&cursor,notes,3,1,0,5,1,note_emit,NULL);CHECK(!*received,"held end never repeats");
    cod_notes_reset(&cursor,1,1);received[0]=0;cod_notes_advance(&cursor,notes,3,1,1,2.5,1,note_emit,NULL);CHECK(!strcmp(received,"start,step,end,start,step,end,start,step,"),"multiple loop crossings keep temporal order");
    cod_notes_reset(&cursor,1,1);received[0]=0;cod_notes_advance(&cursor,notes,3,1,0,.25,1,note_emit,NULL);received[0]=0;
    cod_notes_advance(&cursor,notes,3,1,0,.125,2,note_emit,NULL);CHECK(!strcmp(received,"step,"),"rate change preserves phase");
    cod_notes_reset(&cursor,1,1);received[0]=0;cod_notes_advance(&cursor,notes,3,1,0,.25,1,note_emit,NULL);CHECK(!strcmp(received,"start,"),"replacement starts a new cursor without old markers");
    cod_notes_reset(&cursor,1,-1);received[0]=0;cod_notes_advance(&cursor,notes,3,1,0,1,-1,note_emit,NULL);CHECK(!strcmp(received,"end,step,start,"),"reverse crosses notes in reverse order");
    cod_notes_reset(&cursor,1,1);CHECK(cod_notes_advance(&cursor,notes,3,1,1,10000,1,NULL,NULL)==-1,"unbounded event work is reported");
    CHECK(cod_notes_advance(&cursor,notes,3,1,1,-1,1,NULL,NULL)==-1,"invalid time rejected");
}
static void controller_tests(void){
    CodPlayerControls c={0};hxSGAnimationInput input={0};hxSGPoseInput pose={0};hxSGJointPose joints[7];int i;input.health=100;input.state=1;input.pitch=60;
    cod_player_controls(&c,&input,&pose,0,0,joints);CHECK(fabsf(joints[0].angles[0]-7.2f)<.001f && fabsf(joints[2].angles[0]-18.f)<.001f,"native spine pitch distribution");
    CHECK(fabsf(joints[3].angles[0]+joints[4].angles[0]-24.f)<.001f,"remaining aim is divided over neck and head");
    memset(&c,0,sizeof(c));input.pitch=0;pose.lean_fraction=.5f;cod_player_controls(&c,&input,&pose,0,0,joints);
    CHECK(fabsf(joints[0].angles[2]-13.875f)<.001f && fabsf(joints[6].offset[1]+1.875f)<.001f,"standing right lean uses native curve and stance factor");
    memset(&c,0,sizeof(c));input.stance=1;cod_player_controls(&c,&input,&pose,0,0,joints);CHECK(joints[0].angles[2]>20,"crouched lean uses separate scale");
    memset(&c,0,sizeof(c));input.stance=0;pose.lean_fraction=0;cod_player_controls(&c,&input,&pose,0,0,joints);input.yaw=30;
    for(i=1;i<=10;i++)cod_player_controls(&c,&input,&pose,i*50,0,joints);CHECK(fabsf(c.legs_yaw)<.001f && !c.turn,"idle feet keep forty degree deadband");
    input.yaw=90;cod_player_controls(&c,&input,&pose,550,0,joints);CHECK(c.turn==1 && c.legs_yaw>0 && c.legs_yaw<90,"crossing deadband starts native left turn");
    input.health=0;input.yaw=-90;cod_player_controls(&c,&input,&pose,600,0,joints);CHECK(c.legs_yaw>0,"death freezes prior controller state");
    input.health=100;cod_player_controls(&c,&input,&pose,650,0,joints);CHECK(fabsf(c.legs_yaw+90)<.001f && !c.turn,"respawn resets yaw without inherited turn");
    memset(&c,0,sizeof(c));input.yaw=179;cod_player_controls(&c,&input,&pose,0,0,joints);input.yaw=-179;cod_player_controls(&c,&input,&pose,50,0,joints);CHECK(!c.turn && fabsf(joints[6].angles[1])<3,"yaw wrap follows short arc");
    input.pitch=60;pose.lean_fraction=.5f;for(i=2;i<20;i++)cod_player_controls(&c,&input,&pose,i*50,1,joints);CHECK(fabsf(joints[0].angles[0])<.001f && fabsf(joints[0].angles[2])<.001f,"mounted state clears ordinary body controllers");
}
static void prone_controller_test(void){
    CodPlayerControls c={0};hxSGAnimationInput input={0};hxSGPoseInput pose={0};hxSGJointPose joints[7];int i;
    input.health=100;input.stance=2;input.pitch=70;input.yaw=110;pose.lean_fraction=1;
    for(i=0;i<20;i++)cod_player_controls(&c,&input,&pose,i*50,0,joints);
    CHECK(!c.turn && fabsf(c.pitch)<.001f,"prone never selects upright turn or pitch controllers");
    CHECK(fabsf(joints[0].angles[0])<.001f && fabsf(joints[0].angles[2])<.001f,"prone suppresses upright spine lean");
}
int main(void){note_tests();controller_tests();prone_controller_test();
    const char* tree="root { idle run shoot branch : complete loopsync { aim_left aim_right } }";
    const char* script="DEFINES\nset weaponclass automatic = smg AND mg\nANIMATIONS\nSTATE COMBAT {\nidle { weaponclass automatic { both shoot } default { both idle } }\nrun { default { legs run torso shoot blendtime 50 } }\n}\nEVENTS\nfireweapon { weaponclass pistol { torso shoot duration 150 } default { } }\n";
    char message[192];CodAnimProgram* p=cod_anim_parse(script,tree,message,sizeof(message));hxSGAnimationInput state={0};CodAnimInput input={0};CodAnimAction action;
    CHECK(p!=NULL,"real grammar parses braces, defines, ordered rules and multiple body slots");if(!p){fprintf(stderr,"%s\n",message);return 1;}
    input.state=&state;input.move="idle";input.strafing="not";state.weapon_class="smg";state.weapon_name="mp40_mp";
    CHECK(cod_anim_node(p,"ROOT")==2 && cod_anim_node(p,"shoot")==1 && !cod_anim_node(p,"missing"),"ATR hierarchy distinguishes branch and leaf");
    CHECK(cod_anim_clip_count(p)==3,"script dependencies are deduplicated");
    CHECK(cod_anim_select(p,"combat/idle",&input,0,&action) && !strcmp(action.clips[0],"shoot"),"defines and first matching rule select native clip");
    state.weapon_class="rifle";CHECK(cod_anim_select(p,"combat/idle",&input,0,&action)&&!strcmp(action.clips[0],"idle"),"default follows failed conditions");
    CHECK(cod_anim_select(p,"combat/run",&input,0,&action)&&!strcmp(action.clips[1],"run")&&!strcmp(action.clips[2],"shoot")&&action.blend[2]==50,"two channels on one line are one command");
    state.weapon_class="pistol";CHECK(cod_anim_select(p,"fireweapon",&input,0,&action)&&action.duration[2]==150,"event duration is preserved");
    state.weapon_class="rifle";CHECK(cod_anim_select(p,"fireweapon",&input,0,&action)&&!action.clips[0][0]&&!action.clips[2][0],"empty matching event suppresses special animation");
    cod_anim_free(p);
    p=cod_anim_parse("ANIMATIONS STATE COMBAT { idle { default { both missing } } }","root { a a }",message,sizeof(message));CHECK(!p,"duplicate ATR nodes rejected");
    p=cod_anim_parse("ANIMATIONS STATE COMBAT { idle { unknownvalue yes { both idle } } }",tree,message,sizeof(message));CHECK(!p,"unknown conditions rejected instead of accepted as defaults");
    p=cod_anim_parse("/* unterminated",tree,message,sizeof(message));CHECK(!p,"malformed source rejected atomically");
    fprintf(stdout,"Native animation rules: %d failures\n",errors);return errors?1:0;
}
