---
name: CoD1 látvány és ütközés
overview: "A pályán látott hibák egy marék konkrét okra mennek vissza: az atlasz eldobja a textúra alfáját, a rajzolt háló és az ecset egyszerre ütközik, a modell egyetlen anyagra esik, a fegyvernév mutatója pedig a regisztráció után érvénytelen."
todos:
  - id: atlas-alpha
    content: Atlasz alfa megőrzése és drótbarát zsugorítás (háló, lombozat, kerítés)
    status: completed
  - id: decal-water
    content: Decal keverés és kép, víz folytonos UV, két réteg ne z-fightoljon
    status: completed
  - id: collision
    content: Soup ne duplázza az ecsetet; macskakő szilárd; ablak guggolható; lépcső talajkövetés
    status: completed
  - id: xmodel-mats
    content: usemtl csoportok, kéz bind-pose, jobb LOD a járműn
    status: completed
  - id: hud-menu
    content: Stabil fegyvernév, ui_mp menü marad, loading aránytartó
    status: completed
  - id: verify
    content: Cache rev, tesztek, DLL-ek a build/hxcod mappába
    status: completed
isProject: false
---

# CoD1: háló, ütközés, modell, menü

A képek és a kód ugyanazt a néhány hibát mutatják. A `src/cod2003` pluginhoz nem nyúlunk. A cache rev-et emelni kell, különben a `build/hxcod/cache` a régi pályát adja vissza.

## 1. Filler szín a hálón és a lombozaton

A shader `alphaTest` értéke eljut a GPU-ig, de a pálya-atlasz bemásolásakor az alfa mindig 255 lesz:

```563:565:c:\Users\tothd\Desktop\idTech3-4\src\render\render_world.cpp
            albedo[static_cast<usize>(i)] = p[0];
            albedo[static_cast<usize>(i) + 1] = p[1];
            albedo[static_cast<usize>(i) + 2] = p[2];
            albedo[static_cast<usize>(i) + 3] = 255;
```

A drót és a levél RGB-je megmarad, a lyukak „filler” színe is, mert a teszt sosem dob el pixelt. A vékony kerítésnél a 256 pixeles nearest mintavétel a drótot ki is hagyja, ezért ott az átlátszóság megvan, a drót nincs.

- Az atlasz megőrzi az alfát.
- Zsugorításkor egy célpixel a forrás négyzetéből a legnagyobb alfájú texelt kapja, hogy a vékony drót ne tűnjön el.
- A kerítés és a lombozat marad two-sided, ahogy a shader már kéri.

## 2. Két textúra egymáson, fehér decal, ismétlődő víz

A decal helye fehér, mert a felület megvan, a kép nincs: a `.shader` mapja nem kötődik az anyaghoz, vagy a decal átlátszó RGB-je alfa nélkül fehér lap. A hatodik kép két, egymásra rajzolt, átlátszatlan soup (alap + decal / részlet), z-fighttal.

- `polygonOffset` vagy `decal` anyag: alfa-keverés és polygon offset, nem egy második tömör réteg.
- A decal `map` / `clampMap` útvonala ugyanúgy `.png` legyen, mint a falaké, és a plugin ki is szolgálja.
- Víz: a szomszédos lapok UV-ja most per-felület újraindul, ezért látszik a csempehatár. Víz anyagnál a UV a világ XZ-jéből jöjjön, egy folytonos ismétléssel. A `tcMod` görgetés továbbra is későbbi tétel.

## 3. Palló vége, lépcső, ablak, átesés

Az ecset nodraw és ütközik. A triangle soup emellett tömör, ha a content flag `0x1`. A palló végén és a lépcsőn ez a második prizma függőleges éle állítja meg a karaktert. A görbe macskakő nem `0x1` és nem törmelék, a pályán pedig van ecset, ezért a kód nonsolidnak jelöli: azon átesel.

- Ha a pályán van ecset, a strukturális soup ne ütközzön (a járást az ecset adja). Kivétel: törmelék, és olyan járható háló, amelynek a befoglalója nem metsz ecsetet (a macskakő boltozat).
- A content vizsgálat a dokumentált `0x10001` maszk legyen, ne csak `0x1`.
- Ablak: a kitört nyílást a fal-ecset vagy egy üveg/ablak soup tölti ki. Az ecset vágósíkjait ellenőrizni kell (az első hat oldal távolság, a többi síkindex). Az üveg és a kitört ablak hálója ne legyen tömör; a nyílás guggolva (40 egységes test) férjen be. Az álló test marad 56.
- Lépcső: az ecset-only ütközés után, ha a fokok még darabosak, a már földön álló játékos lefelé `kStepSize` (18) távolságig kövesse a talajt. A pmove-ot ezen kívül nem írjuk át.

## 4. Teherautó és kéz

Az OBJ betöltő minden háromszöget egy mesbbe rak, és csak az utolsó `usemtl` marad meg ([obj_loader.cpp](c:\Users\tothd\Desktop\idTech3-4\src\render\obj_loader.cpp) 70–74, [model_assets.hpp](c:\Users\tothd\Desktop\idTech3-4\include\helix\render\model_assets.hpp) 84). A kocsi minden alkatrésze egyetlen, rossz képet kap, az UV ezért „szét van húzva”. A viewmodel keze skinned (bone 65535), a kód a súlyt kihagyja, a rigid fegyver viszont csonthelyzetbe kerül: a kéz nincs a fegyveren. A `hand` bőrét a surf olvasó el is dobja, ha van másik kép.

- `usemtl` csoportonként külön példány, ugyanazzal a transzformmal. A viewmodel rajzolása végigmegy ezeken a csoportokon.
- A viewmodel megtartja a kéz-bőrt. A riggelt csúcsot a bind-pose csont (a tárolt súly) helyére tesszük, hogy a kéz a fegyveren legyen.
- LOD: a legtöbb háromszöget tartalmazó nevű surf, ne a legkisebb distance, ha az a ritka modell. Egy jármű fájlon ellenőrzendő a választás.

## 5. Fegyvernév, menü, loading

A HUD a `weapon_def().name` mutatót írja ki. A `std::vector` újrafoglalásakor ez a mutató elhal ([weapons.cpp](c:\Users\tothd\Desktop\idTech3-4\src\game\weapons.cpp) `push_stored`). Innen a katyvasz. Javítás: `reserve(kMaxWeapons)`, és minden beszúrás után az összes név- és hangmutató újraállítása. A `_mp` végződés ne jelenjen meg.

A menü azért érintetlen, mert a motor csak akkor hagyja meg a CoD menüt, ha van `main` nevű ([modules.cpp](c:\Users\tothd\Desktop\idTech3-4\src\engine\modules.cpp) 807–811). Ha az `ui_mp` menü más néven töltődik, vagy a konverzió nem ad `main`-t, a Helix `ui/menus.txt` lép a helyére.

- Ha az `ui_mp/menus.txt` töltött menüt, az maradjon a főmenü (a CoD saját nevén, és ha kell, `main` álnéven).
- A megnyíló CoD panelekbe kerüljenek a Helix tételek (pálya, beállítás), a CoD almenük szerkezete maradjon.
- A loading kép most fix 60%×42% doboz ([engine.cpp](c:\Users\tothd\Desktop\idTech3-4\src\engine\engine.cpp) 1483). Az ablakot aránytartóan töltse ki (cover, középre), a sáv és a felirat a kép alatt maradjon.

## Ellenőrzés

- `COD_CACHE_REV` emelése, `hxfs_cod` és a Helix tesztek.
- A lefordított `hxfs_cod.dll` és `cod_sgame.dll` a `build/hxcod` mappába kerül, a cache törlése után, különben a futó játék a régit mutatja.
