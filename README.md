# Helix-Public

GPL-licensed **asset adapters** for the [Helix](https://github.com/thundr89/Helix) engine.

The engine core is proprietary. This public repository is where WAD / PAK / PK3 / PK4 (and later formats) live, as separate shared libraries (`.dll` / `.so`) that speak only the Helix `iasset_plugin.h` C ABI.

Missing adapter DLL: the engine warns, it does not crash.

```
helix --asset-plugin hxfs_wad.dll gfx.wad
helix --asset-plugin hxfs_pak.dll pak0.pak
helix --asset-plugin hxfs_pk3.dll pak0.pk3
helix --asset-plugin hxfs_pk4.dll pak000.pk4
helix --asset-plugin hxfs_iwd.dll iw_00.iwd
helix --asset-plugin hxfs_ff.dll english.ff
```

or in `{game}/gameinfo.txt`:

```
asset_plugin "hxfs_wad.dll" "gfx.wad"
asset_plugin "hxfs_pak.dll" "pak0.pak"
asset_plugin "hxfs_pk3.dll" "pak0.pk3"
asset_plugin "hxfs_pk4.dll" "pak000.pk4"
asset_plugin "hxfs_iwd.dll" "iw_00.iwd"
asset_plugin "hxfs_ff.dll" "english.ff"
```

| Adapter | Archives | Status |
|---|---|---|
| `hxfs_wad` | Doom WAD1 / WAD2 / WAD3 | in tree |
| `hxfs_pak` | Quake 1 / 2 PAK | in tree |
| `hxfs_pk3` | Quake 3 / Enemy Territory PK3 (zip) | in tree |
| `hxfs_pk4` | Doom 3 / Quake 4 PK4 (zip) | in tree |
| `hxfs_iwd` | Call of Duty IWD (zip) | in tree |
| `hxfs_ff` | CoD FastFile (IW3 / WaW / IW4, PC + Xenon) | in tree |

## Build

```
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Needs zlib (`find_package(ZLIB)`). Produces `hxfs_wad.dll` / `hxfs_pak.dll` / `hxfs_pk3.dll` / `hxfs_pk4.dll` / `hxfs_iwd.dll` / `hxfs_ff.dll` (Windows) or `.so` (Unix). Copy next to `helix` (or into the game dir). The host searches `{exe_dir}`, `{exe_dir}/{game}`, `{game}`, `.`.

WAD lumps are lowercase virtual paths (`maps/e1m1/things`, `textures/conchars`). PAK/PK3/PK4/IWD keep archive paths (`maps/e1m1.bsp`, `images/loadscreen.iwi`), lowercased, `\\` → `/`. PK3, PK4 and IWD share a ZIP reader (store + deflate). FastFiles (`hxfs_ff`) pick a profile from magic + version (LE or BE): extra header size, zlib vs 64KiB raw-deflate blocks, IW3 12-byte vs IW4 16-byte rawfiles. Signed `IWffs100` wrappers are skipped (hashes/RSA not verified). Virtual files: `zone.bin`, `ff.profile`, plus scanned rawfiles (`.gsc`, `.cfg`, …). IW4 inner-zlib rawfiles are inflated on read. Later encrypted CoD FFs (IW5+) are not supported. `read()` returns raw (inflated) bytes.

---

A Helix motormag zárt. Ez a nyilvános repó a **GPL adapterek** helye: WAD / PAK / PK3 / PK4 (és későbbi formátumok), külön `.dll` / `.so`, csak az `iasset_plugin.h` ABI-n keresztül.

A **WAD**, **PAK**, **PK3**, **PK4**, **IWD** (`hxfs_iwd`) és **FF** (`hxfs_ff`, verziózott CoD FastFile: IW3/WaW/IW4 PC+Xenon) adapter bent van a fában. IW5+ titkosított FF nincs.
