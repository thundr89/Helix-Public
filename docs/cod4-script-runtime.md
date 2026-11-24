# CoD4 script runtime: current implementation boundary

The native CoD4 perk and FFA replacements in the Helix host were removed on
2026-11-15. GSC must execute the original gameplay decisions; parsing constants
from a script and reimplementing the function in C++ is not that execution path.
CoD4-specific asset decoding and script integration belong in Helix-Public.
The Helix host supplies generic capabilities through its public ABI.

Implemented language/runtime compatibility in the shared GPL frontend:

- CoD4 getDvar/getDvarInt/getDvarFloat/setDvar compile to the existing cvar
  services. Reads preserve the requested type, writes reach the host callback,
  and CoD1 getCvar names retain their behavior.
- Unused duplicate parameters in genuinely empty functions retain separate
  argument slots. The shipped maps/mp/_destructible.gsc contains such helpers.
  Nonempty bodies with duplicate parameter names remain rejected as ambiguous.
  No source bodies are skipped, rewritten or supplied by the probe.

`cod4_gsc_probe` is an optional developer executable linked only on the GPL side.
It opens the user's installation via hxfs_cod4 and reads the unchanged FF/IWD
assets. It does not bundle original scripts or game data. Examples:

```
cod4_gsc_probe <retail-root> maps/mp/gametypes/_class.gsc
cod4_gsc_probe <retail-root> maps/mp/gametypes/_class.gsc --run initPerkDvars
cod4_gsc_probe <retail-root> maps/mp/gametypes/dm.gsc --link
```

The explicit --run mode exercises the existing compatibility VM, loading real
script dependencies and failing on any unknown executed native. It registers no
fake-success native callback. This is a diagnostic, not a shipping CoD4 sgame.
The original initPerkDvars function completes and creates the original three
perk dvars itself (108 asset reads, zero missing assets, zero runtime errors).
The root file contains 27 compiled functions; its gameplay body is not replaced
by regex extraction. dm.gsc, _globallogic.gsc and _weapons.gsc also compile unchanged.

--link uses the strict closed-link profile with NO game-native registry. It is
expected to fail while reporting required bindings; its 2401 diagnostics for the
retail dm dependency corpus are call sites, not 2401 distinct missing operations
and not a measurement of all capabilities absent from the existing CoD1 module.
Preparation failure schedules no gameplay. The next integration must connect the
appropriate shared services and actual CoD4 native contracts, then execute the
original gametype/class lifecycle. Perk state/UI, weapon-native effects, callbacks,
FFA startup and multiplayer prediction/replication are not complete.

The engine retains an attributed damage callback needed by script consumers.
A generic regression checks callback interception and ordinary damage without any
CoD4 specialty names, percentages or loadout rules. Existing menus and font work
were preserved. Earlier native-rule tests were removed with their replacements;
their past pass counts must not be presented as implemented CoD4 gameplay.
