# TODO - implementation backlog

Development target: **0.9.2163**. Last qualified package: **0.9.2163 / b2217** (offline).
Only defects and unfinished implementation belong here. Validation, rendered
acceptance, qualification and results belong in WORK.md and TESTLOG.md.

## Priority regressions

- [x] Local co-op launcher: launch the installed profile DLLs without deploying a build or rewriting game configuration.
- [x] PWR3: complete laptop breaker timers through the verified native continuation so waterlogging, the button and the power panel follow the game script.
- [x] SCX1: preserve the selected satellite across in-flight console commands and exclude remote player mirrors from console panel overlap callbacks.
- [x] PWR2: retry cleanup of managed breaker spinners without resetting native countdowns.
- [x] ROACH2: interpolate estimated crawl yaw on the existing wire and preserve native client tick authority.
- [x] ATVA2: use the visual-only ATV light function for remote state and keep each click positional.

- [x] BRM1: separate Broomba watch registration from layout resolution, wrap native turn targets without restoring over a new target, and feed active host Broombas into the existing prop drive lane.
- [x] PWR1: keep breaker restores bound to the live panel and terminal, cancel conflicts with newer authoritative changes, and restore the native button path outside multiplayer or when the replacement is unavailable.
- [x] ROACH1: send moving roach samples at the existing poll cadence, preserve surviving client components, and retry refused native adds within a fixed budget.
- [x] ATVA1: play positional headlight clicks from admitted ATV state changes and limit native engine emitter range.

- [x] PERF3a: preserve balance delivery and pending client native/HUD state across the every-frame same-session SetSession retry.
- [x] PERF3b: preserve serverbox delivery, native-apply debt and poll cadence across same-session Install retries while continuing watch installation attempts.

- [x] LS1: class-pure switch/root discovery with liveness retained on actual access; permit portable identities for keyless placed base/satellite light actors only. Existing installed local build 2492C08B includes this implementation; rendered acceptance remains in WORK/TESTLOG.
- [x] PERF1a: bounded32-class InstanceOffset working set with slot/serial invalidation and cached missing fields; preserve correct per-class reflection beyond capacity.
- [x] PERF1b: remove successful InstanceOffset resolution INFO output, including cache eviction churn; retain missing-field warnings.
- [x] PERF2: undo only the Desktop clone's unsafe task-pump1.5ms/64 shortcut and existing-ID census skip; canonical task/census logic already preserved its original behavior.

- [x] ENT5a–e: harden Pyramid playback rate, consumed-target safety, component/session binding, active correction and duplicate tail application.
- [x] ENT6a: reuse permanent dead-watch refusals without spending more name/keyed registration slots; keep equivalent-identity query behavior consistent.
- [x] ENT6b: remove five impossible inherited-native destruction script watches without altering mannequin interactions or native pacing.

- [x] ENT1: canonical Hoel/shot direct-hit source resolution, owned ignite delivery and harmless client shot births; preserve separate explosion damage route.
- [x] ENT2: bounded late NPC class/readiness recovery with canonical load paths, latest pose and lifecycle cancellation.
- [x] ENT3: nearest living ready peer for native Kavotia/Tentacle Ball/Antibreather player reads; broader per-family attacks/camera filters remain below.
- [x] ENT4: preserve native pyramid montage/timeline join phase including the consumed-wisp tail; keep legacy base IDs unchanged.

- [x] RG2: preserve native recovery camera position and exclude local ragdoll reconfiguration/restart during get-up.
- [ ] Check missing local ragdoll impact sound; blood is normal and lower damage comparison is against upright falling.
- [x] RG1: restore local selected-ragdoll collision-hit notifications after physics rebuild so native fall/impact damage can run.
- [x] ATVH4: stop mirror-only unbraked parked drift while preserving genuine authoritative coasting and current brake/drive/carry state.
- [x] MN10: reconcile duplicate exact-key walking mannequin identities on clients and preserve canonical held-stream identity through grab/release.
- [x] ATVH3: preserve settled wheel/body geometry during corrector teleports and avoid time-only parked retry rearming.
- [x] MN8: accept authoritative mannequin mirror bindings after held/driven adoption without requiring native and host keys to match.
- [x] MN9: admit native client hover/grab for parked walking mannequins without enabling a second AI or bypassing native frozen/held rules.
- [x] ATVH2: classify unoccupied/ungrabbed ATV rest from stable authoritative pose despite suspension velocity spikes, preserving real movement and carry.
- [x] MN6: use negotiated client-native frustum/render observations for mannequin sight without replacing native stalking.
- [x] MN7: mirror host-native mannequin stand state instead of inferring it from tiny streamed position changes.
- [x] MN4: mannequin stalking prioritizes the nearest eligible host/client with stable target preference and invalid-target replacement.
- [x] MN5: redirect the mannequin's native distance gate and MoveTo together, preserving native pacing and the actual navigation proxy.
- [x] MN3: preserve native mannequin hesitation and movement/damping transitions while incorporating remote sight.
- [x] MN1: client walker animation stands while held and rebases on release.
- [x] MN2: client walker animation history uses incarnation-safe references and explicit sample validity.
- [x] ATVH1: keep stationary grabbed ATV mirrors out of the parked-rig correction limit; preserve parked no-linear-write behavior. Rendered acceptance tracked in WORK.md.
- [x] Fix remote flashlights: every peer currently sees only their own light (bypassed spring-arm lag, disabled shadow map self-occlusion, drove light_R world transform directly).
- [x] FL2: make remote puppet light_R location/rotation absolute at spawn and upon authoritative state application, so movement in its attachment chain cannot overwrite the world-space beam pose between RefreshView updates.
- [ ] Fix the T-pose when players fall into ragdoll on every peer.
- [ ] Finish held-toy native variant schemas and hook/function ownership beyond
  the validated Dingus/ABPlush capture inputs; preserve bounded native work.
- [ ] Finish watoy start-cue and physics-hook ownership beyond the validated
  activation/audio/emitter schemas and native activation admission.
- [ ] Finish thrown-sponge foam and resource synchronization beyond window dabs.

## Events

- [ ] Complete remaining native adapters for Dreams, Ariral Reputation,
  Player-Activated, Time-Related, Endings, Story Mode, Random and Signal events:
  actual triggers, selected outcomes, phases/effects and current-state late join.
  Preserve personal dream/ending consequences, reputation tiers, calendar
  eligibility and story phases.
- [ ] treeSpawner: transport the host-selected tree's visibility, mesh scale,
  movement and lifetime; complete client producer suppression and late join.
  Then scale only the native 600–900-second spawn wait, preserving mode gates
  and the tree's render/proximity-dependent behavior.
- [ ] serverBreaker: finish protected-floppy consumption delivery to every
  recipient, protection/repair sound and delegate consequences. Preserve the
  host-selected box and native day/difficulty/upgrade probabilities; then scale
  only the eligible-roll wait (native 1200–600 seconds).
- [ ] Complete level/map-script event routes using the extracted map headers;
  preserve personal consequences and the native protection/exit branches.

## Shared items and resources

- [ ] Complete Console/events: Rufus/thiccfus cross-player targets/grabs, audio/HUD,
  corpse/loot, damage/explosion transport; llama marker lifecycle; Madness/variant
  AI, damage/effects and remaining funny-rule HUD/events and toy/squeeze sounds.
- [ ] Finish atomic cement/wall repair request/result with exact personal stock
  through hand/slot/drop, rejection and disconnect. Keep no-spend refusal until
  the transaction is complete; never replay an uncertain admitted native effect.
- [ ] Arbitrate concurrent water/cement/coal writers, stale in-flight records,
  dryTimer ownership and personal-hand transitions; finish final output/accounting.
- [ ] Finish personal debit/output/refund as one transaction, including cross-peer
  authority, personal overrides/raw takeObj and receipt-less recovery/persistence.
  Preserve the existing local base world-container escrow/refund boundary.
- [ ] Cover remaining dynamic grime sources and persist explosion/salt-rocket
  scorch provenance through full save/world reconstruction.
- [ ] Complete shared sky-event phases/cues, audio, delayed email/accounting and
  six pre-session latent continuations; add remaining custom effects/creatures.
- [ ] Complete remaining package/food subclass output routes and cross-peer
  portion/output arbitration, personal effects before host acceptance, native
  poison-pour/roach/rewards, uncertain-open refund recovery and persistence.
  Do not admit arbitrary replaceProp or replay uncertain effects.
- [ ] Fix remaining stow stalls beyond indexed save-record class restoration,
  while preserving pending item-state restoration.
- [ ] Fill missing model previews and custom-skin compatibility without removing
  rig exclusions. Keep skeletal scaling and arbitrary creature physics separate.

## Entity implementation

- [x] Pyramid: carry/reconstruct gather progress for mid-sequence late join through optional native montage/timeline state and a consumed-wisp tail.
- [ ] Grayboar: complete join/audio/time coordination, host targets, turn/pitch,
  helper/delegate retirement, held-item handoffs, weapon/collision damage, one
  corpse/loot, lightning replacement and charged explosion/punch/spin effects.
- [ ] Rozitals: complete Great Pyramid, Kavotia, Pagonioplo-Igetis, Scouts and
  Soltomia action/effect adapters. Kavotia needs fire/laying montage, speech,
  canonical beam/impact/damage and exact phase timing.
- [ ] Antibreather / Fossilhound: complete player/cave-dependent Antibreather
  spawning, cross-peer attacks and Fossilhound ticker/attack routes.
- [ ] Arirals: complete follower/shooter/pigBeater and gun-stealer behavior,
  stolen/held/thrown-item ownership, attachments, shots/hits and event phases.
  Slapper Character classification and exact summoner spawn admission are
  implemented; its attacks, item handoffs and effects still need adapters.
- [ ] Greys / Flamethrower Tank: complete grayTest targets/actions/effects and
  firetank turret, fire, fuel, damage and destruction.
- [ ] Geoms: complete Ramiel (geomOcta_C) movement/charge/beam/audio/damage and
  Egg-shaped Geom (eg_C) retrieval/landing/special actions.
- [ ] UFOs: complete child spawns, beam/material/audio phases and join state for
  Boomerang, Joil, Grey Ship, Midas, Pill, Trifo and Orbs; resolve Grey Ship names.
- [ ] Deer / Fish / Cockroach: complete deer/deer_ded spawn/death/rejoin,
  caught prop_fish_0..21_C inventory and roach movement/eating/crushing/join state.
- [ ] Blue Brain Cheese, Restocker, Eye, Wolfgang, Tree with Eyes, Living Mushroom,
  Space Jellyfish: complete exact behavior/effect routes. Wolfgang derives from
  actor_save; do not map every eyes entity to eyer.
- [ ] Bone Man, Bloody Skeleton, Curious Eyes, Mimic, Flesh Slug, Zombie Deer:
  complete attacks/effects/lifetime, missing name mappings, shared mimic
  rendering/disguise/damage and centipede visibility.
- [ ] Ghost, Ghost Log, Fireflies, Walking Mannequin, Black Spot: complete missing
  names/effects/phases. Finish remaining wisp limb/blood presentation beyond the
  implemented animation-readiness and body/world-bound grab/release cancellation.
- [ ] Demons: complete individual targets, rituals/run-end behavior for SKerfur-O,
  Eyeless Insomniac, Insomnia Demon, Abandoned Kerfur, Flesh Erie, Goat Statue,
  Stolas Statue and Furfur. Keep live/zombie/angry Flesh Erie classes distinct.
- [ ] Forbidden: complete missing class mappings, spawn availability and local/global
  effects for Black Friend, CorpseWalker, Crasher, Fast-shadow Figure, Fridge,
  Interloper, The Exit Door, The Head, Pool Entity, Mountain People, Cursed Spider,
  Rune Sphere, Shadow Figure, Fake Kel and Black Sphere. Keep debug actors out of normal play.
- [ ] Living Erie / Girufus / Rufus: complete Girufus mapping and distinct
  interaction, animation, damage, ownership and lifetime behavior.
- [ ] Gnomes / Krampus: complete group spawning, presents, revenge, interaction,
  damage/death/shared lifetime and Krampus attack/fireball/jump/target/despawn.

## Requested mod integrations

- [ ] EmoteWheel 1.0.1: replace the upstream empty network bridge with owned
  emote/variant/phase/music state, interruption, late join and body/world cleanup.
- [ ] VEmu 1.1.0: define shared console/cartridge/AV/input ownership and emulator
  state/audio/video transport. Upstream AV output excludes SmartTV; an explicit
  adapter is required to support that combination.
- [ ] JeepMod 0.11.1: finish custom trunk/key/container/upgrade routes beyond
  inherited ATV pose/seat admission; preserve one shared inventory and lifetime.
- [ ] PhotographyPlus 0.10.3: implement task/reward/save ownership and photo
  submission routing without duplicating host rewards or another player's photos.
- [ ] SmartTV 0.9.6: implement custom player mode/source/playback/join ownership
  beyond vanilla media routes; keep local device/OBS access local.
- [ ] Audit FireTower 1.0.4 door/ModActor ownership and add any missing shared
  interaction/lifetime routes; retain matching local scenery and collision.

## Local shared white lightning orb fix — 1 November 2026

Named implementation items: HOEL1 host orb birth/pose/lifetime and join state; HOEL2 host beam presentation with client native damage refusal.
- [x] HOEL1: native superFog_C -> HoelUfo_C and HoelUfo_C -> hoelUfoAttack_C exact output sources, WorldActor allowlist and host lifecycle.
- [x] HOEL2: bounded endpoint birth bytes; client BeginPlay refusal/readiness; cosmetic particle endpoint and half-second hide; transient beams excluded from join snapshots; PE author cannot pre-enroll an empty beam.
- [ ] Two-computer rendered encounter, sound/beam parity, host world consequences, event-end/rejoin and general event coverage acceptance remain unperformed.
The patch does not change personal metaparanoia, protocol2217 or public package2163. It is a local custom build from matching baseline90fd9e6, not a new qualified upstream release.

## Local cadence repair — 2 November 2026

- [x] CAD1: restore pre-Antigravity every-frame subsystem pumping, including client interpolation/application and authoritative clock sampling; keep each subsystem's existing elapsed-time throttles.
- [x] CAD2: retain and verify the reseed drain's32-candidate cap alongside its elapsed-time budget.
- [ ] Rendered host/client drone movement, desk cursor/simulation smoothness, accelerated clock and performance comparison remain user acceptance checks.
No package/protocol bump and no unrelated gameplay repairs are included.
