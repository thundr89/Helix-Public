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

    /* Let cs16_mdl_to_obj parse it */
    extern int cs16_mdl_to_obj(const unsigned char* mdl_data, size_t mdl_size, unsigned char** out_obj, unsigned int* out_obj_size);
    unsigned char* obj = NULL;
    unsigned int obj_sz = 0;
    cs16_mdl_to_obj(buf, sz, &obj, &obj_sz);

    /* Compute bounding box from generated OBJ vertices */
    float min_x = 1e9, max_x = -1e9;
    float min_y = 1e9, max_y = -1e9;
    float min_z = 1e9, max_z = -1e9;

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
    printf("Transformed OBJ bounds: X(Right)=%.2f..%.2f, Y(Up)=%.2f..%.2f, Z(Forward)=%.2f..%.2f\n",
           min_x, max_x, min_y, max_y, min_z, max_z);
    free(obj);
    free(buf);
    return 0;
}
