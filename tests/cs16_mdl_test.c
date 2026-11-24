#include "../src/cs16/cs16_mdl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails = 0;

#define CHECK(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fails++; \
        } \
    } while (0)

#define TERROR_MDL_PATH "E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/player/terror/terror.mdl"

static unsigned char* load_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

#pragma pack(push, 1)

typedef struct {
    int id;
    int version;
    char name[64];
    int length;
    float eyeposition[3];
    float min[3];
    float max[3];
    float bbmin[3];
    float bbmax[3];
    int flags;
    int numbones, boneindex;
    int numbonecontrollers, bonecontrollerindex;
    int numhitboxes, hitboxindex;
    int numseq, seqindex;
    int numseqgroups, seqgroupindex;
    int numtextures, textureindex, texturedataindex;
    int numskinref, numskinfamilies, skinindex;
    int numbodyparts, bodypartindex;
    int numattachments, attachmentindex;
    int soundtable, soundindex;
    int soundgroups, soundgroupindex;
    int numtransitions, transitionindex;
} SynthHdr;

typedef struct {
    char name[32];
    int parent;
    int flags;
    int bonecontroller[6];
    float value[6];
    float scale[6];
} SynthBone;

typedef struct {
    char name[64];
    int nummodels;
    int base;
    int modelindex;
} SynthBodypart;

typedef struct {
    char name[64];
    int type;
    float boundingradius;
    int nummesh;
    int meshindex;
    int numverts;
    int vertinfoindex;
    int vertindex;
    int numnorms;
    int norminfoindex;
    int normindex;
    int numgroups;
    int groupindex;
} SynthModel;

typedef struct {
    int numtriangles;
    int triangleindex;
    int skinref;
    int numnorms;
    int normindex;
} SynthMesh;

typedef struct {
    char name[64];
    int flags;
    int width;
    int height;
    int index;
} SynthTex;

#pragma pack(pop)

static unsigned char* build_synthetic_mdl(size_t* out_size) {
    size_t total = 4096;
    unsigned char* buf = (unsigned char*)calloc(1, total);
    if (!buf) return NULL;

    SynthHdr* hdr = (SynthHdr*)buf;
    hdr->id = 0x54534449; /* "IDST" */
    hdr->version = 10;
    snprintf(hdr->name, sizeof(hdr->name), "synth.mdl");

    size_t cur = sizeof(SynthHdr);

    /* 1 Bone */
    hdr->numbones = 1;
    hdr->boneindex = (int)cur;
    SynthBone* bone = (SynthBone*)(buf + cur);
    snprintf(bone->name, sizeof(bone->name), "root");
    bone->parent = -1;
    cur += sizeof(SynthBone);

    /* 1 Texture: 4x4 */
    hdr->numtextures = 1;
    hdr->textureindex = (int)cur;
    SynthTex* tex = (SynthTex*)(buf + cur);
    snprintf(tex->name, sizeof(tex->name), "synth_tex.bmp");
    tex->width = 4;
    tex->height = 4;
    cur += sizeof(SynthTex);

    tex->index = (int)cur;
    /* 16 pixels + 768 palette */
    memset(buf + cur, 1, 16);
    cur += 16;
    /* Palette: color 1 = RGB(255, 128, 0) */
    buf[cur + 3] = 255;
    buf[cur + 4] = 128;
    buf[cur + 5] = 0;
    cur += 768;

    /* 1 Bodypart */
    hdr->numbodyparts = 1;
    hdr->bodypartindex = (int)cur;
    SynthBodypart* bp = (SynthBodypart*)(buf + cur);
    snprintf(bp->name, sizeof(bp->name), "body");
    bp->nummodels = 1;
    cur += sizeof(SynthBodypart);

    /* 1 Model */
    bp->modelindex = (int)cur;
    SynthModel* mdl = (SynthModel*)(buf + cur);
    snprintf(mdl->name, sizeof(mdl->name), "submodel");
    mdl->nummesh = 1;
    mdl->numverts = 3;
    mdl->numnorms = 3;
    cur += sizeof(SynthModel);

    /* Vertices */
    mdl->vertindex = (int)cur;
    float* v = (float*)(buf + cur);
    v[0] = 0.f; v[1] = 0.f; v[2] = 0.f;
    v[3] = 1.f; v[4] = 0.f; v[5] = 0.f;
    v[6] = 0.f; v[7] = 1.f; v[8] = 0.f;
    cur += 3 * 3 * sizeof(float);

    /* Normals */
    mdl->normindex = (int)cur;
    float* n = (float*)(buf + cur);
    n[0] = 0.f; n[1] = 0.f; n[2] = 1.f;
    n[3] = 0.f; n[4] = 0.f; n[5] = 1.f;
    n[6] = 0.f; n[7] = 0.f; n[8] = 1.f;
    cur += 3 * 3 * sizeof(float);

    /* Bone mappings */
    mdl->vertinfoindex = (int)cur;
    buf[cur + 0] = 0; buf[cur + 1] = 0; buf[cur + 2] = 0;
    cur += 3;

    mdl->norminfoindex = (int)cur;
    buf[cur + 0] = 0; buf[cur + 1] = 0; buf[cur + 2] = 0;
    cur += 3;

    /* 1 Mesh */
    mdl->meshindex = (int)cur;
    SynthMesh* mesh = (SynthMesh*)(buf + cur);
    mesh->numtriangles = 1;
    mesh->skinref = 0;
    cur += sizeof(SynthMesh);

    /* Triangle commands: strip of 3 points (count = 3) */
    mesh->triangleindex = (int)cur;
    short* cmd = (short*)(buf + cur);
    cmd[0] = 3; /* 3 pts strip */
    /* pt 0: v=0, n=0, s=0, t=0 */
    cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0;
    /* pt 1: v=1, n=1, s=4, t=0 */
    cmd[5] = 1; cmd[6] = 1; cmd[7] = 4; cmd[8] = 0;
    /* pt 2: v=2, n=2, s=0, t=4 */
    cmd[9] = 2; cmd[10] = 2; cmd[11] = 0; cmd[12] = 4;
    /* terminator */
    cmd[13] = 0;
    cur += 14 * sizeof(short);

    hdr->length = (int)cur;
    *out_size = cur;
    return buf;
}

int main(void) {
    printf("=== CS 1.6 StudioMDL v10 Parser Tests ===\n");

    /* 1. Test error handling */
    {
        unsigned char* obj = NULL;
        unsigned int obj_size = 0;
        CHECK(cs16_mdl_to_obj(NULL, 0, &obj, &obj_size) == 0);
        unsigned char dummy[64] = {0};
        CHECK(cs16_mdl_to_obj(dummy, sizeof(dummy), &obj, &obj_size) == 0);

        unsigned char* tga = NULL;
        unsigned int tga_size = 0;
        CHECK(cs16_mdl_extract_texture(NULL, 0, 0, &tga, &tga_size, NULL, 0) == 0);
        CHECK(cs16_mdl_extract_texture(dummy, sizeof(dummy), 0, &tga, &tga_size, NULL, 0) == 0);
    }

    /* 2. Always test synthetic model */
    {
        printf("--- Testing Synthetic Model ---\n");
        size_t syn_size = 0;
        unsigned char* syn_data = build_synthetic_mdl(&syn_size);
        CHECK(syn_data != NULL);

        unsigned char* syn_obj = NULL;
        unsigned int syn_obj_size = 0;
        int ok = cs16_mdl_to_obj(syn_data, syn_size, &syn_obj, &syn_obj_size);
        CHECK(ok == 1);
        CHECK(syn_obj != NULL);
        CHECK(syn_obj_size > 0);
        if (syn_obj) {
            const char* str = (const char*)syn_obj;
            CHECK(strstr(str, "v ") != NULL);
            CHECK(strstr(str, "vn ") != NULL);
            CHECK(strstr(str, "vt ") != NULL);
            CHECK(strstr(str, "f ") != NULL);
            free(syn_obj);
        }

        unsigned char* syn_tga = NULL;
        unsigned int syn_tga_size = 0;
        char syn_tex_name[64] = {0};
        int tex_ok = cs16_mdl_extract_texture(syn_data, syn_size, 0, &syn_tga, &syn_tga_size, syn_tex_name, sizeof(syn_tex_name));
        CHECK(tex_ok == 1);
        CHECK(syn_tga != NULL);
        CHECK(syn_tga_size == 18 + 4 * 4 * 4);
        if (syn_tga) {
            CHECK(syn_tga[2] == 2);
            unsigned short w = (unsigned short)(syn_tga[12] | (syn_tga[13] << 8));
            unsigned short h = (unsigned short)(syn_tga[14] | (syn_tga[15] << 8));
            CHECK(w == 4);
            CHECK(h == 4);
            CHECK(syn_tga[16] == 32);
            free(syn_tga);
        }

        free(syn_data);
        printf("Synthetic model tests PASSED\n");
    }

    /* 3. Test real model if present */
    {
        printf("--- Testing Real Model ---\n");
        size_t mdl_size = 0;
        unsigned char* mdl_data = load_file(TERROR_MDL_PATH, &mdl_size);
        if (mdl_data) {
            printf("Found real model: %s (size: %zu bytes)\n", TERROR_MDL_PATH, mdl_size);
            unsigned char* out_obj = NULL;
            unsigned int out_obj_size = 0;
            int ok = cs16_mdl_to_obj(mdl_data, mdl_size, &out_obj, &out_obj_size);
            CHECK(ok == 1);
            CHECK(out_obj != NULL);
            CHECK(out_obj_size > 0);

            if (out_obj) {
                const char* str = (const char*)out_obj;
                CHECK(strstr(str, "v ") != NULL);
                CHECK(strstr(str, "vn ") != NULL);
                CHECK(strstr(str, "vt ") != NULL);
                CHECK(strstr(str, "f ") != NULL);
                CHECK(strstr(str, "usemtl ") != NULL);
                printf("Generated OBJ size: %u bytes\n", out_obj_size);
                float min_x = 1e9f, max_x = -1e9f;
                float min_y = 1e9f, max_y = -1e9f;
                float min_z = 1e9f, max_z = -1e9f;
                const char* p = (const char*)out_obj;
                while ((p = strstr(p, "v ")) != NULL) {
                    float x, y, z;
                    if (sscanf(p, "v %f %f %f", &x, &y, &z) == 3) {
                        if (x < min_x) min_x = x; if (x > max_x) max_x = x;
                        if (y < min_y) min_y = y; if (y > max_y) max_y = y;
                        if (z < min_z) min_z = z; if (z > max_z) max_z = z;
                    }
                    p += 2;
                }
                FILE* fobj = fopen("terror_test.obj", "wb");
                if (fobj) {
                    fwrite(out_obj, 1, out_obj_size, fobj);
                    fclose(fobj);
                }
                printf("terror OBJ: X: [%.2f, %.2f], Y: [%.2f, %.2f], Z: [%.2f, %.2f]\n",
                       min_x, max_x, min_y, max_y, min_z, max_z);
                free(out_obj);
            }

            unsigned char* out_tga = NULL;
            unsigned int out_tga_size = 0;
            char tex_name[64] = {0};
            int tex_ok = cs16_mdl_extract_texture(mdl_data, mdl_size, 0, &out_tga, &out_tga_size, tex_name, sizeof(tex_name));
            CHECK(tex_ok == 1);
            CHECK(out_tga != NULL);
            CHECK(out_tga_size == 18 + 512 * 512 * 4);

            if (out_tga) {
                CHECK(out_tga[2] == 2);
                unsigned short w = (unsigned short)(out_tga[12] | (out_tga[13] << 8));
                unsigned short h = (unsigned short)(out_tga[14] | (out_tga[15] << 8));
                CHECK(w == 512);
                CHECK(h == 512);
                CHECK(out_tga[16] == 32);
                CHECK(strstr(tex_name, "Terrorist") != NULL || strstr(tex_name, ".BMP") != NULL || strstr(tex_name, ".bmp") != NULL);
                printf("Extracted real texture: \"%s\", %ux%u, %u bytes TGA\n", tex_name, w, h, out_tga_size);
                free(out_tga);
            }

            /* Test out of range texture index */
            unsigned char* bad_tga = NULL;
            unsigned int bad_tga_size = 0;
            CHECK(cs16_mdl_extract_texture(mdl_data, mdl_size, 9999, &bad_tga, &bad_tga_size, NULL, 0) == 0);

            free(mdl_data);
            printf("Real model tests PASSED\n");
        } else {
            printf("Real model %s not found on this system, skipped real model test.\n", TERROR_MDL_PATH);
        }

        size_t vak_size = 0;
        unsigned char* vak_data = load_file("E:/SteamLibrary/steamapps/common/Half-Life/cstrike/models/v_ak47.mdl", &vak_size);
        if (vak_data) {
            unsigned char* out_obj = NULL;
            unsigned int out_obj_size = 0;
            int ok = cs16_mdl_to_obj(vak_data, vak_size, &out_obj, &out_obj_size);
            CHECK(ok == 1);
            CHECK(out_obj != NULL);
            if (out_obj) {
                /* Scan materials and vertices */
                const char* mp = (const char*)out_obj;
                while ((mp = strstr(mp, "usemtl ")) != NULL) {
                    char mname[64] = {0};
                    sscanf(mp, "usemtl %63s", mname);
                    printf("v_ak47 material: %s\n", mname);
                    mp += 7;
                }
                float min_x = 1e9f, max_x = -1e9f;
                float min_y = 1e9f, max_y = -1e9f;
                float min_z = 1e9f, max_z = -1e9f;
                const char* p = (const char*)out_obj;
                while ((p = strstr(p, "v ")) != NULL) {
                    float x, y, z;
                    if (sscanf(p, "v %f %f %f", &x, &y, &z) == 3) {
                        if (x < min_x) min_x = x; if (x > max_x) max_x = x;
                        if (y < min_y) min_y = y; if (y > max_y) max_y = y;
                        if (z < min_z) min_z = z; if (z > max_z) max_z = z;
                    }
                    p += 2;
                }
                printf("v_ak47 OBJ: sz=%u bytes, X: [%.2f, %.2f], Y: [%.2f, %.2f], Z: [%.2f, %.2f]\n",
                       out_obj_size, min_x, max_x, min_y, max_y, min_z, max_z);
                FILE* fv = fopen("v_ak47_test.obj", "wb");
                if (fv) { fwrite(out_obj, 1, out_obj_size, fv); fclose(fv); }
                free(out_obj);
            }
            free(vak_data);
        }
    }

    if (g_fails == 0) {
        printf("ALL CS16 MDL TESTS PASSED!\n");
        return 0;
    } else {
        printf("CS16 MDL TESTS FAILED with %d errors!\n", g_fails);
        return 1;
    }
}
