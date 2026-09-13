# Helix-Public

GPL-licensed **asset adapters** for the [Helix](https://github.com/thundr89/Helix) engine.

The engine core is proprietary. This public repository is where WAD / PAK / PK3 / PK4 (and later formats) live, as separate shared libraries (`.dll` / `.so`) that speak only the Helix `iasset_plugin.h` C ABI.

Missing adapter DLL: the engine warns, it does not crash.

```
helix --asset-plugin hxfs_wad.dll gfx.wad
```

or in `{game}/gameinfo.txt`:

```
asset_plugin "hxfs_wad.dll" "gfx.wad"
```

| Adapter | Archives | Status |
|---|---|---|
| `hxfs_wad` | Doom WAD1 / WAD2 / WAD3 | in tree |
| PAK | Quake 1 / 2 | not yet |
| PK3 | Quake 3 / Enemy Territory (zip) | not yet |
| PK4 | Doom 3 / Quake 4 | not yet |

## Build

```
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Produces `hxfs_wad.dll` (Windows) or `hxfs_wad.so` (Unix). Copy it next to `helix` (or into the game dir). The host searches `{exe_dir}`, `{exe_dir}/{game}`, `{game}`, `.`.

Lumps are exposed as lowercase virtual paths (`maps/e1m1/things`, `textures/conchars`, `flats/floor0_1`). `read()` returns raw lump bytes — no miptex→PNG conversion.

---

A Helix motormag zárt. Ez a nyilvános repó a **GPL adapterek** helye: WAD / PAK / PK3 / PK4 (és későbbi formátumok), külön `.dll` / `.so`, csak az `iasset_plugin.h` ABI-n keresztül.

A **WAD** adapter (`hxfs_wad`) bent van a fában. PAK / PK3 / PK4 még nincs.
