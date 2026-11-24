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

    int animindex = 5252;
    const unsigned char* a = buf + animindex;
    int offs[] = {304, 308, 312, 316, 504, 508, 512, 516};
    for (int i = 0; i < 8; ++i) {
        int off = offs[i];
        const unsigned char* p = a + off;
        printf("off %3d: valid=%d total=%d value=%d\n", off, p[0], p[1], *(short*)(p + 2));
    }
    free(buf);
    return 0;
}
