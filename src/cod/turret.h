/* Native CoD stationary MG42 interaction; engine services remain generic. */
#ifndef COD_TURRET_H
#define COD_TURRET_H
#define COD_TURRETS 32
#define COD_CLIENTS 64
typedef struct CodTurret {
    int id, owner, damage, delay;
    float aim[3], butt[3], flash[3], rest;
    float origin[3], pitch, yaw, left, right, top, bottom, spacing;
} CodTurret;
typedef struct CodTurretClient { int mounted, buttons, next_fire; float anchor[3]; } CodTurretClient;
typedef struct CodTurrets { CodTurret guns[COD_TURRETS]; int count; CodTurretClient clients[COD_CLIENTS]; } CodTurrets;
#endif
