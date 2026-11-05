// coop/player/player_damage.cpp -- see coop/player/player_damage.h.
//
// The refusals on a peer's puppet. A hit lands on the puppet in the host's world and must not write the
// host's per-machine health; its owner takes it on its own player. For a creature of a parked family the
// host also delivers the hit, as a PlayerHit that the owner's machine applies (coop/player/player_hit.h).

#include "coop/player/player_damage.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/creatures/creature_hit.h"
#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/hand_item.h"
#include "coop/player/players_registry.h"
#include "coop/player/pvp.h"
#include "coop/world/world_actor_sync.h"
#include "ue_wrap/engine/engine.h"
#include "coop/player/player_hit.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/engine/engine_playerragdoll.h"
#include <chrono>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace coop::player_damage {
namespace {

namespace R = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;
namespace sg = ue_wrap::script_gate;

std::atomic<coop::net::Session*> g_session{nullptr};  // read in the impact interceptor (any thread)

// The impact-entry cancel. mainPlayer_C's three native->BP impact events, one shared PRE callback
// registered once for each. The class is loaded at the main menu and lives for the process
// (coop/dev/class_lifetime_probe measures it), so the registrations hold across a world change.
UE_FACT_FN(kRowAddPlayerDamage, "player_damage", nullptr, L"Add Player Damage", AnyClass);
constexpr int kDamageVerbTag=0x50445642, kIgniteTag=0x50444947;
bool g_damageWatchDone=false,g_igniteWatchDone=false;
UE_FACT_FN(kRowImpactDamage, "player_damage", L"mainPlayer_C", L"impactDamage", Declared);
UE_FACT_FN(kRowImpactDamageCpp, "player_damage", L"mainPlayer_C", L"impactDamageCPP", Declared);
UE_FACT_FN(kRowImpactSquishCpp, "player_damage", L"mainPlayer_C", L"impactSquishCPP", Declared);
UE_FACT_FN(kRowAddDamageInterceptor, "player_damage", L"mainPlayer_C", L"addDamage", Declared);
UE_FACT_FN(kRowIgnite, "player_damage", L"mainPlayer_C", L"ignite", Declared);
UE_FACT_FN(kRowAddDamage, "player_damage", L"mainPlayer_C", L"addDamage", Declared);
UE_FACT_FN(kRowPunch, "player_damage", L"mainPlayer_C", L"punch", Declared);
UE_FACT_FN(kRowRagdollMode, "player_damage", L"mainPlayer_C", L"ragdollMode", Declared);
UE_FACT_FN(kRowForceGetUp,   "player_damage", L"mainPlayer_C", L"forceGetUp",   Declared);
const ue_wrap::facts::Function* const kImpactEntryRows[3] = {
    &kRowImpactDamage, &kRowImpactDamageCpp, &kRowImpactSquishCpp,
};
const wchar_t* const kImpactEntryNames[3] = {L"impactDamage", L"impactDamageCPP", L"impactSquishCPP"};
void*    g_impactFns[3] = {};
void*    g_addDamageFn = nullptr;
bool     g_impactInterceptorsDone = false;
uint32_t g_canceled = 0;   // rate-latched log counter
std::atomic<uint32_t> g_verbCanceled{0};
std::atomic<uint32_t> g_igniteCanceled{0};

// The LOCAL possessed player, published by Tick (game thread) for the interceptor's any-thread
// compare. The interceptor may not reach the registry: Registry::Local() mutates game-thread-only
// cache state, and a ProcessEvent interceptor can fire on a parallel-anim worker (game_thread.h's
// own contract). A published atomic keeps the CANCEL effective on every dispatching thread with one
// acquire load and no registry access.
std::atomic<void*> g_localPawn{nullptr};
static std::chrono::steady_clock::time_point s_lastRagdollEnter{};

// PRE cancel: a physical impact dispatched on a mainPlayer BODY that is NOT the local possessed
// player (a peer's puppet, or the F1 skin-preview mannequin) must not run the BP body -- it writes
// the PER-MACHINE saveSlot.health singleton, so lethal contact resolved against a puppet in the
// host's world drained the HOST, and killing a player took the server down with them. The victim's
// own machine computes the same contact on its own possessed player (self==local -> pass-through),
// so nothing is lost: MTA's victim-authoritative shape, no relay. Coop-session gated, because solo
// play has only the local body anyway and an unset registry must never eat solo damage.
bool OnImpactEntryPre(void* self, void* params, int which) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected()) return false;
    if (!self) return false;
    void* localPawn = g_localPawn.load(std::memory_order_acquire);
    if (!localPawn) return false;           // pre-resolve boot window: native behavior
    void* fn = (which >= 0 && which < 3) ? g_impactFns[which] : nullptr;
    if (self == localPawn) {
        if (s_lastRagdollEnter.time_since_epoch().count() != 0) {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - s_lastRagdollEnter).count();
            if (elapsedMs >= 0 && elapsedMs < 500) {
                UE_LOGI("player_damage: suppressed startup impact during ragdoll entry (%lld ms < 500 ms)",
                        static_cast<long long>(elapsedMs));
                return true;
            }
        }
        return coop::pvp::CancelLocalImpact(params, fn);
    }
    coop::pvp::OnPuppetImpact(self, params, fn);
    const uint32_t n = ++g_canceled;
    if (n <= 5 || (n % 50) == 0) {
        UE_LOGI("player_damage: canceled impact entry on NON-LOCAL body %p (#%u) -- "
                "the hit body's owner computes its own damage", self, n);
    }
    return true;  // cancel: the singleton saveSlot write must not run for a foreign body
}

bool OnImpact0(void* self, void* params) { return OnImpactEntryPre(self, params, 0); }
bool OnImpact1(void* self, void* params) { return OnImpactEntryPre(self, params, 1); }
bool OnImpact2(void* self, void* params) { return OnImpactEntryPre(self, params, 2); }

// A shovel or crowbar calls addDamage on whoever the trace hit. On a puppet that write does not
// reach the victim's health, and the impact-entry cancel never sees it. Treat it as the swing or
// the throw, and do not let it touch this machine's health.
bool OnAddDamage(void* self, void* params) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || !self) return false;
    void* localPawn = g_localPawn.load(std::memory_order_acquire);
    if (!localPawn) return false;
    if (self == localPawn) return coop::pvp::CancelLocalImpact(params, g_addDamageFn);
    coop::pvp::OnPuppetImpact(self, params, g_addDamageFn);
    return false;
}

// One refusal of any of the four verbs on a puppet, counted in RefusedDamage and logged on a rate.
void NoteRefused(const char* verb, void* puppet) {
    const uint32_t n = ++g_verbCanceled;
    if (n <= 5 || (n % 100) == 0)
        UE_LOGI("player_damage: canceled %s on a peer's puppet %p (#%u) -- its owner computes its own damage", verb,
                puppet, n);
}

sg::Verdict OnDamageVerbPre(const sg::Call& c) {
    // IsPuppet alone: no session or local-pawn early-out in front of it, since the local pawn reads
    // null for a whole death or travel window, and one call let through then burns for the session.
    if (!c.object || !coop::players::Registry::Get().IsPuppet(c.object)) return sg::Verdict::Run;
    NoteRefused("the damage verb", c.object);
    return sg::Verdict::Cancel;
}

// These BP bodies also run through internal Blueprint calls, bypassing the
// ProcessEvent impact interceptors above. An orphan's ignite starts latent loops
// that read player 0's controller and shared health; ragdollMode/fallen hide its
// meshes and enter the single-player death chain. Remote presentation belongs to
// RagdollDisplay instead. The script gate is GT-only, so resolve the current local
// pawn here rather than trusting yesterday's atomic pointer during world travel.
void DeliverIgnite(const sg::Call& c);

ue_wrap::script_gate::Verdict OnNativePlayerState(const ue_wrap::script_gate::Call& c) {
    using ue_wrap::script_gate::Verdict;
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || !c.object) return Verdict::Run;
    void* local = coop::players::Registry::Get().Local();
    if (!local || c.object == local) return Verdict::Run;
    static uint32_t canceled = 0; // script-gate callbacks are game-thread-only
    const uint32_t n = ++canceled;
    g_verbCanceled.fetch_add(1, std::memory_order_relaxed);
    if (c.tag == 0x50530002) {
        g_igniteCanceled.fetch_add(1, std::memory_order_relaxed);
        if (coop::players::Registry::Get().IsPuppet(c.object)) DeliverIgnite(c);
    }
    if (n <= 8 || n % 100 == 0)
        UE_LOGI("player_damage: blocked foreign native state tag=0x%x body=%p local=%p (#%u)",
                c.tag, c.object, local, n);
    return Verdict::Cancel;
}

void InstallNativeStateGuards() {
    static const wchar_t* const names[] = {L"ragdollMode", L"fallen", L"ignite", L"startBurning"};
    static bool registered[4]{};
    for (int i = 0; i < 4; ++i) {
        if (!registered[i]) registered[i] = i == 2
            ? ue_wrap::script_gate::WatchName(kRowIgnite, 0x50530000 + i, &OnNativePlayerState, nullptr)
            : ue_wrap::script_gate::WatchClassName(
                L"mainPlayer_C", names[i], 0x50530000 + i, &OnNativePlayerState, nullptr);
    }
}

sg::Verdict OnIgnitePre(const sg::Call& c) {
    // IsPuppet alone: no session or local-pawn early-out in front of it, since the local pawn reads
    // null for a whole death or travel window, and one call let through then burns for the session.
    if (!c.object || !coop::players::Registry::Get().IsPuppet(c.object))
        return sg::Verdict::Run;
    DeliverIgnite(c);
    const uint32_t n = ++g_igniteCanceled;
    if (n <= 5 || (n % 100) == 0)
        UE_LOGI("player_damage: refused setting a peer's puppet %p alight (#%u) -- its fire is its owner's",
                c.object, n);
    return sg::Verdict::Cancel;
}

// The hit verbs. A creature's hit on a player is addDamage (damage, then a punch above 25) or punch (a
// knock-down, then the damage); ragdollMode is the knock-down itself. Each is refused on a puppet like the
// damage verb above, whatever the caller, a call the puppet makes on itself included: MTA locks a remote copy's
// health and the damage handler restores it (reference/mtasa-blue/Client/mods/deathmatch/logic/CClientGame.cpp:4509).
// Beside the refusal:
//   - HOST, a cancelled addDamage or punch whose caller is a creature of a parked family is delivered to the
//     puppet's peer, rebuilt from the call's parameters. A parked family's mirror makes no hit of its own, so
//     this is the only one; any other caller, and a call routed through ProcessEvent (no caller object), is
//     only refused. Source's delivery with MTA's ownership: Source's NPC runs and hits on the server
//     (reference/source-sdk-2013/src/game/server/hl2/npc_BaseZombie.cpp:1267), MTA's player owns its health
//     on its own client (reference/mtasa-blue/Client/mods/deathmatch/logic/CNetAPI.cpp:1189).
//   - CLIENT, a call on this machine's own player whose caller is a parked creature's mirror, outside the
//     delivery's own apply, is counted and said once per class: the brain-off leaked. A tripwire; it runs.
constexpr int kAddDamageTag = 0x50444144;  // 'PDAD'
constexpr int kPunchTag     = 0x50445055;  // 'PDPU'
constexpr int kRagdollTag   = 0x50445247;  // 'PDRG'
constexpr int kForceGetUpTag = 0x50444647;  // 'PDFG'
constexpr int kAddPlayerDamageTag = 0x50444150; // 'PDAP'
bool g_hitWatchesDone = false;
std::atomic<uint32_t> g_delivered{0};
std::atomic<uint32_t> g_mirrorHits{0};

constexpr size_t kSeenMax = 16;
struct SeenKey {
    void*   cls  = nullptr;
    uint8_t verb = 0;
};
SeenKey g_sentSeen[kSeenMax];     // (creature class, verb) pairs whose first delivery was said
size_t  g_sentSeenCount = 0;
void*   g_mirrorSeen[kSeenMax];   // classes whose mirror hit was said
size_t  g_mirrorSeenCount = 0;

// First time for this key? Records it; a full list says nothing more (the counters still count).
bool FirstSent(void* cls, uint8_t verb) {
    for (size_t i = 0; i < g_sentSeenCount; ++i)
        if (g_sentSeen[i].cls == cls && g_sentSeen[i].verb == verb) return false;
    if (g_sentSeenCount >= kSeenMax) return false;
    g_sentSeen[g_sentSeenCount++] = SeenKey{cls, verb};
    return true;
}

bool FirstMirror(void* cls) {
    for (size_t i = 0; i < g_mirrorSeenCount; ++i)
        if (g_mirrorSeen[i] == cls) return false;
    if (g_mirrorSeenCount >= kSeenMax) return false;
    g_mirrorSeen[g_mirrorSeenCount++] = cls;
    return true;
}

// A parameter out of the call's frame, by its reflected offset; zero when the function has no such parameter.
float ReadFloat(const sg::Call& c, R::ParamOffset& off) {
    const int32_t o = off.Of(c.function);
    float v = 0.f;
    if (o >= 0) std::memcpy(&v, c.locals + o, sizeof(v));
    return v;
}

void ReadVec(const sg::Call& c, R::ParamOffset& off, float (&out)[3]) {
    const int32_t o = off.Of(c.function);
    if (o >= 0) std::memcpy(out, c.locals + o, sizeof(out));
}

// The payload of an addDamage call on a puppet. The target is stamped by player_hit::Deliver.
coop::net::PlayerHitPayload BuildAddDamage(const sg::Call& c) {
    static R::ParamOffset sActor{L"actor"}, sDamage{L"damage"}, sImpact{L"impact"};
    coop::net::PlayerHitPayload p{};
    p.verb = coop::net::kPlayerHitVerbAddDamage;
    p.damage = ReadFloat(c, sDamage);
    ReadVec(c, sImpact, p.v0);
    const int32_t o = sActor.Of(c.function);
    void* actor = nullptr;
    if (o >= 0) std::memcpy(&actor, c.locals + o, sizeof(actor));
    // A null actor travels as 0. An actor with no element travels as the invalid id, which no mirror resolves, so
    // the victim drops the hit rather than run the other branch of the Blueprint.
    if (actor) {
        p.sourceEid = coop::element::Registry::Get().EidForActor(actor);
        // Beam/shot BeginPlay can hit before the deferred-spawn catch drains next frame.
        // Publish its canonical birth before the hit so reliable delivery binds the source first.
        if (p.sourceEid == coop::element::kInvalidId && actor == c.callerObject &&
            coop::creature_hit::IsDelivered(actor)) {
            const auto eid = coop::world_actor_sync::HostEnrollExSpawn(actor);
            if (eid) p.sourceEid = eid;
        }
    }
    return p;
}

// The payload of a punch call on a puppet.
coop::net::PlayerHitPayload BuildPunch(const sg::Call& c) {
    static R::ParamOffset sFullBody{L"fullBody"}, sBoneImp{L"boneImp"}, sBone{L"bn1"}, sDamage{L"damage"},
        sDamageLoc{L"damageLocation"};
    coop::net::PlayerHitPayload p{};
    p.verb = coop::net::kPlayerHitVerbPunch;
    p.damage = ReadFloat(c, sDamage);
    ReadVec(c, sFullBody, p.v0);
    ReadVec(c, sBoneImp, p.v1);
    ReadVec(c, sDamageLoc, p.v2);
    const int32_t o = sBone.Of(c.function);
    if (o >= 0) {
        R::FName bone{};
        std::memcpy(&bone, c.locals + o, sizeof(bone));
        p.bone = coop::player_hit::BoneIndex(R::ToString(bone));
    }
    return p;
}

// HOST: the cancelled hit's delivery. Game thread (a script-gate watch).
void DeliverToOwner(const sg::Call& c, bool punch) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Host) return;
    if (!coop::creature_hit::IsDelivered(c.callerObject)) return;
    const uint8_t slot = coop::players::Registry::Get().PeerIdOfActor(c.object);
    if (slot == coop::players::kPeerIdHost || slot >= coop::players::kMaxPeers) return;
    const coop::net::PlayerHitPayload p = punch ? BuildPunch(c) : BuildAddDamage(c);
    if (!coop::player_hit::Deliver(slot, p)) return;   // not sent: not delivered (Deliver says and counts it)
    ++g_delivered;
    void* const cls = R::ClassOf(c.callerObject);
    if (FirstSent(cls, p.verb)) {
        const std::wstring caller = c.callerFunction ? R::ToString(R::NameOf(c.callerFunction)) : L"none";
        UE_LOGI("[HIT] sent verb=%s slot=%u creature=%ls caller=%ls", punch ? "punch" : "addDamage",
                static_cast<unsigned>(slot), R::ClassNameOf(c.callerObject).c_str(), caller.c_str());
    }
}

// ignite has no actor parameter; it carries fuel in the existing scalar slot.
// No historical fire is replayed on join. Only the addressed owner runs the native burn loop.
void DeliverIgnite(const sg::Call& c) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Host ||
        !coop::creature_hit::IsDelivered(c.callerObject)) return;
    const uint8_t slot = coop::players::Registry::Get().PeerIdOfActor(c.object);
    if (slot == coop::players::kPeerIdHost || slot >= coop::players::kMaxPeers) return;
    static R::ParamOffset fuel{L"fuel"};
    if (!c.locals || fuel.Of(c.function) < 0) return;
    // Both watches share this dispatch's PRE mark. Nested calls have their own
    // word, so they cannot make the second outer watch deliver the fire twice.
    static const char markOwner = 0;
    static const uintptr_t mark = sg::ScopeBitFor(&markOwner);
    if (!mark || !c.scopeMark || (*c.scopeMark & mark)) return;
    sg::MarkScope(c,mark);
    coop::net::PlayerHitPayload p{};
    p.verb = coop::net::kPlayerHitVerbIgnite;
    p.damage = ReadFloat(c, fuel);
    if (coop::player_hit::Deliver(slot, p)) ++g_delivered;
}

// CLIENT: a hit on this machine's own player that a parked creature's mirror made. MTA's guard refuses a remote
// player's duplicate damage event (reference/mtasa-blue/Client/mods/deathmatch/logic/CClientGame.cpp:4345-4377);
// this only counts. A refusal here would hide a leaked brain-off instead of showing it, and the mark table fails open:
// a mirror left unmarked would hit here and again through the delivery, and the count is what shows that double.
void WatchMirrorHit(const sg::Call& c) {
    if (!c.callerObject || coop::player_hit::InApply()) return;
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Client) return;
    if (!coop::players::Registry::Get().IsLocal(c.object)) return;
    if (!coop::creature_hit::IsDelivered(c.callerObject)) return;
    ++g_mirrorHits;
    if (FirstMirror(R::ClassOf(c.callerObject)))
        UE_LOGW("player_damage: a parked %ls mirror hit this player -- the brain-off leaked",
                R::ClassNameOf(c.callerObject).c_str());
}

sg::Verdict OnHitVerbPre(const sg::Call& c) {
    if (!c.object) return sg::Verdict::Run;
    const bool ragdoll = c.tag == kRagdollTag;
    const bool punch   = c.tag == kPunchTag;
    if (!coop::players::Registry::Get().IsPuppet(c.object)) {
        if (!ragdoll) {
            WatchMirrorHit(c);
        } else {
            static int32_t s_ragParamOff = -1;
            static uint8_t s_ragParamMask = 0;
            if (s_ragParamOff == -1 && c.function) {
                R::FindBoolProperty(c.function, L"ragdoll", s_ragParamOff, s_ragParamMask);
            }
            bool entering = true;
            if (c.locals && s_ragParamOff >= 0 && s_ragParamMask != 0) {
                entering = (*(c.locals + s_ragParamOff) & s_ragParamMask) != 0;
            }
            if (entering) {
                s_lastRagdollEnter = std::chrono::steady_clock::now();
                // Clear residual movement vectors on entering ragdoll so WASD doesn't propel the body
                static R::InstanceOffset s_vrOff{L"velRes"};
                static R::InstanceOffset s_fwdOff{L"inpF_forward"};
                static R::InstanceOffset s_backOff{L"inpF_back"};
                static R::InstanceOffset s_rigOff{L"inpF_rig"};
                static R::InstanceOffset s_lefOff{L"inpF_lef"};
                auto* base = static_cast<uint8_t*>(c.object);
                const int vr = s_vrOff.Of(c.object);
                if (vr >= 0) std::memset(base + vr, 0, sizeof(ue_wrap::FVector));
                const int fwd = s_fwdOff.Of(c.object);
                if (fwd >= 0) { float z = 0.f; std::memcpy(base + fwd, &z, sizeof(z)); }
                const int back = s_backOff.Of(c.object);
                if (back >= 0) { float z = 0.f; std::memcpy(base + back, &z, sizeof(z)); }
                const int rig = s_rigOff.Of(c.object);
                if (rig >= 0) { float z = 0.f; std::memcpy(base + rig, &z, sizeof(z)); }
                const int lef = s_lefOff.Of(c.object);
                if (lef >= 0) { float z = 0.f; std::memcpy(base + lef, &z, sizeof(z)); }
            }
        }
        return sg::Verdict::Run;
    }
    NoteRefused(ragdoll ? "ragdollMode" : punch ? "punch" : "addDamage", c.object);
    if (!ragdoll) DeliverToOwner(c, punch);
    return sg::Verdict::Cancel;
}

sg::Verdict OnForceGetUpPre(const sg::Call& c) {
    if (!c.object) return sg::Verdict::Run;
    if (coop::players::Registry::Get().IsPuppet(c.object)) {
        return sg::Verdict::Cancel;
    }
    void* cls = R::ClassOf(c.object);
    if (!cls) return sg::Verdict::Run;

    // 1. Bitfield property reads for isRagdoll, canGetUp, isWakingUp
    static int32_t s_canGetUpOff = -1;
    static uint8_t s_canGetUpMask = 0;
    static int32_t s_isRagdollOff = -1;
    static uint8_t s_isRagdollMask = 0;
    static int32_t s_isWakingUpOff = -1;
    static uint8_t s_isWakingUpMask = 0;
    if (s_canGetUpOff == -1) R::FindBoolProperty(cls, L"canGetUp", s_canGetUpOff, s_canGetUpMask);
    if (s_isRagdollOff == -1) R::FindBoolProperty(cls, L"isRagdoll", s_isRagdollOff, s_isRagdollMask);
    if (s_isWakingUpOff == -1) R::FindBoolProperty(cls, L"isWakingUp", s_isWakingUpOff, s_isWakingUpMask);

    const auto* base = static_cast<const uint8_t*>(c.object);
    bool canGetUp = true;
    if (s_canGetUpOff >= 0 && s_canGetUpMask != 0) {
        canGetUp = (base[s_canGetUpOff] & s_canGetUpMask) != 0;
    }
    bool isRagdoll = false;
    if (s_isRagdollOff >= 0 && s_isRagdollMask != 0) {
        isRagdoll = (base[s_isRagdollOff] & s_isRagdollMask) != 0;
    }
    bool isWakingUp = false;
    if (s_isWakingUpOff >= 0 && s_isWakingUpMask != 0) {
        isWakingUp = (base[s_isWakingUpOff] & s_isWakingUpMask) != 0;
    }

    if (!isRagdoll || isWakingUp) {
        return sg::Verdict::Cancel;
    }

    // 2. Debounce entry race (Space + C on same frame or within 400ms)
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastRagdollEnter).count();
    if (s_lastRagdollEnter.time_since_epoch().count() != 0 && elapsedMs < 400) {
        UE_LOGI("ragdoll_guard: refused forceGetUp during entry debounce (%lld ms < 400 ms)", static_cast<long long>(elapsedMs));
        return sg::Verdict::Cancel;
    }

    // 3. canGetUp gate: native getup sets canGetUp=false until settled
    if (!canGetUp) {
        UE_LOGI("ragdoll_guard: refused forceGetUp (!canGetUp)");
        return sg::Verdict::Cancel;
    }

    // 4. ragVel check: native ExecuteUbergraph requires VSize(ragVel) <= 6.0f
    static R::InstanceOffset s_ragVelOff{L"ragVel"};
    ue_wrap::FVector ragVel{};
    const int32_t rvOff = s_ragVelOff.Of(c.object);
    if (rvOff >= 0) {
        std::memcpy(&ragVel, base + rvOff, sizeof(ragVel));
        const float speedSq = ragVel.X * ragVel.X + ragVel.Y * ragVel.Y + ragVel.Z * ragVel.Z;
        if (speedSq > 36.0f) {
            UE_LOGI("ragdoll_guard: refused forceGetUp (ragVel speedSq=%.2f > 36.0)", speedSq);
            return sg::Verdict::Cancel;
        }
    }

    // 5. Physics velocity check of the ragdoll actor
    static R::InstanceOffset s_ragdollOff{L"ragdoll"};
    const int32_t rOff = s_ragdollOff.Of(c.object);
    if (rOff >= 0) {
        void* ragdollActor = nullptr;
        std::memcpy(&ragdollActor, base + rOff, sizeof(ragdollActor));
        if (ragdollActor && R::IsLive(ragdollActor)) {
            const ue_wrap::FVector physVel = ue_wrap::engine::GetActorVelocity(ragdollActor);
            const float physSpeedSq = physVel.X * physVel.X + physVel.Y * physVel.Y + physVel.Z * physVel.Z;
            if (physSpeedSq > 3600.0f) { // > 60 cm/s
                UE_LOGI("ragdoll_guard: refused forceGetUp (ragdollActor physSpeedSq=%.2f > 3600.0)", physSpeedSq);
                return sg::Verdict::Cancel;
            }
        }
    }

    return sg::Verdict::Run;
}

sg::Verdict OnAddPlayerDamagePre(const sg::Call& c) {
    if (!c.object) return sg::Verdict::Run;
    if (coop::players::Registry::Get().IsPuppet(c.object)) {
        return sg::Verdict::Cancel;
    }
    // Suppress spurious solver damage during ragdoll entry window
    if (s_lastRagdollEnter.time_since_epoch().count() != 0) {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - s_lastRagdollEnter).count();
        if (elapsedMs >= 0 && elapsedMs < 500) {
            UE_LOGI("player_damage: suppressed Add Player Damage during ragdoll entry (%lld ms < 500 ms)",
                    static_cast<long long>(elapsedMs));
            return sg::Verdict::Cancel;
        }
    }
    return sg::Verdict::Run;
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

uint32_t RefusedDamage() { return g_verbCanceled.load(std::memory_order_relaxed); }
uint32_t RefusedIgnites() { return g_igniteCanceled.load(std::memory_order_relaxed); }
uint32_t DeliveredCount() { return g_delivered.load(std::memory_order_relaxed); }
uint32_t MirrorHitCount() { return g_mirrorHits.load(std::memory_order_relaxed); }

void Tick() {
    InstallNativeStateGuards();
    // Publish the local pawn for the interceptor's any-thread compare (GT here;
    // Registry::Local() is GT-only and cached).
    void* local = coop::players::Registry::Get().Local();
    g_localPawn.store(local, std::memory_order_release);
    coop::pvp::NoteBodies(local, coop::hand_item::LocalHandActor());
    coop::pvp::Tick();
    g_localPawn.store(coop::players::Registry::Get().Local(), std::memory_order_release);
    // The dmghazard probe's red: no refusal is watched, so a puppet takes damage and fire here, and
    // nothing is delivered.
    static const bool s_noRefusal =
        coop::config::ResolveFlag(coop::config_registry::rows::player_damage_no_refusal);
    if (s_noRefusal && (!g_damageWatchDone || !g_igniteWatchDone || !g_hitWatchesDone)) {
        g_damageWatchDone = g_igniteWatchDone = g_hitWatchesDone = true;
        UE_LOGW("player_damage: DRILL -- the damage, hit-verb and ignite refusals on a peer's puppet are OFF "
                "(dev.player_damage_no_refusal); a puppet takes them here and no hit is delivered");
    }
    if (!g_damageWatchDone) {
        g_damageWatchDone = true;
        if (!sg::WatchName(kRowAddPlayerDamage, kDamageVerbTag, &OnDamageVerbPre, nullptr))
            UE_LOGE("player_damage: the damage verb watch did not register -- a burning puppet damages "
                    "this machine's player");
    }
    if (!g_igniteWatchDone) {
        g_igniteWatchDone = true;
        if (!sg::WatchName(kRowIgnite, kIgniteTag, &OnIgnitePre, nullptr))
            UE_LOGE("player_damage: the ignite watch did not register -- a peer's puppet can burn here");
    }
    if (!g_hitWatchesDone) {
        g_hitWatchesDone = true;
        if (!sg::WatchName(kRowAddDamage, kAddDamageTag, &OnHitVerbPre, nullptr))
            UE_LOGE("player_damage: the addDamage watch did not register -- a creature's hit runs on a puppet here");
        if (!sg::WatchName(kRowPunch, kPunchTag, &OnHitVerbPre, nullptr))
            UE_LOGE("player_damage: the punch watch did not register -- a creature's punch runs on a puppet here");
        if (!sg::WatchName(kRowRagdollMode, kRagdollTag, &OnHitVerbPre, nullptr))
            UE_LOGE("player_damage: the ragdollMode watch did not register -- a puppet can ragdoll here");
        if (!sg::WatchName(kRowForceGetUp, kForceGetUpTag, &OnForceGetUpPre, nullptr))
            UE_LOGE("player_damage: the forceGetUp watch did not register -- space+c can fling player");
        if (!sg::WatchName(kRowAddPlayerDamage, kAddPlayerDamageTag, &OnAddPlayerDamagePre, nullptr))
            UE_LOGW("player_damage: Add Player Damage watch did not register");
    }
    if (g_impactInterceptorsDone) return;
    void* cls = ue_wrap::object_index::ClassByName(L"mainPlayer_C");
    if (!cls) return;
    // A Blueprint class loads with its functions, so the first attempt with the class present is the
    // last: a name that does not resolve is a recooked Blueprint, and a full table is a capacity fault
    // to fix, not a slot to wait for.
    g_impactInterceptorsDone = true;
    bool all = true;
    GT::UFunctionInterceptor cbs[3] = { &OnImpact0, &OnImpact1, &OnImpact2 };
    for (int i = 0; i < 3; ++i) {
        const auto resolved = ue_wrap::facts::ResolveOn(*kImpactEntryRows[i], cls);
        g_impactFns[i] = resolved.Fn();
        if (resolved && GT::RegisterInterceptor(resolved, cbs[i])) continue;
        all = false;
        UE_LOGE("player_damage: impact entry %ls %s -- a non-local body runs its damage body", kImpactEntryRows[i]->Name(),
                g_impactFns[i] ? "did not register (the interceptor table is full)"
                               : "is not declared on mainPlayer_C (a recooked Blueprint)");
    }
    const auto addDamage = ue_wrap::facts::ResolveOn(kRowAddDamageInterceptor, cls);
    g_addDamageFn = addDamage.Fn();
    if (!addDamage || !GT::RegisterInterceptor(addDamage, &OnAddDamage)) {
        UE_LOGE("player_damage: addDamage %s -- a shovel swing on a puppet never reaches that player",
                g_addDamageFn ? "did not register (the interceptor table is full)"
                              : "is not declared on mainPlayer_C");
    }
    if (all) {
        UE_LOGI("player_damage: impact-entry PRE cancels installed (impactDamage=%p "
                "impactDamageCPP=%p impactSquishCPP=%p addDamage=%p) -- non-local bodies never run "
                "the saveSlot damage body",
                g_impactFns[0], g_impactFns[1], g_impactFns[2], g_addDamageFn);
    }
}

void SendPlayerDamage(int slot,float damage) {
    coop::net::PlayerHitPayload p{};p.verb=coop::net::kPlayerHitVerbAddDamage;p.damage=damage;
    coop::player_hit::Deliver(static_cast<uint8_t>(slot),p);
}

bool DebugForceHitPuppet(int ownerSlot, float damage) {
    if (ownerSlot <= 0 || ownerSlot >= coop::players::kMaxPeers) {
        UE_LOGW("player_damage: DebugForceHitPuppet invalid ownerSlot=%d", ownerSlot);
        return false;
    }
    auto& reg = coop::players::Registry::Get();
    if (!reg.Puppet(static_cast<uint8_t>(ownerSlot))) {
        UE_LOGW("player_damage: DebugForceHitPuppet slot=%d has no puppet (peer not connected) "
                "-- cannot simulate a hit", ownerSlot);
        return false;
    }
    coop::net::PlayerHitPayload p{};
    p.verb = coop::net::kPlayerHitVerbAddDamage;
    p.damage = damage;
    return coop::player_hit::Deliver(static_cast<uint8_t>(ownerSlot), p);
}

}  // namespace coop::player_damage
