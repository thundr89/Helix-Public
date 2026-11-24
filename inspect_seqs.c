#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    FILE* f = fopen("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_ak47.mdl", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    int numseq = *(int*)(buf + 168);
    int seqindex = *(int*)(buf + 172);
    printf("numseq: %d\n", numseq);

    for (int i = 0; i < numseq; ++i) {
        /* seqdesc is 176 bytes */
        const unsigned char* sq = buf + seqindex + i * 176;
        char label[32];
        memcpy(label, sq, 32);
        label[31] = '\0';
        int numframes = *(int*)(sq + 68);
        printf("  seq %d: '%s' numframes=%d\n", i, label, numframes);
    }

    free(buf);
    return 0;
}
