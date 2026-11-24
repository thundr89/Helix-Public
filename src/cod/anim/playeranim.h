#ifndef COD_PLAYERANIM_H
#define COD_PLAYERANIM_H
#include "helix/game/iserver_game.h"
typedef struct CodAnimProgram CodAnimProgram;
typedef struct CodAnimAction {char clips[3][128],sound[128];int duration[3],blend[3],turret[3];} CodAnimAction;
typedef struct CodAnimInput {const hxSGAnimationInput* state;const char* move;const char* strafing;int mounted;} CodAnimInput;
CodAnimProgram* cod_anim_parse(const char* script,const char* tree,char* error,unsigned capacity);
void cod_anim_free(CodAnimProgram* program);
int cod_anim_node(const CodAnimProgram*,const char* name); /* 1 leaf, 2 branch, 0 absent */
int cod_anim_clip_count(const CodAnimProgram*);
const char* cod_anim_clip(const CodAnimProgram*,int index);
int cod_anim_select(const CodAnimProgram*,const char* context,const CodAnimInput*,unsigned choice,CodAnimAction* result);
#endif
