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
    float bonetransforms[CS16_MAX_BONES][3][4];
    int numbones = hdr->numbones;
    const cs16_mstudioseqdesc_t* seq = (const cs16_mstudioseqdesc_t*)(buf + hdr->seqindex);
    const cs16_mstudioanim_t* anims = (const cs16_mstudioanim_t*)(buf + seq->animindex);
    const cs16_studiobone_t* bones = (const cs16_studiobone_t*)(buf + hdr->boneindex);

    for (int i = 0; i < numbones; ++i) {
        float pos[3], rot[3];
        for (int j = 0; j < 3; ++j) {
            pos[j] = bones[i].value[j];
            if (anims && anims[i].offset[j] != 0) {
                const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)((const unsigned char*)anims + anims[i].offset[j]);
                float val = 0.f;
                cs16_extract_anim_value(0, av, bones[i].scale[j], &val);
                pos[j] += val;
            }
        }
        for (int j = 0; j < 3; ++j) {
            rot[j] = bones[i].value[j + 3];
            if (anims && anims[i].offset[j + 3] != 0) {
                const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)((const unsigned char*)anims + anims[i].offset[j + 3]);
                float val = 0.f;
                cs16_extract_anim_value(0, av, bones[i].scale[j + 3], &val);
                rot[j] += val;
            }
        }
        float q[4], bm[3][4];
        cs16_angle_quaternion(rot, q);
        cs16_quat_matrix(q, pos, bm);
        if (bones[i].parent == -1 || bones[i].parent >= i) {
            memcpy(bonetransforms[i], bm, sizeof(bm));
        } else {
            cs16_concat_transforms(bonetransforms[bones[i].parent], bm, bonetransforms[i]);
        }
    }

    for (int bp_idx = 0; bp_idx < hdr->numbodyparts; ++bp_idx) {
        const cs16_studiobodypart_t* bp = (const cs16_studiobodypart_t*)(buf + hdr->bodypartindex + bp_idx * sizeof(cs16_studiobodypart_t));
        for (int m_idx = 0; m_idx < bp->nummodels; ++m_idx) {
            const cs16_studiomodel_t* mod = (const cs16_studiomodel_t*)(buf + bp->modelindex + m_idx * sizeof(cs16_studiomodel_t));
            const float* raw_verts = (const float*)(buf + mod->vertindex);
            const unsigned char* vert_bones = (const unsigned char*)(buf + mod->vertinfoindex);
            float min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9, min_z = 1e9, max_z = -1e9;
            for (int v = 0; v < mod->numverts; ++v) {
                float in_v[3] = { raw_verts[v * 3 + 0], raw_verts[v * 3 + 1], raw_verts[v * 3 + 2] };
                float out_v[3];
                int b = vert_bones[v];
                cs16_vector_transform(in_v, bonetransforms[b], out_v);
                if (out_v[0] < min_x) min_x = out_v[0]; if (out_v[0] > max_x) max_x = out_v[0];
                if (out_v[1] < min_y) min_y = out_v[1]; if (out_v[1] > max_y) max_y = out_v[1];
                if (out_v[2] < min_z) min_z = out_v[2]; if (out_v[2] > max_z) max_z = out_v[2];
            }
            printf("BP %d '%s' out_v: [0]=%.1f..%.1f, [1]=%.1f..%.1f, [2]=%.1f..%.1f\n",
                   bp_idx, mod->name, min_x, max_x, min_y, max_y, min_z, max_z);
        }
    }

    free(buf);
    return 0;
}
