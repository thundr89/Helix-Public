#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helix/fs/iasset_plugin.h"

hxAssetPlugin* helix_asset_plugin_create(void);

int main(void) {
    hxAssetPlugin* p = helix_asset_plugin_create();
    if (!p) {
        printf("Failed to create plugin\n");
        return 1;
    }
    if (!p->open_dir(p, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Call of Duty\\Main")) {
        printf("Failed to open Main dir\n");
        p->destroy(p);
        return 1;
    }
    printf("Dir opened.\n");

    const char* models[] = {
        "xmodel/static_vehicle_german_truck",
        "xmodel/vehicle_german_truck",
        "xmodel/vehicle_truck_soviet"
    };

    for (int i = 0; i < 3; i++) {
        unsigned char* out = NULL;
        unsigned int sz = 0;
        printf("Reading %s...\n", models[i]);
        int ok = p->read(p, models[i], &out, &sz);
        printf("Result %s: ok=%d, sz=%u\n", models[i], ok, sz);
        if (ok && out) {
            int usemtl = 0, verts = 0, faces = 0;
            char* text = (char*)malloc(sz + 1);
            memcpy(text, out, sz);
            text[sz] = '\0';
            char* line = strtok(text, "\r\n");
            while (line) {
                if (strncmp(line, "usemtl ", 7) == 0) {
                    usemtl++;
                    printf("  %s\n", line);
                } else if (line[0] == 'v' && line[1] == ' ') verts++;
                else if (line[0] == 'f' && line[1] == ' ') faces++;
                line = strtok(NULL, "\r\n");
            }
            free(text);
            printf("  Total: %d usemtl, %d verts, %d faces\n", usemtl, verts, faces);
            p->free_buf(p, out);
        }
    }

    p->destroy(p);
    return 0;
}
