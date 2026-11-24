#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/cs16/cs16_mdl.c"

int main() {
    FILE* f = fopen("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_ak47.mdl", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    const cs16_studiohdr_t* hdr = (const cs16_studiohdr_t*)buf;
    const cs16_studiobodypart_t* bp = (const cs16_studiobodypart_t*)(buf + hdr->bodypartindex + 2 * sizeof(cs16_studiobodypart_t));
    const cs16_studiomodel_t* mod = (const cs16_studiomodel_t*)(buf + bp->modelindex);
    const float* v = (const float*)(buf + mod->vertindex);

    float min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9, min_z = 1e9, max_z = -1e9;
    for (int i = 0; i < mod->numverts; ++i) {
        if (v[i*3+0] < min_x) min_x = v[i*3+0]; if (v[i*3+0] > max_x) max_x = v[i*3+0];
        if (v[i*3+1] < min_y) min_y = v[i*3+1]; if (v[i*3+1] > max_y) max_y = v[i*3+1];
        if (v[i*3+2] < min_z) min_z = v[i*3+2]; if (v[i*3+2] > max_z) max_z = v[i*3+2];
    }
    printf("Raw AK47 mesh coords: X=%.2f..%.2f, Y=%.2f..%.2f, Z=%.2f..%.2f\n", min_x, max_x, min_y, max_y, min_z, max_z);
    free(buf);
    return 0;
}
