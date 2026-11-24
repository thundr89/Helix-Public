#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cs16_mdl.h"

int main() {
    FILE* f = fopen("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_ak47.mdl", "rb");
    if (!f) { printf("Could not open v_ak47.mdl\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    unsigned char* obj = NULL;
    unsigned int obj_sz = 0;
    int ok = cs16_mdl_to_obj(buf, sz, &obj, &obj_sz);
    printf("cs16_mdl_to_obj result: %d, size: %u\n", ok, obj_sz);
    if (ok && obj) {
        char* line = (char*)obj;
        for (int i = 0; i < 60; ++i) {
            char* next = strchr(line, '\n');
            if (!next) break;
            *next = '\0';
            if (strncmp(line, "usemtl", 6) == 0 || strncmp(line, "v ", 2) == 0 || i < 10) {
                printf("%s\n", line);
            }
            line = next + 1;
        }
        free(obj);
    }
    free(buf);
    return 0;
}
