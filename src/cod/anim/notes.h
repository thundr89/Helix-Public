#ifndef COD_ANIM_NOTES_H
#define COD_ANIM_NOTES_H
#include "helix/game/iserver_game.h"
typedef struct CodNoteCursor {double phase;int initial;} CodNoteCursor;
typedef void (*CodNoteEmit)(void*,const char*);
void cod_notes_reset(CodNoteCursor*,float duration,float rate);
/* Returns -1 on bounded event overflow; phase still advances to the target. */
int cod_notes_advance(CodNoteCursor*,const hxSGAnimationNote*,int count,float duration,int looped,double seconds,float rate,CodNoteEmit,void*);
#endif
