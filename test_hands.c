#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/cs16/cs16_mdl.c"

int main() {
    FILE* f = fopen("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_ak47.mdl", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(sz); fread(buf, 1, sz, f); fclose(f);
    const cs16_studiohdr_t* hdr = (const cs16_studiohdr_t*)buf;
    const cs16_studiobone_t* bones = (const cs16_studiobone_t*)(buf + hdr->boneindex);
    for (int i = 0; i < hdr->numbones; ++i) {
        if (strstr(bones[i].name, "hand") || strstr(bones[i].name, "Hand") || strstr(bones[i].name, "AK47")) {
            printf("Bone %2d: '%-15s' parent=%2d pos=[%6.2f, %6.2f, %6.2f]\n",
                   i, bones[i].name, bones[i].parent, bones[i].value[0], bones[i].value[1], bones[i].value[2]);
        }
    }
    return 0;
}
