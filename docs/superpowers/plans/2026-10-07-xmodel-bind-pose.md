# Xmodel Bind-Pose Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One general CoD1 xmodel bind-pose so map props keep their parts, sit on the ground, and carry one skin per surface.

**Architecture:** The plugin reader bakes each bone's world matrix once from immutable locals, binds the emitted surf to its own tail skin block, and writes a single OBJ for the first named LOD. Helix already draws each `usemtl` group and collides with those triangles. Runtime LOD stays unwired.

**Tech Stack:** C, CMake, `hxfs_cod_test`, retail PK3 only when `Main` is present.

## Global Constraints

- Work stays in `src/cod`. `src/cod2003` and the Helix engine stay unchanged.
- No per-model name table.
- Viewmodels, hands, the weapon list, and GSC stay out. The 24-unit collision thickness stays out. `ModelAssets::add_lod` stays unwired.
- The converter always emits every mesh it could parse. A short skin block does not drop the model.
- Parent 255, or a parent index equal to the bone itself, is a root. World matrix = parent world × local, each ancestor once, dependency order.
- Four bone-rule candidates. Default is the current reader: surf bone `N` selects `parts[N-1]`, parent byte `0` is bone 0. Retail Peugeot and Flakpanzer may replace that default only when exactly one other candidate passes. If several pass, or none pass, stop.
- A mesh center matches when it stays within 30 units.
- Skin names with no directory become `skins/<name>.png`. The `@` stays. No names means `skins/unbound`. Extra meshes reuse the block's last name.
- Prop conversion does not hand-sort skins. Viewmodel conversion still does, behind `cod_xmodel_set_keep_hands(1)`.
- `COD_CACHE_REV` becomes 15.
- Retail checks read `COD_MAIN`, or `C:\Program Files (x86)\Steam\steamapps\common\Call of Duty\Main` when the variable is unset. A missing directory skips those checks. Synthetic tests still run.
- If the zero-bone harbor truck stands half in the ground, stop. Entity axes are a separate spec.

---

### Task 1: LOD slots keep their distance

**Files:**
- Modify: `src/cod/xmodel/read.h`
- Modify: `src/cod/xmodel/read.c` (the v14 loop inside `cod_xmodel_lod_names`)
- Test: `tests/hxfs_cod_test.c`

**Interfaces:**
- Consumes: existing `cod_xmodel_lod_name` / `cod_xmodel_lod_names`
- Produces:

```c
typedef struct CodXmodelLod {
    float distance;
    char name[64];
} CodXmodelLod;

/* v14 writes 3 slots, including empty names. v5/v20 writes 4. Returns the slot count, or 0. */
int cod_xmodel_lod_slots(const void* data, size_t size, CodXmodelLod* out, int cap);
```

- [ ] **Step 1: Write the failing test**

In `tests/hxfs_cod_test.c`, add `put_f32` next to `put_u16` and call `test_xmodel_lod_slots` from `main` after `test_xmodel_v14_lod`.

```c
static void put_f32(unsigned char* p, float v) {
    memcpy(p, &v, 4);
}

static void test_xmodel_lod_slots(void) {
    int before = checkpoint();
    unsigned char header[128];
    CodXmodelLod slots[3];
    size_t hoff = 0;
    int n;
    memset(header, 0, sizeof(header));
    put_u16(header + hoff, 14);
    hoff += 2;
    hoff += 24; /* mins/maxs */
    put_f32(header + hoff, 1000.f); hoff += 4;
    memcpy(header + hoff, "flak88_anti-tank0", 18); hoff += 18;
    put_f32(header + hoff, 2500.f); hoff += 4;
    memcpy(header + hoff, "flak88_anti-tank_medLOD0", 25); hoff += 25;
    put_f32(header + hoff, 0.f); hoff += 4;
    memcpy(header + hoff, "flak88_anti-tank_lowLOD0", 25); hoff += 25;
    n = cod_xmodel_lod_slots(header, hoff, slots, 3);
    expect(n == 3, "three slots");
    expect(slots[0].distance == 1000.f, "near distance");
    expect(strcmp(slots[0].name, "flak88_anti-tank0") == 0, "near name");
    expect(slots[2].distance == 0.f, "low distance stays 0");
    expect(strcmp(slots[2].name, "flak88_anti-tank_lowLOD0") == 0, "low name");
    expect(cod_xmodel_lod_name(header, hoff, slots[0].name, 64) == 1, "first name still works");
    report("test_xmodel_lod_slots", before);
}
```

- [ ] **Step 2: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: FAIL to compile, `cod_xmodel_lod_slots` undeclared.

- [ ] **Step 3: Implement the slot reader**

Add the struct and prototype to `src/cod/xmodel/read.h` after `cod_xmodel_lod_names`.

In `src/cod/xmodel/read.c`, implement `cod_xmodel_lod_slots` by the same version split as `cod_xmodel_lod_names`, but store `distance` and keep empty names. v14: skip 24 bytes of bounds, then three `(f32 distance, cstring)` pairs. v5 and v20: four `(u32 ignored, cstring)` pairs with `distance` left 0. `cod_xmodel_lod_names` becomes a wrapper that copies only the non-empty names from `cod_xmodel_lod_slots`. `cod_xmodel_lod_name` stays the first non-empty name.

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: `test_xmodel_lod_slots: PASS` and the existing suite still prints PASS.

- [ ] **Step 5: Commit**

```bash
git add src/cod/xmodel/read.h src/cod/xmodel/read.c tests/hxfs_cod_test.c
git commit -m "$(cat <<'EOF'
feat(cod): keep xmodel LOD distance with each surf name

EOF
)"
```

---

### Task 2: Prop skins stay in file order

**Files:**
- Modify: `src/cod/xmodel/read.h`
- Modify: `src/cod/xmodel/read.c` (`shader_names`, `store_mat`, `sort_mats_non_hand_first`, the `usemtl` writes in `emit_surfs_v14`)
- Test: `tests/hxfs_cod_test.c` (`test_xmodel_v14_surf` and a new test)

**Interfaces:**
- Consumes: `cod_xmodel_set_keep_hands`, `cod_xmodel_to_obj`
- Produces:

```c
/* Image names in the contiguous .dds/.tga/.jpg/.png run at the end of the xmodel.
 * `names` is cap rows of 160 chars. Returns the count. Does not sort and does not add skins/. */
int cod_xmodel_skin_names(const void* xmodel, size_t size, char names[][160], int cap);
```

- [ ] **Step 1: Write the failing test**

Add `test_xmodel_skin_order` and call it from `main`. Change the skin half of `test_xmodel_v14_surf`: with `cod_xmodel_set_keep_hands(0)`, the only mesh takes the first file-order name.

```c
static void fill_one_tri(unsigned char* surf) {
    size_t i;
    memset(surf, 0, 20 + 3 * 32);
    put_u16(surf + 0, 14);
    put_u16(surf + 2, 1);
    surf[5] = 3;
    surf[7] = 1;
    surf[13] = 3;
    put_u16(surf + 14, 0);
    put_u16(surf + 16, 1);
    put_u16(surf + 18, 2);
    for (i = 0; i < 3; i++) {
        float pos[3];
        pos[0] = (float)i;
        pos[1] = 1.f;
        pos[2] = 2.f;
        memcpy(surf + 20 + i * 32 + 20, pos, sizeof(pos));
    }
}

static void test_xmodel_skin_order(void) {
    int before = checkpoint();
    unsigned char xm[128];
    unsigned char surf[20 + 3 * 32];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    char names[4][160];
    const char* a = "metal@ford.dds";
    const char* b = "metal@fordhub.dds";
    size_t n;
    int count;
    memset(xm, 0, sizeof(xm));
    fill_one_tri(surf);
    put_u16(xm, 14);
    n = 2;
    memcpy(xm + n, a, strlen(a) + 1); n += strlen(a) + 1;
    memcpy(xm + n, b, strlen(b) + 1); n += strlen(b) + 1;
    count = cod_xmodel_skin_names(xm, n, names, 4);
    expect(count == 2, "two tail skins");
    expect(strcmp(names[0], "metal@ford.dds") == 0, "file order");
    expect(strcmp(names[1], "metal@fordhub.dds") == 0, "second skin kept");
    cod_xmodel_set_keep_hands(0);
    expect(cod_xmodel_to_obj(xm, n, surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "prop convert");
    expect(obj && strstr((const char*)obj, "usemtl skins/metal@ford.png\n") != NULL, "first skin");
    expect(obj && strstr((const char*)obj, "characterhand") == NULL, "prop path does not sort");
    free(obj);
    obj = NULL;
    put_u16(xm, 14);
    expect(cod_xmodel_to_obj(xm, 2, surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "no skins still converts");
    expect(obj && strstr((const char*)obj, "usemtl skins/unbound.png\n") != NULL, "unbound");
    free(obj);
    obj = NULL;
    n = 2;
    memcpy(xm + n, "body@characterhand.dds", 22); n += 22;
    memcpy(xm + n, "viewmodel@bar_body.dds", 22); n += 22;
    cod_xmodel_set_keep_hands(1);
    expect(cod_xmodel_to_obj(xm, n, surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "viewmodel skins");
    expect(obj && strstr((const char*)obj, "usemtl skins/viewmodel@bar_body.png\n") != NULL, "hand sorted behind flag");
    free(obj);
    cod_xmodel_set_keep_hands(0);
    report("test_xmodel_skin_order", before);
}
```

`fill_one_tri` matches the one-mesh layout already used by `test_xmodel_v14_surf`: unknown byte, 3 verts, 1 triangle, strip length 3, indices 0,1,2, positions at byte 20 of each 32-byte vertex. Update `test_xmodel_v14_surf` so the unsorted call expects `usemtl skins/body@characterhand.png`, the first string in that buffer. The hand-sort expectation lives only in `test_xmodel_skin_order`. The short-block case (two meshes, one skin, second mesh reuses the last name) is `test_xmodel_bind_apply` in Task 4, because it needs two complete meshes.

- [ ] **Step 2: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: FAIL to compile, `cod_xmodel_skin_names` undeclared, or FAIL on the old hand-sorted `usemtl`.

- [ ] **Step 3: Implement tail skins**

`cod_xmodel_skin_names` walks the buffer for NUL-terminated image names (`.dds`, `.tga`, `.jpg`, `.png`). It keeps only the suffix run whose strings sit directly against each other. It does not prefix `skins/` and does not sort.

`shader_names` fills `g_mats` from that suffix. For a name with no slash it writes `skins/` and replaces the extension with `.png`. A name that already has a slash only changes the extension. `sort_mats_non_hand_first` runs only when `g_keep_hands` is 1.

In `emit_surfs_v14`, mesh `m` uses `g_mats[m]` when `m < g_nmat`. When `g_nmat > 0` and `m >= g_nmat`, it uses `g_mats[g_nmat - 1]`. When `g_nmat == 0`, it writes `usemtl skins/unbound.png`.

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: `test_xmodel_skin_order: PASS` and `test_xmodel_v14_surf: PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/cod/xmodel/read.h src/cod/xmodel/read.c tests/hxfs_cod_test.c
git commit -m "$(cat <<'EOF'
feat(cod): bind xmodel surfaces to file-order skins

EOF
)"
```

---

### Task 3: Bake each bone world matrix once

**Files:**
- Modify: `src/cod/xmodel/read.h`
- Modify: `src/cod/xmodel/read.c` (replace the in-place parent walk around the `CoD1Bone` loop)
- Test: `tests/hxfs_cod_test.c`

**Interfaces:**
- Consumes: nothing from Task 1 or 2
- Produces:

```c
typedef struct CodXmodelBoneRule {
    int one_based;       /* 1: surf N -> parts[N-1]. 0: surf N -> parts[N]. */
    int parent0_is_root; /* 1: parent byte 0 is a root. 0: parent byte 0 is bone 0. */
} CodXmodelBoneRule;

void cod_xmodel_set_bone_rule(CodXmodelBoneRule rule);
CodXmodelBoneRule cod_xmodel_bone_rule(void);

/* World translation of each bone, xyz packed. Returns the bone count, or 0. */
int cod_xmodel_world_translations(const void* parts, size_t size,
                                 float* out_xyz, int cap);
```

Default rule, process-wide, starting value `{1, 0}`.

- [ ] **Step 1: Write the failing test**

```c
static void test_xmodel_bone_once(void) {
    int before = checkpoint();
    unsigned char parts[6 + 19 * 2];
    float xyz[6];
    CodXmodelBoneRule rule;
    int n;
    memset(parts, 0, sizeof(parts));
    put_u16(parts + 0, 14);
    put_u16(parts + 2, 2);
    put_u16(parts + 4, 1);
    /* bone 0 parent = 1 (higher index), translation (1,0,0), quat 0 */
    parts[6] = 1;
    put_f32(parts + 7, 1.f);
    /* bone 1 parent = 255, translation (0,0,10) */
    parts[6 + 19] = 255;
    put_f32(parts + 6 + 19 + 1 + 8, 10.f);
    rule.one_based = 1;
    rule.parent0_is_root = 0;
    cod_xmodel_set_bone_rule(rule);
    n = cod_xmodel_world_translations(parts, sizeof(parts), xyz, 2);
    expect(n == 2, "two bones");
    expect(xyz[3] == 0.f && xyz[4] == 0.f && xyz[5] == 10.f, "root stays");
    expect(xyz[0] == 1.f && xyz[1] == 0.f && xyz[2] == 10.f, "child adds parent once");
    /* Cycle: each parent points at the other. Both worlds are identity. */
    parts[6] = 1;
    parts[6 + 19] = 0;
    n = cod_xmodel_world_translations(parts, sizeof(parts), xyz, 2);
    expect(n == 2, "cycle count");
    expect(xyz[0] == 0.f && xyz[1] == 0.f && xyz[2] == 0.f, "cycle bone 0 identity");
    expect(xyz[3] == 0.f && xyz[4] == 0.f && xyz[5] == 0.f, "cycle bone 1 identity");
    /* Quat shorts outside the unit sphere: w becomes 0 and the child translation stays finite. */
    parts[6] = 1;
    parts[6 + 19] = 255;
    put_u16(parts + 6 + 19 + 13, 32767);
    put_u16(parts + 6 + 19 + 15, 32767);
    put_u16(parts + 6 + 19 + 17, 32767);
    put_f32(parts + 7, 1.f);
    put_f32(parts + 6 + 19 + 1 + 8, 0.f);
    n = cod_xmodel_world_translations(parts, sizeof(parts), xyz, 2);
    expect(n == 2, "oversize quat count");
    expect(xyz[0] > -100.f && xyz[0] < 100.f, "oversize quat stays finite");
    report("test_xmodel_bone_once", before);
}
```

Call it from `main`.

- [ ] **Step 2: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: FAIL to compile, `cod_xmodel_world_translations` undeclared.

- [ ] **Step 3: Implement the bake**

Keep a local `CoD1Bone` array and a separate world array. Read the 19-byte record as today: parent byte, three floats, three signed shorts. `w = sqrt(max(0, 1 - x*x - y*y - z*z))` with the short divided by 32767.

Parent resolution for bone `i`: byte 255, or byte equal to `i`, becomes -1. If `parent0_is_root` and the byte is 0, it becomes -1. Any other byte is a 0-based parent index.

Bake by walking dependencies, not by array index. A repeat of a bone already on the stack marks every bone on that stack as identity (`trans` 0, quat `0,0,0,1`) and does not multiply further. Otherwise apply from the root end: `world = parent.world × local` using the existing `quat_rot` then add translation, and `quat_mul(parent.q, local.q, world.q)`. Locals are never written.

`cod_xmodel_world_translations` returns those world translations. `cod_xmodel_set_bone_rule` stores the rule used by this function and, in Task 4, by the surf emitter. The file-scope default is `{1, 0}`.

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: `test_xmodel_bone_once: PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/cod/xmodel/read.h src/cod/xmodel/read.c tests/hxfs_cod_test.c
git commit -m "$(cat <<'EOF'
feat(cod): bake xmodel bone worlds once from local joints

EOF
)"
```

---

### Task 4: Apply the world matrix and keep a partial surf

**Files:**
- Modify: `src/cod/xmodel/read.c` (`emit_surfs_v14` rigid and skinned vertex loops, the `stored != num_tris` failure)
- Test: `tests/hxfs_cod_test.c`

**Interfaces:**
- Consumes: `cod_xmodel_set_bone_rule`, the world bake from Task 3, `usemtl` fallback from Task 2
- Produces: `cod_xmodel_to_obj` writes rigid vertices as `R * position + T` and normals as `R * normal`, using the active rule. Skinned vertices use the same world bones. A later broken mesh does not discard earlier meshes.

- [ ] **Step 1: Write the failing test**

Build a one-bone parts blob and a one-mesh rigid surf whose bone field is 1. Bone 0 translation is `(4, 5, 6)`, parent 255, identity quat. The vertex position in the surf is `(0, 0, 0)`. With rule `{1, 0}` the OBJ contains `v 4 5 6`.

Build the same parts and a surf whose bone field is 9. The OBJ vertex stays `v 0 0 0`.

Build a surf with `num_meshes = 2`. Mesh 0 is the valid triangle from `test_xmodel_v14_surf`. Mesh 1 is a single byte `0` and then the buffer ends. `cod_xmodel_to_obj` returns 1 and the OBJ still contains `v 0 1 2`.

Build a rigged mesh: bone field 65535, one vertex, `wc = 1`, weight bone 0, weight `1`, offset `(1, 0, 0)`, position ignored by the skinned path. Bone 0 world translation `(0, 0, 5)`. The OBJ contains `v 1 0 5`.

Use `put_u16` / `put_f32` and the vert layout already in `emit_surfs_v14`: after the strip, each rigid vert is `normal[3] uv[2] pos[3]` (32 bytes). A rigged vert inserts `u16 wc, u16 bone` before `pos`, then 4 pad bytes when `wc != 0`, and after all verts the weight run `u16 bone, f32 weight, f32 offset[3]`.

Call the test `test_xmodel_bind_apply` from `main`. It uses `fill_one_tri` from Task 2.

Short skin block: duplicate the one-triangle mesh into a buffer with `num_meshes = 2`, both meshes valid, and an xmodel tail of one name `metal@ford.dds`. Expect return 1, one `usemtl skins/metal@ford.png` before the first mesh's vertices, and a second `usemtl skins/metal@ford.png` before the second mesh. The second mesh must not be `skins/unbound.png`.

Empty surf: `num_meshes = 1` and the buffer ends after the mesh header (`num_verts = 0` is a failure, so set `num_verts = 3`, `num_tris = 1`, and omit the strip). Expect `cod_xmodel_to_obj` returns 0.

- [ ] **Step 2: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: FAIL because the current walk double-applies or the truncated second mesh returns 0.

- [ ] **Step 3: Implement apply and partial keep**

Before the mesh loop, bake world bones once. For a rigid mesh, index `bi = one_based ? bone - 1 : bone`. If `bi` is outside the bone array, `tx = ty = tz = 0` and there is no rotation. Otherwise rotate the position and the normal by that bone's world quat, then add the world translation.

For a skinned vertex, replace the current accumulation so each weight uses `world[bone_i]`, not a bone that was overwritten in place. An out-of-range weight bone contributes nothing. If the total weight is 0, leave the stored position.

On a mesh whose strips or verts run out, or whose stored triangle count does not match: free that mesh's temporary arrays. If an earlier mesh already wrote vertices, stop the mesh loop and return 1. If none did, return 0.

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: `test_xmodel_bind_apply: PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/cod/xmodel/read.c tests/hxfs_cod_test.c
git commit -m "$(cat <<'EOF'
feat(cod): place xmodel parts with one bone world matrix

EOF
)"
```

---

### Task 5: Emit the first named surf

**Files:**
- Modify: `src/cod/xmodel/helix.c` (`xmodel_to_helix`)
- Test: `tests/hxfs_cod_test.c`

**Interfaces:**
- Consumes: `cod_xmodel_lod_slots` from Task 1, `cod_xmodel_to_obj` from Task 4
- Produces: `xmodel_to_helix` writes the first named slot whose surf file converts. A missing surf file is skipped. `count_faces` is no longer the selector.

- [ ] **Step 1: Write the failing test**

```c
static void test_xmodel_first_lod(void) {
    int before = checkpoint();
    CodArchive* a = cod_archive_create();
    unsigned char header[96];
    unsigned char hi[20 + 96];
    unsigned char lo[20 + 96];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    size_t hoff = 0;
    size_t i;
    memset(header, 0, sizeof(header));
    memset(hi, 0, sizeof(hi));
    memset(lo, 0, sizeof(lo));
    put_u16(header + hoff, 14); hoff += 2; hoff += 24;
    put_f32(header + hoff, 1000.f); hoff += 4;
    memcpy(header + hoff, "hi0", 4); hoff += 4;
    put_f32(header + hoff, 0.f); hoff += 4;
    memcpy(header + hoff, "lo0", 4); hoff += 4;
    put_u16(hi, 14); put_u16(hi + 2, 1);
    hi[5] = 3; hi[7] = 1; hi[13] = 3;
    put_u16(hi + 16, 1); put_u16(hi + 18, 2);
    put_u16(lo, 14); put_u16(lo + 2, 1);
    lo[5] = 3; lo[7] = 1; lo[13] = 3;
    put_u16(lo + 16, 1); put_u16(lo + 18, 2);
    for (i = 0; i < 3; i++) {
        float p[3];
        p[0] = 10.f; p[1] = 0.f; p[2] = 0.f;
        memcpy(hi + 20 + i * 32 + 20, p, 12);
        p[0] = 99.f;
        memcpy(lo + 20 + i * 32 + 20, p, 12);
    }
    expect(cod_archive_add_blob(a, "xmodel/truck", header, (unsigned)hoff) == 1, "xm");
    expect(cod_archive_add_blob(a, "xmodelsurfs/lo0", lo, sizeof(lo)) == 1, "lo");
    expect(xmodel_to_helix(a, "xmodel/truck", &obj, &sz) == 1, "fallback lod");
    expect(obj && strstr((const char*)obj, "v 99 ") != NULL, "low surf when hi is missing");
    free(obj); obj = NULL;
    expect(cod_archive_add_blob(a, "xmodelsurfs/hi0", hi, sizeof(hi)) == 1, "hi");
    expect(xmodel_to_helix(a, "xmodel/truck", &obj, &sz) == 1, "near lod");
    expect(obj && strstr((const char*)obj, "v 10 ") != NULL, "first named surf");
    expect(obj && strstr((const char*)obj, "v 99 ") == NULL, "low surf not chosen by density");
    free(obj);
    obj = NULL;
    expect(xmodel_to_helix(a, "xmodel/missing", &obj, &sz) == 0, "unknown model fails");
    cod_archive_destroy(a);
    /* Header only, no surf blobs: conversion fails. */
    a = cod_archive_create();
    expect(cod_archive_add_blob(a, "xmodel/truck", header, (unsigned)hoff) == 1, "xm only");
    expect(xmodel_to_helix(a, "xmodel/truck", &obj, &sz) == 0, "no readable slot fails");
    free(obj);
    cod_archive_destroy(a);
    report("test_xmodel_first_lod", before);
}
```

- [ ] **Step 2: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: FAIL. The current loop prefers the surf with more faces, and both surfs have one face, so the last one written (`lo0`) wins once `hi0` exists. `v 10` is missing or `v 99` is present.

- [ ] **Step 3: Implement first-slot selection**

In `xmodel_to_helix`, after the text-export early return, call `cod_xmodel_lod_slots`. Walk the slots in order. Skip an empty name. Read `xmodelsurfs/<name>` and, when present, `xmodelparts/<name>`. The first `cod_xmodel_to_obj` that returns 1 is the OBJ. Do not compare face counts. `cod_xmodel_set_keep_hands` stays wrapped around this loop, true only when the asset name contains `viewmodel`. The view-basis rewrite stays limited to that same name check.

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: `test_xmodel_first_lod: PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/cod/xmodel/helix.c tests/hxfs_cod_test.c
git commit -m "$(cat <<'EOF'
feat(cod): emit the first named xmodel LOD surf

EOF
)"
```

---

### Task 6: Freeze the bone rule on retail vehicles

**Files:**
- Modify: `src/cod/xmodel/read.c` (the default `CodXmodelBoneRule` only, if the probe says so)
- Test: `tests/hxfs_cod_test.c`

**Interfaces:**
- Consumes: `cod_xmodel_set_bone_rule`, `cod_xmodel_bone_rule`, `cod_xmodel_world_translations`, `xmodel_to_helix`, `cod_xmodel_lod_slots`, `cod_xmodel_skin_names`
- Produces: the process-wide default rule that passes Peugeot and Flakpanzer, or a failing test that stops the plan

- [ ] **Step 1: Write the failing test**

Add `test_xmodel_retail_rule`. If `COD_MAIN` is unset, use `C:/Program Files (x86)/Steam/steamapps/common/Call of Duty/Main`. If that directory cannot be opened, print `test_xmodel_retail_rule: SKIP` and return without incrementing `g_fail`.

Otherwise `cod_archive_add_zip` on `pak0.pk3` inside that directory. Save the incoming default with `cod_xmodel_bone_rule`. Try these four rules in order:

```c
static const CodXmodelBoneRule k_rules[4] = {
    { 1, 0 },
    { 1, 1 },
    { 0, 0 },
    { 0, 1 }
};
```

For each rule, `cod_xmodel_set_bone_rule` then `xmodel_to_helix` on `xmodel/vehicle_peugeot_static` and `xmodel/vehicle_tank_flakpanzer`. A group is the `v` lines after one `usemtl`. The largest group has the most `v` lines. Its center is their average.

Raw Peugeot center: `cod_archive_read` the xmodel, the first named surf, and call `cod_xmodel_to_obj` with `parts = NULL`. That center is the unmoved hull. A ruled Peugeot passes when the largest-group center is within 30 of that raw center, and four other groups each sit within 30 of a different entry from `cod_xmodel_world_translations` on `xmodelparts/peugtuot1`, chosen from the four bones with the largest horizontal distance from the origin.

A ruled Flakpanzer passes when some group other than the largest has its center Z above the largest group's center Z, and its X and Y lie inside that group's min/max X and Y.

A rule passes only when both models pass. If the saved default passes, expect that and do not print `RULE`. If exactly one other rule passes, `fprintf(stderr, "RULE one_based=%d parent0_is_root=%d\n", ...)` and fail. If zero or more than one pass, fail with `bone rule ambiguous`.

With the saved default restored, also expect:

- `xmodel/static_vehicle_german_truck`: slot 0 from `cod_xmodel_lod_slots` is named `germmantruck0`. `cod_xmodel_skin_names` returns 55 and entry 0 is `metal@ford.dds`. The OBJ from `xmodel_to_helix` contains `usemtl skins/metal@ford.png`.
- `xmodelparts/Kubelwagen0` and `xmodelparts/flak88_anti-tank0`: `cod_xmodel_world_translations` returns 0.

Call `cod_xmodel_set_bone_rule` with the saved default before every return.

- [ ] **Step 2: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: PASS when `{1, 0}` meets the 30-unit checks. FAIL with one `RULE` line when another single candidate meets them. FAIL with `bone rule ambiguous` when the spec says to stop.

- [ ] **Step 3: Freeze a single alternative**

If Step 2 printed one `RULE` line, change the file-scope default in `read.c` to those two integers and change nothing else. If Step 2 passed, leave `{1, 0}`. If Step 2 reported ambiguous, stop this plan.

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: `test_xmodel_retail_rule: PASS` when `Main` exists, otherwise `SKIP`. Every earlier test still PASS.

- [ ] **Step 5: Commit**

```bash
git add src/cod/xmodel/read.c tests/hxfs_cod_test.c
git commit -m "$(cat <<'EOF'
test(cod): lock the xmodel bone rule on retail vehicles

EOF
)"
```

---

### Task 7: Bump the cache revision

**Files:**
- Modify: `src/cod/cache.h`
- Modify: `src/cod/cache.c`
- Test: `tests/hxfs_cod_test.c`

**Interfaces:**
- Consumes: `COD_CACHE_REV`
- Produces:

```c
unsigned cod_cache_rev(void);
```

- [ ] **Step 1: Write the failing test**

```c
static void test_cache_rev(void) {
    int before = checkpoint();
    expect(cod_cache_rev() == 15u, "cache rev 15");
    report("test_cache_rev", before);
}
```

Call it from `main`.

- [ ] **Step 2: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: FAIL to compile, or FAIL because the revision is 14.

- [ ] **Step 3: Implement**

In `cache.c`, set `COD_CACHE_REV` to `15u`. Add `unsigned cod_cache_rev(void) { return COD_CACHE_REV; }` and declare it in `cache.h`.

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target hxfs_cod_test && ./build/hxfs_cod_test.exe`

Expected: `test_cache_rev: PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/cod/cache.h src/cod/cache.c tests/hxfs_cod_test.c
git commit -m "$(cat <<'EOF'
fix(cod): invalidate cached xmodels after the bind-pose change

EOF
)"
```

---

### Task 8: Playtest the map props

**Files:**
- Modify: none

**Interfaces:**
- Consumes: the DLL built from Tasks 1–7, `COD_CACHE_REV` 15
- Produces: a written pass/fail note in the task log. No commit.

- [ ] **Step 1: Install the DLL and clear the cache**

Copy `build/hxfs_cod.dll` to the Helix `hxcod` directory that `gameinfo.txt` loads. Delete that directory's `cache` folder so revision 15 is not served an old OBJ.

- [ ] **Step 2: Check the four bound maps**

Launch Helix on each map. Pass means the body is on the ground, parts sit on the body, and each surface has its own image. Collision is checked by walking into the visible part: the block is where the part is seen.

- `mp_harbor`: `static_vehicle_german_truck` on the ground. Both Flak 88 props (`turret_flak88_static_antiairlow`, `turret_flak88_static_antitank`) are one assembly.
- `mp_dawnville`: `vehicle_tank_flakpanzer` pieces are on the hull, turret included.
- `mp_railyard`: `static_vehicle_tank_panzeriv` and `static_vehicle_tank_tiger_snow` turrets are on the roof.
- `mp_chateau`: `static_vehicle_german_kubelwagen`, `static_vehicle_german_staffcar`, `vehicle_peugeot_static`, and `static_vehicle_german_bmw_bike` wheels are at the axles.

- [ ] **Step 3: Stop or continue**

If the harbor truck is half buried while its entity angles are `0 0 0`, stop. Do not start the entity-axis spec in this plan.

If the truck sits on the ground, walk every other multiplayer map's `misc_model` props with the same pass rule. Record any prop that fails by map name and model path.

- [ ] **Step 4: Do not commit**

This task changes no files.
