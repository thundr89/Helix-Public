#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/cs16/cs16_mdl.c"

void check_model(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    unsigned char* obj = NULL;
    unsigned int obj_sz = 0;
    cs16_mdl_to_obj(buf, sz, &obj, &obj_sz);

    float min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9, min_z = 1e9, max_z = -1e9;
    char* line = (char*)obj;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next = '\0';
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                if (x < min_x) min_x = x; if (x > max_x) max_x = x;
                if (y < min_y) min_y = y; if (y > max_y) max_y = y;
                if (z < min_z) min_z = z; if (z > max_z) max_z = z;
            }
        }
        line = next ? next + 1 : NULL;
    }
    printf("%-20s -> X(R)=%5.1f..%5.1f, Y(U)=%5.1f..%5.1f, Z(F)=%5.1f..%5.1f\n",
           path + strlen(path) - 10, min_x, max_x, min_y, max_y, min_z, max_z);
    free(obj);
    free(buf);
}

int main() {
    check_model("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_ak47.mdl");
    check_model("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_m4a1.mdl");
    check_model("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_usp.mdl");
    check_model("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_knife.mdl");
    return 0;
}
