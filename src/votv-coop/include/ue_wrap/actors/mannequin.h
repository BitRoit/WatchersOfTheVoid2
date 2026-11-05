#pragma once

namespace ue_wrap::mannequin {
// Same local-view predicate as prop_wMannequin's native graph: its SkeletalMesh
// bounds in the actual player's frustum AND WasRecentlyRendered(3 seconds).
// Does not run AI, set looking/saw, or change physics. False means unreadable;
// a successful read with seen=false is an ordinary off-screen/occluded result.
bool TryReadLocalSight(void* walker, void* localPlayer, bool& seen);
}
