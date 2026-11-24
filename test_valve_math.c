#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "src/cs16/cs16_mdl.c"

static void AngleMatrix(const float* angles, float matrix[3][4]) {
    float angle;
    float sr, sp, sy, cr, cp, cy;

    angle = angles[2];
    sy = sinf(angle);
    cy = cosf(angle);
    angle = angles[1];
    sp = sinf(angle);
    cp = cosf(angle);
    angle = angles[0];
    sr = sinf(angle);
    cr = cosf(angle);

    matrix[0][0] = cp * cy;
    matrix[1][0] = cp * sy;
    matrix[2][0] = -sp;
    matrix[0][1] = sr * sp * cy + cr * -sy;
    matrix[1][1] = sr * sp * sy + cr * cy;
    matrix[2][1] = sr * cp;
    matrix[0][2] = (cr * sp * cy + -sr * -sy);
    matrix[1][2] = (cr * sp * sy + -sr * cy);
    matrix[2][2] = cr * cp;
    matrix[0][3] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[2][3] = 0.0f;
}

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
    const cs16_studiobone_t* bones = (const cs16_studiobone_t*)(buf + hdr->boneindex);
    const cs16_mstudioseqdesc_t* seq = (const cs16_mstudioseqdesc_t*)(buf + hdr->seqindex);
    const cs16_mstudioanim_t* anims = (const cs16_mstudioanim_t*)(buf + seq->animindex);

    float bonetransforms[64][3][4];
    for (int i = 0; i < hdr->numbones; ++i) {
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
        float bm[3][4];
        AngleMatrix(rot, bm);
        bm[0][3] = pos[0];
        bm[1][3] = pos[1];
        bm[2][3] = pos[2];

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
                float vx = -out_v[1];
                float vy = out_v[2];
                float vz = out_v[0];
                if (vx < min_x) min_x = vx; if (vx > max_x) max_x = vx;
                if (vy < min_y) min_y = vy; if (vy > max_y) max_y = vy;
                if (vz < min_z) min_z = vz; if (vz > max_z) max_z = vz;
            }
            printf("Valve AngleMatrix -> BP %d Model %d '%s': X(R)=%.1f..%.1f Y(U)=%.1f..%.1f Z(F)=%.1f..%.1f\n",
                   bp_idx, m_idx, mod->name, min_x, max_x, min_y, max_y, min_z, max_z);
        }
    }

    free(buf);
    return 0;
}
