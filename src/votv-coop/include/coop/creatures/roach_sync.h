// coop/creatures/roach_sync.h -- the host-authoritative roach-infestation mirror.
//
// Roaches are NOT actors: they are UStaticMeshComponent entries in the `roaches` TArray of the ONE
// world-anchored AcockroachMaster_C, capped at 128 by its CDO. The simulation is SHARED-WORLD state
// rather than a per-player effect -- nests spawn near FOOD props, roaches multiply at an existing
// roach's location, and calc(), driven from the master's ReceiveTick, moves each roach AND mutates
// shared props: it drains prop_food_C.foodData while eating, destroys depleted food and grows the
// roach's scale. Any peer can stomp one, producing a prop_deadRoach_C husk, or eat one. So a client
// running its own simulation diverges the shared food and roach state.
//
// The design mirrors STATE and drives the notify-free re-appliers, the serverbox shape. Principle
// 7: this is coop/ network logic, and every engine access goes through ue_wrap reflection and call.
// One concept, one folder -- a roach infestation is a creature system.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct RoachStatePayload;
struct RoachConsumedPayload;
}  // namespace coop::net

namespace coop::roach_sync {

// Cache the session pointer (boot-lifetime; the session_holder pattern).
void Install(coop::net::Session* session);

// TickGameplay driver (game thread).
//
// HOST: poll the roaches array and broadcast a PAGED full snapshot -- RoachState carries a seq, a
// page, and per roach a location and scale -- on a population change, on drift, and as a periodic
// keepalive.
//
// CLIENT: interpolate admitted mirrors every frame; at 1 Hz check for local consumption, retry
// pending samples and attempt bounded native births. Horizontal yaw is estimated from successive
// host positions, while existing local pitch/roll is retained; the wire has no rotation, so this
// cannot reproduce stationary turns or changing surface orientation. Client SUPPRESSION lives in
// coop/world/spawn_authority: it refuses the cockroachMaster_C and ticker_roachSummoner_C ticks,
// summonRoach, and three timer delegates. Native eat/stomp interactions stay available.
void Tick();

// CLIENT receive: assemble the pages of one snapshot seq and apply when complete, checked to have
// come from the host. Game thread (event_feed drain).
//
// Snapshots contain positions and scales in host ordinal order, but no identity. On population
// growth the existing prefix is retained, matching native append behavior; on shrink an ordered
// proximity match chooses survivors. Simultaneous births/deletions and nearby roaches are therefore
// ambiguous. Unmatched components use the game's deleteRoach(idx, crush=false) and missing entries
// use addRoach(loc, size, bypassCheck=true). Native camera, population and placement gates can refuse
// an add, so admission is checked and missing entries retry without rebuilding the survivors.
void OnState(const coop::net::RoachStatePayload& payload);

// HOST receive: a client consumed a roach locally, by eating or stomping it -- delete the nearest
// live roach within the adjudication radius. The next snapshot converges everyone. Game thread.
//
// The dead-roach husk the acting peer spawns locally is NOT mirrored: its BeginDeferred runs inside
// the script function tryCrush, which ProcessEvent cannot see, and the husk is a 300 s cosmetic. A
// known gap, not a correctness hole.
void OnConsumedIntent(const coop::net::RoachConsumedPayload& payload, int senderPeerSlot);

// HOST: unconditional current-population snapshot to a world-ready joiner
// (the serverbox connect-replay shape).
void QueueConnectBroadcastForSlot(int slot);

// Session teardown: clear assembly buffers, baselines, and the tracked set.
void OnDisconnect();

}  // namespace coop::roach_sync
