# GSC veremgép

A plugin `gsc_vm_exec` függvénye a forrást bájtkódra fordítja, és egy veremgéppel futtatja. A minta a Brenz gépe, ami az OpenCoDUO `VM_Execute` hívási szabályait követi. A `gsc_read`, a `.hxsc` kimenet és a `gsc_allied_body` marad. A `src/cod2003` változatlan.

## Mit futtat

Egy fájl saját függvényei futnak. A `script::func` hívás a `GscHost.read_file` hoston át betölti a másik scriptet, és annak a függvényét hívja. A pálya `maps/mp/<pálya>.gsc` és a gametype `maps/mp/gametypes/<mód>.gsc` `main` függvénye a mai `sgame` úton indul.

A hívás hely szerint köti a paramétereket. A hiányzó argumentum `undefined`. A felesleges argumentum elvész. A hívásmélység 32. Egy üzemanyag-korlát megállítja a végtelen ciklust. Ha egy ág hibával áll meg, a már összegyűjtött fegyverek, a csapatnevek, a gametype és az ambient megmaradnak.

## Értékek és vezérlés

Az értékek: `undefined`, egész, lebegő, szöveg, tömb, és mező. A mező a `level.gametype` és a `game["allies"]` formát fedi. A `spawnstruct` mezői is értékek.

A vezérlés: `if`/`else`, `while`, `for`, `switch` áteséssel, `return`, `break`, `continue`. A számolás, az összehasonlítás, a tömbindex és a helyi hívás a Brenz szabálya szerint megy. Az egész literál 32 biten körbefordul, ahogy a motor `sscanf` `%d` olvasása.

## Szál és idegen script

A `thread` nem ütemező. A hívás egyszer, sorban lefut. A `wait` és a `waittill` ott megállítja azt az ágat, a hívó pedig megy tovább. Egy `thread maps\mp\_load::main()` belsejében lévő `giveWeapon` így is lefut.

Entitás, `self` metódus és `getent` nincs. Ha a futás ilyen híváshoz ér, az az ág megáll. A hívó, ha nem ez az ág volt, folytatódik.

## Host

A host a mai `GscHost`: `give_weapon`, `take_weapons`, `ambient`, `set_team_names`, `set_gametype`, `read_file`. A literál `giveWeapon` pásztázás (`gsc_vm_scan_weapons`) megmarad tartaléknak, ha a `main` nem hívja közvetlenül.

## Ellenőrzés

A mai host-teszt zöld marad: ambient, gametype, és az utolsó `giveWeapon`. Új esetek: Fibonacci 10 az 55, egy tömbös `for` összege, egy idegen scriptből jövő fegyvernév, és egy `thread` ág, ami `wait`-nél megáll, miközben a hívó még rögzít egy fegyvert.

## Kimarad

A szálütemező, a `notify`, az entitások, a `getent`, és a játékbeli fegyverváltás. A `gsc_read` szövegolvasója nem lesz veremgép.
