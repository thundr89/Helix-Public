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

    /* Let's read bones and see what out_v gives */
    /* Print what the current cs16_mdl_to_obj outputs */
    extern int cs16_mdl_to_obj(const unsigned char* mdl_data, size_t mdl_size, unsigned char** out_obj, unsigned int* out_obj_size);
    unsigned char* obj = NULL;
    unsigned int obj_sz = 0;
    cs16_mdl_to_obj(buf, sz, &obj, &obj_sz);

    /* Print all usemtl in the obj */
    char* line = (char*)obj;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next = '\0';
        if (strncmp(line, "usemtl", 6) == 0) {
            printf("%s\n", line);
        }
        line = next ? next + 1 : NULL;
    }
    free(obj);
    free(buf);
    return 0;
}
