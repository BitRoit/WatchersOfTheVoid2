#pragma once
#include "ue_wrap/core/types.h"
#include <cmath>
#include <cstddef>
#include <cstring>
namespace ue_wrap::hoel_ufo {
// Existing WorldActor birth bytes; no protocol layout change.
inline bool DecodeEndpoint(const void* bytes, size_t size, FVector& out) {
    if (!bytes || size != sizeof(FVector)) return false;
    FVector p; std::memcpy(&p, bytes, sizeof(p));
    if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z) ||
        std::fabs(p.X)>1.0e6f || std::fabs(p.Y)>1.0e6f || std::fabs(p.Z)>1.0e6f) return false;
    out=p; return true;
}
bool ReadBeamEndpoint(void* actor, FVector& out);
// BeginPlay is refused by spawn_authority: this only paints its particle.
bool PaintBeam(void* actor, const FVector& endpoint);
void TickBeamPresentation(void* actor);
}
