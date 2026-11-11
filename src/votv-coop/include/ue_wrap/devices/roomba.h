// ue_wrap/devices/roomba.h -- engine access and native bug mitigation for the Broomba
// (prop_roomba_C / prop_roomba_mx_C). Principle-7 wrapper layer: no gameplay or network logic.
// Mitigates the native Blueprint's scalar yaw comparison across angle branches.

#pragma once

#include <cstdint>

namespace ue_wrap::roomba {

// Resolve prop_roomba_C and its property offsets. Idempotent; true once resolved. Game thread.
bool EnsureResolved();

// True iff obj's class is prop_roomba_C or a subclass (e.g. prop_roomba_mx_C). False if not yet resolved.
bool IsRoomba(void* obj);

// Register the prop_roomba_C::ReceiveTick angular-wrap watch. Safe before class load.
bool Install();

// Read active on a fresh, already identified Roomba object. Game thread.
bool IsActive(void* obj);

}  // namespace ue_wrap::roomba
