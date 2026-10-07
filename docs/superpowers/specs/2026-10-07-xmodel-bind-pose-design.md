# Xmodel bind-pose

A pályára tett CoD1 xmodel egy közös szabállyal áll össze. A torony a tetőn van, a kerék a tengelynél, a test a talajon fekszik, minden felület a saját skinjét kapja, és az ütközés ugyanazon a hálón van, mint a kép. Nincs modellnév-tábla.

A munka a `src/cod` xmodel olvasójában van. A `src/cod2003` és a Helix motor változatlan. A Helix a `usemtl` csoportokat külön rajzolja, és ugyanabból a háromszögből ütközik. A 24 egységes vastagság, a fegyver, a viewmodel és a kéz kimarad.

## Felépítés

Négy egység, egy OBJ kimenet.

1. **Bind-pose** (`xmodel/read.c`). A lokális csont változatlan marad. A világmátrix ősenként egyszer áll össze. A rigid felület a saját csontját kapja. Csont nélküli modellhez nincs mátrix.
2. **Anyag** (`xmodel/read.c`). A kibocsátott surf anyagblokkja sorrendben kötődik a felületekhez. A konverzió anyaghiba miatt nem áll meg.
3. **LOD** (`xmodel/read.c`, `xmodel/helix.c`). Az olvasó minden slotot megtart: távolság és surf név. A `xmodel_to_helix` egy OBJ-t ad, az első megnevezett surfot. A Helix `ModelAssets::add_lod` a pályabetöltésen nincs bekötve. A runtime váltás későbbi spec. A „legtöbb háromszög” választás megszűnik.
4. **Helix.** Az ütközés a kijavított hálót követi.

Ellenőrző eset: a csont nélküli, nulla szögű harbor teherautó. Ha félig a földben áll, az entitás tengelye külön spec, és ez a munka ott megáll.

## Bind-pose

A csont rekordja: szülő bájt, eltolás, három shortból álló kvaternió. A hiányzó `w` a pozitív gyök. A `q` és a `-q` ugyanaz a forgás. Ha a három komponens nem fér el az egységgömbön, a `w` nulla. A lokális értékek nem íródnak felül.

A világmátrix függőség szerint áll össze, index szerint nem, mert a szülő indexe lehet nagyobb, mint a gyereké. Minden ős egyszer szerepel. A gyökér szülője 255, vagy a csont saját indexe. Minden más csont világmátrixa a szülő világmátrixa szorozva a saját lokálisával: először a forgás, utána az eltolás.

Négy jelölt van. A felület csontszáma `N` vagy a `parts[N-1]`, vagy a `parts[N]` csont. A szülő `0` vagy a nulladik csont, vagy gyökér. Minden modell ugyanazt a párt kapja.

A Peugeot és a Flakpanzer geometriai tesztje választ. Ha a mai olvasó (`parts[N-1]`, a szülő `0` a nulladik csont) teljesíti, az marad. Ha csak egy másik jelölt teljesíti, az a szabály, és az kerül a szintetikus tesztbe. Ha több jelölt teljesít a mai olvasón kívül, vagy egy sem, a spec megáll, és a hierarchiaszabályt újra kell írni, mielőtt más modellhez nyúlunk.

A teszt akkor teljesül, ha a legnagyobb felület középpontja a transzform után 30 egységen belül marad a nyers középpontjához képest, négy felület középpontja 30 egységen belül esik négy külön, vízszintesen szélső csonteltoláshoz, és van a legnagyobból különböző felület, amelynek a középpontja a test fölött van, a test vízszintes befoglalóján belül.

A rigid csúcs ezt az egy világmátrixot kapja. A normál csak a forgást. A skinnelt csúcs ugyanazokat a világcsontokat használja, súlyozott összeggel, egy menetben. Nulla csontnál a csúcs a surf nyers pozíciója.

## Anyag

A skin-nevek az xmodel végén vannak, LOD-blokkonként, felületi sorrendben. A teherautón a három surf hálószáma 21, 18 és 16, és a három blokk hossza ugyanez. A kibocsátott surf a saját blokkját használja. A felület `i` a blokk `i` nevét kapja.

Útvonal nélküli név `skins/<név>`. A kérés kiterjesztése `.png`. A plugin a meglévő `.dds` fájlt adja. A `@` a fájlnév része marad (`skins/metal@ford.png` kérés, `skins/metal@ford.dds` a pakkban). Ha a névben már van útvonal, csak a kiterjesztés normalizálódik.

Ha a blokk rövidebb, mint a felületek száma, a kimaradó felület is kiíródik, és a blokk utolsó nevét kapja. Ha egyetlen név sincs, a felület a `skins/unbound` nevet kapja. Ha a blokk hosszabb, a többlet név az olvasó LOD rekordján marad. Ez a rekord a reader API, nem egy új fájlformátum. A prop útvonal a fájl végén lévő LOD-blokkot olvassa. A teljes fájl képneveinek pásztázása itt nem fut. A kéz miatti átrendezés a viewmodel útvonalon marad, a prop útvonalon nem fut.

## LOD

A retail xmodel három slotot tárol: távolság és surf név. Az első megnevezett surf a legrészletesebb. A Flak 88 harmadik slotjának távolsága 0, és az a low LOD. Az olvasó mind a három slotot visszaadja, távolsággal együtt. Az OBJ az első megnevezett, olvasható surf. A későbbi LOD spec ugyanezt a bind-pose-t és ugyanezt az anyagvágást használja minden slotra.

## Hibakezelés

A parts fájl hiánya és a nulla csont ugyanaz az út: a csúcs változatlanul megy ki. A tömbön kívüli csontindex és a körbeérő szülőlánc identitásmátrixot kap, a felület megmarad.

Ha egy surf közepén elfogy a bájt, vagy a háromszögszám nem jön ki, az addig ép felületek az OBJ-ban maradnak. Ha egy felület sem ép, az xmodel konverziója sikertelen, és a Helix a modellt hiányzónak jelöli. Hiányzó LOD-surf esetén a következő megnevezett slot kerül sorra. Ha egy slot sem olvasható, a konverzió sikertelen.

Anyaghiba a modellt nem dobja el.

## Ellenőrzés

A `hxfs_cod_test` szintetikus buffereken fut. A csontindex szabálya azután fagy bele, hogy a Peugeot és a Flakpanzer geometriai tesztje lefutott.

A retail ellenőrzés a `COD_MAIN` környezeti változó könyvtárát olvassa. Ha a változó nincs beállítva, a `C:\Program Files (x86)\Steam\steamapps\common\Call of Duty\Main` könyvtárat. Ha egyik sem elérhető, a négy retail ellenőrzés kimarad. A szintetikus tesztek ettől még futnak. A retail ellenőrzés ezt olvassa:

- **Peugeot.** A legnagyobb felület középpontja 30 egységen belül marad. Négy felület középpontja 30 egységen belül esik négy külön, vízszintesen szélső csonteltoláshoz.
- **Flakpanzer.** Van a legnagyobból különböző felület, amelynek a középpontja a test fölött van, a test vízszintes befoglalóján belül.
- **Teherautó.** Az első slot a `germmantruck0`, 21 felület, az első anyag `skins/metal@ford.png`. A medium és a low slot távolsága és hálószáma (18 és 16) a LOD rekordon megvan. A kiírt OBJ az első slot.
- **Csont nélküli** Kübelwagen, teherautó és statikus Flak 88: a csúcsok a surf nyers pozíciói.

Rövid anyagblokk és csonka surf kész OBJ-t ad, a fenti szabály szerint. A `COD_CACHE_REV` nő.

Játékban, a cache ürítése után:

- `mp_harbor`: a teherautó a talajon van, a két Flak 88 egyben van, minden felületnek megvan a képe.
- `mp_dawnville`: a Flakpanzer darabjai a testen vannak.
- `mp_railyard`: a Panzer és a Tiger tornya a tetőn van.
- `mp_chateau`: a Kübelwagen, a staff car, a Peugeot és a motorkerékpár kereke a tengelynél van, a test a talajon fekszik.

Utána minden más multiplayer pálya `misc_model` propja ugyanezzel a mércével. Az ütközés a látható alkatrész helyén van. Ha a csont nélküli, nulla szögű teherautó félig a földben áll, a spec megáll.

## Kimarad

A viewmodel, a kéz, a fegyverlista és a GSC. A 24 egységes ütközővastagság. A runtime LOD és az `add_lod` bekötése. Az entitás-tengely javítása, ha a teherautó-ellenőrzés megállítja a specet.
