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

    /* Read header and compute seq 0 frame 0 transformed vertices */
    /* Let's see the bone transforms and vertex coordinates */
    extern int cs16_mdl_to_obj(const unsigned char* mdl_data, size_t mdl_size, unsigned char** out_obj, unsigned int* out_obj_size);
    /* We'll edit cs16_mdl.c and check */
    return 0;
}
