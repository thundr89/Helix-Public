#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cs16_mdl.c"

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
    const cs16_studiobone_t* bones = (const cs16_studiobone_t*)(buf + hdr->boneindex);
    for (int i = 0; i < hdr->numbones; ++i) {
        printf("Bone %2d: '%-20s' parent=%2d\n", i, bones[i].name, bones[i].parent);
    }
    free(buf);
    return 0;
}
