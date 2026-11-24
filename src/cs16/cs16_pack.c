#include "helix/game/ihost_facade.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define CS16_PACK_EXPORT __declspec(dllexport)
#else
#define CS16_PACK_EXPORT __attribute__((visibility("default")))
#endif

typedef struct Cs16SimState {
    const hxSimulationService* service;
    uint32_t ticks;
    uint64_t round_start_time;
    int phase; /* 0: freeze (5s), 1: active (105s), 2: round over (5s) */
    int round_number;
    int t_score;
    int ct_score;
    int bomb_planted;
    uint32_t bomb_timer_ms;
} Cs16SimState;

static void HELIX_HOST_CALL cs16_sim_frame(void* userdata, const hxHostFacade* host,
                                           uint64_t simulation_time_ms, uint32_t delta_ms) {
    Cs16SimState* sim = (Cs16SimState*)userdata;
    (void)host;
    (void)delta_ms;
    if (!sim) return;

    ++sim->ticks;

    /* Maintain CS 1.6 round flow and update team scores */
    if (sim->round_start_time == 0) {
        sim->round_start_time = simulation_time_ms;
        sim->phase = 0; /* Freeze period */
    }

    uint64_t elapsed_in_round = simulation_time_ms - sim->round_start_time;
    if (sim->phase == 0 && elapsed_in_round >= 5000) {
        sim->phase = 1; /* Round active */
    } else if (sim->phase == 1 && elapsed_in_round >= 110000) {
        sim->phase = 2; /* Round time expired -> CTs win */
        ++sim->ct_score;
        if (sim->service && sim->service->struct_size >= offsetof(hxSimulationService, set_team_score) + sizeof(sim->service->set_team_score) &&
            sim->service->set_team_score) {
            sim->service->set_team_score(sim->service->context, 0, sim->t_score);
            sim->service->set_team_score(sim->service->context, 1, sim->ct_score);
        }
    } else if (sim->phase == 2 && elapsed_in_round >= 115000) {
        /* New round */
        ++sim->round_number;
        sim->round_start_time = simulation_time_ms;
        sim->phase = 0;
        sim->bomb_planted = 0;
    }
}

static void HELIX_HOST_CALL cs16_sim_stop(void* userdata) {
    free(userdata);
}

static int cs16_define_weapons(const hxHostFacade* host) {
    if (!host->pack || !host->pack->define_weapon) return 0;

    static const struct {
        int id;
        const char* name;
        const char* display;
        int damage;
        int fire_delay;
        int mag_size;
        int start_ammo;
        int slot;
        int reload_ms;
        int max_ammo;
        int automatic;
        int pellets;
        int spread_deg;
        int ads;
        int ads_fov;
        int proj_type;
        int proj_speed;
        int fuse_ms;
        int expl_radius;
        int expl_damage;
        int disposable;
        int melee_dmg;
        int melee_range;
        const char* category;
    } kWeapons[] = {
        /* Melee */
        { 16, "cs16.knife", "Knife", 0, 400, 1, 1, 2, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 65, 64, "melee" },

        /* Pistols (Slot 1) */
        { 17, "cs16.glock18", "Glock-18", 25, 160, 20, 20, 1, 2200, 120, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "pistol" },
        { 18, "cs16.usp", "USP .45", 30, 170, 12, 12, 1, 2500, 100, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "pistol" },
        { 19, "cs16.p228", "P228", 28, 160, 13, 13, 1, 2700, 52, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "pistol" },
        { 20, "cs16.deagle", "Desert Eagle", 54, 220, 7, 7, 1, 2200, 35, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "pistol" },
        { 21, "cs16.fiveseven", "Five-Seven", 20, 160, 20, 20, 1, 2700, 100, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "pistol" },
        { 22, "cs16.elites", "Dual Elites", 24, 130, 30, 30, 1, 3500, 120, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "pistol" },

        /* Shotguns (Slot 0) */
        { 23, "cs16.m3", "M3 Super 90", 20, 900, 8, 8, 0, 3000, 32, 0, 6, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "shotgun" },
        { 24, "cs16.xm1014", "XM1014", 20, 250, 7, 7, 0, 3000, 32, 1, 6, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "shotgun" },

        /* SMGs (Slot 0) */
        { 25, "cs16.mp5", "MP5 Navy", 26, 110, 30, 30, 0, 2600, 120, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "smg" },
        { 26, "cs16.tmp", "TMP", 20, 85, 30, 30, 0, 2100, 120, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "smg" },
        { 27, "cs16.mac10", "MAC-10", 29, 85, 30, 30, 0, 3100, 100, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "smg" },
        { 28, "cs16.ump45", "UMP45", 30, 130, 25, 25, 0, 3400, 100, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "smg" },
        { 29, "cs16.p90", "ES P90", 21, 90, 50, 50, 0, 3400, 100, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "smg" },

        /* Rifles & Snipers (Slot 0) */
        { 30, "cs16.galil", "IDF Defender", 30, 110, 35, 35, 0, 2600, 90, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "rifle" },
        { 31, "cs16.famas", "Clarion 5.56", 30, 110, 25, 25, 0, 3300, 90, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "rifle" },
        { 32, "cs16.ak47", "AK-47", 36, 120, 30, 30, 0, 2400, 90, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "rifle" },
        { 33, "cs16.m4a1", "M4A1 Carbine", 33, 110, 30, 30, 0, 3100, 90, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "rifle" },
        { 34, "cs16.aug", "Bullpup", 32, 110, 30, 30, 0, 3300, 90, 1, 1, 1, 1, 55, 0, 0, 0, 0, 0, 0, 0, 0, "rifle" },
        { 35, "cs16.sg552", "Krieg 552", 33, 110, 30, 30, 0, 3000, 90, 1, 1, 1, 1, 55, 0, 0, 0, 0, 0, 0, 0, 0, "rifle" },
        { 36, "cs16.scout", "Schmidt Scout", 75, 1200, 10, 10, 0, 2800, 90, 0, 1, 0, 1, 40, 0, 0, 0, 0, 0, 0, 0, 0, "sniper" },
        { 37, "cs16.awp", "AWP", 115, 1500, 10, 10, 0, 2700, 30, 0, 1, 0, 1, 30, 0, 0, 0, 0, 0, 0, 0, 0, "sniper" },
        { 38, "cs16.m249", "M249 Para", 32, 100, 100, 100, 0, 4600, 200, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "heavy" },

        /* Equipment (Slot 3) */
        { 41, "cs16.hegrenade", "HE Grenade", 0, 1000, 1, 1, 3, 0, 1, 0, 1, 0, 0, 0, 2, 700, 2500, 250, 100, 1, 0, 0, "grenade" }
    };

    const size_t num_weapons = sizeof(kWeapons) / sizeof(kWeapons[0]);
    for (size_t i = 0; i < num_weapons; ++i) {
        hxWeaponDesc w;
        memset(&w, 0, sizeof(w));
        w.version = HELIX_WEAPON_DESC_VERSION;
        w.struct_size = sizeof(hxWeaponDesc);
        w.id = kWeapons[i].id;
        w.name = kWeapons[i].name;
        w.display_name = kWeapons[i].display;
        w.damage = kWeapons[i].damage;
        w.fire_delay_ms = kWeapons[i].fire_delay;
        w.mag_size = kWeapons[i].mag_size;
        w.start_ammo = kWeapons[i].start_ammo;
        w.slot = kWeapons[i].slot;
        w.reload_ms = kWeapons[i].reload_ms;
        w.max_ammo = kWeapons[i].max_ammo;
        w.automatic = kWeapons[i].automatic;
        w.pellets = kWeapons[i].pellets;
        w.spread_deg = kWeapons[i].spread_deg;
        w.ammo_per_shot = 1;
        w.melee_damage = kWeapons[i].melee_dmg;
        w.melee_range = kWeapons[i].melee_range ? kWeapons[i].melee_range : 64;
        w.melee_delay_ms = 400;
        w.ads = kWeapons[i].ads;
        w.ads_fov = kWeapons[i].ads_fov ? kWeapons[i].ads_fov : 55;
        w.ads_in_ms = 200;
        w.ads_out_ms = 200;
        w.projectile_type = kWeapons[i].proj_type;
        w.projectile_speed = kWeapons[i].proj_speed;
        w.fuse_ms = kWeapons[i].fuse_ms;
        w.explosion_radius = kWeapons[i].expl_radius;
        w.explosion_damage = kWeapons[i].expl_damage;
        w.disposable = kWeapons[i].disposable;
        w.category = kWeapons[i].category;

        if (!host->pack->define_weapon(host->pack->context, &w)) return 0;
    }
    return 1;
}

static int cs16_define_items(const hxHostFacade* host) {
    if (!host->pack || !host->pack->define_item) return 0;

    static const struct {
        const char* name;
        int kind; /* 1: health, 2: armor, 3: ammo */
        int amount;
        const char* weapon;
    } kItems[] = {
        { "cs16.item_kevlar", 2, 100, NULL },
        { "cs16.item_assaultsuit", 2, 100, NULL },
        { "cs16.ammo_9mm", 3, 30, "cs16.glock18" },
        { "cs16.ammo_45acp", 3, 12, "cs16.usp" },
        { "cs16.ammo_50ae", 3, 7, "cs16.deagle" },
        { "cs16.ammo_556nato", 3, 30, "cs16.m4a1" },
        { "cs16.ammo_762nato", 3, 30, "cs16.ak47" },
        { "cs16.ammo_buckshot", 3, 8, "cs16.m3" },
        { "cs16.ammo_338mag", 3, 10, "cs16.awp" }
    };

    const size_t num_items = sizeof(kItems) / sizeof(kItems[0]);
    for (size_t i = 0; i < num_items; ++i) {
        hxItemDesc it;
        memset(&it, 0, sizeof(it));
        it.version = HELIX_ITEM_DESC_VERSION;
        it.struct_size = sizeof(hxItemDesc);
        it.name = kItems[i].name;
        it.kind = kItems[i].kind;
        it.amount = kItems[i].amount;
        it.weapon = kItems[i].weapon;

        if (!host->pack->define_item(host->pack->context, &it)) return 0;
    }
    return 1;
}

static int cs16_setup_simulation(const hxHostFacade* host) {
    if (!host->simulation || !host->simulation->register_controller) return 1;

    Cs16SimState* state = (Cs16SimState*)calloc(1, sizeof(Cs16SimState));
    if (!state) return 0;
    state->service = host->simulation;

    hxSimulationController ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.version = HELIX_SIMULATION_CONTROLLER_VERSION;
    ctrl.struct_size = sizeof(hxSimulationController);
    ctrl.id = "cs16.simulation";
    ctrl.userdata = state;
    ctrl.frame = cs16_sim_frame;
    ctrl.shutdown = cs16_sim_stop;

    if (!host->simulation->register_controller(host->simulation->context, &ctrl)) {
        free(state);
        return 0;
    }
    return 1;
}

static int cs16_setup(const hxHostFacade* host, int is_server) {
    if (!host || host->version != HELIX_HOST_FACADE_VERSION ||
        host->struct_size < offsetof(hxHostFacade, pack) + sizeof(host->pack)) {
        return 0;
    }

    /* 1. Rules */
    if (host->pack && host->pack->struct_size >= offsetof(hxPackService, set_rules) + sizeof(host->pack->set_rules) &&
        host->pack->set_rules) {
        static const char kCs16Rules[] =
            "gametype=de\n"
            "teamplay=1\n"
            "timelimit=25\n"
            "fraglimit=0\n"
            "maxhealth=100\n"
            "maxarmor=100\n"
            "speed=250\n";
        host->pack->set_rules(host->pack->context, kCs16Rules);
    }

    /* 2. Weapons */
    if (!cs16_define_weapons(host)) return 0;

    /* 3. Items */
    if (!cs16_define_items(host)) return 0;

    /* 4. Server-only simulation controller */
    if (is_server) {
        if (!cs16_setup_simulation(host)) return 0;
    }

    /* 5. Request default map if request_map is available */
    if (host->pack && host->pack->struct_size >= offsetof(hxPackService, request_map) + sizeof(host->pack->request_map) &&
        host->pack->request_map) {
        static const hxMapRequest req = {
            HELIX_MAP_REQUEST_VERSION,
            sizeof(hxMapRequest),
            "hxmap",
            "maps/de_dust2.hxmap",
            NULL,
            0,
            NULL,
            0
        };
        host->pack->request_map(host->pack->context, &req);
    }

    return 1;
}

CS16_PACK_EXPORT int HELIX_HOST_CALL helix_game_server(const hxHostFacade* host) {
    return cs16_setup(host, 1);
}

CS16_PACK_EXPORT int HELIX_HOST_CALL helix_game_client(const hxHostFacade* host) {
    return cs16_setup(host, 0);
}
