#include "coop/creatures/mannequin_sync.h"
#include "coop/creatures/mannequin_target.h"
#include "coop/creatures/mannequin_sight_sync.h"
#include "coop/creatures/mannequin_interaction.h"
#include "coop/creatures/mannequin_identity.h"

#include "coop/net/session.h"
#include "coop/player/local_prop_hold.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/prop_drive_host.h"
#include "coop/props/prop_element_tracker.h"
#include "coop/props/prop_snapshot.h"
#include "coop/props/prop_synth_key.h"

#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/mannequin.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/sdk_profile.h"
#include "ue_wrap/core/trace.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/engine/world_identity.h"

#include <windows.h>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace coop::mannequin_sync {
namespace {

namespace R  = ue_wrap::reflection;
namespace SG = ue_wrap::script_gate;
namespace OI = ue_wrap::object_index;
namespace WI = ue_wrap::world_identity;

std::atomic<net::Session*> g_session{nullptr};

struct ClientWalkerState {
    ue_wrap::CachedObjRef actor;
    ue_wrap::FVector lastPos{};
    uint64_t lastMovedMs = 0;
    bool haveSample = false;
    bool haveMovement = false;
};
std::unordered_map<void*, ClientWalkerState> g_clientWalkers;

struct RemoteSightScope {
    ue_wrap::CachedObjRef actor;
    bool wasLooking = false;
};
std::unordered_map<uint64_t, RemoteSightScope> g_remoteSightScopes;

bool IsWalker(void* actor) {
    if (!actor || !R::IsLive(actor)) return false;
    void* cls = R::ClassOf(actor);
    for (int depth = 0; depth < 64 && cls; ++depth, cls = R::SuperStructOf(cls)) {
        if (R::NameEquals(R::NameOf(cls), L"prop_wMannequin_C")) return true;
    }
    return false;
}

bool SetWalkerBool(void* obj, const wchar_t* name, bool val) {
    if (!obj || !R::IsLive(obj)) return false;
    int32_t byteOff = -1;
    uint8_t mask = 0;
    if (!R::FindBoolProperty(R::ClassOf(obj), name, byteOff, mask)) return false;
    uint8_t* bytePtr = reinterpret_cast<uint8_t*>(obj) + byteOff;
    if (val) *bytePtr |= mask;
    else     *bytePtr &= ~mask;
    return true;
}

bool ReadWalkerBool(void* obj, const wchar_t* name, bool& out) {
    if (!obj || !R::IsLive(obj)) return false;
    int32_t byteOff = -1;
    uint8_t mask = 0;
    if (!R::FindBoolProperty(R::ClassOf(obj), name, byteOff, mask)) return false;
    uint8_t* bytePtr = reinterpret_cast<uint8_t*>(obj) + byteOff;
    out = (*bytePtr & mask) != 0;
    return true;
}

void SetWalkerAnimStand(void* walker, bool stand) {
    if (!walker || !R::IsLive(walker)) return;
    int32_t off = R::FindPropertyOffset(R::ClassOf(walker), L"anim");
    if (off < 0) return;
    void* anim = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(walker) + off);
    if (!anim || !R::IsLive(anim)) return;
    SetWalkerBool(anim, L"stand", stand);
}

bool ReadWalkerAnimStand(void* walker, bool& stand) {
    if (!walker || !R::IsLive(walker)) return false;
    const int32_t offset = R::FindPropertyOffset(R::ClassOf(walker), L"anim");
    if (offset < 0) return false;
    void* anim = *reinterpret_cast<void**>(static_cast<uint8_t*>(walker) + offset);
    return anim && R::IsLive(anim) && ReadWalkerBool(anim, L"stand", stand);
}

void TickClientWalkerAnimation(void* walker, uint64_t now) {
    auto& state = g_clientWalkers[walker];
    if (!state.actor.Is(walker)) {
        state = ClientWalkerState{};
        state.actor.Set(walker);
    }

    // A hand, rather than stalking, may move the streamed actor. Do not infer
    // walking from that motion, or from the first pose after the hand releases.
    if (coop::local_prop_hold::IsHeld(walker) || coop::prop_drive_host::InAHand(walker)) {
        state.haveSample = state.haveMovement = false;
        SetWalkerAnimStand(walker, true);
        return;
    }

    ue_wrap::FVector loc{};
    if (ue_wrap::engine::TryGetActorLocation(walker, loc)) {
        if (state.haveSample) {
            const float dx = loc.X - state.lastPos.X;
            const float dy = loc.Y - state.lastPos.Y;
            const float dz = loc.Z - state.lastPos.Z;
            if (dx * dx + dy * dy + dz * dz > 0.09f) {
                state.lastMovedMs = now;
                state.haveMovement = true;
            }
        }
        state.lastPos = loc;
        state.haveSample = true;
    }
    // Native animation state wins over residual interpolation/physics motion.
    // Older peers retain the position-derived presentation fallback below.
    bool stand = true;
    if (coop::mannequin_sight_sync::TryGetHostStand(walker, stand)) {
        SetWalkerAnimStand(walker, stand);
        return;
    }
    const bool moving = state.haveMovement && now >= state.lastMovedMs &&
                        now - state.lastMovedMs < 250;
    SetWalkerAnimStand(walker, !moving);
}

// Tests whether a viewer at eyePos looking in lookDir has line of sight to walker at mannPos
bool IsSeenByViewer(void* walker, const ue_wrap::FVector& eyePos, const ue_wrap::FVector& lookDir, const ue_wrap::FVector& mannPos) {
    if (!walker) return false;

    constexpr float kMaxSightDist = 20000.f;  // 200 meters
    const ue_wrap::FVector targets[] = {
        {mannPos.X, mannPos.Y, mannPos.Z + 100.f},  // chest
        {mannPos.X, mannPos.Y, mannPos.Z + 30.f},   // legs
        {mannPos.X, mannPos.Y, mannPos.Z + 160.f}   // head
    };

    for (const auto& target : targets) {
        const float dx = target.X - eyePos.X;
        const float dy = target.Y - eyePos.Y;
        const float dz = target.Z - eyePos.Z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > kMaxSightDist * kMaxSightDist) continue;

        const float dist = std::sqrt(distSq);
        if (dist < 1.0f) continue;

        const float dirX = dx / dist;
        const float dirY = dy / dist;
        const float dirZ = dz / dist;

        const float dot = lookDir.X * dirX + lookDir.Y * dirY + lookDir.Z * dirZ;
        // ~55 degree half-angle FOV cone (cos 55 deg ~ 0.5735)
        if (dot > 0.57f) {
            // Line of sight raycast against static and dynamic world geometry
            if (ue_wrap::trace::LineBlockedStatDyn(walker, eyePos, target) == 0) {
                return true;
            }
        }
    }
    return false;
}

bool IsSeenByAnyRemoteClient(void* walker, void* world, const ue_wrap::FVector& mannPos) {
    if (!walker || !world) return false;
    auto* session = g_session.load(std::memory_order_acquire);
    if (!session || !session->running() || !session->connected() || session->role() != net::Role::Host)
        return false;

    for (uint8_t slot = 1; slot < coop::players::kMaxPeers; ++slot) {
        if (!session->IsSlotConnected(slot) || !session->IsSlotReady(slot) ||
            !session->IsSlotWorldReady(slot) || !session->peerGenerationForSlot(slot)) continue;
        coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(slot);
        if (!rp || !rp->valid() || !rp->HasPose()) continue;
        void* puppetActor = rp->GetActor();
        if (!puppetActor || !R::IsLive(puppetActor)) continue;
        if (ue_wrap::world_identity::WorldOf(puppetActor) != world) continue;

        bool seen = false;
        if (coop::mannequin_sight_sync::TryGetRemoteSight(slot, walker, seen)) {
            if (seen) return true;
            continue;  // A native negative observation is not a missing report.
        }
        ue_wrap::FVector eyePos{};
        if (!rp->TryGetEyePosition(eyePos)) continue;

        const ue_wrap::FVector lookDir = rp->GetSyncedAimDirection();
        if (IsSeenByViewer(walker, eyePos, lookDir, mannPos)) {
            return true;
        }
    }
    return false;
}

void EnsureWalkerEnrolled(void* walker, net::Session& session) {
    if (!walker || !R::IsLive(walker)) return;
    if (session.role() != net::Role::Host) return;

    std::wstring key = ue_wrap::prop::GetInteractableKeyString(walker);
    if (key.empty() || key == L"None") {
        key = coop::prop_synth_key::EnsureKeyForBroadcast(walker, key, /*mintForAprop=*/true);
        if (key.empty() || key == L"None") {
            // Direct write fallback to guarantee non-None key
            wchar_t buf[64];
            swprintf(buf, 64, L"wMann_%08x", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(walker)));
            const R::FName keyFName = ue_wrap::fname_utils::StringToFName(buf);
            if (keyFName.ComparisonIndex != 0) {
                *reinterpret_cast<R::FName*>(reinterpret_cast<uint8_t*>(walker) + ue_wrap::profile::off::Aprop_Key) = keyFName;
                key = buf;
            }
        }
        if (!key.empty() && key != L"None") {
            const std::wstring cls = R::ClassNameOf(walker);
            coop::prop_element_tracker::MarkProcessedInit(walker);
            coop::prop_element_tracker::MarkPropElement(walker, key, cls,
                coop::prop_element_tracker::EnrollSource::kExpressSeam);
            coop::prop_element_tracker::TrackActorKey(walker, key);
            coop::prop_snapshot::ExpressIncrementalSpawn(walker);
            UE_LOGI("[MANNEQUIN] Host enrolled and broadcasted walker %p cls='%ls' key='%ls'",
                    walker, cls.c_str(), key.c_str());
        }
    }
    coop::prop_drive_host::Claim(walker, "walker-stalk");
    coop::mannequin_target::Track(walker);
}

SG::Verdict OnClientWalkerTick(const SG::Call& call) {
    auto* session = g_session.load(std::memory_order_acquire);
    if (!session || !session->running() || !session->connected())
        return SG::Verdict::Run;

    // On clients: cancel ReceiveTick on prop_wMannequin_C so it does not run independent
    // AI pathfinding / AddActorWorldOffset.
    // The client mirror is authoritatively driven by host's prop_drive_stream.
    if (session->role() == net::Role::Client) {
        return SG::Verdict::Cancel;
    }

    if (session->role() != net::Role::Host || !IsWalker(call.object))
        return SG::Verdict::Run;
    if (coop::local_prop_hold::IsHeld(call.object) || coop::prop_drive_host::InAHand(call.object)) {
        SetWalkerAnimStand(call.object, true);
        return SG::Verdict::Cancel;
    }

    bool saw = false, looking = false;
    ue_wrap::FVector pos{};
    void* world = WI::CurrentWorld();
    if (!world || !ReadWalkerBool(call.object, L"saw", saw) || saw ||
        !ReadWalkerBool(call.object, L"looking", looking) ||
        !ue_wrap::engine::TryGetActorLocation(call.object, pos) ||
        !IsSeenByAnyRemoteClient(call.object, world, pos))
        return SG::Verdict::Run;

    // Supply remote sight to the native decision. Do not set looking ourselves:
    // its native transition applies damping and starts the 10-20s timer_saw.
    RemoteSightScope scope;
    scope.actor.Set(call.object);
    scope.wasLooking = looking;
    g_remoteSightScopes[call.invocation] = scope;
    if (!SetWalkerBool(call.object, L"saw", true))
        g_remoteSightScopes.erase(call.invocation);

    return SG::Verdict::Run;
}

void RestoreBorrowedSight(const RemoteSightScope& scope) {
    void* actor = scope.actor.Get();
    if (!actor) return;
    bool looking = false;
    if (!ReadWalkerBool(actor, L"looking", looking)) return;
    // A real native seen transition now owns saw and its timer. Otherwise this
    // was only a one-call input (e.g. its timer expired while still watched).
    if (!scope.wasLooking && looking) return;
    SetWalkerBool(actor, L"saw", false);
}

void OnWalkerTickPost(const SG::Call& call) {
    const auto it = g_remoteSightScopes.find(call.invocation);
    if (it == g_remoteSightScopes.end()) return;
    const RemoteSightScope scope = it->second;
    g_remoteSightScopes.erase(it);
    if (scope.actor.Is(call.object)) RestoreBorrowedSight(scope);
}

}  // namespace

void Tick(net::Session& session) {
    // Another observer can cancel a native call after our pre-observer. Its post
    // then never runs; retire that temporary input once the invocation is over.
    for (auto it = g_remoteSightScopes.begin(); it != g_remoteSightScopes.end();) {
        if (SG::IsInvocationActive(it->first)) { ++it; continue; }
        const RemoteSightScope scope = it->second;
        it = g_remoteSightScopes.erase(it);
        RestoreBorrowedSight(scope);
    }
    if (!session.running() || !session.connected()) return;

    void* world = WI::CurrentWorld();
    if (!world) return;
    coop::mannequin_sight_sync::Tick(session);
    coop::mannequin_interaction::Tick(session);

    void* walkerCls = OI::ClassByName(L"prop_wMannequin_C");
    if (!walkerCls) return;

    std::vector<void*> walkers;
    struct Ctx { std::vector<void*>* list; void* world; } ctx{&walkers, world};
    OI::ForEachInstance(walkerCls, [](void* c, void* obj, int32_t) {
        auto* cx = static_cast<Ctx*>(c);
        if (R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__") &&
            WI::WorldOf(obj) == cx->world) {
            cx->list->push_back(obj);
        }
    }, &ctx);

    if (session.role() == net::Role::Host) {
        for (void* walker : walkers) {
            // Keep the walker enrolled and claimed in prop_drive_host so its movement
            // is continuously streamed to all peers.
            EnsureWalkerEnrolled(walker, session);

            bool stand = true;
            if (ReadWalkerAnimStand(walker, stand))
                coop::mannequin_sight_sync::PublishHostStand(walker, stand);

            // Native ReceiveTick owns the stalking state and its timers. Its
            // pre/post observers merge remote sight at the actual decision.
        }
        coop::mannequin_target::Tick();
    } else if (session.role() == net::Role::Client) {
        coop::mannequin_identity::Tick(session, walkers);
        const uint64_t now = GetTickCount64();

        // Cached references check slots/serials without dereferencing dead actors.
        for (auto it = g_clientWalkers.begin(); it != g_clientWalkers.end();) {
            if (!it->second.actor.Alive()) it = g_clientWalkers.erase(it);
            else ++it;
        }

        for (void* walker : walkers) {
            bool seen = false;
            if (ue_wrap::mannequin::TryReadLocalSight(walker, coop::players::Registry::Get().Local(), seen))
                coop::mannequin_sight_sync::ReportLocalSight(walker, seen);
            TickClientWalkerAnimation(walker, now);
        }
    }
}

void Install(net::Session* session) {
    g_session.store(session, std::memory_order_release);
    coop::mannequin_sight_sync::Install(session);
    coop::mannequin_target::Install(session);

    // K2_DestroyActor is inherited native Actor code, not either mannequin's
    // script body. Its lifecycle belongs to the existing prop/native seam.
    // A class-local name watch cannot intercept it and used to exhaust the
    // registration table when Install was called on every pump.

    // Cancel ReceiveTick ONLY on clients for prop_wMannequin_C so client doesn't run local AI pathfinding.
    // ExecuteUbergraph and BeginPlay are NOT cancelled, preserving player interactions (grab, hold, damage).
    SG::WatchClassName(L"prop_wMannequin_C", L"ReceiveTick", 0x4D414E56 /* 'MANV' */,
                       &OnClientWalkerTick, &OnWalkerTickPost);
}

void OnDisconnect() {
    coop::mannequin_sight_sync::OnDisconnect();
    coop::mannequin_interaction::OnDisconnect();
    coop::mannequin_identity::OnDisconnect();
    coop::mannequin_target::OnDisconnect();
    for (const auto& kv : g_remoteSightScopes) RestoreBorrowedSight(kv.second);
    g_remoteSightScopes.clear();
    g_session.store(nullptr, std::memory_order_release);
    g_clientWalkers.clear();
}

}  // namespace coop::mannequin_sync
