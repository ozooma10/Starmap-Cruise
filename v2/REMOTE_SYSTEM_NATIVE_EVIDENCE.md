# Remote-system native evidence gate

Date: 2026-08-18

Runtime: Starfield 1.16.244.0

Result: **PASS for planet/moon native contracts — production implementation
builds and passes pure tests; fresh in-game acceptance is pending**

This checkpoint evaluates the native identity contracts required by the v2
one-action remote-system Cruise design. It is deliberately fail-closed. The
current-system Cruise path remains unchanged, and no `BodyIndex`,
`RecordReader`, plugin-file parsing, or localized-name identity is accepted.

Scope decision, 2026-08-19: remote Cruise supports **planet and moon targets
only**. Remote stations are an explicit non-feature rather than a partial
fallback; same-system station Cruise remains unchanged. The station evidence
below is retained because it explains and enforces that boundary, but station
orbital ancestry and post-travel reacquisition no longer gate the planet/moon
implementation.

## Production implementation checkpoint: 2026-08-19

The planet/moon-only production path is now wired. `Destination` carries the
pair `{STDT FormID, numeric system ID}` plus a copied nearest-parent-first
waypoint plan; zero remains a valid numeric Sol ID. A monotonic operation ID
correlates the source map session/movie, concrete route handoff, travel,
arrival, Cruise press, final course request, and lock completion. Remote
stations remain disabled, while the existing same-system station path is
unchanged.

The concrete `RemoteRouteBridge` owns all version-private Starmap work. Its
pure protocol covers stock Back, exact galaxy-root and selected-STDT readback,
Quick Select ownership consumption, a new structured endpoint resolving to
the captured `SystemIdentity`, 500 ms of continuous public Execute readiness,
the native Execute event, and the same-session close acknowledgement. Each
stage is bounded, foreground loss pauses its clock and clears readiness dwell,
and failure clears only the correlated operation.

Travel and HUD callbacks publish copied bounded observations. The post-advance
consumer preserves pending arrival through LoadingMenus and intermediate
systems, resets on a real load or overflow, and requires a completed player
jump, map-independent current-system equality, 2.5 seconds of settled flight,
and a fresh HUD row publication. A direct exact final row starts Cruise; an
otherwise unique first allowed parent row can start latent moon acquisition.
Only the retained final course ID is requested, and ordered waypoint locks are
observed without ever requesting a waypoint.

Eleven pure v2 suites pass, including the full correlated application
lifecycle, queue bounds, Sol, intermediate-system preservation, delayed and
ordered locks, ambiguity/reset cases, wrong and pre-existing routes, focus
suspension, and Execute without close. The no-deploy releasedbg production
build links at 702,464 bytes with SHA-256
`5C346AE8FE4EDECA5BE0E1C189E6A629CCFB6E426C2D42238E5FB890E503393E`.
Production source and PE audits find no `BodyIndex`, `RecordReader`, plugin-file
parsing, filesystem identity read, or diagnostic-probe string. This is
test/build evidence only. The hash-identical DLL/PDB were then copied to the
production MO2 mod while Starfield was closed; the `Default` profile now
enables `Starmap Cruise` and disables `Starmap Cruise Native Probe`. A backup
of the prior profile list is retained beside `modlist.txt`. Deployment is not
runtime proof: a fresh relaunched-game acceptance remains open, so the README
is intentionally unchanged.

## Fresh passive campaign: 2026-08-18 22:01

The dedicated non-production target was built, deployed by itself through a
separate MO2 mod, and relaunched on Starfield 1.16.244.0. The source and
deployed DLL SHA-256 matched
`3D75CECEE1CE3A55EF9E3342AB5371E696E85A0BE43F62DE4FE96FDED52AFC6E`.
The live log emitted one passive `READY` record and confirmed that the probe
did not consume input or dispatch a stock action.

Fresh live readbacks establish:

- the map-independent current pair in Sol is current body `0005DEB8`, STDT
  `0005E5CB`, numeric wrapper `00000000`, and copied Satellite system
  `00000000`; the two numeric reads agree and zero is valid;
- a real `TESLoadGameEvent` performs one evidence reset, after which the same
  current pair is reacquired;
- Mars PNDT `0003F59A` resolves to STDT `0005E5CB`, numeric system `0`, parent
  ordinal `0`, and planet ordinal `4`;
- Callisto PNDT `0005DEBE` resolves to the same `SystemIdentity`, parent
  ordinal `5`, and planet ordinal `0x12`; Jupiter PNDT `0005DEBA` independently
  resolves with planet ordinal `5`.

The campaign then stopped on a useful negative control: the assumed
`TESDataHandler::formArrays[kPNDT]` array has live count `0` on 1.16.244.0.
Direct PNDT lookup and copied ComponentDB rows remain valid, so the failure is
only the global-discovery seam. No ancestry result from that array is valid,
and no production code may depend on it.

## Fresh passive campaign: 2026-08-18 22:51

The guarded-AllForms diagnostic replacement was built and independently
reviewed, all nine v2 suites passed, and production/probe PE isolation was
verified before deployment. The source and dedicated MO2 probe copies matched
SHA-256
`B313B6282656E12B1621769C2FC1A8E7215E4CF5D812AB763FF7A597EF4CAA4B`.
The relaunched game emitted exactly one passive `READY` record on 1.16.244.0
and remained responsive.

Callisto again read back exactly as PNDT `0005DEBE`, STDT `0005E5CB`, numeric
system `00000000`, parent ordinal `5`, and planet ordinal `0x12`. The guarded
AllForms snapshot copied 1,788 live PNDT FormIDs from 1,315,655 active forms
under a capacity of 2,097,152, then released the lock before ComponentDB work.
The first complete processing pass correctly failed closed: 1,708 rows
produced the then-required full `{STDT, numeric, Satellite}` fact and 80 did
not. Consequently the wrong-ordinal, Jupiter reverse lookup, waypoint, and
ancestry-complete claims were not emitted.

Static follow-up explains why the all-or-nothing predicate was too broad:
PNDT is the form type, while `SatelliteCSVData` and membership in the
`BodyChild` graph are separate runtime component contracts with native miss
paths. The next diagnostic must classify every snapshotted PNDT as unrooted,
other-STDT, exact selected-STDT body, or fatal/stale. It may exclude an exact
ID `124608` result of `0` and a different live STDT because ID `124608` climbs
the same `BodyChild` graph that ID `124772` traverses for parent lookup. Every
selected-STDT row must still have an exact copied Satellite tuple and numeric
agreement. This is a diagnostic correction, not evidence that the ancestry
gate passes.

### Corrected classifier build checkpoint

The replacement probe partitions the bounded AllForms snapshot by the
authoritative ID `124608` STDT readback before inspecting private component
rows. An exact zero result is recorded as unrooted, a different live STDT as
unrelated, and every row in the selected STDT graph must provide a copied
Satellite tuple with exact wrapper agreement. Missing Satellite data inside
that graph remains fatal because native ID `124772` resolves ordinal parents
through `BodyChild` and `CTDatabaseID`, not through Satellite data; silently
dropping such a row could hide an ambiguity. CTCellData is tri-state and is a
fatal identity contract only for station evidence. The initial guarded PNDT ID
snapshot must exactly equal a second completion snapshot, the selected moon
must remain uniquely retained, and all relevant native pointer-return ABIs are
checked exactly.

The corrected diagnostic target builds and links with SHA-256
`E1E98D2B24159E9850348A04336F94011D743925889A41F757ED19035A619237`
(882,176 bytes). A fresh production build has SHA-256
`D378E235D01D967A1CBFDE00F5243872B159CC40983AF9350001CD0D8AB378BB`;
all nine v2 suites pass, and source/PE audits still find no probe or disk-based
identity dependency in that production artifact. This checkpoint is build
evidence only: the corrected probe has not yet been deployed or run in-game.

## Fresh passive campaign: 2026-08-18 23:29

The corrected diagnostic DLL was deployed alone through the dedicated MO2 mod;
the deployed copy matched SHA-256
`E1E98D2B24159E9850348A04336F94011D743925889A41F757ED19035A619237`.
The relaunched game emitted exactly one passive `READY` record and no `FAIL`
record. Before the save was ready the exact current readers reported one
expected unavailable sample; across the load boundary they twice resolved
current body `0005DEB8` to STDT `0005E5CB` and numeric Sol ID `00000000`, with
exact Satellite agreement and zero-valid presence semantics.

Callisto resolved as PNDT `0005DEBE`, STDT `0005E5CB`, numeric system
`00000000`, parent ordinal `5`, and planet ordinal `0x12`. The guarded
AllForms scan copied and processed 1,788 PNDT FormIDs, then classified every
row exactly: 80 unrooted, 1,675 belonging to other live STDTs, and 33 in the
selected Sol STDT. All 33 selected-STDT rows had exact copied Satellite and
numeric agreement; there were zero rejected, stale, ambiguous, or failed rows.
The completion resnapshot was byte-for-byte identical to the initial sorted ID
set.

The native reverse resolver accepted numeric Sol ID `0`, returned `0` for the
wrong-ordinal control `7FFFFFFE`, and resolved Callisto's parent ordinal `5` to
the sole AllForms candidate, Jupiter PNDT `0005DEBA`. A fresh Jupiter readback
matched STDT `0005E5CB`, numeric `0`, root parent ordinal `0`, and planet
ordinal `5`. The emitted ordered plan contains exactly one nearest-parent-first
allowed waypoint (`0005DEBA`) and terminates at that root without a cycle or
depth failure.

This passes the bounded disk-free Callisto parent-identity and native reverse
lookup slice for the current load order. It does not by itself prove that a
latent Cruise request locks Jupiter before Callisto, any remote route or
post-jump current-system transition, or station orbital ancestry/reacquisition.

### Stock Set Course structured endpoint identity

In the same live session, the operator returned to galaxy view, selected Alpha
Centauri STDT `0005E60A`, and invoked stock `SetRouteDestination` once. The
probe copied an empty pre-existing route endpoint, correlated the input to map
session/generation `1/1`, and observed a stable normal route with one point.
The route endpoint changed from `00000000` to `0003F5A1`, but that FormID was
not an STDT and did not equal the selected `0005E60A`. A corrected probe then
repeated this stock action in two map generations and classified the same
endpoint from live engine state both times: form type `186`/PNDT, resolved STDT
`0005E60A`, numeric system `00011720`, and copied Satellite system
`00011720`. The endpoint's resolved `SystemIdentity` therefore matched the
selected Alpha Centauri STDT exactly without names or plugin-file reads.

This disproves raw endpoint-FormID equality while establishing the more useful
contract: a stock route's structured terminal PNDT must resolve natively to the
captured destination `SystemIdentity`. The diagnostic's older direct-STDT
completion predicate intentionally emitted no completion record and must be
updated to express this resolved identity contract. Execute was deliberately
not invoked, so Execute/close acknowledgement remains open.

### Execute observer and arrival-correlation build checkpoint: 2026-08-19

The 1.16.244 binary contains a genuine global
`StarMapMenu_ExecuteRoute` event source, but the local CommonLib declaration's
ID `142945` does not point to its getter; it resolves to the UI event
dispatcher's auto-registration constructor. The actual acquiring magic-static
getter is Address Library ID `94774` (RVA `0x16C3420`), returns the exact global
source at ID `948974`, and constructs the event source with vtable ID `446781`.
The getter's first 16 bytes, exact returned address, and vtable are all guarded
before the passive sink is registered.

The planet/moon-only diagnostic now retains route correlation after the
terminal FormID resolves to the captured `{STDT, numeric}` pair, requires the
public Execute gate to remain continuously ready for 500 ms, observes the
native Execute event, and requires a same-session later Starmap close within
two foreground seconds. A successful close arms a process-local arrival
sample which survives intermediate grav jumps and LoadingMenus but resets on
`TESLoadGameEvent`. Final-system evidence requires a completed player jump,
fresh exact current-system readback, map and LoadingMenu closed, 2.5 seconds of
settling, flight state, and a HUD publication newer than the last travel
transition.

This diagnostic build links at 894,464 bytes and was hash-verified into the
dedicated MO2 probe mod with SHA-256
`1E06284BA5BC8300A7FFD485769C957B3A3941D4AE2CF5C6281F959E7E94A98F`.
Source inspection finds no `BodyIndex`, `RecordReader`, plugin parsing, or file
I/O dependency in the probe. This is static/build/deployment evidence only;
the new Execute and arrival chain had not yet been relaunched at this build
checkpoint.

### Fresh Execute and final-system arrival campaign: 2026-08-19 05:36

The hash-verified probe relaunched on Starfield 1.16.244.0 and emitted one
`READY` record containing the guarded Execute getter/source IDs. The true save
load emitted one `TESLoadGameEvent` reset, after which current Sol identity was
reacquired as body `0005DEB8`, STDT `0005E5CB`, and numeric ID `0` with exact
Satellite agreement. No probe `FAIL` occurred.

In map session/generation `1/1`, stock Set Course captured Alpha Centauri STDT
`0005E60A` and numeric ID `00011720` with no pre-existing endpoint. The normal
route changed to terminal PNDT `0003F5A1`, which independently resolved back
to exactly the captured `{0005E60A, 00011720}` pair. The public Execute gate
then remained continuously ready for 526 ms. The native
`StarMapMenu_ExecuteRoute` event arrived at observation sequence `92`, and the
same map session closed later at sequence `94`, inside the two-second bound.

The player jump then emitted the exact ordered states `0`, `1`, and `2` at
sequences `96`, `99`, and `102`. A subsequent LoadingMenu opened and closed at
sequences `103` and `104` without a `TESLoadGameEvent`, preserving the pending
arrival. The map-independent current reader resolved body `0003F5A1` to Alpha
Centauri STDT `0005E60A` and numeric ID `00011720` with copied Satellite
agreement. After 2,509 ms settled, with the map and LoadingMenu closed, the
player still flying, and fresh nonoverflowed HUD publication sequence `107`
newer than travel, the probe emitted `PASS: final-system arrival exact`.

This live campaign closes the direct Sol-to-Alpha-Centauri structured-route,
public-readiness, native-Execute, matching-close, grav-jump boundary, and
authoritative final-system identity slice. It does not yet establish a genuine
multi-hop route, replacement/wrong-route controls, or final planet/moon Cruise
target acquisition.

While still in Alpha Centauri, the same live process completed a non-Sol moon
sample for Chawla PNDT `0005E315`. Its guarded readback was STDT `0005E60A`,
numeric ID `00011720`, copied Satellite system `00011720`, parent ordinal `4`,
and planet ordinal `0x0A`, with exact agreement. The stable AllForms campaign
again snapshotted and processed all 1,788 PNDT IDs: 80 unrooted, 1,696 in
other STDTs, and 12 in Alpha Centauri, with zero classification failures,
rejections, numeric disagreements, or row failures. The completion resnapshot
exactly matched the initial sorted ID vector.

The native reverse resolver returned zero for wrong ordinal `7FFFFFFE`, and
Chawla's parent ordinal uniquely resolved through both AllForms and the native
BodyChild graph to PNDT `0005E313`. The emitted nearest-parent-first plan has
exactly that one allowed waypoint and terminates at the root without a cycle
or depth failure. This extends the copied parent/reverse-lookup contract beyond
Sol, but does not yet prove the final latent Cruise lock sequence.

From Alpha Centauri, stock Set Course to Cheyenne captured STDT `0005E607` and
numeric ID `00011AF0`. The structured route reported two points and terminated
at PNDT `0005E2B6`, which resolved exactly to that pair. The public Execute
gate, native Execute event, matching close, and final arrival predicates all
passed again. However, the actual travel emitted only one player grav-jump
`0 -> 1 -> 2` sequence, and the first post-jump current readback was already
Cheyenne `{0005E607, 00011AF0}`. Therefore route point count `2` is **not**
sufficient evidence of an intermediate system and this run is classified as
another direct jump, not genuine multi-hop proof.

The fresh settled HUD feeds also establish both final-row shapes required by
the planet/moon acquisition policy. In Sol, direct planet Mars `0003F59A` and
parent Jupiter `0005DEBA` were present while latent moon Callisto `0005DEBE`
was absent. In Alpha Centauri, parent `0005E313` was repeatedly present while
latent moon Chawla `0005E315` was absent. These are exact FormID observations,
not name inference, and they agree with the independently copied parent plans.
They prove that final direct rows and unique ordered parent fallbacks both
occur in the v2 HUD feed; they do not yet prove Cruise request/lock actuation.

Additional galaxy selections produced unowned route previews without a stock
Set Course input: Fermi had eight points and was out of range, Porrima had
three points and was out of fuel range, and Volii showed two points but did not
emit `SetRouteDestination` when the attempted binding was pressed. The probe
correctly did not arm route or Execute correlation for any of these previews.
This demonstrates why an existing route vector alone is not ownership or
commitment evidence. Genuine multi-hop travel remains an acceptance test with
a ship/route that can actually execute it.

### Deimos body and station probe

After closing the map without Execute, the next map generation independently
completed a second moon ancestry sample. Deimos PNDT `0005DEB8` resolved to
STDT `0005E5CB`, numeric Sol ID `0`, parent ordinal `4`, and planet ordinal
`0x0D`. The stable 1,788-row AllForms partition again completed with zero row
failures, the wrong-ordinal control returned `0`, and native reverse lookup
uniquely resolved the one allowed parent to Mars PNDT `0003F59A`, whose root
parent ordinal is `0` and planet ordinal is `4`.

The Deimos Staryard same-frame scan also passed three times with the exact
copied tuple CELL/map `00219DFF`, physical REFR `003120D6`, base `000090B3`,
distinct course XMRK `00219DE0`, displayed STDT `0005E5CB`, six CELL
references, one station, one marker, and CELL editor ID `scStationDeimos`.
The repeated same-session tuple was identical but is correctly classified as
repeatability-only, not post-travel reacquisition.

Station orbital ancestry did not pass. An initial diagnostic incorrectly
rejected 32 readable empty CT strings as malformed. The corrected rerun treated
them as authoritative nonmatches and completed the selected-STDT partition
with zero row failures and a stable resnapshot. Of the 33 Sol rows, 32 CT
strings were empty and the sole nonempty value was
`scLC018BattleAboveEarth` on PNDT `002CF271` (`parent=3`, `planet=0x27`), not
`scStationDeimos`; the exact CT join count therefore remained zero. One
transient tuple sample also failed closed when displayed STDT briefly read
zero, followed by further exact tuple readbacks.

This selected-system negative is not yet a global CT absence proof because the
80 unrooted and 1,675 other-STDT PNDTs were not inspected for a matching editor
identity. The final bounded diagnostic must copy CTCellData for all 1,788 live
PNDT IDs, log any exact global match, and require that a match also reacquire
complete Satellite and selected `SystemIdentity` data. No station orbital PNDT
or waypoint claim is valid from this run.

## Fresh passive campaign: 2026-08-19 00:38

The final all-PNDT CT diagnostic was built and deployed through the dedicated
probe mod. The source and deployed DLL copies matched SHA-256
`99673E680F3E58629CF3C3ECE2F4F9347656D424C21D1395264D1C4D3400B7AF`
(892,928 bytes). The live process loaded SFSE 1.16.244 and that exact probe
DLL. After the real load reset, the passive probe reacquired the zero-valid Sol
current pair and produced no failure before the requested station scan.

Deimos Staryard again produced the exact copied tuple CELL/map `00219DFF`,
physical REFR `003120D6`, base `000090B3`, distinct course XMRK `00219DE0`,
displayed STDT `0005E5CB`, six CELL references, one station, one marker, and
CELL editor ID `scStationDeimos`. No engine or Scaleform pointer survived the
selecting frame.

The station-scoped scan then processed all 1,788 guarded AllForms PNDT IDs and
repeated the snapshot exactly at completion. Every row received a held-guard
CT lookup: 89 had an authoritative absent result, 1,699 had a readable payload,
1,696 readable payloads contained an empty editor string, and three contained
text. The only nonempty values were:

- PNDT `0017176B`: `sCTestBetaTemp`;
- PNDT `0018334F`: `scRL048`;
- PNDT `002CF271`: `scLC018BattleAboveEarth`.

There were zero invalid CT reads and zero exact global matches for
`scStationDeimos`. The selected Sol partition independently remained stable at
33 rows with exact Satellite/numeric agreement, but it likewise contained zero
matches. The probe therefore emitted its expected fail-closed station-join
failure. This is a global runtime negative for the current load order, not a
sampling gap: PNDT `CTCellData` does not expose the selected Deimos station
CELL editor identity needed by the proposed disk-free ancestry join.

Consequently the planned CTCellData-to-CELL station ancestry contract is
disproved for this representative station. The physical REFR/base/XMRK tuple
remains proven, but it cannot be associated with a unique orbital PNDT and
ordered waypoint chain through this native seam. Per the design's explicit
stop condition, production remote routing remains unwired; the probe does not
fall back to plugin-file indexing, localized names, or partial target support.

### Post-failure native reference-resolver candidate

A bounded static follow-up identified existing CommonLibSF API
`BGSPlanet::ResolvePlanetFromRef`, Address Library ID `52188` (VA
`0x1407BCBD0` on 1.16.244). Its machine code initializes both caller-owned
integer outputs, first consults live reference extra data, and otherwise uses
the reference/cell position path. The primary output is compared directly with
`BGSPlanet::Manager::currentPlanetFormId`, making a PNDT FormID interpretation
plausible. The function has 42 direct native call sites. Its first 16 bytes are
now fingerprinted by the diagnostic before use.

The replacement probe calls ID `52188` only while the uniquely validated
physical station REFR `NiPointer` is local to the same-frame CELL scan. It
copies the returned primary/secondary IDs into `StationTuple`, releases all
engine pointers on return, and requires the primary to be a live PNDT whose
STDT, numeric system, copied Satellite tuple, unique AllForms
`{numeric, planetOrdinal}` candidate, and native ID `124772` reverse lookup all
agree before emitting ordered ancestry. CTCellData is no longer an identity
input on this path.

This diagnostic builds and links with SHA-256
`7912424EC789E9E8F50567A5C707F467C09D55B4C3778D90DA42F6F1F3AB715A`
(894,976 bytes). The exact DLL and PDB were subsequently deployed through the
dedicated probe mod, and the relaunched process emitted the fingerprinted
`READY` line with `planet-from-ref=52188`.

The first live ID `52188` attempt was inconclusive. One Starmap open and close
completed normally. On a later open, the final probe observation was only the
pre-layout native state (`open=true`, `layout=false`, `view=none`); no new
GalaxyStarMap movie, station marker, same-frame station scan, ID `52188`
readback, or probe failure was recorded before Starfield crashed. Trainwreck
reported a null indirect call in the engine UI-update chain, with the seven
engine frames `+254089D -> +1890E42 -> +1885D6F -> +188911A -> +189D7FB ->
+1881569 -> +365D896`. The same complete signature exists in the 2026-07-23
crash log from a process that did not load any Starmap Cruise DLL. The
`+1890E42` frame is inside Address Library ID `99438`, `UI::UpdateMenus`.

This crash therefore invalidates the sample but supplies no live evidence for
or against ID `52188`: the candidate call was never reached. One fresh retry
is justified. If the same pre-call crash repeats, stop and isolate the probe
build from the previously live-stable diagnostic before any further station
testing. The production gate remains failed until representative station
readbacks pass.

The one permitted fresh retry reached the same-frame station scan without a
crash. Deimos Staryard again resolved to the exact copied tuple CELL/map
`00219DFF`, physical REFR `003120D6`, base `000090B3`, distinct course XMRK
`00219DE0`, and displayed STDT `0005E5CB`, with six references, one station,
one marker, and no truncation. ID `52188` returned false for physical REFR
`003120D6` and left both caller-owned outputs at `00000000`. Consequently no
live orbital PNDT, STDT, numeric system, or Satellite tuple could be
revalidated, and the probe failed closed before AllForms uniqueness, reverse,
or ordered-ancestry claims.

This is a live negative for using `BGSPlanet::ResolvePlanetFromRef` to obtain
the required orbital PNDT from the physical Deimos station REFR. It does not
invalidate the already proven station tuple, and it does not test whether the
distinct CELL-owned course XMRK has a different native relationship. The ID
`52188` physical-REFR candidate is rejected; production remote routing remains
unwired.

A final bounded comparison probe now invokes the same already-fingerprinted ID
`52188` on both locally retained references: the unique physical station REFR
and the unique CELL-owned course XMRK. It copies and logs both result pairs,
rejects conflicting nonzero PNDTs, and selects neither unless one result passes
the existing live PNDT, STDT, numeric/Satellite, stable AllForms uniqueness,
native reverse, and ordered-ancestry checks. Both `NiPointer`s die on return
from the same-frame scan. This adds no new native hook, offset, name join, or
disk dependency.

The comparison probe builds and links as 897,536 bytes with SHA-256
`F62E9CCBB49BD9BB6C285287759303CACE5A45D00C499AE0D3FB0115CFF1AD4A`.
The exact DLL and PDB were deployed to the dedicated native-probe MO2 mod while
Starfield was closed, and the deployed DLL hash matches. This is build and
deployment evidence only.

The fresh live comparison loaded that exact DLL and completed its load reset
with the authoritative zero-valid Sol pair. Deimos Staryard again produced the
exact unambiguous tuple. ID `52188` then returned false with zero primary and
secondary outputs for both physical REFR `003120D6` and course XMRK
`00219DE0`. The probe selected no candidate and failed closed before AllForms,
reverse, or ancestry processing.

This rejects both locally available reference forms as inputs to the existing
native resolver. Together with the complete CTCellData negative, the required
disk-free Deimos station orbital-PNDT/ancestry contract remains unresolved.
Per the requested gate, the station investigation stops here and production
remote routing must not be wired.

## Evidence inspected

- Production source baseline at clean HEAD `6a8731b` (`Remove probes`),
  following `b141e88` (the selected-CELL station resolver).
- Live `Starmap Cruise.log` captured on 2026-08-18 at 19:20-19:22 with the
  compile-gated system-target probe on Starfield 1.16.244.0.
- Deployed probe DLL SHA-256
  `9648869DD8C6634204668C3B3F1E1BBB697D845224B4ED6B7ABF33E1801A9F68`.
  It is not the current HEAD build. A fresh no-deploy releasedbg build from
  this checkpoint has SHA-256
  `E42F7EE36BF394A7084E7914E06799BF6E9A816A82A3E975B30364508C06264E`.
- The prior station investigation was recovered read-only from Git object
  storage because `v2/STATION_IDENTITY_INVESTIGATION.md` is not present in
  the worktree. It was not restored over the editor's unsaved buffer.
- Current v2 and v1 production sources, including the v1 route handoff and
  v2 station/current-system resolution seams.
- Bounded 1.16.244 static inspection of the current-system, galaxy component,
  and stock route-builder call paths. These findings identify probe seams;
  they are not substitutes for the live readbacks required by this gate.

## Proven contracts

| Contract | Live readback | Scope |
| --- | --- | --- |
| Valid numeric system zero | Mars and local Deimos resolve to numeric system `00000000` | Proves that Sol requires presence-based validity, not a nonzero sentinel |
| Displayed system STDT | Native `SystemState+0xA10 = 0005E5CB`, agreeing with Scaleform `uSystemLocationID = 0005E5CB` | Current Sol system view only |
| Selected station identity | Native `SystemState+0xA1C = 00219DFF` | Current Deimos selection only |
| Selected-CELL station tuple | map/CELL `00219DFF` -> station REFR `003120D6`, base `000090B3`, distinct map-marker/XMRK candidate `00219DE0` | Deimos before travel; the CELL children were live while the CELL was not loaded |
| Same-system station course | Prior production run accepted Deimos physical target `003120D6` and exact HUD course `00219DE0` | Same-system only; not post-jump reacquisition |
| Separate station identities | The Eye map/CELL `0001285A`, physical REFR `00012894`, and course XMRK `002900A9` are distinct | Same-system Alpha Centauri only |
| Travel event boundary | Prior live work observed player grav-jump `0 -> 1 -> 2` and no `TESLoadGameEvent` during grav jumps | Event lifecycle only; not an authoritative native `SystemIdentity` readback |

These results are useful but do not establish a remote operation. In
particular, selected-CELL resolution proves how to inspect a station while its
map selection is live; it does not prove that the same physical/course tuple
can be reacquired and revalidated after system travel.

## Strong static seams awaiting live proof

| Contract | 1.16.244 static finding | Remaining proof |
| --- | --- | --- |
| Map-independent current STDT | Address Library ID `97914` resolves the current body/location FormID; ID `124608` maps that FormID to its system STDT. The SystemState refresh path at `0x1416E8360` computes `124608(97914())` and compares it with `SystemState+0xA10`. | Exact current Sol pair is live-proven; every leg of a multi-hop jump and a non-Sol system remain |
| PNDT numeric system | ID `124767` is a guarded acquiring wrapper which copies offset `+0` from runtime `BSGalaxy::SatelliteCSVData` into a caller-owned `FormID`. Its existing `FormID* (FormID*, FormID)` v2 signature is correct and a copied value of `0` is valid. | Pair the copied numeric ID with the authoritative current STDT across known systems and jump transitions |
| PNDT GNAM tuple | Runtime `BSGalaxy::SatelliteCSVData` is the 12-byte `{system, parent, planet}` GNAM payload. ID `124770` safely copies `planet` at `+8`; ID `124799` exposes the full row only inside an already-held ComponentDB guard. | Guarded copies are live-proven for Callisto and Jupiter; direct/latent/staged target coverage remains, and ID `124799`'s pointer must never be retained |
| PNDT parent reverse lookup | ID `124772` is an acquiring wrapper with ABI `FormID* (FormID* out, FormID numericSystem, FormID planetOrdinal)`. It resolves the `BSGalaxy::BodyChild` graph through held-guard ID `124771`, initializes output to `0`, accepts numeric Sol ID `0`, and rejects only the `FFFFFFFF` missing sentinel. | Live Callisto `(0, 5) -> Jupiter 0005DEBA`, wrong-ordinal zero, unique AllForms agreement, and ordered termination now pass; broader target coverage remains |
| Live PNDT discovery | `TESForm::AllFormsMap` ID `883341` is a pointer to `BSGuarded<BSTHashMap2<FormID, TESForm*>, BSReadWriteLock>`. ID `47401` proves the table, capacity, 24-byte entry, value, and lock offsets used by CommonLib on 1.16.244.0. | A bounded 1,788-ID snapshot, exact classification, uniqueness, and stable completion resnapshot pass for the current load order; repeatability across travel/load and station joins remains. The empty `TESDataHandler` PNDT array is rejected. |
| Station ancestry source | PNDT DNAM is present as runtime `BSGalaxy::CTCellData`, keyed by data ID `938337`. A guarded copy and complete stable scan of all 1,788 live PNDTs found no `scStationDeimos` match. Existing API ID `52188` was also tested against both live station references. | Both candidate contracts are disproved for Deimos: there is no CTCellData CELL-editor join, and ID `52188` returns false/zero for both the physical station REFR and course XMRK. No production station-ancestry seam remains from this bounded investigation. |
| Station tuple reacquisition | `TESObjectCELL` has guarded live-reference storage at `+0x80` with its read lock at `+0x118`; engine walker ID `63054` uses this path. | Remote pre-travel and post-arrival scans proving the same exact IDs and target assignment/readback |
| Structured route endpoint | The normal stock route uses the route object at `StarMapMenu+0x1258`, point count at `+0x1280`, point data at `+0x1288`, stride `0x28`, and endpoint FormID at final point `+0x04`. ID `94692` compares that field with the requested Set Course FormID; ID `94689` builds it. Alternate mode (`+0x12B8 != 0`) stores the endpoint at `+0x1294`. | Two correlated stock actions produced PNDT `0003F5A1`, which resolved exactly to Alpha Centauri `{STDT 0005E60A, numeric 00011720}`; the corrected resolved-identity predicate is built but awaits live proof, and guarded Quick Select, alternate mode, and controls remain |
| Stock Execute event | The true global `StarMapMenu_ExecuteRoute` source getter is ID `94774`, returning exact source ID `948974` with vtable ID `446781`. CommonLib's annotated ID `142945` is the event-dispatcher registration constructor and is rejected as a getter. | Exact guarded sink registration, 500 ms public readiness dwell, same-session event/close ordering, and post-jump arrival correlation are built and deployed but not yet live-proven |

The final route-point field is a generic requested endpoint FormID, not
intrinsically an STDT. It is an exact STDT contract only for the guarded
system-level Set Course path whose request is the captured destination STDT.
`StarMapMenu+0x1250` is an initial/current-selection seed, not the route
endpoint. The local CommonLib `BGSStar::uniqueID` member offset was also found
stale on 1.16.244 and is not an acceptable substitute.

## Remaining planet/moon implementation and acceptance work

1. **Genuine multi-hop preservation.** Direct Sol-to-Alpha-Centauri and
   Alpha-Centauri-to-Cheyenne arrivals prove authoritative final pairs, but no
   executable route in this campaign entered an intermediate system. The
   operation must preserve its final destination across every completed
   nonfinal jump and be accepted live with a ship/route that can execute it.
2. **Final Cruise actuation.** The settled HUD proves a direct final-row shape
   and two latent parent-row shapes. Production must still send one stock
   Cruise press, request only the retained final course ID, enforce ordered
   waypoint locks, and accept only the exact final lock.
3. **Automated route bridge controls.** The exact native Execute event and
   same-session close acknowledgement pass live for manual stock Set Course.
   Production still needs guarded system selection, Quick Select consumption,
   focus suspension, alternate-mode handling, wrong/pre-existing-route
   rejection, unreachable-route timeout, and movie/session replacement tests.
4. **End-to-end target matrix.** Fresh acceptance remains required for a
   remote direct planet, latent and parent-assisted moons, keyboard/controller,
   genuine multi-hop, load/cancellation boundaries, and exact final locks.

## Explicitly excluded station contracts

The safe held-guard CT copy, complete guarded AllForms enumeration, and stable
resnapshot work, but the proposed station join is disproved for Deimos: none
of all 1,788 live PNDTs has CT editor identity `scStationDeimos`. ID `52188`
also returned false with zero outputs for both physical station REFR
`003120D6` and course XMRK `00219DE0`. No disk-free station orbital-PNDT
contract was found, and no remote station reacquisition campaign exists.
Remote stations therefore remain ineligible by policy and contribute no
`RemoteTargetPlan`.

## Baseline verification

- All nine existing v2 suites pass:
  `NavigationRuntime`, `SelectionPolicy`, `ActionPolicy`, `MapSessionState`,
  `CruiseRuntime`, `ActionPresenter`, `MapObservationInbox`,
  `HudObservationInbox`, and `MapActionInputState`.
- A fresh releasedbg production build succeeds with deployment environment
  variables cleared.
- Source and built-DLL audits find no `BodyIndex`, `RecordReader`, plugin-list
  parsing, removed system-target probe, or v1 localized route-end dependency in
  the v2 production target.
- The earlier no-deploy checkpoint was followed by the fresh passive campaign
  recorded above. Build, hash-verified deployment, launch, and individual live
  readbacks remain distinct evidence; the overall gate still fails.

## Gate decision

The revised native evidence gate **passes for planet/moon production wiring**:

- `SystemIdentity {starFormId, numericId}` is available entirely from guarded
  live engine state, including numeric Sol ID `0` and non-Sol post-jump pairs;
- direct and latent planet/moon HUD shapes, copied GNAM parent chains, unique
  reverse resolution, wrong-ordinal controls, and stable live-form discovery
  all agree without disk reads;
- structured route endpoints resolve to the captured destination pair, and the
  public readiness, native Execute event, same-session close, grav-jump, load,
  settled-flight, and fresh-HUD arrival boundaries pass live;
- no station contract is required because remote stations are explicitly
  unsupported, while the existing same-system station path is unchanged.

The feature itself is not complete or accepted yet:

- no `SystemIdentity`, `RemoteTargetPlan`, `RemoteCruiseOperation`, remote
  navigation phases, or `RemoteRouteBridge` has been added to production;
- remote planet/moon selections remain disabled until the core operation,
  isolated route bridge, tests, build audit, and live acceptance are complete;
- remote stations are permanently ineligible in this product scope, while
  same-system station Cruise remains unchanged;
- no disk-backed identity fallback has been introduced;
- the complete live PNDT/CT scan found no Deimos station CELL-editor join, so
  no station orbital plan is permitted through the proposed runtime component
  contract;
- the existing native reference resolver returned no orbital identity for
  either the physical Deimos station REFR or its distinct course XMRK;
- genuine multi-hop and final Cruise lock behavior still require fresh live
  acceptance after production wiring;
- same-system Cruise behavior remains the shipped boundary in the current
  artifact.

## Remaining live acceptance after implementation

No further station probe is justified or scheduled. The next instrumented
build is restricted to planet/moon implementation acceptance and must record,
using FormIDs rather than names:

1. the same map-independent current pair after every leg of a genuine
   multi-hop route;
2. the normal/alternate structured route endpoint immediately after stock
   system-level Set Course, proven equal to the captured `SystemIdentity`,
   including wrong and pre-existing-route controls;
3. correlated automatic stock Execute and matching Starmap-close
   acknowledgement with focus/session/movie controls;
4. final-arrival activation and exact target lock for a remote direct planet
   plus latent and parent-assisted moons, with ordered waypoint controls.

If any item fails, the planet/moon remote feature remains unreleased even if
the production target builds and unit tests pass.
