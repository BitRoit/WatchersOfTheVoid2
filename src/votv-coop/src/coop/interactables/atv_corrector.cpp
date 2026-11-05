// coop/interactables/atv_corrector.cpp -- see coop/interactables/atv_corrector.h. The corrector
// for an ATV this peer does not author: a velocity bias between packets, a cut when the error
// is too far to converge, and coherent horizontal motion for an idle mirror.

#include "coop/interactables/atv_corrector.h"

#include "coop/config/config.h"
#include "coop/net/protocol.h"

#include "ue_wrap/core/log.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/engine/engine.h"

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace coop::atv_corrector {

namespace A = ue_wrap::atv;
using ue_wrap::FVector;
using ue_wrap::FRotator;
using coop::atv_sync::AtvEntry;
using coop::atv_sync::NowMs;
using coop::atv_sync::Len;

namespace {

// The warp distance is speed-scaled, after MTA's CClientVehicle, but sized off the measured
// rig: the ATV is about 2 m long and its native suspension travel is a few centimetres, so 2 m
// plus half a second of travel is too far apart to close smoothly.
constexpr float kWarpBaseCm     = 200.f;
constexpr float kWarpPerSpeedS  = 0.5f;
constexpr float kWarpAngleDeg   = 45.f;   // orientation alone can justify a warp: a body can spin in place
constexpr float kCorrDeadbandCm = 5.f;    // inside this, the wire velocity alone is the correction
constexpr float kCorrMaxCmS     = 400.f;  // bound the corrective term: it must nudge, never launch
// The corrective velocity closes kCorrGain of the remaining error by the time the next packet
// is due, over the measured interval since the last one. A fixed window cannot: this lane has
// two cadences (50 ms authored, 200 ms idle), and an error over a fixed 0.1 s held for 200 ms
// travels twice the error, overshoots and oscillates at constant amplitude, which looks
// exactly like the jitter this exists to remove. A gain below 1 over the real interval
// converges geometrically at any cadence.
constexpr float kCorrGain     = 0.5f;
// The corrector must converge or cut. A velocity nudge corrects a moving body and is powerless
// against a resting one: a mirror that came to rest 40 cm below the host's, both still, sees
// a 20 cm/s upward nudge erased by gravity in 20 ms, and the error stands. So the corrector
// watches itself: if the distance stops shrinking while still outside the deadband, the nudge
// is not working and it stops pretending. Cadence-independent by construction, counting
// packets in which the error failed to shrink, with no threshold on velocity, the quantity
// that was lying about whether convergence was possible.
constexpr float kStallShrinkFrac  = 0.95f;  // "shrank" = at least 5% closer than last packet
constexpr int   kStallWarpPackets = 5;      // 250 ms on the drive lane, 1 s on the idle one
// Keep root-only vertical writes away from a settled constraint rig: they woke
// the chassis independently of its wheels and repeatedly disturbed suspension.
// This band is not a declaration that the mirror has stopped. Even below it an
// unbraked author may coast; coherent planar motion is matched on all four roots.
constexpr float kRestLinCmS  = 5.f;
constexpr float kRestAngDegS = 5.f;
constexpr uint64_t kRestPoseMs = 1000;
constexpr uint64_t kRestPoseGapMs = 3000; // idle keepalive is 2 s; a longer silence proves nothing
constexpr float kRestPoseCm = 5.f;
constexpr float kRestPoseDeg = 3.f;
// How many times a resting mirror is re-placed before admitting the pose lane cannot hold it:
// three distinguishes settled from cannot-settle-here, and past that the difference is under
// the vehicle, where saying so beats teleporting forever.
constexpr int   kRestMaxReplaces = 3;
// A rest episode ends on actual author movement/ownership change, never elapsed
// wall time. A time-based reset made a stationary rig teleport three more times
// every ten seconds for the entire session.

constexpr uint64_t kCorrMinDtMs = 20;    // a burst must not manufacture a huge corrective velocity
constexpr uint64_t kCorrMaxDtMs = 1000;  // a long gap must not manufacture a vanishing one

// Ceilings for a velocity that arrives over the wire, well above anything the vehicle reaches
// (its own speed_turbo is 3200), so a real throw or fall lands intact.
constexpr float kMaxWireLinCmS  = 20000.f;
constexpr float kMaxWireAngDegS = 3600.f;

FVector ClampVelocity(const FVector& v, float maxMag) {
    const float m = Len(v);
    if (!(m > maxMag)) return v;   // inverted, so the property holds against a NaN
    const float k = maxMag / m;
    return FVector{ v.X * k, v.Y * k, v.Z * k };
}

uint64_t g_warps = 0;        // diagnostic counters: a corrector nobody can see is one nobody can falsify
uint64_t g_corrs = 0;
uint64_t g_stallWarps = 0;   // ...and specifically: how often the nudge had to give up
uint64_t g_restPlaces = 0;   // ...and how often a PARKED author's pose had to be re-placed

// The value the lane acts on, logged where it is written: the received wire velocity at the
// instant the corrector acts, since a mirror sinking the moment it stops authoring cannot be
// explained from a log that samples each peer's own root velocity at another time.
// Rate-limited to about 1 Hz for the routine case; every cut logs unconditionally, since the
// cut is the instant in question.
uint64_t g_lastSampleLogMs = 0;
constexpr uint64_t kSampleLogMs = 1000;

// The one write rule, at every site that writes a velocity onto a mirror: assigning a linear
// velocity to one body of a settled constraint rig wakes it and it sinks, so the rule is about
// the linear component alone and applied here rather than at one branch. A gate on a whole
// early return let the warp arm keep writing a linear velocity onto a resting mirror, and was
// defeated by a parked-but-rocking author whose angular velocity exceeded the band and routed
// the packet onto the full write path. Two quantities, two gates, one place.
void WriteMirrorVelocity(void* actor, const FVector& lin, const FVector& ang, bool linAtRest) {
    if (linAtRest) {
        ue_wrap::engine::SetActorRootPhysicsAngularVelocity(actor, ang);
        return;
    }
    ue_wrap::engine::SetActorRootPhysicsVelocity(actor, lin, ang);
}

void LogWire(const char* what, const AtvEntry& e, float dist,
             const FVector& wireLin, const FVector& cur, const FVector& wirePos) {
    UE_LOGI("[ATVC] %s key='%ls' dist=%.1f cur.z=%.1f wire.z=%.1f wireLin=(%.1f,%.1f,%.1f) |v|=%.1f "
            "stall=%d", what, A::GetKeyString(e.actor).c_str(), dist, cur.Z, wirePos.Z,
            wireLin.X, wireLin.Y, wireLin.Z, Len(wireLin), e.stallPackets);
}

bool MatchIdlePlanar(AtvEntry& e,const coop::net::AtvStatePayload& p,const FVector& desired,uint64_t now) {
    const bool logDue=now-e.lastPlanarCorrectionLogMs>=5000;
    if(logDue)e.lastPlanarCorrectionLogMs=now;
    const std::wstring key=logDue?A::GetKeyString(e.actor):std::wstring{};
    FVector before{};int writes=0;
    const bool ok=A::MatchRigPlanarVelocity(e.actor,desired,before,writes);
    if(logDue){
        UE_LOGI("[ATVC] PLANAR key='%ls' ok=%d writes=%d brake=%d mirror=(%.2f,%.2f,%.2f) desired=(%.2f,%.2f) wire=(%.2f,%.2f,%.2f)",
            key.c_str(),ok,writes,(p.stateBits&coop::net::kAtvStateBitBrake)!=0,
            before.X,before.Y,before.Z,desired.X,desired.Y,p.linVelX,p.linVelY,p.linVelZ);
    }
    if(ok&&writes)++g_corrs;
    return ok;
}

}  // namespace

// The corrector, called only on packet arrival for an ATV this peer does not author; there is
// no per-frame mirror work. The body simulates between packets; its velocity is biased so it
// converges, and it is cut to the authority's pose when too far gone to converge gracefully.
// MTA's shape with one divergence: their pure-sync read writes the wire velocity hard every
// packet, as this does, and then nudges the transform by a per-frame slice of the position
// error; this biases velocity instead, because their vehicle is one rigid body and the ATV is
// a five-body constraint rig, where a per-frame root nudge stretches the suspension and axle
// bodies by the slice every frame, the very defect this model removes. Steering by velocity
// leaves the solver free to keep the rig rigid.
void ApplyCorrection(AtvEntry& e, const coop::net::AtvStatePayload& p, bool snap) {
    // The control arm, resolved once: a config read per packet on a lane at 20 Hz per vehicle is
    // not free, and the value cannot change mid-session. Off, this function is the only thing that
    // stops: the rig still simulates, still receives vitals, still runs its own tick, so the two
    // arms are a single-variable comparison.
    static const bool sEnabled =
        ::coop::config::ResolveFlag(::coop::config_registry::rows::atv_corrector);
    if (!sEnabled) return;

    FVector cur; FRotator curRot;
    if (!A::GetRootTransform(e.actor, cur, curRot)) return;

    const FVector wirePos{ p.x, p.y, p.z };
    const FRotator wireRot{ p.pitch, p.yaw, p.roll };
    // Finite is not the same as sane: these go straight into PhysX. The dispatch rejects NaN and
    // Inf; this bounds the finite-but-absurd. Client-scoped by the standing rule: a symmetric
    // clamp would be the bug, since the host is authoritative about physics.
    const FVector wireLin = ClampVelocity({ p.linVelX, p.linVelY, p.linVelZ }, kMaxWireLinCmS);
    const FVector wireAng = ClampVelocity({ p.angVelX, p.angVelY, p.angVelZ }, kMaxWireAngDegS);

    const FVector err{ wirePos.X - cur.X, wirePos.Y - cur.Y, wirePos.Z - cur.Z };
    const float dist  = Len(err);
    const float warpD = kWarpBaseCm + kWarpPerSpeedS * Len(wireLin);
    const float dPitch = std::fabs(ue_wrap::NormalizeAxis(wireRot.Pitch - curRot.Pitch));
    const float dYaw   = std::fabs(ue_wrap::NormalizeAxis(wireRot.Yaw   - curRot.Yaw));
    const float dRoll  = std::fabs(ue_wrap::NormalizeAxis(wireRot.Roll  - curRot.Roll));

    // The two gates, computed once and read by every write site below.
    // A grav-hand can hold the authority still in mid-air. Its mirror has no
    // matching hand constraint, so it still needs velocity/position correction
    // against gravity. Treating it as parked spends three re-places and then
    // leaves it on the floor. The already-authorized grab bit distinguishes it
    // from an ordinary settled rig, whose no-linear-write protection stays intact.
    const bool carried = (p.stateBits & coop::net::kAtvStateBitGrabbed) != 0;
    const uint64_t now = NowMs();
    // A parked native suspension can report root speeds above the rest band while
    // its pose only rocks a few centimetres. Writing those speeds wakes the mirror
    // and restarts its fall. Confirm rest against a FIXED wire-pose anchor, never
    // against the mirror or only the preceding packet (slow travel accumulates).
    // A driver, grabber, adoption or receive gap invalidates that observation.
    const bool idle = !carried && p.occupantSlot == 0xFF;
    const bool newAuthority = e.restAnchorSyncer != p.syncerSlot;
    const FVector anchorDelta{wirePos.X-e.restAnchorPos.X,wirePos.Y-e.restAnchorPos.Y,wirePos.Z-e.restAnchorPos.Z};
    const bool withinAnchor = Len(anchorDelta) <= kRestPoseCm &&
        std::fabs(ue_wrap::NormalizeAxis(wireRot.Pitch-e.restAnchorRot.Pitch)) <= kRestPoseDeg &&
        std::fabs(ue_wrap::NormalizeAxis(wireRot.Yaw-e.restAnchorRot.Yaw)) <= kRestPoseDeg &&
        std::fabs(ue_wrap::NormalizeAxis(wireRot.Roll-e.restAnchorRot.Roll)) <= kRestPoseDeg;
    if(e.restAnchorSinceMs && !withinAnchor) e.restReplaces = 0;
    if (!idle || snap || !e.lastPktMs || newAuthority) e.restReplaces = 0;
    if (idle  || snap || !e.lastPktMs || newAuthority) e.seatedReplaces = 0;
    if (!idle || snap || !e.lastPktMs || now - e.lastPktMs > kRestPoseGapMs || newAuthority)
        e.restAnchorSinceMs = 0;
    e.restAnchorSyncer = p.syncerSlot;
    bool poseAtRest = false;
    if (idle) {
        const bool within = e.restAnchorSinceMs && withinAnchor;
        if (!within) {
            // A packet gap invalidates confidence but does not grant another
            // parked teleport budget. Only displacement/rotation of an existing
            // authority anchor does that.
            if(e.restAnchorSinceMs) e.restReplaces = 0;
            e.restAnchorPos = wirePos;
            e.restAnchorRot = wireRot;
            e.restAnchorSinceMs = now;
        } else poseAtRest = now - e.restAnchorSinceMs >= kRestPoseMs;
    }
    const bool linAtRest = !carried && (Len(wireLin) <= kRestLinCmS || poseAtRest);
    const bool angAtRest = Len(wireAng) <= kRestAngDegS || poseAtRest;
    if(now-e.lastCorrectionDecisionMs >= 5000) {
        e.lastCorrectionDecisionMs=now;
        const FVector d{wirePos.X-e.restAnchorPos.X,wirePos.Y-e.restAnchorPos.Y,wirePos.Z-e.restAnchorPos.Z};
        UE_LOGI("[ATVC] DECISION key='%ls' mode=%s source=%u seat=%u grabbed=%d speed=%.1f angular=%.1f anchorAge=%llu anchorDelta=%.1f error=%.1f places=%d cur=(%.1f,%.1f,%.1f) wire=(%.1f,%.1f,%.1f)",
            A::GetKeyString(e.actor).c_str(),poseAtRest?"RESTPOSE":linAtRest&&angAtRest?"RESTVEL":"MOVING",
            static_cast<unsigned>(p.syncerSlot),static_cast<unsigned>(p.occupantSlot),carried,
            Len(wireLin),Len(wireAng),static_cast<unsigned long long>(e.restAnchorSinceMs?now-e.restAnchorSinceMs:0),Len(d),dist,idle ? e.restReplaces : e.seatedReplaces,
            cur.X,cur.Y,cur.Z,wirePos.X,wirePos.Y,wirePos.Z);
    }

    const uint64_t rawDt = e.lastPktMs ? (now - e.lastPktMs) : kCorrMaxDtMs;
    const uint64_t dtMs = rawDt < kCorrMinDtMs ? kCorrMinDtMs
                                               : (rawDt > kCorrMaxDtMs ? kCorrMaxDtMs : rawDt);
    const bool haveMotionSample=e.haveCorrectionWirePos&&e.lastPktMs&&rawDt>0&&rawDt<=kRestPoseGapMs&&!snap&&!newAuthority;
    FVector idleMotion=wireLin;
    if(poseAtRest&&haveMotionSample){
        // A stable pose can coexist with suspension speed spikes. Actual planar
        // travel also preserves an unbraked 1cm/s coast below the old rest gate.
        idleMotion.X=(wirePos.X-e.lastCorrectionWirePos.X)*1000.f/static_cast<float>(rawDt);
        idleMotion.Y=(wirePos.Y-e.lastCorrectionWirePos.Y)*1000.f/static_cast<float>(rawDt);
    }
    e.lastCorrectionWirePos=wirePos;e.haveCorrectionWirePos=true;
    e.lastPktMs = now;

    // Every test is inverted on purpose, MTA's own idiom: a comparison against NaN is false, so
    // written this way a NaN warps rather than feeding a corrective velocity computed from
    // garbage. The three angles are tested separately: folding them through a max first destroys
    // the property, since a nested-ternary max silently returns the finite operand.
    if (snap || !(dist <= warpD) ||
        !(dPitch <= kWarpAngleDeg) || !(dYaw <= kWarpAngleDeg) || !(dRoll <= kWarpAngleDeg)) {
        // Fail closed: if the rig could not be re-placed (the teleport unresolved after a game
        // update), do not write the authority's velocity onto a body still in the wrong place; that
        // accelerates the error. Logged after the teleport succeeds; an instrument reporting an
        // action it did not take is worse than none.
        if (!A::TeleportRigPreservingShape(e.actor, wirePos, wireRot)) return;
        LogWire("WARP", e, dist, wireLin, cur, wirePos);
        if(idle&&linAtRest&&angAtRest)MatchIdlePlanar(e,p,idleMotion,now);
        else WriteMirrorVelocity(e.actor, wireLin, wireAng, linAtRest);
        ++g_warps;
        return;
    }

    // A low wire speed does not mean the mirror has stopped. Pose-only replaces
    // retained its horizontal momentum and rolled away again (fresh 9F730A logs).
    // Correct common planar motion across ALL native roots, preserving each Z
    // velocity, relative suspension motion, angular velocity and actual brake bit.
    if (linAtRest && angAtRest) {
        e.stallPackets = 0;
        e.lastErrCm    = -1.f;
        const FVector idleBaseMotion=idleMotion;
        const float planarError=std::sqrt(err.X*err.X+err.Y*err.Y);
        if(idle&&planarError>kCorrDeadbandCm){
            // Idle packets may be 2s apart. Use that real interval rather than
            // the moving-lane 1s cap, which would overshoot the correction.
            const float interval=static_cast<float>(std::max(kCorrMinDtMs,std::min(rawDt,kRestPoseGapMs)));
            const float gain=kCorrGain*1000.f/interval;
            const FVector corr=ClampVelocity({err.X*gain,err.Y*gain,0},kCorrMaxCmS);
            idleMotion.X+=corr.X;idleMotion.Y+=corr.Y;
        }
        if(idle&&!MatchIdlePlanar(e,p,idleMotion,now))return;
        // Horizontal error converges through velocity; teleporting for it creates
        // a visible snap. Retain the bounded vertical recovery independently.
        if ((idle?std::fabs(err.Z):dist) <= kCorrDeadbandCm) return;
        int& replaces = idle ? e.restReplaces : e.seatedReplaces;
        if (e.restReplaces >= kRestMaxReplaces) return;   // already said our piece, below
        if (replaces >= kRestMaxReplaces) return;
        if (!A::TeleportRigPreservingShape(e.actor, wirePos, wireRot)) return;
        if(idle&&!MatchIdlePlanar(e,p,idleBaseMotion,now))return; // completed placement no longer needs position bias
        ++replaces;
        e.lastRestPlaceMs = now;
        ++g_restPlaces;
        // Keep recovery bounded even if the peers' vertical contacts disagree.
        if (replaces >= kRestMaxReplaces) {
            UE_LOGW("atv: %s mirror key='%ls' still has vertical mismatch after %d re-places "
                    "(before-place dz=%.1f cm). Compare [ATVR] geometry and [ATVC] PLANAR motion on both peers.",
                    idle ? "idle" : "seated", A::GetKeyString(e.actor).c_str(), kRestMaxReplaces, err.Z);
        }
        return;
    } else {
        e.restReplaces   = 0;
        e.seatedReplaces = 0;
    }

    // Is the correction working? Count packets where the error stayed outside the deadband and
    // refused to shrink; past the limit, cut instead of nudging.
    if (dist <= kCorrDeadbandCm) {
        e.stallPackets = 0;
    } else if (e.lastErrCm >= 0.f && dist >= e.lastErrCm * kStallShrinkFrac) {
        ++e.stallPackets;
    } else {
        e.stallPackets = 0;
    }
    e.lastErrCm = dist;
    if (e.stallPackets >= kStallWarpPackets) {
        e.stallPackets = 0;
        e.lastErrCm = -1.f;
        if (!A::TeleportRigPreservingShape(e.actor, wirePos, wireRot)) return;
        LogWire("CUT", e, dist, wireLin, cur, wirePos);
        WriteMirrorVelocity(e.actor, wireLin, wireAng, linAtRest);
        ++g_stallWarps;
        UE_LOGI("atv: correction stalled at %.1f cm -- cut to the authority's pose "
                "(a nudge cannot move a body at rest)", dist);
        return;
    }

    FVector lin = wireLin;
    // The corrective term is a linear push, so the linear gate governs it too: a parked-but-rocking
    // author reaches here, and pushing its resting mirror is the defect.
    if (dist > kCorrDeadbandCm && !linAtRest) {
        // Close kCorrGain of the error over the observed interval, not a fixed window (see the
        // constant).
        const float gain = kCorrGain * 1000.f / static_cast<float>(dtMs);
        FVector corr{ err.X * gain, err.Y * gain, err.Z * gain };
        const float mag = Len(corr);
        if (mag > kCorrMaxCmS) {
            const float k = kCorrMaxCmS / mag;
            corr.X *= k; corr.Y *= k; corr.Z *= k;
        }
        lin.X += corr.X; lin.Y += corr.Y; lin.Z += corr.Z;
    }
    // Rotation gets no continuous corrective term: mapping a rotator delta onto an angular
    // velocity is only exact for small aligned deltas, and an ATV on the ground takes its
    // orientation from the terrain once its position and velocity agree. Orientation divergence
    // is caught by the angle warp arm instead: one measurable mechanism, not a guessed gain.
    if (now - g_lastSampleLogMs >= kSampleLogMs) {
        g_lastSampleLogMs = now;
        LogWire(dist > kCorrDeadbandCm ? "NUDGE" : "INBAND", e, dist, wireLin, cur, wirePos);
    }
    WriteMirrorVelocity(e.actor, lin, wireAng, linAtRest);
    ++g_corrs;
}

Counters ReadCounters() {
    return Counters{ g_corrs, g_warps, g_stallWarps, g_restPlaces };
}

}  // namespace coop::atv_corrector
