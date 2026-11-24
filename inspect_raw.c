#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    FILE* f = fopen("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_ak47.mdl", "rb");
    if (!f) { printf("Cannot open v_ak47.mdl\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    /* Read header */
    int numbodyparts = *(int*)(buf + 204);
    int bodypartindex = *(int*)(buf + 208);
    printf("bodyparts: %d\n", numbodyparts);

    float min_x = 1e9, max_x = -1e9;
    float min_y = 1e9, max_y = -1e9;
    float min_z = 1e9, max_z = -1e9;

    /* Check vertices in first bodypart, first model */
    int modelindex = *(int*)(buf + bodypartindex + 68);
    int numverts = *(int*)(buf + modelindex + 68);
    int vertindex = *(int*)(buf + modelindex + 76);
    printf("model 0 numverts: %d\n", numverts);

    const float* v = (const float*)(buf + vertindex);
    for (int i = 0; i < numverts; ++i) {
        float gx = v[i * 3 + 0];
        float gy = v[i * 3 + 1];
        float gz = v[i * 3 + 2];
        /* GoldSrc coords */
        if (gx < min_x) min_x = gx; if (gx > max_x) max_x = gx;
        if (gy < min_y) min_y = gy; if (gy > max_y) max_y = gy;
        if (gz < min_z) min_z = gz; if (gz > max_z) max_z = gz;
    }
    printf("Raw verts: X=%.2f..%.2f, Y=%.2f..%.2f, Z=%.2f..%.2f\n", min_x, max_x, min_y, max_y, min_z, max_z);
    free(buf);
    return 0;
}
