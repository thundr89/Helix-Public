/* GPL-2.0-or-later. Material-aware authoritative CoD movement sounds. */
#ifndef COD_FOOTSTEPS_H
#define COD_FOOTSTEPS_H
#include "helix/game/iserver_game.h"
typedef struct CodFootsteps {
    struct {int known,mode,last_ground;float origin[3],distance;int last_variant;} clients[64];
    unsigned char variants[3][23],fallback[3][23];
    int last_time;unsigned rng;
    void (*on_sound)(void*,int,const float*,float);
    void* sound_context;
} CodFootsteps;
const char* cod_surface_name(int flags);
void cod_footsteps_init(CodFootsteps*,hxSGEngine*,const hxSGImports*);
void cod_footsteps_frame(CodFootsteps*,hxSGEngine*,const hxSGImports*,int now);
#endif
