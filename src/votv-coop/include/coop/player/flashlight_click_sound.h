// coop/player/flashlight_click_sound.h -- a 3D positional click at a puppet's location when a
// remote peer toggles their flashlight.
//
// Plays the game's own `flashlight` USoundWave through UGameplayStatics::PlaySoundAtLocation
// with a USoundAttenuation we build at runtime, so no cooked `att_*` content asset is borrowed.
// Sphere, linear distance (distanceAlgorithm = 0): extents[0] = 100 cm (1 m) full volume,
// falloffDistance = 900 cm (9 m), with total inaudibility at >= 1000 units (10 m).
// That baseline guarantees clicks do not propagate to players 1000+ units away.
// SpawnSoundAttenuation pins the object for the process, since a C++ static is invisible to
// UE's reachability scan.
//
// Only a state CHANGE clicks: a hold-F mode-change packet keeps state=on and moves the cones
// alone. The last applied state is kept per peer SLOT rather than per puppet pointer, which
// would dangle after a disconnect and a respawn into the same slot. Game thread only -- the
// reflection calls touch UObject memory directly.

#pragma once

#include <cstdint>

namespace coop::flashlight_click_sound {

// Play the click for a wire packet's just-applied state on `puppetActor`. A `newState` equal to
// the last one applied for `peerSlot` is a no-op: that packet was a mode change. Otherwise the
// sound asset and attenuation are resolved with live incarnation checks. Invalid/local/
// unregistered bodies are refused. Preparation failure keeps the state retryable on a
// subsequent apply; one admitted native attempt commits the latch. Session/world/body
// replacement retires the latch, and nested calls cannot complete over a newer state.
// Game thread only.
void PlayIfStateChanged(void* puppetActor, uint8_t peerSlot, bool newState);

// Play one admitted remote ATV headlight edge. The ATV receiver owns the baseline.
void PlayAtvHeadlightClick(void* atvActor, bool enabled);

}  // namespace coop::flashlight_click_sound
