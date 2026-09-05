# Helix-Public

GPL-licensed **asset adapters** for the [Helix](https://github.com/thundr89/Helix) engine.

The engine core is proprietary. This public repository is where WAD / PAK / PK3 / PK4 (and later formats) live, as separate shared libraries (`.dll` / `.so`) that speak only the Helix `iasset_plugin.h` C ABI.

Missing adapter DLL: the engine warns, it does not crash.

```
helix --asset-plugin hxfs_pk3.dll pak0.pk3
```

or in `{game}/gameinfo.txt`:

```
asset_plugin "hxfs_pk3.dll" "pak0.pk3"
```

| Adapter | Archives |
|---|---|
| WAD | Doom WAD1 / WAD2 / WAD3 |
| PAK | Quake 1 / 2 |
| PK3 | Quake 3 / Enemy Territory (zip) |
| PK4 | Doom 3 / Quake 4 |

Decoder source is not in the tree yet.

---

A Helix motormag zárt. Ez a nyilvános repó a **GPL adapterek** helye: WAD / PAK / PK3 / PK4 (és későbbi formátumok), külön `.dll` / `.so`, csak az `iasset_plugin.h` ABI-n keresztül. A decoder forrás még nincs a fában.
