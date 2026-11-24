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
    const cs16_mstudioseqdesc_t* seq = (const cs16_mstudioseqdesc_t*)(buf + hdr->seqindex);
    const cs16_mstudioanim_t* anims = (const cs16_mstudioanim_t*)(buf + seq->animindex);

    for (int b = 0; b < hdr->numbones; ++b) {
        printf("Bone %2d offsets: %u %u %u %u %u %u\n", b,
               anims[b].offset[0], anims[b].offset[1], anims[b].offset[2],
               anims[b].offset[3], anims[b].offset[4], anims[b].offset[5]);
    }
    free(buf);
    return 0;
}
