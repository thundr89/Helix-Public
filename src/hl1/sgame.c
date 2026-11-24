#include "helix/game/iserver_game.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define HL1_SG_EXPORT __declspec(dllexport)
#else
#define HL1_SG_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    hxServerGame base;
    hxSGEngine* eng;
    hxSGImports* imp;
    char map_name[64];
    int health;
    int suit;
    int ammo;
    int god_mode;
} Hl1SGame;

static int hl_strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void hl1_init(hxServerGame* self, hxSGEngine* eng, hxSGImports* imp,
                     const char* map_name, int restart) {
    (void)restart;
    Hl1SGame* g = (Hl1SGame*)self;
    g->eng = eng;
    g->imp = imp;
    snprintf(g->map_name, sizeof(g->map_name), "%s", map_name ? map_name : "c1a0");
    g->health = 100;
    g->suit = 0;
    g->ammo = 17;
    g->god_mode = 0;

    if (imp && imp->print) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[HL1] Singleplayer init: map=%s\n", g->map_name);
        imp->print(eng, msg);
    }
}

static void hl1_shutdown(hxServerGame* self, int restart) {
    (void)self; (void)restart;
}

static void hl1_run_frame(hxServerGame* self, int server_time_ms) {
    (void)server_time_ms;
    Hl1SGame* g = (Hl1SGame*)self;
    if (g->imp && g->imp->player_ui) {
        char buf[128];
        snprintf(buf, sizeof(buf), "HALF-LIFE [%s]  HP:%d%%  HEV:%d%%  AMMO:%d%s",
                 g->map_name, g->health, g->suit, g->ammo,
                 g->god_mode ? "  [GOD]" : "");
        g->imp->player_ui(g->eng, 0, 4, "", buf);
    }
}

static int hl1_client_connect(hxServerGame* self, int client, const char* name) {
    (void)self; (void)client; (void)name;
    return 1;
}

static void hl1_client_disconnect(hxServerGame* self, int client) {
    (void)self; (void)client;
}

static void hl1_client_begin(hxServerGame* self, int client) {
    (void)self; (void)client;
}

static int hl1_console_command(hxServerGame* self, const char* cmd) {
    Hl1SGame* g = (Hl1SGame*)self;
    if (!cmd) return 0;

    if (hl_strcasecmp(cmd, "god") == 0) {
        g->god_mode = !g->god_mode;
        g->health = 100;
        return 1;
    }
    if (hl_strcasecmp(cmd, "impulse 101") == 0) {
        g->suit = 100;
        g->ammo = 250;
        return 1;
    }
    return 0;
}

static void hl1_destroy(hxServerGame* self) {
    free(self);
}

HL1_SG_EXPORT hxServerGame* helix_sgame_create(void) {
    Hl1SGame* g = (Hl1SGame*)calloc(1, sizeof(Hl1SGame));
    if (!g) return NULL;
    g->base.api_version = HELIX_SGAME_API_VERSION;
    g->base.init = hl1_init;
    g->base.shutdown = hl1_shutdown;
    g->base.run_frame = hl1_run_frame;
    g->base.client_connect = hl1_client_connect;
    g->base.client_disconnect = hl1_client_disconnect;
    g->base.client_begin = hl1_client_begin;
    g->base.console_command = hl1_console_command;
    g->base.destroy = hl1_destroy;
    return &g->base;
}
