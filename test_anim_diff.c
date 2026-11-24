#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
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
    const cs16_studiobone_t* bones = (const cs16_studiobone_t*)(buf + hdr->boneindex);
    const cs16_mstudioseqdesc_t* seq = (const cs16_mstudioseqdesc_t*)(buf + hdr->seqindex);
    const cs16_mstudioanim_t* anims = (const cs16_mstudioanim_t*)(buf + seq->animindex);

    for (int i : {0, 1, 18, 19, 36}) {
        float pos[3], rot[3];
        float apos[3] = {0}, arot[3] = {0};
        for (int j = 0; j < 3; ++j) {
            pos[j] = bones[i].value[j];
            if (anims && anims[i].offset[j] != 0) {
                const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)((const unsigned char*)anims + anims[i].offset[j]);
                cs16_extract_anim_value(0, av, bones[i].scale[j], &apos[j]);
            }
            rot[j] = bones[i].value[j + 3];
            if (anims && anims[i].offset[j + 3] != 0) {
                const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)((const unsigned char*)anims + anims[i].offset[j + 3]);
                cs16_extract_anim_value(0, av, bones[i].scale[j + 3], &arot[j]);
            }
        }
        printf("Bone %2d '%-16s' base_pos=(%.2f, %.2f, %.2f) anim_dpos=(%.2f, %.2f, %.2f)\n"
               "                     base_rot=(%.2f, %.2f, %.2f) anim_drot=(%.2f, %.2f, %.2f)\n",
               i, bones[i].name, pos[0], pos[1], pos[2], apos[0], apos[1], apos[2],
               rot[0], rot[1], rot[2], arot[0], arot[1], arot[2]);
    }
    free(buf);
    return 0;
}
