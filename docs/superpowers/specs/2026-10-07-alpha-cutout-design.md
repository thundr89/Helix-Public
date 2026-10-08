# Alfa vágás és keverés

A shader sora a törvény. Az `alphaFunc` vág, a `blendFunc` kever. Ha a shader hallgat, és a kép alfája lyukas, a motor a teljes felbontású képről vág. A levél, a drót és a shader nélküli kerékagy kivágódik. A keverésre írt poszter és üveg átlátszik.

A kód a Helix motorban van: `Engine::ensure_map_materials`, a `c:\Users\tothd\Desktop\idTech3-4\src\engine\modules.cpp` fájlban. A döntés a dekódolt RGBA-n fut, a `register_map_albedo` hívása előtt. Az atlasz zsugorítása marad: a célpixel a forrásnégyzet legnagyobb alfájú texelét kapja. A `src/cod` plugin kimenete változatlan, a `COD_CACHE_REV` nem nő. A motor újrafordítása kell.

## Szabály

A névheurisztika és a beolvasott shader előbb lefut. Utána a kép:

- Ha az anyag `blend` értéke nem `Opaque`, vagy az `alpha_test` nagyobb nullánál, a kép alfája nem írja felül sem a keverést, sem a küszöböt.
- Különben a texel alfa bájtja két csoport: 128 alatt, és legalább 128.
- Ha mindkét csoport legalább 64 texel, az `alpha_test` 0,5 lesz, a `two_sided` igaz, a `cull` pedig `None`.
- Ha valamelyik csoport 64 alatt marad, az `alpha_test`, a `two_sided` és a `cull` változatlan.

A döntés tiszta függvény. Bemenete a `blend`, az `alpha_test` és az RGBA bájtok. Kimenete a módosított `alpha_test`, `two_sided` és `cull`. A pályabetöltés ezt hívja. A teszt is ezt hívja, pálya nélkül.

Egy zajos DXT-pixel nem kapcsolja be a vágást. A teljesen üres kép nem tűnik el. A vékony drót, amelyből mindkét csoport megvan, kivágódik. A hiányzó kép a mai út: nincs vágási döntés.

A 0,5 küszöb a CoD `GE128` tesztje. A GPU a meglévő cutout úton dobja el a küszöb alatti pixelt.

## Ellenőrzés

A Helix teszt szintetikus RGBA-val hívja a döntést:

- Teli kép, shader nélkül: átlátszatlan, egyoldalas.
- Legalább 64 texel 128 alatt és legalább 64 legalább 128-on, shader nélkül: `alpha_test` 0,5, kétoldalas.
- Keverő anyag, lyukas képpel: a keverés marad, vágás nem kapcsolódik be.
- `alpha_test` már 0,004: a küszöb marad.
- Egyetlen 128 alatti texel: nincs vágás.
- Minden texel 128 alatt: nincs vágás.

Játékban, a motor újrafordítása után: a levél, a drót és a shader nélküli kerékagy kivágódik, a `blendFunc` posztere átlátszik.

## Kimarad

A modellkép felfeszítése. Két egymáson lévő réteg vibrálása. A víz UV-folytonossága. A plugin shader-olvasójának módosítása.
