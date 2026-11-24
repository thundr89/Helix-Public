#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helix/fs/iasset_plugin.h"

int main() {
    extern hxAssetPlugin* helix_asset_plugin_create(void);
    hxAssetPlugin* p = helix_asset_plugin_create();
    if (!p) { printf("Failed to create cs16 asset plugin\n"); return 1; }

    int opened = p->open_dir(p, "E:/SteamLibrary/steamapps/common/Half-Life/cstrike");
    printf("open_dir result: %d\n", opened);

    const char* test_textures[] = {
        "textures/models/view_glove.tga",
        "textures/models/view_skin.tga",
        "textures/models/wood.tga",
        "textures/models/barrel.tga",
        "textures/models/magazine.tga",
        "textures/models/player_t1.tga",
        "models/view_mg.obj",
        "textures/models/player.tga"
    };

    for (size_t i = 0; i < sizeof(test_textures)/sizeof(test_textures[0]); ++i) {
        int contains = p->contains(p, test_textures[i]);
        unsigned char* buf = NULL;
        unsigned int sz = 0;
        int read_ok = 0;
        if (contains) {
            read_ok = p->read(p, test_textures[i], &buf, &sz);
        }
        printf("%-35s contains=%d read=%d size=%u\n", test_textures[i], contains, read_ok, sz);
        if (read_ok && buf) {
            p->free_buf(p, buf);
        }
    }

    p->destroy(p);
    return 0;
}
