#include "helix/game/iserver_game.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define DOOM_SG_EXPORT __declspec(dllexport)
#else
#define DOOM_SG_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    hxServerGame base;
    hxSGEngine* eng;
    hxSGImports* imp;
    char map_name[64];
    int health;
    int armor;
    int ammo;
    int kills;
    int god_mode;
    int keys; /* bit 0=blue, 1=yellow, 2=red */
} DoomSGame;

static int dm_strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void next_doom_map(const char* cur, char* out, size_t cap) {
    if ((cur[0] == 'e' || cur[0] == 'E') && isdigit((unsigned char)cur[1]) &&
        (cur[2] == 'm' || cur[2] == 'M') && isdigit((unsigned char)cur[3])) {
        int e = cur[1] - '0';
        int m = cur[3] - '0' + 1;
        if (m > 8) { m = 1; e++; }
        if (e > 4) e = 1;
        snprintf(out, cap, "e%dm%d", e, m);
        return;
    }
    if (strncasecmp(cur, "map", 3) == 0 && isdigit((unsigned char)cur[3])) {
        int m = atoi(cur + 3) + 1;
        if (m > 32) m = 1;
        snprintf(out, cap, "map%02d", m);
        return;
    }
    snprintf(out, cap, "e1m1");
}

static void doom_init(hxServerGame* self, hxSGEngine* eng, hxSGImports* imp,
                      const char* map_name, int restart) {
    (void)restart;
    DoomSGame* g = (DoomSGame*)self;
    g->eng = eng;
    g->imp = imp;
    snprintf(g->map_name, sizeof(g->map_name), "%s", map_name ? map_name : "e1m1");
    g->health = 100;
    g->armor = 0;
    g->ammo = 50;
    g->kills = 0;
    g->god_mode = 0;
    g->keys = 0;

    if (imp && imp->print) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[DOOM] Singleplayer init: map=%s\n", g->map_name);
        imp->print(eng, msg);
    }
}

static void doom_shutdown(hxServerGame* self, int restart) {
    (void)self; (void)restart;
}

static void doom_run_frame(hxServerGame* self, int server_time_ms) {
    (void)server_time_ms;
    DoomSGame* g = (DoomSGame*)self;
    if (g->imp && g->imp->player_ui) {
        char buf[128];
        snprintf(buf, sizeof(buf), "DOOM [%s]  HP:%d%%  ARM:%d%%  AMMO:%d  KILLS:%d%s",
                 g->map_name, g->health, g->armor, g->ammo, g->kills,
                 g->god_mode ? "  [GOD]" : "");
        g->imp->player_ui(g->eng, 0, 4, "", buf);
    }
}

static int doom_client_connect(hxServerGame* self, int client, const char* name) {
    (void)self; (void)client; (void)name;
    return 1;
}

static void doom_client_disconnect(hxServerGame* self, int client) {
    (void)self; (void)client;
}

static void doom_client_begin(hxServerGame* self, int client) {
    (void)self; (void)client;
}

static int doom_console_command(hxServerGame* self, const char* cmd) {
    DoomSGame* g = (DoomSGame*)self;
    if (!cmd) return 0;

    if (dm_strcasecmp(cmd, "iddqd") == 0) {
        g->god_mode = !g->god_mode;
        g->health = 100;
        if (g->imp && g->imp->print) g->imp->print(g->eng, "[DOOM] Degreelessness Mode\n");
        return 1;
    }
    if (dm_strcasecmp(cmd, "idkfa") == 0) {
        g->armor = 200;
        g->ammo = 200;
        g->keys = 7;
        if (g->imp && g->imp->print) g->imp->print(g->eng, "[DOOM] Very Happy Ammo Added\n");
        return 1;
    }
    if (dm_strcasecmp(cmd, "nextmap") == 0) {
        char next[64];
        next_doom_map(g->map_name, next, sizeof(next));
        if (g->imp && g->imp->request_map) {
            g->imp->request_map(g->eng, next);
        }
        return 1;
    }
    return 0;
}

static void doom_destroy(hxServerGame* self) {
    free(self);
}

DOOM_SG_EXPORT hxServerGame* helix_sgame_create(void) {
    DoomSGame* g = (DoomSGame*)calloc(1, sizeof(DoomSGame));
    if (!g) return NULL;
    g->base.api_version = HELIX_SGAME_API_VERSION;
    g->base.init = doom_init;
    g->base.shutdown = doom_shutdown;
    g->base.run_frame = doom_run_frame;
    g->base.client_connect = doom_client_connect;
    g->base.client_disconnect = doom_client_disconnect;
    g->base.client_begin = doom_client_begin;
    g->base.console_command = doom_console_command;
    g->base.destroy = doom_destroy;
    return &g->base;
}
