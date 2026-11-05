// ue_wrap/devices/roomba.h -- engine access and native bug mitigation for the Broomba
// (prop_roomba_C / prop_roomba_mx_C). Principle-7 wrapper layer: no gameplay or network logic.
// Mitigates native Blueprint angular wrap across +/-180 deg in NearlyEqual_FloatFloat, prevents
// endless spinning on collision or obstacle obstruction, and suppresses client wheel torque during
// multiplayer sessions.

#pragma once

#include <cstdint>

namespace ue_wrap::roomba {

// Resolve prop_roomba_C and its property offsets. Idempotent; true once resolved. Game thread.
bool EnsureResolved();

// True iff obj's class is prop_roomba_C or a subclass (e.g. prop_roomba_mx_C). False if not yet resolved.
bool IsRoomba(void* obj);

// Hook prop_roomba_C::ReceiveTick via script_gate to correct angular wrap and stuck rotation.
bool Install();

// Inform roomba wrapper whether the local peer is a multiplayer client (to suppress local physics torque).
void SetClientRole(bool isClient);

}  // namespace ue_wrap::roomba
