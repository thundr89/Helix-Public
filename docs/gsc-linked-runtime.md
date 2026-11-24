# Opt-in closed-link GSC runtime foundation

The shipping `cod_sgame` default remains legacy. Select the new profile with
`set g_gsc_runtime linked` before map initialization, or use
`gsc_runtime_create_linked()` in `src/cod/gsc/linked.h`.
It reuses the strict frontend, value/object implementation and coroutine
scheduler. It does not claim a completed replacement interpreter or full CoD1
compatibility. The legacy compiler/runner remains available during transition.

## Preparation and execution

1. Register native function/method entries when creating the runtime. Names and
   registry records are copied. Callback/user lifetimes belong to the plugin.
2. Add all root sources with `gsc_runtime_add_source()`. Dependencies compile
   recursively through the plugin's file reader. No script thread starts here;
   dependency precache/animation declarations may reach existing setup hooks.
3. Validate required root functions with `gsc_runtime_require_entry()`, then call
   `gsc_runtime_link()`. Every loaded function is checked, including unused
   functions. Script calls/references resolve to function indices; external
   calls resolve to native slots; supported language intrinsics use integer
   dispatch operands. Unknown calls, missing dependencies, ambiguous imports,
   unsupported native references/thread forms and syntax errors prevent startup.
4. Start a prepared entry using `gsc_runtime_start(runtime,path,NULL,0,entry,self)`
   and drive `gsc_runtime_tick()`. Source loading is forbidden after sealing.

The registry is immutable. Function and method namespaces may have separate
bindings; overlapping duplicate names are rejected case-insensitively. External
bound calls invoke their registered callback directly, without the old
`host.native_call(name,...)` lookup or automatic legacy fallback. Rejected native
calls stop their thread with an error. Preparation/link failure prevents all
subsequent thread creation in that runtime; rebuild in a fresh runtime.

Dynamic local names, object fields, array keys, runtime values and script
function references remain supported. The linker does not enumerate gameplay
variables. `self`, `level`, `game`, `anim` and events retain existing runtime
semantics. In particular, events are scoped to their receiver object. This
profile adds no GSC-specific Helix engine import or ABI dependency.

Link diagnostics retain the module path, frontend emission line/column, symbol
and instruction index. The emission site can be the end of a multiline call;
exact identifier source spans and runtime source stack traces remain future work.

## Acceptance slice and remaining work

`gsc_linked_test` runs authored fixtures for dynamic fields/arrays, local and
included functions, script function references, native function/method slots,
copied registry ownership, waits and event payloads, object-scoped events,
`endon`, rejected natives and errors in unused code. Existing frontend, CoD
asset-plugin and sgame suites also run unchanged. No retail scripts are added.

The CoD1 adapter now has an explicit inventory of 128 native names plus seven
world-state/spawnpoint adapters in the GPL plugin. Function/method namespaces
are declared in `src/cod/gsc/cod1_natives.inc`. Bound entry points share existing
plugin implementations during migration; the plugin's internal handler branches
have not all been replaced with dedicated opcode-independent handlers. This is
not a claim that every registered operation is semantically complete.

`g_gsc_runtime` accepts `legacy` (or empty) and `linked`. Unknown selectors fail;
they do not fall back. `g_gsc_runtime_active` reports `legacy`, `linked` or
`failed`, while `g_gsc_linked` is 1 only after successful preparation. Link or
required-entry failures block startup and client acceptance. The linked path
does not use the legacy literal weapon-source scan. Both map and gametype roots
are prepared before either is scheduled; the optional startup wrapper is also
prepared and validated before sealing when installed.

Retained-map restarts reuse the sealed program. Changing map, gametype or
runtime profile creates a new program. Preservation of script `game` state
across a different map/program in linked mode remains future work; it must not
be inferred from retained-map restart acceptance.

Remaining work includes deeper semantic coverage and frame-time measurement.
Existing hardcoded builtins, native field access and internal
intrinsic handlers still use their current implementations. Native function
references and threaded native invocations need explicit language/compatibility
decisions and implementation; they currently fail linking. This is an initial
execution-contract milestone, not a declaration of a complete language.

The CoD2 [function reference](https://github.com/M-itch/codscriptdoc) is a guide
for native inventory. Its `stock` and `libcod` sections are distinct. Neither
CoD2 functionality nor extension functionality is automatically a CoD1
requirement; uncertain behavior needs targeted CoD1 verification.

## Headless acceptance

`cod_sgame_linked_test` repeats the sgame integration suite with an explicit
linked-mode selector and asserts the active/sealed profile. Additional fixtures
check that an unknown call in unused mod code and a missing map `main` prevent
all root execution, with no fallback to legacy. `gsc_linked_test` has 107 checks,
including dynamic entity-field lookup and a waiting coroutine observing shared
state created later by another thread.

The engine's `cod_lifecycle_probe` accepts `HELIX_PROBE_GSC_RUNTIME=linked` and
asserts the selected profile is actually active. Retail probes use the user's
installed scripts/assets, never redistributed fixtures. Windows headless probes
exercise three moving/combat bots, actual SD plant/defuse, stock lifecycle,
dedicated UDP and killcam. These are bounded acceptance scenarios, not proof of
all mod behavior, full HQ/RE objectives, or GPU performance.

The later all-modes matrix is described in the engine's
`docs/cod-gsc-all-modes.md` and driven by `tools/gsc_headless_acceptance.py`.
It adds all-mode bot joins/combat, controlled HQ capture/scoring/neutralization
and delivery of every RE objective through its real triggers. This extension
found and fixed localized announcement arguments, player-following objectives,
stock `stopLoopSound(alias)` and HQ's post-delete objective metadata updates.
HQ/RE controlled placement is not a claim of autonomous objective navigation.

The subsequent navigation extension adds separate autonomous HQ/RE probes.
Plugin-owned bot tasks read dynamic radio/item/trigger/carrier state from the VM;
the engine receives ordinary bot commands only. HQ attacks or guards the active
radio; RE attackers pick up items, carriers route to their matching delivery
trigger, and defenders guard objectives. Use input pulses for edge-triggered
pickup and is released while carrying. The collision graph starts at 20-unit
spacing (coarsens if its bounded node budget overflows), handles stairs, and
attaches grounded, swept endpoints for small targets. No engine ABI was added.
The new probes traverse retail Carentan without player placement; combat is
isolated by suppressing fire/holding the opposing phase, separately covered by
the combat matrix. This establishes these paths, not arbitrary-map navigation.
The expanded HQ combat path also exposed repeated HUD cleanup across bounded
scheduler slices. Both runtimes ignore `destroy()` on an already destroyed
object without touching a newly reused native handle; a regression covers both
execution paths. This does not change the scheduler into a fully cooperative VM.

Windows checkpoint, 2026-11-05:

- Five CTest suites passed: asset plugin, legacy sgame, linked sgame, strict
  frontend and linked runtime (`build/gsc-linked-integration-tests-final.log`).
- Eleven engine probes passed 138 assertions with zero failures: DM/TDM/BEL/HQ/
  RE/SD lifecycle, three-bot movement/combat, SD bot plant/defuse, bot menus,
  dedicated UDP and killcam. Every probe asserted `linked` was actually active.
- Engine logs/results are under the sibling engine's `build/`, named
  `gsc-linked-final-*.log` and `gsc-linked-final-results.json`. The tested module
  SHA-256 is `52d87cf8027e9a21dceb596b514bdef83f6e1afdab8eea591d378cc6b0c6ada3`.
- The bot movement probe observed all three bots leave spawn, fire, and an
  empty-clip reload. The SD probe completed both planting and defusing through
  the stock GSC path. No retail source was copied into the repository.
- The same rebuilt module also passed legacy DM/SD probes (14/20 assertions),
  with the requested legacy profile explicitly checked
  (`gsc-linked-legacy-{dm,sd}.log` in the engine build directory).

Implementation stays in GPL-2.0-or-later Helix-Public. HotCell `.hc` and the
closed Helix core are untouched. Shared C99/CMake paths support Windows and
Unix builds; Linux/Mint runtime execution remains unverified.

Navigation checkpoint (2026-11-05): the sibling engine's expanded matrix passed
all 25 cases / 214 assertions in each runtime profile (50/428 total), including
the autonomous HQ/RE paths. Reports are
`build/gsc-navigation-{linked,legacy}-verified/results.json` in the engine tree.
All five affected CTest suites passed (`build/gsc-navigation-final-tests.log`);
the runtime suite passed 134 checks. Both matrices used unchanged matching
binaries, sgame SHA-256
`edb412e410b344c08c5800a999234aa91edc06287d363d9d175c537400a0fb9b`.
