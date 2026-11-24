#ifndef COD_PLAYER_CONTROLS_H
#define COD_PLAYER_CONTROLS_H
#include "helix/game/iserver_game.h"
typedef struct CodPlayerControls {
    int known,time,health,legs_active,torso_active,pitch_active,turn;
    float legs_yaw,torso_yaw,pitch,angles[7][3],offset[3];
} CodPlayerControls;
void cod_player_controls(CodPlayerControls*,const hxSGAnimationInput*,const hxSGPoseInput*,int time,int mounted,hxSGJointPose out[7]);
#endif
