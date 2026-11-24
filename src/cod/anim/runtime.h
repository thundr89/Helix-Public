#ifndef COD_ANIMATION_RUNTIME_H
#define COD_ANIMATION_RUNTIME_H
#include "helix/game/iserver_game.h"
typedef struct CodNativeAnimation CodNativeAnimation;
CodNativeAnimation* cod_animation_init(CodNativeAnimation*,hxSGEngine*,hxSGImports*);
void cod_animation_free(CodNativeAnimation*);
void cod_animation_frame(CodNativeAnimation*,int client,int time_ms,int mounted);
void cod_animation_reset_client(CodNativeAnimation*,int client);
void cod_animation_reference(CodNativeAnimation*,const char* tree,const char* clip);
int cod_animation_length(CodNativeAnimation*,const char* tree,const char* clip,float* result);
int cod_animation_has_note(CodNativeAnimation*,const char* tree,const char* clip,const char* note,int* found);
void cod_animation_note_callback(CodNativeAnimation*,void (*emit)(void*,int,const char*,const char*,int),void*);
#endif
