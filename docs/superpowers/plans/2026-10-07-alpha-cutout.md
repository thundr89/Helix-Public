# Alpha Cutout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a shader is silent and the image alpha is punched, the Helix material cuts at 0.5 and draws both sides.

**Architecture:** A pure function reads the full-resolution RGBA and returns `alpha_test`, `two_sided`, and `cull`. `Engine::ensure_map_materials` calls it after the image is decoded and before `register_map_albedo`. Shader blend and an existing alpha-test threshold are left as they arrived.

**Tech Stack:** C++17, the Helix executable in `c:\Users\tothd\Desktop\idTech3-4`, MinGW, `helix --test`.

## Global Constraints

- The code lives in `c:\Users\tothd\Desktop\idTech3-4`. Commits for these tasks go in that repository. Do not change `Helix-Public` `src/cod`, and do not bump `COD_CACHE_REV`.
- The decision runs on the decoded RGBA, before `register_map_albedo`. Do not change `blit_map_albedo`.
- If `blend` is not `Opaque`, or `alpha_test` is greater than 0, the image alpha does not change blend, threshold, `two_sided`, or `cull`.
- Otherwise count alpha bytes below 128 and alpha bytes at least 128. Both counts must be at least 64.
- When both counts pass: `alpha_test` is 0.5, `two_sided` is true, `cull` is `None`.
- When either count is below 64: `alpha_test`, `two_sided`, and `cull` stay as passed in.
- A missing image does not call the decision. The current bake return stays.
- Out of scope: model UV placement, two stacked layers fighting, water UV continuity, and the plugin shader reader.

---

### Task 1: Decide the cut from image alpha

**Files:**
- Modify: `c:\Users\tothd\Desktop\idTech3-4\include\helix\render\renderer.hpp` (declare next to `material_albedo_file`)
- Modify: `c:\Users\tothd\Desktop\idTech3-4\src\render\render_world.cpp` (define next to `material_albedo_file`'s callers; the function itself can sit above `register_map_albedo`)
- Test: `c:\Users\tothd\Desktop\idTech3-4\src\tests.cpp` (new `HX_TEST` after `HX_TEST(Material, ParsePbrAndBrdf)`)

**Interfaces:**
- Consumes: `BlendMode`, `CullMode` from `include/helix/render/material.hpp`
- Produces:

```cpp
struct AlphaCut {
    float alpha_test = 0.f;
    bool two_sided = false;
    CullMode cull = CullMode::Back;
};

/* Shader flags win. A silent material cuts only when at least 64 texels
 * are below 128 and at least 64 are at or above 128. */
AlphaCut alpha_cut_from_image(BlendMode blend, float alpha_test, bool two_sided, CullMode cull,
                              const u8* rgba, int texel_count);
```

- [ ] **Step 1: Write the failing test**

`src/tests.cpp` already includes `helix/render/renderer.hpp`. Add this test after `HX_TEST(Material, ParsePbrAndBrdf)`.

```cpp
HX_TEST(Material, SilentAlphaCut) {
    auto fill = [](std::vector<hx::u8>& rgba, int texels, hx::u8 alpha, int punch) {
        rgba.assign(static_cast<hx::usize>(texels) * 4, 255);
        for (int i = 0; i < punch && i < texels; ++i) rgba[static_cast<hx::usize>(i) * 4 + 3] = alpha;
    };
    std::vector<hx::u8> rgba;
    fill(rgba, 128, 255, 0);
    hx::AlphaCut solid = hx::alpha_cut_from_image(hx::BlendMode::Opaque, 0.f, false, hx::CullMode::Back,
                                                  rgba.data(), 128);
    HX_CHECK(solid.alpha_test == 0.f);
    HX_CHECK(solid.two_sided == false);
    HX_CHECK(solid.cull == hx::CullMode::Back);

    fill(rgba, 128, 0, 64);
    hx::AlphaCut cut = hx::alpha_cut_from_image(hx::BlendMode::Opaque, 0.f, false, hx::CullMode::Back,
                                                rgba.data(), 128);
    HX_CHECK(cut.alpha_test > 0.49f && cut.alpha_test < 0.51f);
    HX_CHECK(cut.two_sided == true);
    HX_CHECK(cut.cull == hx::CullMode::None);

    hx::AlphaCut blended = hx::alpha_cut_from_image(hx::BlendMode::Alpha, 0.f, false, hx::CullMode::Back,
                                                    rgba.data(), 128);
    HX_CHECK(blended.alpha_test == 0.f);
    HX_CHECK(blended.two_sided == false);
    HX_CHECK(blended.cull == hx::CullMode::Back);

    hx::AlphaCut kept = hx::alpha_cut_from_image(hx::BlendMode::Opaque, 0.004f, false, hx::CullMode::Back,
                                                 rgba.data(), 128);
    HX_CHECK(kept.alpha_test > 0.003f && kept.alpha_test < 0.005f);
    HX_CHECK(kept.two_sided == false);

    fill(rgba, 65, 0, 1);
    hx::AlphaCut one = hx::alpha_cut_from_image(hx::BlendMode::Opaque, 0.f, true, hx::CullMode::None,
                                                rgba.data(), 65);
    HX_CHECK(one.alpha_test == 0.f);
    HX_CHECK(one.two_sided == true);
    HX_CHECK(one.cull == hx::CullMode::None);

    fill(rgba, 128, 0, 128);
    hx::AlphaCut empty = hx::alpha_cut_from_image(hx::BlendMode::Opaque, 0.f, true, hx::CullMode::None,
                                                  rgba.data(), 128);
    HX_CHECK(empty.alpha_test == 0.f);
    HX_CHECK(empty.two_sided == true);
    HX_CHECK(empty.cull == hx::CullMode::None);
}
```

- [ ] **Step 2: Run the test to verify it fails**

From `c:\Users\tothd\Desktop\idTech3-4`, with `export PATH="/c/msys64/mingw64/bin:$PATH"`. If `build` has no CMake cache, configure once with `cmake -S . -B build -G "MinGW Makefiles"` before the build.

Run: `cmake --build build --target helix`

Expected: compile FAIL, `alpha_cut_from_image` is not a member of `hx`.

- [ ] **Step 3: Write the minimal implementation**

In `include/helix/render/renderer.hpp`, immediately after `material_albedo_file`:

```cpp
struct AlphaCut {
    float alpha_test = 0.f;
    bool two_sided = false;
    CullMode cull = CullMode::Back;
};

AlphaCut alpha_cut_from_image(BlendMode blend, float alpha_test, bool two_sided, CullMode cull,
                              const u8* rgba, int texel_count);
```

In `src/render/render_world.cpp`, before `register_map_albedo`:

```cpp
AlphaCut alpha_cut_from_image(BlendMode blend, float alpha_test, bool two_sided, CullMode cull,
                              const u8* rgba, int texel_count) {
    AlphaCut out;
    out.alpha_test = alpha_test;
    out.two_sided = two_sided;
    out.cull = cull;
    if (blend != BlendMode::Opaque || alpha_test > 0.f || !rgba || texel_count <= 0) return out;
    int below = 0;
    int at_least = 0;
    for (int i = 0; i < texel_count; ++i) {
        const u8 a = rgba[static_cast<usize>(i) * 4u + 3u];
        if (a < 128) ++below;
        else ++at_least;
        if (below >= 64 && at_least >= 64) break;
    }
    if (below >= 64 && at_least >= 64) {
        out.alpha_test = 0.5f;
        out.two_sided = true;
        out.cull = CullMode::None;
    }
    return out;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target helix && ./build/helix.exe --test`

Expected: the line `  - SilentAlphaCut` is followed by `    ok`. If some other case fails, leave it; this task is done when `SilentAlphaCut` is ok.

- [ ] **Step 5: Commit**

In `c:\Users\tothd\Desktop\idTech3-4` only:

```bash
git add include/helix/render/renderer.hpp src/render/render_world.cpp src/tests.cpp
git commit -m "$(cat <<'EOF'
feat: cut silent materials whose image alpha is punched

EOF
)"
```

---

### Task 2: Apply the cut when a map image is baked

**Files:**
- Modify: `c:\Users\tothd\Desktop\idTech3-4\src\engine\modules.cpp` (`Engine::ensure_map_materials`, the `bake` lambda, after the RGBA vector is filled and before `register_map_albedo`)
- Test: `c:\Users\tothd\Desktop\idTech3-4\src\tests.cpp` (the Task 1 test, re-run)

**Interfaces:**
- Consumes: `hx::alpha_cut_from_image` from Task 1
- Produces: map materials with silent punched images get `alpha_test == 0.5`, `two_sided == true`, `cull == CullMode::None` before the atlas tile is registered

- [ ] **Step 1: Write the failing observation**

There is no separate test. The bake does not call `alpha_cut_from_image` yet. Confirm with a search that `modules.cpp` has no `alpha_cut_from_image`. That absence is the red state.

- [ ] **Step 2: Call the function on the decoded image**

`modules.cpp` already sees `Material` through the engine headers. After the loop that fills `rgba` and before `register_map_albedo`:

```cpp
        const AlphaCut cut = alpha_cut_from_image(live->blend, live->alpha_test, live->two_sided, live->cull,
                                                  rgba.data(), img.w * img.h);
        live->alpha_test = cut.alpha_test;
        live->two_sided = cut.two_sided;
        live->cull = cut.cull;
```

Do not move the name heuristic. It still runs before the file is read, so a shader threshold and a decal blend are already on `live` when this call happens.

- [ ] **Step 3: Run the test to verify it passes**

Run: `cmake --build build --target helix && ./build/helix.exe --test`

Expected: `SilentAlphaCut` prints `ok`. The build of `modules.cpp` succeeds.

- [ ] **Step 4: Commit**

In `c:\Users\tothd\Desktop\idTech3-4` only:

```bash
git add src/engine/modules.cpp
git commit -m "$(cat <<'EOF'
feat: apply silent image alpha cut while baking map materials

EOF
)"
```

---

### Task 3: Playtest the cut and the blend

**Files:**
- None

**Interfaces:**
- Consumes: the rebuilt Helix executable from Task 2
- Produces: no commit

- [ ] **Step 1: Rebuild and start a map**

Run: `cmake --build build --target helix`

Load a map that has foliage, a wire fence, a masked wheel hub, and a blended poster. The plugin DLL already serves the images. No cache revision bump.

- [ ] **Step 2: Look**

The leaf, the wire, and the shader-less wheel hub are cut out on both sides. The poster whose shader says `blendFunc` stays translucent. A solid wall stays solid. Report anything that vanishes entirely or stays a full card.
