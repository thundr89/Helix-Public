# CoD4 multiplayer import

The adapter reads the PC IWffu100/version 5 zlib-compressed, typed asset stream.
It does not treat a fastfile as a ZIP directory. The world reader consumes child
assets in serialization order before GfxWorld and ClipMap. Light definitions,
FX, animation tracks, weapon definitions, raw files and sound aliases now have
bounded readers; reading these assets does not execute their gameplay/effects.
Collision patch/terrain triangles are exported alongside brushes, selecting
player-solid partitions through leaf AABB material contents and reversing IW3
clockwise triangle winding. Without these triangles, players fell through
Backlot courtyard floors despite successful brush conversion.

## Verification

Windows: all 22 installed stock MP zones, including mp_test and mp_crash_snow,
pass complete FF-to-HXMAP conversion. This is an export check, not a claim that
every map has been visually inspected or that CoD4 gameplay is implemented.
The large generated text maps (up to about 881 MB in this installation) still
have a substantial load/memory cost.

Windows and Linux: cod4_ffmap_regression_test exercises child-array bounds,
truncated assets, FX count overflow and the existing geometry/material tests.
Linux ASan/UBSan also passed real mp_backlot conversion and ui_mp layout export.
Windows Vulkan: the native frontend and three-page map catalog were checked
interactively, and Backlot was launched from the map selector. A headless
menu-to-map lifecycle probe verified stable ground contact after 240 frames
(initial Z=112, settled Z=91.61, ground normal Z=0.999347). Backlot also passed
a 180-frame Vulkan render check. Seven material images were still unresolved
on that map; successful import does not mean full material/lighting fidelity.
Linux graphical execution was not tested in this change.

Optional local retail checks (game assets are not included):

    cod4_ffmap_regression_test --ff /path/to/mp_backlot.ff
    cod4_ffmap_regression_test --ui /path/to/ui_mp.ff /tmp/layout.hxmenu

The optional final argument writes the converted output. Normal CTest uses
synthetic data and does not require a retail installation.

## Menus

The engine-side cod4/ui_mp frontend uses Helix commands and its dynamic map
catalog. The launcher opens the frontend unless an explicit --map is supplied.
Only native Deathmatch is currently supported; CoD4 GSC/gameplay is not loaded.

cod4_ff_to_menu_layout is a diagnostic compiled MenuList reader. It traverses
all 113 assets in the installed ui_mp.ff, including nested menus, items,
expressions, key handlers and type-specific widget data. Its output is NOT
loaded as the live frontend: reused string aliases, localization, materials,
visibility expressions and IW3 UIScript still need translation. Placeholder
buttons in the diagnostic output intentionally have no actions. The current
live frontend is a separate native Helix menu, not the original CoD4 interface.

Serialized field layouts were checked against the public IW3 schemas in
[OpenAssetTools](https://github.com/Laupetin/OpenAssetTools/tree/main/src/ZoneCode/Game/IW3/XAssets)
and its [IW3 asset declarations](https://github.com/Laupetin/OpenAssetTools/blob/main/src/Common/Game/IW3/IW3_Assets.h).
The implementation uses byte reads rather than casting host structs, retaining
32-bit on-disk pointer/field widths on 64-bit Windows and Linux.

## MaterialMemory and player clips

World surface aliases refer to GfxWorld MaterialMemory slots. The importer now
retains the names in that table instead of indexing backwards from the last
material seen before surfaces. Sun/effect materials serialized after the table
shifted every world texture on affected maps (143/143 entries on Crossfire).
All 22 installed MP maps have a complete, nonempty retained table and the
surface pointer span matches its entry count. The existing world-layer render
policy is retained: a camera-pass/culling/vertex-color experiment regressed
Killhouse decals and was removed. That shader-state translation remains open.

Playerclip-only brushes are now exported invisibly with Helix playerclip
contents, rather than discarded for lacking CONTENTS_SOLID. The arbitrary
6000x6000x4000 brush size cutoff was raised to allow large perimeter clips.
A synthetic regression includes a 9000-unit playerclip brush.

Windows checks cover material-slot stability against unrelated later materials,
playerclip export, plugin fixtures, and Crossfire/Carentan render captures.
Linux ASan/UBSan synthetic tests and real Backlot export were also run. This
change does not establish a fix for the reported Carentan leaf appearance or
intermittent grass darkening; these still require renderer/shader investigation.

## Shared world-image relocations

The MaterialMemory reader now records typed VIRTUAL allocations for material
names, texture/constant/state tables and image names. Asset headers and image
load definitions live in TEMP, but a -2 reusable loadDef still allocates an
inserted 4-byte pointer in VIRTUAL. Missing these slots shifted later aliases.
Constants retain 16-byte alignment and the stream is resolved from the observed
MaterialMemory base. Unsupported nested technique loading disables this scoped
resolver rather than guessing offsets.

The regression fixture verifies that a TEMP loadDef insertion advances the next
material's texture pointer from the formerly guessed position, and that a
following material can share that image by address. No material-name-specific
texture substitutions were introduced. A 22-stock-map audit found no conflicting
image names assigned to the same resolved address. Windows plugin/parser tests
passed; Linux ASan/UBSan tests and real Backlot export passed as well.

Carentan runtime image misses dropped from 10 to 3. Shared images now bind on the
green/red/yellow brick variants, opaque sanded glass, second awning, outer fish
tank and hotel sign. Remaining misses are ,default, ,mc/lambert1 and the model
material mc/mtl_sign_atm2. The latter is a model-local image relocation outside
the current MaterialMemory scope. The two comma-prefixed references need their
external/default material definitions rather than guessed image filenames.

The river-birch appearance was separately fixed in the engine's material
inference; see the engine repository's docs/cod4-vulkan-texture-upload.md.

## Menu integration: shared string references

The compiled UI layout reader now tracks absolute VIRTUAL allocations in the
ui_mp zone, including item/widget/key/expression arrays, and resolves reused
XString references by their actual serialized allocation. Invalid references
are rejected rather than silently becoming empty labels. TEMP asset headers
remain separate from this cursor. The ui_mp export still walks 290 menus and
17,221 items; nonempty static text entries increased from 101 to 315. Dynamic
text expressions, materials and executable UI actions still need their Helix
adaptation; this diagnostic export is not yet the live CoD4 menu.

Windows synthetic/plugin tests and the real ui_mp export pass. Linux ASan/UBSan
synthetic tests and the real ui_mp export pass. No Linux graphical UI validation
is implied by these decoder checks.

## Menu integration: constant text expressions

The reader also extracts literal textExp statements consisting of IW3's leading
OP_LEFTPAREN sentinel and one string operand. The literal replaces the item's
static text, including an explicitly empty literal. Functions, concatenations,
unresolved shared expression entries and other dynamic statements are not folded.
Localization keys remain intact for the later localization stage.

The retail ui_mp export still contains 290 menus and 17,221 items, now with 2,176
nonempty text entries (previously 315), producing 1,889,535 bytes. Regression
coverage checks literal extraction, rejection of a function as a literal, and
truncated inline strings. Windows build/synthetic/retail export and Linux
ASan/UBSan synthetic/retail export pass. These are decoder checks; the diagnostic
layout is still not installed as the live frontend.

## Typed localization export

cod4_ff_to_localization walks the localized FF asset stream and exports original
LocalizeEntry key/value pairs. The walker handles preceding Font assets and their
material/glyph children, plus sound aliases, materials, techniques and raw files.
Inserted font material pointers reserve their VIRTUAL slots even though material
headers are in TEMP; this is necessary for reused values such as the June label.
Unresolvable localization references fail the export rather than inventing text.

The retail localized_code_post_gfx_mp.ff exports all 3,593 entries (177,464 bytes),
including MENU_JOIN_GAME = Join Game and MENU_START_NEW_SERVER = Start New Server.
The regression runner accepts --loc in addition to --ui and --ff. Windows build,
synthetic tests and retail localization export pass. Linux ASan/UBSan synthetic,
retail localization and retail menu exports pass. This API supplies original text;
loading it into the live frontend remains part of menu integration.

## Localized menu and native action bridge

cod4_ff_to_localized_menu_layout imports the MenuList using a per-call dictionary
from localized_code_post_gfx_mp.ff. Dictionary strings own their storage across
zone teardown; missing keys remain explicit and empty localized values stay empty.
The retail layout contains 2,188 nonempty labels with 20 unresolved localization
keys after this stage. Additional localized zones may supply the missing keys.

The action translator handles original quoted command names, escaped strings,
semicolons inside arguments, and whole nested statements. Native open, close,
play, exec, show/hide and setdvar/setcvar equivalents are emitted. Close self is
resolved to the owning menu. Menu onOpen/onClose/onESC and item action/onFocus/
onLeave bindings are preserved. Unsupported IW3 commands, including uiScript,
are skipped whole; they are not implemented by this subset.

The asset plugin exposes this original import as ui_mp/cod4_original.hxmenu when
both source zones are present. It reads the typed FF importer directly rather
than the legacy RawFile scanner. The current default frontend is not replaced
until materials, dynamic visibility/widget semantics and IW3-specific actions
are supported and graphically verified.

Windows synthetic plugin and FF tests pass. Reading the actual retail sources
through the plugin produces 2,621,358 bytes. A probe using the engine's actual
parse_hxmenu implementation accepts all 290 menus / 17,221 items / 15,386 item
actions, and verifies main opens main_text and Join Game opens pc_join_unranked.
Linux ASan/UBSan synthetic tests, retail localized-menu export, Backlot export,
and plugin read with the real UI/localization FFs plus a synthetic IWD mount
fixture pass. No Linux graphical validation is claimed. The plugin check can be
repeated with hxfs_cod4_test --original-menu <game-root> <output.hxmenu>; the
FF runner also accepts --localized-menu <ui.ff> <localized.ff> [output.hxmenu].

## Original UI image bindings and layout identity

The UI importer now recognizes TS_2D (semantic 0) as its texture source; world
and model imports retain their previous COLOR_MAP-only behavior. Inline window
materials are registered at their actual VIRTUAL pointer slots, so later shared
window references resolve to the same material. Root material aliases are also
registered at their asset header slots. Shared image relocations use the existing
allocation ledger. Original nonempty item names, text alignment and text anchors
are preserved; unnamed items keep generated identifiers. Menus use Helix codstyle.

The retail localized export now has 93 image bindings across 10 unique images,
including main-menu background/fog layers and popup blurred backgrounds. The
logo and many highlight materials are external definitions (some are explicit
comma-prefixed references in ui_mp.ff); they still need the common-zone material
library. Dynamic conditions, animated material behavior and button backgrounds
are not proved by these bindings. The default frontend remains pending graphical
integration rather than drawing every overlapping conditional layer at once.

Windows FF/plugin synthetic tests pass, and the engine parser accepts all 290
menus / 17,221 items with main/Join Game navigation preserved. Linux ASan/UBSan
synthetic and real localized-menu export checks pass. The export is 3,338,572
bytes. These checks do not establish graphical correctness on either platform.

## Common UI asset library and reused image names

The original-menu plugin read now imports code_post_gfx_mp.ff and common_mp.ff
when present before importing the localized UI. The public menu-bundle API accepts
an ordered array of shared zones without retaining the input buffers. Shared
material lookups use actual asset names, including explicit comma-prefixed external
references, rather than image filename guesses. Shared zones must be fully walked;
they are not searched for plausible material headers.

The UI walker now handles StringTable, GfxImage, SndCurve, SndDriverGlobals,
PhysPreset, XAnim, XModel, light/FX/impact and weapon children needed by these zones.
The serialized impact table contains 12 entries with 33 effect pointers each;
the loader count is documented in OpenAssetTools:
https://github.com/Laupetin/OpenAssetTools/blob/main/src/ZoneCode/Game/IW3/XAssets/FxImpactTable.txt
The library walk discards unsupported shared UI string values when their absolute
ledger is invalidated by currently untracked asset children. These discarded
values are not used as frontend strings. Material/image references without a
verified allocation remain unresolved. In particular, some highlight/gradient
images still need complete common-zone relocation tracking.

Correction to the preceding image section: logo_cod2 itself is inline in ui_mp;
its image name is a reused XString, not an absent common-zone material. The image
reader now resolves these names against verified UI VIRTUAL string allocations.
The original-menu bundle grows from 93 to 100 image bindings (17 unique images),
including the CoD logo and a shared popup image. Current complete material-library
exports contain 77 named images from code_post_gfx_mp and 252 from common_mp.
The bundled original menu is 3,338,783 bytes; the engine parser still accepts all
290 menus / 17,221 items and preserves main/Join Game action targets.

Regression checks cover reused image-name lifetimes, external material lookup,
string-table overflow/truncation and the last serialized impact-table entry.
Windows synthetic FF/plugin tests, both retail material-library exports and the
retail plugin bundle read pass. Linux ASan/UBSan synthetic tests and both retail
material-library exports pass. Graphical validation and dynamic UI semantics
remain pending.
The Linux ASan/UBSan plugin bundle read with both real common zones also passes
(3,338,783 bytes), including buffer release and plugin destruction. Library mode
additionally requires consuming the complete inflated zone before publishing its
material dictionary.

## Visibility IR and shared expression allocations

Menu items now emit iw3Visible typed token blocks. The decoder caches both entry
arrays and individual entries by VIRTUAL allocation address, allowing reused
expressions and their string operands to be decoded without consuming children
again. Constant textExp extraction also benefits from the shared-entry cache.
The real ui_mp export preserves 13,380 visibility statements without unknown
token markers. Array length mismatches and truncated children are regression-tested.
The engine now evaluates the supported arithmetic/comparison/boolean operations
and dvar/local-variable functions at runtime; other IW3 functions remain pending.
setLocalVarInt/Bool/Float/String translate to native setlocal actions.

Windows FF/plugin synthetic tests and actual ui_mp export pass. Linux ASan/UBSan
synthetic and actual expression export pass. The engine's docs/cod4-menu-runtime.md
records the native runtime and graphical checks, including remaining layout,
logo and panel-layering failures. Original-menu integration is still incomplete.

## Text alignment and wavelet IWI support

The importer now preserves IW3 rectangle-relative text alignment bits rather than
mapping middle-right (10) to left. The native renderer supports these separately
from existing CoD1 anchor semantics. Rectangle/alpha expression IR is also emitted
for later runtime support; these fields are not yet applied.

Logo investigation confirmed images/logo_cod2.iwi exists in iw_01.iwd. It is a
wavelet-compressed IWI (format 6), not a missing FF asset. The new C99 wavelet
adapter supports formats 6..10 and returns standard 32-bit TGA bytes through
pack_read, including alias reads. It is adapted from OpenAssetTools and kept in
the GPL adapter. See docs/third-party-openassettools-wavelet.md and
COPYING.OpenAssetTools.GPL-3.0.txt; the combined CoD4 adapter includes GPLv3 code.

Windows synthetic decoder/plugin tests and real logo decode pass. Linux
ASan/UBSan synthetic decoder/plugin tests and real logo decode pass. The new
cod4_iwi_wavelet_test accepts <input.iwi> <output.tga> for retail probes. No retail
asset is included in the source tests. The engine's cod4-menu-runtime.md records
the native graphical confirmation of the logo, complete labels and main/browser
round trip, plus the separate atlas-capacity fix needed to draw all layers.

## Native widget property translation

Dvar names and original multi/yes-no choices now reach the native UI. Shared
widget structures are cached by verified VIRTUAL allocation address and data
kind, so reused choices never reread their inline strings. Numeric/string lists,
valid field ranges and character limits are preserved. Continuous sliders use
step 0; invalid ranges remain unsupported. The real UI contains 110 multi
controls, 19 edit fields and 5 sliders. Screen center/right anchors are translated
to the canonical frame for these layouts; viewport-specific modes remain pending.

Windows synthetic FF/plugin tests and actual bundled menu/parser checks pass.
Linux ASan/UBSan synthetic, real ui_mp and full plugin-bundle imports pass.
Native runtime/graphical checks and the remaining initialization/Apply work are
recorded in the engine repository's docs/cod4-menu-runtime.md. Merely changing
an original UI staging dvar does not prove the corresponding Helix setting has
been applied.

## Conditional setting actions

The compiled menu action bridge preserves execOnDvarIntValue/FloatValue/
StringValue as typed native queue conditionals, quoting nested commands as data.
The original initialization/Apply CFGs remain loaded through the adapter. A generic
--read <game-root> <resource> <output> plugin test mode can inspect those resources.
The engine now executes CFGs and copies supported CoD4 setting names to native
settings; see its cod4-menu-runtime.md for tested scope and remaining GPU checks.
Master-volume dvar bindings use the actual host s_volume variable.

Windows synthetic/plugin/retail bundled-menu/parser checks pass; the bundle is
7,147,725 bytes. Linux ASan/UBSan synthetic and real-ui export checks pass.

## Parent-relative popup rectangles

Items now export window.rectClient and its alignment fields instead of the already
resolved window.rect. The host adds the menu origin, so using rect previously
applied that origin twice. This made the original Apply confirmation buttons
fall offscreen. The new regression fixture distinguishes parent-relative and
absolute item coordinates. Windows synthetic/retail/parser checks and Linux
ASan/UBSan synthetic/retail UI export checks pass.

The engine's graphical probe confirms the original AA Apply flow creates a native
2-sample OpenGL scene and saves r_msaa 2. See its cod4-menu-runtime.md for exact
validation and remaining UI work. The test configuration was restored afterward.

### Native video modes and Apply state

The original r_mode enum now uses the engine video-mode feeder and copies WxH
values through native r_width/r_height with validation. Other enum feeds remain
pending. Successful system-settings application clears ui_showApply while
preserving other original-menu locals; editing can mark the menu dirty again.

Windows OpenGL runtime verification changed 1280x960 to 1280x1024 through the
original menu and confirmation dialog. The renderer resized, config persisted
both dimensions, and Apply disappeared. The test frontend and user config were
restored afterwards. Windows checks passed: UI 38 tests/7808 checks and Cod4
3 tests/15 checks, standalone settings validation and adapter regression/export.
The full bundle still parses 290 menus and 17,221 items. Linux sanitizer settings
and importer checks pass; Linux graphical behavior has not been verified.

This supersedes the earlier resolution-enumeration and dirty-state limitations.
Original-menu integration and the subsequent weapon/classes/MP GSC phases remain
in progress.

## Original graphics menu: inline Helix controls

The typed FF importer now appends nine native multi controls to options_graphics:
renderer (platform-filtered), borderless, anisotropy, PBR, ACES, physical falloff,
shadow atlas, surface relief and normal mapping. Existing MSAA is retained.
Stock content rows, their backgrounds and highlights are marked scrollable;
scrollRegion 98/404/604 retains fixed tabs, Back and the original Apply flow.
New controls mark the original ui_showApply local. No separate hub is installed.

Windows adapter build/synthetic tests pass. The actual localized FF export parses
as 290 menus/17,230 items/15,412 actions, with nine scrollable Helix rows and the
expected viewport checked by the native parser. This is structural verification;
visual scrolling, Apply/cancel behavior and the legal-text position remain open.

## Linux Mint 22.3 native runtime verification

Built the current engine, OpenGL renderer and CoD4 plugin in isolated directories
under /tmp/helix-cod4-check.dvWiEw/current-engine and current-plugin. Linked libm
explicitly for the Linux CoD4 plugin and plugin test (sqrtf was unresolved).
Actual assets are /home/nvr/cod4; all 21 IWD sizes and four menu FF sizes match
the Windows installation. Native UI suite: 38 passed / 7808 checks on Mint.

Graphical runtime on Mesa Intel UHD 630 OpenGL 4.6 at 1024x768 loaded the original
main menu, logo/background, Options and Graphics by mouse navigation. A real
scrolling defect was found and fixed: stock row classification must use converted
screen anchors, not raw FF positions. After rebuilding/restarting, stock and
Helix rows scroll together with fixed tabs/Back/Apply. Clicking Borderless Off to
On exposed Apply; clicking Apply switched the native window to borderless, hid Apply. The isolated config subsequently contained r_borderless=0; persistence
therefore remains unverified and needs investigation. The bounded probe exited.

Outstanding visual defects include the long stock graphics warning overflowing
its panel and the reported main-menu legal text placement. This is not a complete
menu or gameplay validation, nor Linux Vulkan/map testing.

## Conditional original menu opening

Recovered the original apply2 script from ui_mp.ff: uiScript openMenuOnDvar
and openMenuOnDvarNot choose vid_restart_popmenu_listen versus
vid_restart_popmenu_dead based on g_reloading. The importer now translates both
to bounded, quoted native conditional command execution, preserving the original
popup names rather than bypassing confirmation. Added native exec_if_int_not
using the same validated numeric comparison as exec_if_int.

Windows and Linux Mint plugin regression tests pass. The native engine branch
test passes on both platforms (1 test/4 checks), including invalid numeric input;
Windows UI suite remains 38 tests/7808 checks. Graphical confirmation, persistence
and the popup-specific Yes actions still require runtime verification; this does
not establish a completed settings save fix.

## Menu-window anchors

Menu rectangles now normalize their source screen anchors just like item
rectangles. A synthetic centered window at (-164,-54), aligned center/center,
exports at (156,186) in the canonical 640x480 space. This preserves the original
window/item relationship for centered profile popups. It does not prove the
reported Cancel navigation failure is solved: player_profile already contains an
onClose reopening main_text, and the prior screenshot may have followed a nested
popup's Cancel rather than the profile panel's Cancel.

Windows and Mint adapter regressions pass, including the centered-window fixture.
The actual localized UI export from installed Mint assets parses with the native
Helix parser: 290 menus, 17,230 items, 17,551 actions. Increased actions include the
newly preserved conditional menu opens. Graphical profile/Apply/save verification
remains open.

Centered menu graphical follow-up exposed double application of screen-center
anchors: normalizing the menu alone shifted its already-normalized item positions
again. ui_item_relative now subtracts the parent menu's screen-anchor offset from
item coordinates; the menu translation supplies that offset once. Top-left menus
keep zero adjustment. Regression covers a centered child retaining local (4,26)
under a (320,240) screen-anchor offset, in addition to centered-window placement.
Windows and Mint synthetic regressions pass. Graphical confirmation of the
combined correction remains open. Probe stopped and config restored.

Profile runtime with combined window/item anchors: Create Player Profile Cancel
opens the stock "You need a player profile" notice; OK returns to Create. This
corrects the previous interpretation of a lost-main-menu failure. There is no
implemented add/create/load profile backend yet, so ui_playerProfileCount cannot
reflect persistent profiles. Do not bypass this flow as completed integration.

Source recovery also shows openMenuOnDvar compares strings (including empty
com_playerProfile), not exclusively integers. Import now uses exec_if_string and
exec_if_string_not; native conditional registration includes the inverse string
variant. Windows/Mint importer regression checks pass; graphical end-to-end
profile creation and Apply persistence remain open. Probe config restored.

Original profile UI bridge: addPlayerProfiles/createPlayerProfile/loadPlayerProfile
and selectActivePlayerProfile now map to native profile commands. The named stock
profilelist is a native listbox fed by cod4_profiles with selection in
ui_playerProfileSelected. Menu open/close actions after a mapped profile operation
are queued in the same command stream, so create completes before popup onClose
checks profile count. A regression checks create-before-close export ordering.
Windows and Mint importer regressions pass; the full installed FF export is
accepted by the native parser (290 menus/17,230 items). Graphical create/load and
persisting settings across restarts remain to be verified. Delete/sort profile
operations are still unsupported.

Profile restart/navigation audit: a native engine regression reproduces the
exported main onOpen action sequence with a real persisted profile, shuts down
and reinitializes the engine, then verifies count=1 and main_text remains top.
Passes on Windows and Mint (1 test/4 checks).

Mint graphical startup with existing mintprobe explicitly selected in the probe
config now reaches the main controls after ready/first-render completion. Select
Profile displays the persisted mintprobe name with readable selected-row contrast;
Cancel returns to main controls. Earlier blank-background restart observations
are not reproduced by this controlled run and are not proof of a persistent
startup defect. Config restored, probe stopped. This does not establish profile
switching among multiple profiles, deletion, or graphics Apply persistence.

## Verified original Apply/save/restart milestone on Mint

Using the original Options > Graphics UI and existing mintprobe, changed stock
Anti-aliasing Off to 2x, clicked Apply and Yes in the original confirmation popup.
OpenGL logged 2 samples/request=2, config.cfg contained seta r_msaa "2", the
confirmation popup closed and Apply disappeared. Stopped and restarted the actual
GPU client: VFS loaded config.cfg and OpenGL again initialized with 2 samples /
request=2. This proves the staged MSAA setting's Apply, config persistence and
restart path on Linux Mint/Mesa UHD 630. Restored the pre-probe configuration and
stopped the second test process afterwards.

This supersedes the earlier unproven graphics persistence status for this MSAA
path. It does not verify every added direct-cvar control, Cancel rollback,
in-game restriction branches, other renderers, or all menu settings.

Original wrapping flags: confirmed IW3 windowDef_t staticFlags layout and
WINDOW_FLAG_AUTO_WRAPPED=0x800000 against OpenAssetTools IW3_Assets.h
https://raw.githubusercontent.com/Laupetin/OpenAssetTools/main/src/Common/Game/IW3/IW3_Assets.h
The importer now emits textWrap 1 from serialized staticFlags at offset 76.
Actual retail export shows this flag on both the Game experience legal text and
graphic_warning; no text/name/language heuristic was added. Windows and Mint
adapter regressions pass with a flag fixture; the complete retail export parses
as 290 menus/17,230 items. Graphical verification of the resulting placement and
whether the source rectangles need viewport clipping remains open.

Mint graphical wrapping verification completed on OpenGL at 1024x768. Original
main legal text now wraps into two lines in its lower-left source rectangle;
Options > Graphics warning wraps within its left column and no longer overlays
the settings rows. Evidence captured in scratch/wrap-main.png and
scratch/wrap-graphics.png. No localized-caption offsets were introduced. Probe
stopped/config restored. This verifies these English texts at this resolution;
other languages/aspect ratios and unrelated menu operations remain unaudited.
