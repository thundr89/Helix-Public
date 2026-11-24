#include "cs16_mdl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#define CS16_MDL_ID 0x54534449 /* "IDST" */
#define CS16_MDL_VERSION 10
#define CS16_MAX_BONES 512

/* --- GoldSrc StudioModel Binary Layout Structures --- */

#pragma pack(push, 1)

typedef struct {
    int id;                     /* 0x54534449 ("IDST") */
    int version;                /* 10 */
    char name[64];
    int length;

    float eyeposition[3];
    float min[3];
    float max[3];
    float bbmin[3];
    float bbmax[3];

    int flags;

    int numbones;
    int boneindex;

    int numbonecontrollers;
    int bonecontrollerindex;

    int numhitboxes;
    int hitboxindex;

    int numseq;
    int seqindex;

    int numseqgroups;
    int seqgroupindex;

    int numtextures;
    int textureindex;
    int texturedataindex;

    int numskinref;
    int numskinfamilies;
    int skinindex;

    int numbodyparts;
    int bodypartindex;

    int numattachments;
    int attachmentindex;

    int soundtable;
    int soundindex;
    int soundgroups;
    int soundgroupindex;

    int numtransitions;
    int transitionindex;
} cs16_studiohdr_t;

typedef struct {
    char name[32];
    int parent;
    int flags;
    int bonecontroller[6];
    float value[6];
    float scale[6];
} cs16_studiobone_t;

typedef struct {
    char name[64];
    int nummodels;
    int base;
    int modelindex;
} cs16_studiobodypart_t;

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
} cs16_studiomodel_t;

typedef struct {
    int numtriangles;
    int triangleindex;
    int skinref;
    int numnorms;
    int normindex;
} cs16_studiomesh_t;

typedef struct {
    char name[64];
    int flags;
    int width;
    int height;
    int index;
} cs16_studiotexture_t;

typedef struct {
    char label[32];
    float fps;
    int flags, activity, actweight, numevents, eventindex, numframes;
    int numpivots, pivotindex, motiontype, motionbone;
    float linearmovement[3];
    int automoveposindex, automoveangleindex;
    float bbmin[3], bbmax[3];
    int numblends, animindex;
    int blendtype[2];
    float blendstart[2], blendend[2];
    int blendparent, seqgroup, entrynode, exitnode, nodeflags, nextseq;
} cs16_mstudioseqdesc_t;

typedef struct {
    unsigned short offset[6];
} cs16_mstudioanim_t;

typedef union {
    struct {
        unsigned char valid;
        unsigned char total;
    } num;
    short value;
} cs16_mstudioanimvalue_t;

#pragma pack(pop)

/* --- Studio Animation RLE Decoder --- */

static void cs16_extract_anim_value(int frame, const cs16_mstudioanimvalue_t* panimvalue, float scale, float* v) {
    if (!panimvalue || !v) return;
    if (panimvalue->num.total == 0) {
        *v = 0.f;
        return;
    }
    int k = frame;
    while (panimvalue->num.total <= k) {
        k -= panimvalue->num.total;
        panimvalue += panimvalue->num.valid + 1;
        if (panimvalue->num.total == 0) {
            *v = 0.f;
            return;
        }
    }
    if (panimvalue->num.valid > k) {
        *v = panimvalue[k + 1].value * scale;
    } else {
        *v = panimvalue[panimvalue->num.valid].value * scale;
    }
}

/* --- Math Helper Functions --- */

static void cs16_angle_quaternion(const float angles[3], float q[4]) {
    float angle = angles[2] * 0.5f;
    float sy = sinf(angle);
    float cy = cosf(angle);
    angle = angles[1] * 0.5f;
    float sp = sinf(angle);
    float cp = cosf(angle);
    angle = angles[0] * 0.5f;
    float sr = sinf(angle);
    float cr = cosf(angle);

    q[0] = sr * cp * cy - cr * sp * sy; /* x */
    q[1] = cr * sp * cy + sr * cp * sy; /* y */
    q[2] = cr * cp * sy - sr * sp * cy; /* z */
    q[3] = cr * cp * cy + sr * sp * sy; /* w */
}

static void cs16_quat_matrix(const float q[4], const float pos[3], float m[3][4]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];

    m[0][0] = 1.0f - 2.0f * (y * y + z * z);
    m[0][1] = 2.0f * (x * y - w * z);
    m[0][2] = 2.0f * (x * z + w * y);
    m[0][3] = pos[0];

    m[1][0] = 2.0f * (x * y + w * z);
    m[1][1] = 1.0f - 2.0f * (x * x + z * z);
    m[1][2] = 2.0f * (y * z - w * x);
    m[1][3] = pos[1];

    m[2][0] = 2.0f * (x * z - w * y);
    m[2][1] = 2.0f * (y * z + w * x);
    m[2][2] = 1.0f - 2.0f * (x * x + y * y);
    m[2][3] = pos[2];
}

static void cs16_concat_transforms(const float in1[3][4], const float in2[3][4], float out[3][4]) {
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] + in1[0][2] * in2[2][3] + in1[0][3];

    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] + in1[1][2] * in2[2][3] + in1[1][3];

    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] + in1[2][2] * in2[2][3] + in1[2][3];
}

static void cs16_vector_transform(const float in[3], const float m[3][4], float out[3]) {
    out[0] = in[0] * m[0][0] + in[1] * m[0][1] + in[2] * m[0][2] + m[0][3];
    out[1] = in[0] * m[1][0] + in[1] * m[1][1] + in[2] * m[1][2] + m[1][3];
    out[2] = in[0] * m[2][0] + in[1] * m[2][1] + in[2] * m[2][2] + m[2][3];
}

static void cs16_vector_rotate(const float in[3], const float m[3][4], float out[3]) {
    out[0] = in[0] * m[0][0] + in[1] * m[0][1] + in[2] * m[0][2];
    out[1] = in[0] * m[1][0] + in[1] * m[1][1] + in[2] * m[1][2];
    out[2] = in[0] * m[2][0] + in[1] * m[2][1] + in[2] * m[2][2];
}

/* --- Dynamic String Buffer for Wavefront OBJ text --- */

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
} cs16_strbuf_t;

static int cs16_strbuf_init(cs16_strbuf_t* sb, size_t initial_cap) {
    if (initial_cap < 4096) initial_cap = 4096;
    sb->buf = (char*)malloc(initial_cap);
    if (!sb->buf) return 0;
    sb->buf[0] = '\0';
    sb->len = 0;
    sb->cap = initial_cap;
    return 1;
}

static int cs16_strbuf_append(cs16_strbuf_t* sb, const char* str) {
    size_t slen = strlen(str);
    if (sb->len + slen + 1 > sb->cap) {
        size_t new_cap = sb->cap * 2;
        if (new_cap < sb->len + slen + 1) new_cap = sb->len + slen + 1 + 8192;
        char* new_buf = (char*)realloc(sb->buf, new_cap);
        if (!new_buf) return 0;
        sb->buf = new_buf;
        sb->cap = new_cap;
    }
    memcpy(sb->buf + sb->len, str, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
    return 1;
}

static int cs16_strbuf_printf(cs16_strbuf_t* sb, const char* fmt, ...) {
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (written < 0) return 0;
    if ((size_t)written < sizeof(tmp)) {
        return cs16_strbuf_append(sb, tmp);
    }
    char* big = (char*)malloc(written + 1);
    if (!big) return 0;
    va_start(args, fmt);
    vsnprintf(big, written + 1, fmt, args);
    va_end(args);
    int ok = cs16_strbuf_append(sb, big);
    free(big);
    return ok;
}

static void cs16_strbuf_free(cs16_strbuf_t* sb) {
    if (sb->buf) {
        free(sb->buf);
        sb->buf = NULL;
    }
    sb->len = 0;
    sb->cap = 0;
}

/* --- Public API: cs16_mdl_to_obj --- */

typedef struct {
    int v_idx;
    int vt_idx;
    int vn_idx;
} cs16_tri_point_t;

int cs16_mdl_to_obj_ex(const unsigned char* mdl_data, size_t mdl_size, unsigned char** out_obj, unsigned int* out_obj_size, int include_backpack) {
    if (!mdl_data || mdl_size < sizeof(cs16_studiohdr_t) || !out_obj || !out_obj_size) {
        return 0;
    }

    const cs16_studiohdr_t* hdr = (const cs16_studiohdr_t*)mdl_data;
    if (hdr->id != CS16_MDL_ID || hdr->version != CS16_MDL_VERSION) {
        return 0;
    }

    const int is_viewmodel = (hdr->name[0] == 'v' && hdr->name[1] == '_') || (strstr(hdr->name, "/v_") != NULL) || (strstr(hdr->name, "\\v_") != NULL);
    const int is_p_model = (hdr->name[0] == 'p' && hdr->name[1] == '_') || (strstr(hdr->name, "/p_") != NULL) || (strstr(hdr->name, "\\p_") != NULL);
    const int is_player = !is_viewmodel && !is_p_model && (hdr->numseq > 10);

    /* Compute bone transformation matrices:
       - Viewmodels: Seq 0 Frame 0 (idle pose)
       - p_ models: Seq 0 Frame 0 relative to "Bip01 R Hand" (so attachment to player's "Bip01 R Hand" aligns in the hand)
       - Player models: Seq 13 Frame 0 (ref_aim_carbine, authentic weapon-holding combat stance) or Seq 1 (idle1)
         so player bots do not stand in a frozen T-pose! */
    float bonetransforms[CS16_MAX_BONES][3][4];
    int numbones = hdr->numbones;
    if (numbones > CS16_MAX_BONES) numbones = CS16_MAX_BONES;

    int target_seq = -1;
    if ((is_viewmodel || is_p_model) && hdr->numseq > 0) {
        target_seq = 0;
    } else if (hdr->numseq > 13) {
        target_seq = 13; /* CS 1.6: ref_aim_carbine / two-handed weapon aim stance */
    } else if (hdr->numseq > 1) {
        target_seq = 1;  /* CS 1.6: idle1 */
    } else if (hdr->numseq > 0) {
        target_seq = 0;
    }

    const cs16_mstudioseqdesc_t* seq = NULL;
    const cs16_mstudioanim_t* anims = NULL;
    if (target_seq >= 0 && (size_t)hdr->seqindex + (size_t)(target_seq + 1) * sizeof(cs16_mstudioseqdesc_t) <= mdl_size) {
        const cs16_mstudioseqdesc_t* all_seqs = (const cs16_mstudioseqdesc_t*)(mdl_data + hdr->seqindex);
        seq = &all_seqs[target_seq];
        if ((size_t)seq->animindex + (size_t)numbones * sizeof(cs16_mstudioanim_t) <= mdl_size) {
            anims = (const cs16_mstudioanim_t*)(mdl_data + seq->animindex);
        }
    }

    float bone_local_q[CS16_MAX_BONES][4];
    float bone_local_pos[CS16_MAX_BONES][3];
    int bone_parent[CS16_MAX_BONES];
    char bone_clean_names[CS16_MAX_BONES][64];

    if (numbones > 0 && (size_t)hdr->boneindex + (size_t)numbones * sizeof(cs16_studiobone_t) <= mdl_size) {
        const cs16_studiobone_t* bones = (const cs16_studiobone_t*)(mdl_data + hdr->boneindex);
        for (int i = 0; i < numbones; ++i) {
            float pos[3], rot[3];
            const cs16_mstudioanim_t* panim = anims ? &anims[i] : NULL;
            const unsigned char* panim_bytes = (const unsigned char*)panim;

            for (int j = 0; j < 3; ++j) {
                pos[j] = bones[i].value[j];
                if (panim && panim->offset[j] != 0 &&
                    (size_t)(panim_bytes - mdl_data) + panim->offset[j] + sizeof(cs16_mstudioanimvalue_t) <= mdl_size) {
                    const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)(panim_bytes + panim->offset[j]);
                    float val = 0.f;
                    cs16_extract_anim_value(0, av, bones[i].scale[j], &val);
                    pos[j] += val;
                }
            }
            for (int j = 0; j < 3; ++j) {
                rot[j] = bones[i].value[j + 3];
                if (panim && panim->offset[j + 3] != 0 &&
                    (size_t)(panim_bytes - mdl_data) + panim->offset[j + 3] + sizeof(cs16_mstudioanimvalue_t) <= mdl_size) {
                    const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)(panim_bytes + panim->offset[j + 3]);
                    float val = 0.f;
                    cs16_extract_anim_value(0, av, bones[i].scale[j + 3], &val);
                    rot[j] += val;
                }
            }
            float q[4];
            float bonematrix[3][4];
            cs16_angle_quaternion(rot, q);
            cs16_quat_matrix(q, pos, bonematrix);

            int par = (bones[i].parent < 0 || bones[i].parent >= i) ? -1 : bones[i].parent;
            bone_parent[i] = par;
            memcpy(bone_local_q[i], q, sizeof(q));
            bone_local_pos[i][0] = pos[0];
            bone_local_pos[i][1] = pos[1];
            bone_local_pos[i][2] = (par == -1 && !is_viewmodel) ? (pos[2] + 37.0f) : pos[2];

            strncpy(bone_clean_names[i], bones[i].name, 63);
            bone_clean_names[i][63] = '\0';
            for (int k = (int)strlen(bone_clean_names[i]) - 1; k >= 0 && bone_clean_names[i][k] == ' '; --k) {
                bone_clean_names[i][k] = '\0';
            }
            for (int k = 0; bone_clean_names[i][k]; ++k) {
                if (bone_clean_names[i][k] == '"') bone_clean_names[i][k] = '_';
            }
            if (!bone_clean_names[i][0]) snprintf(bone_clean_names[i], 64, "bone_%d", i);
            for (int prev = 0; prev < i; ++prev) {
                if (strcmp(bone_clean_names[prev], bone_clean_names[i]) == 0) {
                    snprintf(bone_clean_names[i], 64, "bone_%d", i);
                    break;
                }
            }

            if (is_p_model && strcmp(bone_clean_names[i], "Bip01 R Hand") == 0) {
                memset(bonetransforms[i], 0, sizeof(bonetransforms[i]));
                bonetransforms[i][0][0] = 1.0f;
                bonetransforms[i][1][1] = 1.0f;
                bonetransforms[i][2][2] = 1.0f;
            } else if (par == -1) {
                memcpy(bonetransforms[i], bonematrix, sizeof(bonematrix));
            } else {
                cs16_concat_transforms(bonetransforms[par], bonematrix, bonetransforms[i]);
            }
        }
    }

    /* Texture references */
    const cs16_studiotexture_t* textures = NULL;
    if (hdr->numtextures > 0 && (size_t)hdr->textureindex + (size_t)hdr->numtextures * sizeof(cs16_studiotexture_t) <= mdl_size) {
        textures = (const cs16_studiotexture_t*)(mdl_data + hdr->textureindex);
    }

    /* Skin reference remap table */
    const short* pskinref = NULL;
    if (hdr->numskinref > 0 && (size_t)hdr->skinindex + (size_t)hdr->numskinref * sizeof(short) <= mdl_size) {
        pskinref = (const short*)(mdl_data + hdr->skinindex);
    }

    cs16_strbuf_t sb;
    if (!cs16_strbuf_init(&sb, 65536)) {
        return 0;
    }

    cs16_strbuf_printf(&sb, "# Wavefront OBJ converted from StudioMDL v10\n");

    if (!is_viewmodel && !is_p_model && numbones > 0) {
        for (int i = 0; i < numbones; ++i) {
            cs16_strbuf_printf(&sb, "bone \"%s\" %d %.6f %.6f %.6f %.6f %.4f %.4f %.4f\n",
                bone_clean_names[i], bone_parent[i],
                bone_local_q[i][0], bone_local_q[i][1], bone_local_q[i][2], bone_local_q[i][3],
                bone_local_pos[i][0], bone_local_pos[i][1], bone_local_pos[i][2]);
        }
    }

    int global_v_offset = 0;
    int global_vn_offset = 0;
    int global_vt_offset = 0;
    int total_faces = 0;

    if (hdr->numbodyparts <= 0 || (size_t)hdr->bodypartindex >= mdl_size) {
        cs16_strbuf_free(&sb);
        return 0;
    }

    for (int bp_idx = 0; bp_idx < hdr->numbodyparts; ++bp_idx) {
        size_t bp_offset = (size_t)hdr->bodypartindex + (size_t)bp_idx * sizeof(cs16_studiobodypart_t);
        if (bp_offset + sizeof(cs16_studiobodypart_t) > mdl_size) break;
        const cs16_studiobodypart_t* bp = (const cs16_studiobodypart_t*)(mdl_data + bp_offset);

        if (is_player && bp_idx > 0 && !include_backpack) {
            /* Skip backpack bodypart so standard players don't wear a backpack */
            continue;
        }

        for (int m_idx = 0; m_idx < bp->nummodels; ++m_idx) {
            if (is_player && bp_idx > 0 && include_backpack) {
                /* When backpack is requested, only export the actual backpack model (m_idx == 1), not blank */
                if (m_idx != 1) continue;
            }
            size_t model_offset = (size_t)bp->modelindex + (size_t)m_idx * sizeof(cs16_studiomodel_t);
            if (model_offset + sizeof(cs16_studiomodel_t) > mdl_size) break;
            const cs16_studiomodel_t* model = (const cs16_studiomodel_t*)(mdl_data + model_offset);

            if (model->numverts <= 0 || model->nummesh <= 0) {
                continue;
            }

            /* Validate vertex and normal offsets */
            if ((size_t)model->vertindex + (size_t)model->numverts * 12 > mdl_size) continue;
            const float* raw_verts = (const float*)(mdl_data + model->vertindex);
            const unsigned char* vert_bones = NULL;
            if (model->vertinfoindex > 0 && (size_t)model->vertinfoindex + (size_t)model->numverts <= mdl_size) {
                vert_bones = mdl_data + model->vertinfoindex;
            }

            const float* raw_norms = NULL;
            const unsigned char* norm_bones = NULL;
            if (model->numnorms > 0 && (size_t)model->normindex + (size_t)model->numnorms * 12 <= mdl_size) {
                raw_norms = (const float*)(mdl_data + model->normindex);
                if (model->norminfoindex > 0 && (size_t)model->norminfoindex + (size_t)model->numnorms <= mdl_size) {
                    norm_bones = mdl_data + model->norminfoindex;
                }
            }

            /* 1. Emit vertices for this submodel */
            for (int v = 0; v < model->numverts; ++v) {
                float in_v[3] = { raw_verts[v * 3 + 0], raw_verts[v * 3 + 1], raw_verts[v * 3 + 2] };
                float out_v[3];
                int b = (vert_bones && numbones > 0) ? vert_bones[v] : -1;
                if (b >= 0 && b < numbones) {
                    cs16_vector_transform(in_v, bonetransforms[b], out_v);
                } else {
                    out_v[0] = in_v[0]; out_v[1] = in_v[1]; out_v[2] = in_v[2];
                }
                if (is_viewmodel) {
                    /* Viewmodel orientation and eye offset alignment:
                       Helix authored_viewmodel_transform:
                         Column 0 (Right) multiplies model X
                         Column 1 (Up) multiplies model Y
                         Column 2 (Forward) multiplies model Z
                       In GoldSrc StudioMDL viewmodels:
                         out_v[0] is Forward (+X is muzzle, -X is stock)
                         out_v[1] is Right (+Y is right hand, -Y is left hand; center ~ 6.85f)
                         out_v[2] is Up (+Z is sights, -Z is magazine / grip; center ~ -3.50f)
                       Map cleanly to Helix viewport:
                         vx: placed naturally in lower-right of view
                         vy: sights and barrel aligned below camera line
                         vz: brought close to camera so forearm stump falls behind the near plane */
                    const float vm_scale = 0.38f;
                    float vx = (out_v[1] - 6.85f) * vm_scale + 1.85f;
                    float vy = (out_v[2] + 3.50f) * vm_scale - 2.20f;
                    float vz = out_v[0] * vm_scale - 0.20f;
                    cs16_strbuf_printf(&sb, "v %.4f %.4f %.4f\n", vx, vy, vz);
                } else if (is_p_model) {
                    /* World weapon attachment (p_*.mdl): already in "Bip01 R Hand" local space, no Z offset and no skinning */
                    cs16_strbuf_printf(&sb, "v %.4f %.4f %.4f\n", out_v[0], out_v[1], out_v[2]);
                } else {
                    /* Player and world/w_ models: GoldSrc foot origin is at ~ -37.0f.
                       Helix player_pose_origin places the model at origin - 24, expecting feet at Z=0.
                       Offset Z by +37.0f so feet align exactly with the ground plane! */
                    cs16_strbuf_printf(&sb, "v %.4f %.4f %.4f\n", out_v[0], out_v[1], out_v[2] + 37.0f);
                    if (numbones > 0) {
                        int sb_idx = (b >= 0 && b < numbones) ? b : 0;
                        cs16_strbuf_printf(&sb, "skin %d %d 1.0 %.4f %.4f %.4f 0.0 0.0 1.0\n",
                            global_v_offset + v + 1, sb_idx, in_v[0], in_v[1], in_v[2]);
                    }
                }
            }

            /* 2. Emit normals for this submodel */
            for (int n = 0; n < model->numnorms; ++n) {
                float in_n[3] = { raw_norms[n * 3 + 0], raw_norms[n * 3 + 1], raw_norms[n * 3 + 2] };
                float out_n[3];
                int b = (norm_bones && numbones > 0) ? norm_bones[n] : -1;
                if (b >= 0 && b < numbones) {
                    cs16_vector_rotate(in_n, bonetransforms[b], out_n);
                } else {
                    out_n[0] = in_n[0]; out_n[1] = in_n[1]; out_n[2] = in_n[2];
                }
                if (is_viewmodel) {
                    /* Match the axis permutation: X=Right(out_n[1]), Y=Up(out_n[2]), Z=Forward(out_n[0]) */
                    float nx = out_n[1];
                    float ny = out_n[2];
                    float nz = out_n[0];
                    cs16_strbuf_printf(&sb, "vn %.4f %.4f %.4f\n", nx, ny, nz);
                } else {
                    cs16_strbuf_printf(&sb, "vn %.4f %.4f %.4f\n", out_n[0], out_n[1], out_n[2]);
                }
            }

            /* 3. Emit meshes: usemtl, vt, faces */
            for (int mesh_idx = 0; mesh_idx < model->nummesh; ++mesh_idx) {
                size_t mesh_offset = (size_t)model->meshindex + (size_t)mesh_idx * sizeof(cs16_studiomesh_t);
                if (mesh_offset + sizeof(cs16_studiomesh_t) > mdl_size) break;
                const cs16_studiomesh_t* mesh = (const cs16_studiomesh_t*)(mdl_data + mesh_offset);

                /* Resolve texture */
                int tex_idx = mesh->skinref;
                if (pskinref && tex_idx >= 0 && tex_idx < hdr->numskinref) {
                    tex_idx = pskinref[tex_idx];
                }

                int tex_w = 256;
                int tex_h = 256;
                if (textures && tex_idx >= 0 && tex_idx < hdr->numtextures) {
                    const cs16_studiotexture_t* tex = &textures[tex_idx];
                    if (tex->width > 0 && tex->height > 0) {
                        tex_w = tex->width;
                        tex_h = tex->height;
                    }
                    char mat_name[96];
                    snprintf(mat_name, sizeof(mat_name), "textures/models/%s", tex->name);
                    size_t nl = strlen(mat_name);
                    if (nl >= 4 && (strcmp(mat_name + nl - 4, ".bmp") == 0 || strcmp(mat_name + nl - 4, ".BMP") == 0)) {
                        memcpy(mat_name + nl - 4, ".tga", 4);
                    }
                    cs16_strbuf_printf(&sb, "usemtl %s\n", mat_name);
                }

                /* Process triangle command list */
                size_t cmd_offset = (size_t)mesh->triangleindex;
                while (cmd_offset + 2 <= mdl_size) {
                    short count;
                    memcpy(&count, mdl_data + cmd_offset, 2);
                    cmd_offset += 2;
                    if (count == 0) break;

                    int is_fan = (count < 0);
                    int num_pts = is_fan ? -count : count;
                    if (num_pts > 256) num_pts = 256;

                    if (cmd_offset + (size_t)num_pts * 8 > mdl_size) break;

                    cs16_tri_point_t pts[256];
                    for (int p = 0; p < num_pts; ++p) {
                        short vidx, nidx, s, t;
                        memcpy(&vidx, mdl_data + cmd_offset + 0, 2);
                        memcpy(&nidx, mdl_data + cmd_offset + 2, 2);
                        memcpy(&s, mdl_data + cmd_offset + 4, 2);
                        memcpy(&t, mdl_data + cmd_offset + 6, 2);
                        cmd_offset += 8;

                        float u = (float)s / (float)tex_w;
                        float v = (float)t / (float)tex_h;
                        cs16_strbuf_printf(&sb, "vt %.6f %.6f\n", u, v);
                        global_vt_offset++;

                        pts[p].v_idx = global_v_offset + (int)vidx + 1;
                        pts[p].vn_idx = global_vn_offset + (int)nidx + 1;
                        pts[p].vt_idx = global_vt_offset;
                    }

                    if (is_fan) {
                        /* Fan unwinding */
                        for (int i = 1; i + 1 < num_pts; ++i) {
                            if (pts[0].v_idx == pts[i].v_idx || pts[i].v_idx == pts[i + 1].v_idx || pts[0].v_idx == pts[i + 1].v_idx) {
                                continue;
                            }
                            cs16_strbuf_printf(&sb, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                                pts[0].v_idx, pts[0].vt_idx, pts[0].vn_idx,
                                pts[i].v_idx, pts[i].vt_idx, pts[i].vn_idx,
                                pts[i + 1].v_idx, pts[i + 1].vt_idx, pts[i + 1].vn_idx);
                            total_faces++;
                        }
                    } else {
                        /* Strip unwinding */
                        for (int j = 0; j + 2 < num_pts; ++j) {
                            int i0 = (j & 1) ? (j + 1) : j;
                            int i1 = (j & 1) ? j : (j + 1);
                            int i2 = j + 2;

                            if (pts[i0].v_idx == pts[i1].v_idx || pts[i1].v_idx == pts[i2].v_idx || pts[i0].v_idx == pts[i2].v_idx) {
                                continue;
                            }
                            cs16_strbuf_printf(&sb, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                                pts[i0].v_idx, pts[i0].vt_idx, pts[i0].vn_idx,
                                pts[i1].v_idx, pts[i1].vt_idx, pts[i1].vn_idx,
                                pts[i2].v_idx, pts[i2].vt_idx, pts[i2].vn_idx);
                            total_faces++;
                        }
                    }
                }
            }

            global_v_offset += model->numverts;
            global_vn_offset += model->numnorms;
        }
    }

    if (total_faces == 0) {
        cs16_strbuf_free(&sb);
        return 0;
    }

    *out_obj = (unsigned char*)sb.buf;
    *out_obj_size = (unsigned int)sb.len;
    return 1;
}

int cs16_mdl_to_obj(const unsigned char* mdl_data, size_t mdl_size, unsigned char** out_obj, unsigned int* out_obj_size) {
    return cs16_mdl_to_obj_ex(mdl_data, mdl_size, out_obj, out_obj_size, 0);
}

/* --- Public API: cs16_mdl_extract_texture --- */

int cs16_mdl_extract_texture(const unsigned char* mdl_data, size_t mdl_size, int tex_index, unsigned char** out_tga, unsigned int* out_tga_size, char* out_name, size_t max_name) {
    if (!mdl_data || mdl_size < sizeof(cs16_studiohdr_t) || !out_tga || !out_tga_size || tex_index < 0) {
        return 0;
    }

    const cs16_studiohdr_t* hdr = (const cs16_studiohdr_t*)mdl_data;
    if (hdr->id != CS16_MDL_ID || hdr->version != CS16_MDL_VERSION) {
        return 0;
    }

    if (tex_index >= hdr->numtextures) {
        return 0;
    }

    size_t tex_offset = (size_t)hdr->textureindex + (size_t)tex_index * sizeof(cs16_studiotexture_t);
    if (tex_offset + sizeof(cs16_studiotexture_t) > mdl_size) {
        return 0;
    }

    const cs16_studiotexture_t* tex = (const cs16_studiotexture_t*)(mdl_data + tex_offset);
    if (tex->width <= 0 || tex->height <= 0 || tex->width > 4096 || tex->height > 4096) {
        return 0;
    }

    size_t num_pixels = (size_t)tex->width * (size_t)tex->height;
    size_t pix_offset = (size_t)tex->index;
    size_t pal_offset = pix_offset + num_pixels;

    if (pal_offset + 768 > mdl_size) {
        return 0;
    }

    if (out_name && max_name > 0) {
        strncpy(out_name, tex->name, max_name - 1);
        out_name[max_name - 1] = '\0';
    }

    /* Allocate 18-byte TGA header + 32-bit BGRA pixels */
    size_t tga_size = 18 + num_pixels * 4;
    unsigned char* tga = (unsigned char*)malloc(tga_size);
    if (!tga) {
        return 0;
    }

    memset(tga, 0, 18);
    tga[2] = 2;  /* Uncompressed true-color image */
    tga[12] = (unsigned char)(tex->width & 0xFF);
    tga[13] = (unsigned char)((tex->width >> 8) & 0xFF);
    tga[14] = (unsigned char)(tex->height & 0xFF);
    tga[15] = (unsigned char)((tex->height >> 8) & 0xFF);
    tga[16] = 32; /* 32 bits per pixel */
    tga[17] = 0x20 | 0x08; /* Top-down ordering + 8 alpha bits */

    const unsigned char* pixels = mdl_data + pix_offset;
    const unsigned char* palette = mdl_data + pal_offset;
    unsigned char* dst = tga + 18;

    int is_blue_key = (palette[255 * 3 + 0] == 0 && palette[255 * 3 + 1] == 0 && palette[255 * 3 + 2] == 255);
    int is_masked = (tex->name[0] == '{') || ((tex->flags & 0x0040) != 0) || is_blue_key;

    for (size_t i = 0; i < num_pixels; ++i) {
        unsigned char pal_idx = pixels[i];
        unsigned char r = palette[pal_idx * 3 + 0];
        unsigned char g = palette[pal_idx * 3 + 1];
        unsigned char b = palette[pal_idx * 3 + 2];
        int transparent = is_masked && (pal_idx == 255 || (r == 0 && g == 0 && b == 255));
        if (transparent) {
            dst[i * 4 + 0] = 0;
            dst[i * 4 + 1] = 0;
            dst[i * 4 + 2] = 0;
            dst[i * 4 + 3] = 0;
        } else {
            dst[i * 4 + 0] = b;
            dst[i * 4 + 1] = g;
            dst[i * 4 + 2] = r;
            dst[i * 4 + 3] = 255;
        }
    }

    if (is_masked) {
        unsigned int w = (unsigned int)tex->width;
        unsigned int h = (unsigned int)tex->height;
        for (unsigned int y = 0; y < h; ++y) {
            for (unsigned int x = 0; x < w; ++x) {
                unsigned int i = y * w + x;
                if (dst[i * 4 + 3] != 0) continue;
                unsigned int sr = 0, sg = 0, sb = 0, cnt = 0;
                const int dx[4] = { -1, 1, 0, 0 };
                const int dy[4] = { 0, 0, -1, 1 };
                for (int d = 0; d < 4; ++d) {
                    int nx = (int)x + dx[d];
                    int ny = (int)y + dy[d];
                    if (nx >= 0 && nx < (int)w && ny >= 0 && ny < (int)h) {
                        unsigned int ni = (unsigned int)ny * w + (unsigned int)nx;
                        if (dst[ni * 4 + 3] == 255) {
                            sb += dst[ni * 4 + 0];
                            sg += dst[ni * 4 + 1];
                            sr += dst[ni * 4 + 2];
                            cnt++;
                        }
                    }
                }
                if (cnt > 0) {
                    dst[i * 4 + 0] = (unsigned char)(sb / cnt);
                    dst[i * 4 + 1] = (unsigned char)(sg / cnt);
                    dst[i * 4 + 2] = (unsigned char)(sr / cnt);
                }
            }
        }
    }

    *out_tga = tga;
    *out_tga_size = (unsigned int)tga_size;
    return 1;
}

static void cs16_write_u32_le(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void cs16_write_f32_le(unsigned char* p, float f) {
    unsigned int bits = 0;
    memcpy(&bits, &f, 4);
    cs16_write_u32_le(p, bits);
}

int cs16_mdl_seq_to_hxac(const unsigned char* mdl_data, size_t mdl_size,
                         const char* seq_label, int looped, int bone_filter,
                         unsigned char** out_hxac, unsigned int* out_size) {
    if (!mdl_data || mdl_size < sizeof(cs16_studiohdr_t) || !seq_label || !out_hxac || !out_size) return 0;
    const cs16_studiohdr_t* hdr = (const cs16_studiohdr_t*)mdl_data;
    if (hdr->id != CS16_MDL_ID || hdr->version != CS16_MDL_VERSION) return 0;

    int numbones = hdr->numbones;
    if (numbones <= 0 || numbones > CS16_MAX_BONES) return 0;
    if ((size_t)hdr->boneindex + (size_t)numbones * sizeof(cs16_studiobone_t) > mdl_size) return 0;
    const cs16_studiobone_t* bones = (const cs16_studiobone_t*)(mdl_data + hdr->boneindex);

    if (hdr->numseq <= 0 || (size_t)hdr->seqindex + (size_t)hdr->numseq * sizeof(cs16_mstudioseqdesc_t) > mdl_size) return 0;
    const cs16_mstudioseqdesc_t* all_seqs = (const cs16_mstudioseqdesc_t*)(mdl_data + hdr->seqindex);

    const cs16_mstudioseqdesc_t* seq = NULL;
    for (int s = 0; s < hdr->numseq; ++s) {
        if (strcmp(all_seqs[s].label, seq_label) == 0) {
            seq = &all_seqs[s];
            break;
        }
    }
    if (!seq && hdr->numseq > 1) seq = &all_seqs[1];
    if (!seq) return 0;
    if ((size_t)seq->animindex + (size_t)numbones * sizeof(cs16_mstudioanim_t) > mdl_size) return 0;
    const cs16_mstudioanim_t* anims = (const cs16_mstudioanim_t*)(mdl_data + seq->animindex);

    char bone_names[CS16_MAX_BONES][64];
    int selected_bones[CS16_MAX_BONES];
    int num_channels = 0;

    for (int i = 0; i < numbones; ++i) {
        strncpy(bone_names[i], bones[i].name, 63);
        bone_names[i][63] = '\0';
        for (int k = (int)strlen(bone_names[i]) - 1; k >= 0 && bone_names[i][k] == ' '; --k) {
            bone_names[i][k] = '\0';
        }
        for (int k = 0; bone_names[i][k]; ++k) {
            if (bone_names[i][k] == '"') bone_names[i][k] = '_';
        }
        if (!bone_names[i][0]) snprintf(bone_names[i], 64, "bone_%d", i);
        for (int prev = 0; prev < i; ++prev) {
            if (strcmp(bone_names[prev], bone_names[i]) == 0) {
                snprintf(bone_names[i], 64, "bone_%d", i);
                break;
            }
        }

        /* Check if bone belongs to requested layer:
           1 = lower body (root 0, pelvis 1, and leg hierarchy descending from thigh/butt)
           2 = upper body (spine 2 and all descendants except legs) */
        int is_upper = 0;
        int cur = i;
        int depth = 0;
        while (cur >= 0 && cur < numbones && depth++ < numbones) {
            if (strstr(bone_names[cur], "Spine") != NULL || strstr(bone_names[cur], "Neck") != NULL ||
                strstr(bone_names[cur], "Head") != NULL || strstr(bone_names[cur], "Clavicle") != NULL ||
                strstr(bone_names[cur], "Bomb") != NULL) {
                is_upper = 1;
                break;
            }
            cur = bones[cur].parent;
        }
        if (bone_filter == 1 && is_upper) continue;
        if (bone_filter == 2 && !is_upper) continue;
        selected_bones[num_channels++] = i;
    }

    if (num_channels <= 0) return 0;

    int num_frames = seq->numframes;
    if (num_frames < 1) num_frames = 1;
    if (num_frames > 512) num_frames = 512;
    float fps = (seq->fps > 0.5f && seq->fps < 500.0f) ? seq->fps : 20.0f;
    unsigned int flags = looped ? 1u : 0u;

    size_t header_sz = 36 + (size_t)num_channels * 64;
    size_t cells = (size_t)num_frames * (size_t)num_channels;
    size_t total_sz = header_sz + cells * 28;
    unsigned char* buf = (unsigned char*)malloc(total_sz);
    if (!buf) return 0;
    memset(buf, 0, total_sz);

    memcpy(buf, "HXAC1\0\0\0", 8);
    cs16_write_u32_le(buf + 8, (unsigned int)num_frames);
    cs16_write_u32_le(buf + 12, (unsigned int)num_channels);
    cs16_write_f32_le(buf + 16, fps);
    cs16_write_u32_le(buf + 20, flags);
    cs16_write_f32_le(buf + 24, 0.0f);
    cs16_write_f32_le(buf + 28, 0.0f);
    cs16_write_f32_le(buf + 32, 0.0f);

    for (int c = 0; c < num_channels; ++c) {
        int b = selected_bones[c];
        memcpy(buf + 36 + (size_t)c * 64, bone_names[b], strlen(bone_names[b]) + 1);
    }

    /* Reference frame 0 XY of root bone so locomotion cycles stay in-place on entity origin */
    float root_xy0[2] = { bones[0].value[0], bones[0].value[1] };
    {
        const cs16_mstudioanim_t* ranim = &anims[0];
        const unsigned char* rbytes = (const unsigned char*)ranim;
        for (int j = 0; j < 2; ++j) {
            if (ranim->offset[j] != 0 &&
                (size_t)(rbytes - mdl_data) + ranim->offset[j] + sizeof(cs16_mstudioanimvalue_t) <= mdl_size) {
                const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)(rbytes + ranim->offset[j]);
                float val = 0.f;
                cs16_extract_anim_value(0, av, bones[0].scale[j], &val);
                root_xy0[j] += val;
            }
        }
    }

    size_t sample_ofs = header_sz;
    for (int f = 0; f < num_frames; ++f) {
        for (int c = 0; c < num_channels; ++c) {
            int i = selected_bones[c];
            float pos[3], rot[3];
            const cs16_mstudioanim_t* panim = &anims[i];
            const unsigned char* panim_bytes = (const unsigned char*)panim;

            for (int j = 0; j < 3; ++j) {
                pos[j] = bones[i].value[j];
                if (panim->offset[j] != 0 &&
                    (size_t)(panim_bytes - mdl_data) + panim->offset[j] + sizeof(cs16_mstudioanimvalue_t) <= mdl_size) {
                    const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)(panim_bytes + panim->offset[j]);
                    float val = 0.f;
                    cs16_extract_anim_value(f, av, bones[i].scale[j], &val);
                    pos[j] += val;
                }
            }
            for (int j = 0; j < 3; ++j) {
                rot[j] = bones[i].value[j + 3];
                if (panim->offset[j + 3] != 0 &&
                    (size_t)(panim_bytes - mdl_data) + panim->offset[j + 3] + sizeof(cs16_mstudioanimvalue_t) <= mdl_size) {
                    const cs16_mstudioanimvalue_t* av = (const cs16_mstudioanimvalue_t*)(panim_bytes + panim->offset[j + 3]);
                    float val = 0.f;
                    cs16_extract_anim_value(f, av, bones[i].scale[j + 3], &val);
                    rot[j] += val;
                }
            }
            int par = (bones[i].parent < 0 || bones[i].parent >= i) ? -1 : bones[i].parent;
            if (par == -1) {
                pos[0] = root_xy0[0];
                pos[1] = root_xy0[1];
                pos[2] += 37.0f;
            }
            float q[4];
            cs16_angle_quaternion(rot, q);
            cs16_write_f32_le(buf + sample_ofs + 0, q[0]);
            cs16_write_f32_le(buf + sample_ofs + 4, q[1]);
            cs16_write_f32_le(buf + sample_ofs + 8, q[2]);
            cs16_write_f32_le(buf + sample_ofs + 12, q[3]);
            cs16_write_f32_le(buf + sample_ofs + 16, pos[0]);
            cs16_write_f32_le(buf + sample_ofs + 20, pos[1]);
            cs16_write_f32_le(buf + sample_ofs + 24, pos[2]);
            sample_ofs += 28;
        }
    }

    *out_hxac = buf;
    *out_size = (unsigned int)total_sz;
    return 1;
}
