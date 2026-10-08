# Nézeti kéz és MP44

Az első személyű kép a német kéz és egy MP44. A kéz a szülő, a puska a `tag_weapon` bind-csontján ül. A lövés, a hang és a harmadik személyű modell a most kiválasztott Helix-fegyver marad. A GSC később csak a puska mesht cseréli.

A plugin a `src/cod`. A `src/cod2003` változatlan. A Helix a két mesht rajzolja, és a tagmátrixot a szülőre szorozza. A kéz az idle első képkockája, plusz a csípő: a `viewmodel_mp44_ADS_up` 0. ideje, ami csak a `tag_torso`-t tolja el. A célzott póz ugyanannak a klipnek a vége, és még nincs gombra kötve.

## Mit szolgál a plugin

A `cod/viewhand.txt` akkor olvasható, ha az archívumban megvan az `xmodel/viewmodel_hands_whermact` és az `xmodel/viewmodel_mp44`. A név a kiskereskedelmi elírás. Ha bármelyik hiányzik, a fájl nincs a VFS-ben.

A szöveg három sor:

```
hands xmodel/viewmodel_hands_whermact
gun xmodel/viewmodel_mp44
tag Xx Xy Xz Yx Yy Yz Zx Zy Zz Tx Ty Tz
```

A tizenkét szám a Helix oszlopfolytonos 3×4-ese, ugyanabban a sorrendben, ahogy a `Mat4::from_axes` tölti: az X, az Y és a Z tengely, aztán az eltolás. A tér megegyezik a viewmodel OBJ csúcsaival. A plugin a `(x, y, z) → (-y, z, x)` cserét a csúcsokon és ezen a mátrixon is elvégzi. A Helix a hordót nem forgatja meg még egyszer.

A mátrix a kezek `xmodelparts` bind-pózából jön. A fájl eleje a mai olvasó szerint: `u16` verzió, `u16` gyerekszám, `u16` gyökérszám, majd a gyerekek 19 bájtos rekordjai. Utána pontosan `gyökérszám + gyerekszám` bejegyzés: NUL-zárt név, majd 24 bájt, amit a tag nem használ. A `tag_weapon` név indexe választja ki a már megsütött világcsontot. A 24 bájt nem póz.

A csere a merev transzformon fut. `P(x, y, z) = (-y, z, x)`. Az eltolás `P t`. A forgatás `P R P⁻¹`. `P⁻¹(a, b, c) = (c, -a, b)`. A kiírt tengelyek ennek a mátrixnak az oszlopai.

A két xmodel a meglévő OBJ-úton jön ki, viewmodel-tengelycserével. A `cod/viewhand.txt` számított szöveg, nem archívumfájl és nem pályagyorsítótár. A kéz OBJ-ja a `COD_CACHE_REV`-en keresztül érvényteleníthető.

## Mit rajzol a motor

A motor induláskor beolvassa a `cod/viewhand.txt`-t. Mindkét xmodel minden `usemtl` csoportja külön mesh. A betöltés nem hívja az `orient_barrel_to_z` függvényt. A csoportnevek ugyanazon az albedó-sütésen mennek át, mint a viewmodel-csoportok.

Ha a szöveg, a tag tizenkét száma, a kéz vagy a puska hiányzik, az első személy a mai Helix nézetfegyvert rajzolja. Fél pár nincs: kéz puska nélkül, vagy puska kéz nélkül nem jelenik meg.

Ha a pár megvan és a játékos él, a készlet nézetfegyver helyett ez a pár megy ki. A kéz szülője a szem bázisa. A csípőeltolás a megsütött `tag_torso`, nem a szülő. Ez nem a készletfegyver 7,4 / 4,25 / −2,75 eltolása, és a 0,46-os méret nem kerül rá. A rúgás `k` és az újratöltés aránya `ru` a hívótól jön. A fázis `bob`. A nézetsík mérete `s = 1.589`. A szülő helye:

```
eye
+ forward * (-k * 0.52 - ru * 2.4)
+ right * s * (4 + sin(bob) * 0.32)
+ up * s * (-3 + cos(bob * 2) * 0.14 + k * 0.22 - ru * 4.5)
```

A jobb és a fel tengely is `s`-sel szorzódik, az előre tengely 1 marad. A puska mátrixa a szülő szorozva a taggel, minden puskacsoportra ugyanaz. A bókolás és a rúgás így a markolatot nem nyitja szét.

A kéz meshe a szem előtt 8 egységnél le van vágva. A váll a szem mellett ül, és 90 fokos függőleges látószögnél a kar a képernyő alsó élén metszi a z = 8 síkot. A puska nincs vágva.

## Ellenőrzés

A tengelycsere tiszta függvény. Bemenete a CoD-térbeli forgatás és eltolás, kimenete a tizenkét szám. A nézetfegyver tengelye `AnglesToAxisNegRight`, ezért a modell +Y a -jobb irány. A leképezés `P(x, y, z) = (-y, z, x)`. Egy eset: 90 fok a CoD Z körül, `(x, y)` helyett `(-y, x)`, eltolás `(1, 2, 3)`. A kiírt sor `0 0 1 0 1 0 -1 0 0 -2 3 1`.

A parts-olvasó tesztje archívum nélkül fut. Egy gyökér, identitás, és egy `tag_weapon` gyerek, helyi eltolás `(4, 5, 6)`, nulla kvaternió-rövid. A név után 24 nemnulla bájt áll. A kiválasztott csont a gyerek, a kiírt eltolás `(-5, 6, 4)`, a 3×3 identitás. A 24 bájt nem része a póznak.

A motor tesztje egy helyi pontot a `szülő * tag` szorzaton visz át. Identitás szülőnél a puska origója a tag eltolása. Eltolt szülőnél ugyanez a pont a szülő által transzformált tag-eltolás. A CoD a kezet a szemhez teszi, a `cg_gunX/Y/Z` alapértéke 0, a `cg_fov` 80 fok vízszintesen. 4:3-on ez 64,4 fok függőlegesen. A Helix világ látószöge 90 fok függőlegesen marad, a nézetsík 1,589-szeres, hogy a puska ugyanakkora legyen a képen. A nyugalmi eltolás ezen felül 4 jobbra és 3 lefelé, plusz a 0,14-es bókolás. A puska a saját hossztengelye mentén még 10 egységgel előrébb ül, mert a megsütött kéz a csövön van, a pisztolymarkolat mögötte.

Játékban, a plugin és a `helix.exe` újrafordítása után: első betöltéskor német kéz fogja az MP44-et. A lövés a Helix fegyveré marad. Ha a két xmodel nem olvasható, a régi nézetfegyver látszik.

## Kimarad

Az idle és a tüzelő xanim. A fegyverváltás és a GSC VM. A nemzet vagy a pálya szerinti kézcsere. A harmadik személyű fegyver. A készlet nézetfegyver transzformjának módosítása, amikor a viewhand pár nincs betöltve.
