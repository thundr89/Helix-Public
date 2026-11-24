/* GPL-2.0-or-later. CoD controller-tag targets; recovered OpenCoDUO BG_PlayerAngles /
 * BG_Player_DoControllers are the reference for coefficients and tag semantics.
 * Native x87 bit-for-bit emulation and vehicle/prone controllers are not included. */
#include "anim/controls.h"
#include <math.h>
#include <string.h>
static float clamp(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
static float angle(float x){x=fmodf(x+180.f,360.f);if(x<0)x+=360.f;return x-180.f;}
static void swing(float target,float deadband,float maximum,float scale,float dt,float* value,int* active){
    float d=angle(target-*value),step;
    if(!*active && fabsf(d)>deadband)*active=1;
    if(*active){step=fmaxf(.5f,fabsf(d)*.05f)*dt*scale;if(step>=fabsf(d)){*value=angle(target);*active=0;}else *value=angle(*value+(d<0?-step:step));}
    d=angle(target-*value);if(fabsf(d)>maximum)*value=angle(target-(d<0?-maximum:maximum));
}
void cod_player_controls(CodPlayerControls* c,const hxSGAnimationInput* s,const hxSGPoseInput* pose,int now,int mounted,hxSGJointPose out[7]){
    static const char* names[]={"back_low","back_mid","back_up","neck","head","pelvis","tag_origin"};
    float target[7][3]={{0}},offset[3]={0},dt,old_legs,dp,dy,op,oy,lean,swing_lean,secondary;int fresh,i,j,restricted;
    fresh=!c->known || (c->health<=0 && s->health>0) || now<c->time;
    dt=fresh?0:clamp((float)(now-c->time),0,200);c->time=now;c->turn=0;
    if(fresh){memset(c,0,sizeof(*c));c->known=1;c->time=now;c->legs_yaw=c->torso_yaw=angle(s->yaw);c->pitch=clamp(angle(s->pitch),-89,89)*.6f;}
    if(s->health<=0 && !fresh)goto emit;
    restricted=mounted || s->on_ladder || s->stance==2;
    old_legs=c->legs_yaw;
    if(sqrtf(s->velocity[0]*s->velocity[0]+s->velocity[1]*s->velocity[1])>5){c->legs_active=1;c->torso_active=1;c->pitch_active=1;}
    if(s->firing){c->torso_active=1;c->pitch_active=1;}
    swing(s->yaw,0,90,.2f,dt,&c->torso_yaw,&c->torso_active);
    swing(s->yaw,c->legs_active?0:40,150,.2f,dt,&c->legs_yaw,&c->legs_active);
    swing(restricted?0:clamp(angle(s->pitch),-89,89)*.6f,0,45,.15f,dt,&c->pitch,&c->pitch_active);
    if(c->legs_active || fabsf(angle(c->legs_yaw-old_legs))>.01f)c->turn=angle(c->legs_yaw-old_legs)>0?1:-1;
    if(restricted){c->legs_yaw=c->torso_yaw=angle(s->yaw);c->pitch=0;c->turn=0;}
    dp=restricted?0:angle(s->pitch-c->pitch);dy=restricted?0:angle(s->yaw-c->torso_yaw);op=c->pitch;oy=angle(c->torso_yaw-c->legs_yaw);
    lean=restricted?0:clamp(pose->lean_fraction,-1,1);lean=(2.f-fabsf(lean))*lean;
    swing_lean=lean*46.25f;
    if(lean!=0)swing_lean*=s->stance==1?(lean>0?1.5f:1.25f):(lean>0?.8f:1.25f);
    secondary=swing_lean;
    target[0][0]=op*.2f;target[0][1]=oy*.4f;target[0][2]=swing_lean*.5f;
    target[1][0]=op*.3f;target[1][1]=oy*.4f;target[1][2]=swing_lean*.5f;
    target[2][0]=op*.5f;target[2][1]=oy*.2f;target[2][2]=swing_lean*-.6f;
    target[3][0]=dp*.3f;target[3][1]=dy*.3f;
    target[4][0]=dp*.7f;target[4][1]=dy*.7f;target[4][2]=secondary*-.3f;
    target[6][1]=angle(c->legs_yaw-s->yaw);target[6][2]=lean*3.75f;offset[1]=-lean*2.5f;
    for(i=0;i<7;i++)for(j=0;j<3;j++)c->angles[i][j]=fresh?target[i][j]:angle(c->angles[i][j]+clamp(angle(target[i][j]-c->angles[i][j]),-dt*.36f,dt*.36f));
    for(j=0;j<3;j++)c->offset[j]=fresh?offset[j]:c->offset[j]+clamp(offset[j]-c->offset[j],-dt*.1f,dt*.1f);
emit:
    c->health=s->health;
    for(i=0;i<7;i++){out[i].name=names[i];out[i].model_axes=i<6;for(j=0;j<3;j++){out[i].angles[j]=c->angles[i][j];out[i].offset[j]=i==6?c->offset[j]:0;}}
}
