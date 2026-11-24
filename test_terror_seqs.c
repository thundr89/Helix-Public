#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/cs16/cs16_mdl.c"

int main() {
    FILE* f = fopen("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/player/terror/terror.mdl", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(sz); fread(buf, 1, sz, f); fclose(f);
    const cs16_studiohdr_t* hdr = (const cs16_studiohdr_t*)buf;
    printf("terror.mdl numseq=%d\n", hdr->numseq);
    const cs16_mstudioseqdesc_t* seqs = (const cs16_mstudioseqdesc_t*)(buf + hdr->seqindex);
    for (int i = 0; i < hdr->numseq; ++i) {
        printf("Seq %2d: '%s' (numframes=%d, fps=%.1f)\n", i, seqs[i].label, seqs[i].numframes, seqs[i].fps);
    }
    return 0;
}
