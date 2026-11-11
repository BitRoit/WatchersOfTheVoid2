// coop/creatures/roach_sync.cpp -- see coop/creatures/roach_sync.h for the
// design + the bytecode ground truth. Shapes reused: serverbox_sync (lazy
// resolve + 1 Hz host poll + client apply of the host's row + connect
// replay), prop lanes (IsLiveByIndex liveness on a tracked set).

#include "coop/creatures/roach_sync.h"

#include "coop/element/lerp_window.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"  // kMaxPeers

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/world/world_singleton.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace coop::roach_sync {
namespace {

namespace R  = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;

std::atomic<coop::net::Session*> g_session{nullptr};

constexpr long long kPollIntervalMs     = 1000;   // host poll + client liveness scan
constexpr long long kKeepaliveMs        = 10000;  // periodic full resend (join/loss convergence)
constexpr float     kAdjudicateRadius   = 200.f;  // RoachConsumed nearest-match cap
constexpr int       kMaxIntentsPerTick  = 4;      // client consumption-intent send cap
constexpr float     kTeleportSnapDist   = 500.f;  // snap large position jumps instead of interpolating
constexpr long long kConsumedGraceMs     = 5000;   // wait briefly for host adjudication
constexpr int       kBirthAttemptsPerSecond = 8;
constexpr long long kBirthRetryMs = 5000;  // native add may refuse a distant or otherwise invalid location
constexpr int kInterpCushionMs = 50;  // small cushion for late samples, including in tests

// ---- resolution (lazy, 2 s retry, capped LOUD latch -- the serverbox pattern) ----
void*   g_gmCls        = nullptr;  // mainGamemode_C
int32_t g_offMaster    = -1;       // AcockroachMaster_C* cockroachMaster (0x910 in the dump)
void*   g_masterCls    = nullptr;  // cockroachMaster_C
int32_t g_offRoaches   = -1;       // TArray<UStaticMeshComponent*> roaches (0x238)
void*   g_addRoachFn   = nullptr;  // addRoach(FVector Location, FVector Size, bool bypassCheck)
void*   g_delRoachFn   = nullptr;  // deleteRoach(int32 IndexToRemove, bool crush)
void*   g_getLocFn     = nullptr;  // USceneComponent::K2_GetComponentLocation
void*   g_getScaleFn   = nullptr;  // USceneComponent::K2_GetComponentScale
void*   g_setLocFn     = nullptr;  // USceneComponent::K2_SetWorldLocation
void*   g_setScaleFn   = nullptr;  // USceneComponent::SetWorldScale3D
void*   g_setLocRotFn  = nullptr;  // USceneComponent::K2_SetWorldLocationAndRotation
void*   g_getRotFn = nullptr;      // USceneComponent::K2_GetComponentRotation
std::chrono::steady_clock::time_point g_nextResolve{};
int  g_postClassAttempts = 0;
bool g_resolveLatched = false;
constexpr int kMaxPostClassAttempts = 5;

struct Vec3 { float x, y, z; };

bool Resolved() {
    return g_offMaster >= 0 && g_offRoaches >= 0 && g_addRoachFn && g_delRoachFn &&
           g_getLocFn && g_getScaleFn && g_setLocFn && g_setScaleFn;
}

void ResolvePass() {
    if (g_resolveLatched) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextResolve) return;
    g_nextResolve = now + std::chrono::seconds(2);
    if (!g_gmCls)     g_gmCls     = R::FindClass(L"mainGamemode_C");
    if (!g_masterCls) g_masterCls = R::FindClass(L"cockroachMaster_C");
    if (!g_gmCls || !g_masterCls) return;  // world not loaded yet
    if (g_offMaster  < 0) g_offMaster  = R::FindPropertyOffset(g_gmCls, L"cockroachMaster");
    if (g_offRoaches < 0) g_offRoaches = R::FindPropertyOffset(g_masterCls, L"roaches");
    if (!g_addRoachFn) g_addRoachFn = R::FindFunction(g_masterCls, L"addRoach");
    if (!g_delRoachFn) g_delRoachFn = R::FindFunction(g_masterCls, L"deleteRoach");
    if (void* sceneCls = R::FindClass(L"SceneComponent")) {
        if (!g_getLocFn)   g_getLocFn   = R::FindFunction(sceneCls, L"K2_GetComponentLocation");
        if (!g_getScaleFn) g_getScaleFn = R::FindFunction(sceneCls, L"K2_GetComponentScale");
        if (!g_setLocFn)   g_setLocFn   = R::FindFunction(sceneCls, L"K2_SetWorldLocation");
        if (!g_setScaleFn) g_setScaleFn = R::FindFunction(sceneCls, L"SetWorldScale3D");
        if (!g_setLocRotFn) g_setLocRotFn = R::FindFunction(sceneCls, L"K2_SetWorldLocationAndRotation");
        if (!g_getRotFn) g_getRotFn = R::FindFunction(sceneCls, L"K2_GetComponentRotation");
    }
    if (Resolved()) {
        g_resolveLatched = true;
        UE_LOGI("roach_sync: resolved (master@0x%X roaches@0x%X addRoach=%p deleteRoach=%p "
                "loc/scale get+set=ok)", g_offMaster, g_offRoaches, g_addRoachFn, g_delRoachFn);
        return;
    }
    if (++g_postClassAttempts >= kMaxPostClassAttempts) {
        g_resolveLatched = true;
        UE_LOGW("roach_sync: resolution INCOMPLETE after %d passes (master@0x%X roaches@0x%X "
                "addRoach=%s deleteRoach=%s) -- latched OFF; game version mismatch?",
                g_postClassAttempts, g_offMaster, g_offRoaches,
                g_addRoachFn ? "yes" : "no", g_delRoachFn ? "yes" : "no");
    }
}

// ---- the live master, read off the world's gamemode ----

void* Master() {
    void* gm = ue_wrap::world_singleton::Gamemode();
    if (!gm || g_offMaster < 0) return nullptr;
    void* m = *reinterpret_cast<void* const*>(reinterpret_cast<uint8_t*>(gm) + g_offMaster);
    if (!m || !R::IsLive(m)) return nullptr;
    return m;
}

struct FArrayRaw { void* Data; int32_t Num; int32_t Max; };

// Valid (non-null, live) roach components in array order, with their RAW index
// (deleteRoach takes the raw index; the wire carries the ORDINAL order).
struct RoachSlot { void* comp; int32_t rawIdx; };

bool ReadRoaches(void* master, std::vector<RoachSlot>& out) {
    out.clear();
    if (!master || g_offRoaches < 0) return false;
    const FArrayRaw* arr = reinterpret_cast<const FArrayRaw*>(
        reinterpret_cast<uint8_t*>(master) + g_offRoaches);
    // The game leaves holes. Refuse an invalid or pathologically sparse array
    // rather than spend an unbounded scan or publish a truncated population.
    constexpr int32_t kMaxRawRoachSlots = 32768;
    if (arr->Num < 0 || arr->Max < arr->Num || arr->Num > kMaxRawRoachSlots ||
        (arr->Num && !arr->Data)) return false;
    if (arr->Num == 0) return true;
    void* const* elems = reinterpret_cast<void* const*>(arr->Data);
    for (int32_t i = 0; i < arr->Num; ++i) {
        void* c = elems[i];
        if (c && R::IsLive(c)) {
            if (out.size() == coop::net::kRoachSnapshotCap) return false;
            out.push_back({c, i});
        }
    }
    return true;
}

bool GetCompLocation(void* comp, Vec3& out) {
    ue_wrap::ParamFrame f(g_getLocFn);
    if (!f.valid() || !ue_wrap::Call(comp, f)) return false;
    return f.GetRaw(L"ReturnValue", &out, sizeof(out));
}

bool GetCompScale(void* comp, Vec3& out) {
    ue_wrap::ParamFrame f(g_getScaleFn);
    if (!f.valid() || !ue_wrap::Call(comp, f)) return false;
    return f.GetRaw(L"ReturnValue", &out, sizeof(out));
}

void SetCompLocation(void* comp, const Vec3& v, bool bTeleport = false) {
    ue_wrap::ParamFrame f(g_setLocFn);
    if (!f.valid()) return;
    f.SetRaw(L"NewLocation", &v, sizeof(v));
    f.Set<bool>(L"bSweep", false);
    f.Set<bool>(L"bTeleport", bTeleport);
    ue_wrap::Call(comp, f);
}

void SetCompScale(void* comp, float s) {
    Vec3 v{s, s, s};
    ue_wrap::ParamFrame f(g_setScaleFn);
    if (!f.valid()) return;
    // SetWorldScale3D's parameter is NewScale (SetRelativeScale3D's is NewScale3D). The frame is
    // zeroed, so a call after a failed write would scale the roach to nothing.
    if (!f.SetRaw(L"NewScale", &v, sizeof(v))) return;
    ue_wrap::Call(comp, f);
}

struct FRotator3 { float Pitch = 0.f, Yaw = 0.f, Roll = 0.f; };

bool GetCompRotation(void* comp, FRotator3& out) {
    if (!g_getRotFn) return false;
    ue_wrap::ParamFrame f(g_getRotFn);
    return f.valid() && ue_wrap::Call(comp, f) && f.GetRaw(L"ReturnValue", &out, sizeof(out)) &&
           std::isfinite(out.Pitch) && std::isfinite(out.Yaw) && std::isfinite(out.Roll);
}

void SetCompLocationAndRotation(void* comp, const Vec3& loc, const FRotator3& rot,
                                bool bTeleport = false) {
    if (g_setLocRotFn) {
        ue_wrap::ParamFrame f(g_setLocRotFn);
        if (f.valid() && f.SetRaw(L"NewLocation", &loc, sizeof(loc)) &&
            f.SetRaw(L"NewRotation", &rot, sizeof(rot)) && f.Set<bool>(L"bSweep", false) &&
            f.Set<bool>(L"bTeleport", bTeleport) && ue_wrap::Call(comp, f)) return;
    }
    SetCompLocation(comp, loc, bTeleport);
}

float Dist2(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

long long g_lastPollMs = 0;

// ---- HOST: snapshot + change detection + paged send ----
struct SnapEntry { Vec3 loc; float scale; };
std::vector<SnapEntry> g_lastSent;    // baseline for drift detection
uint32_t  g_seq = 0;
long long g_lastSendMs = 0;
ue_wrap::CachedObjRef g_polledMaster;  // re-prime on world/save reload/reincarnation

bool ReadSnapshot(void* master, std::vector<SnapEntry>& out) {
    std::vector<RoachSlot> slots;
    if (!ReadRoaches(master, slots)) return false;
    out.clear();
    out.reserve(slots.size());
    for (const auto& s : slots) {
        SnapEntry e{};
        // Never silently omit one component: subsequent ordinals would shift.
        if (!GetCompLocation(s.comp, e.loc)) return false;
        Vec3 sc{1.f, 1.f, 1.f};
        if (!GetCompScale(s.comp, sc)) return false;
        e.scale = sc.x;  // uniform (the game scales roaches uniformly)
        if (!std::isfinite(e.loc.x) || !std::isfinite(e.loc.y) ||
            !std::isfinite(e.loc.z) || !std::isfinite(e.scale)) return false;
        out.push_back(e);
    }
    return true;
}

bool SnapshotChanged(const std::vector<SnapEntry>& cur) {
    if (cur.size() != g_lastSent.size()) return true;
    for (size_t i = 0; i < cur.size(); ++i) {
        // A one-second interpolation needs a one-second stream whenever any
        // roach moves. The old 25-unit gate made slow roaches pause for seconds.
        if (cur[i].loc.x != g_lastSent[i].loc.x ||
            cur[i].loc.y != g_lastSent[i].loc.y ||
            cur[i].loc.z != g_lastSent[i].loc.z) return true;
        if (cur[i].scale != g_lastSent[i].scale) return true;
    }
    return false;
}

// Send the full snapshot as pages; toSlot < 0 broadcasts to every client.
bool SendSnapshot(coop::net::Session* s, const std::vector<SnapEntry>& snap, int toSlot) {
    ++g_seq;
    const int total = static_cast<int>(snap.size());
    const int pages = total == 0 ? 1
                                 : (total + coop::net::kRoachEntriesPerPage - 1) /
                                       coop::net::kRoachEntriesPerPage;
    for (int pg = 0; pg < pages; ++pg) {
        coop::net::RoachStatePayload p{};
        p.seq        = g_seq;
        p.page       = static_cast<uint8_t>(pg);
        p.pageCount  = static_cast<uint8_t>(pages);
        p.totalCount = static_cast<uint8_t>(total);
        const int base = pg * coop::net::kRoachEntriesPerPage;
        int n = 0;
        for (; n < coop::net::kRoachEntriesPerPage && base + n < total; ++n) {
            const SnapEntry& e = snap[base + n];
            p.entries[n] = {e.loc.x, e.loc.y, e.loc.z, e.scale};
        }
        p.entryCount = static_cast<uint8_t>(n);
        const bool ok = toSlot < 0
            ? s->SendReliable(coop::net::ReliableKind::RoachState, &p, sizeof(p))
            : s->SendReliableToSlot(toSlot, coop::net::ReliableKind::RoachState, &p, sizeof(p));
        if (!ok) {
            UE_LOGW("roach_sync: RoachState send FAILED (seq=%u page %d/%d, toSlot=%d)",
                    g_seq, pg + 1, pages, toSlot);
            return false;  // retain broadcast baseline for the next poll
        }
    }
    return true;
}

// ---- CLIENT: page assembly + ordinal apply + local-consumption tracking ----
uint32_t g_asmSeq = 0;
uint16_t g_asmPagesGot = 0;      // bitmask (pageCount <= 11 for 128 roaches)
uint8_t  g_asmPageCount = 0;
uint8_t  g_asmTotal = 0;
bool g_asmInitialized = false;
bool g_asmComplete = false;
SnapEntry g_asmEntries[coop::net::kRoachSnapshotCap];

// Entries follow the most recently applied host ordinals; the component refs
// follow the actual local incarnations, which need not occupy those ordinals.
struct RoachInterp {
    ue_wrap::CachedObjRef comp;
    coop::LerpWindow window;
    Vec3 curPos{0.f, 0.f, 0.f};
    Vec3 targetPos{0.f, 0.f, 0.f};
    Vec3 errorPos{0.f, 0.f, 0.f};
    Vec3 hostPos{0.f, 0.f, 0.f};
    float curScale = 1.f;
    float targetScale = 1.f;
    float errorScale = 0.f;
    float curYaw = 0.f;
    float targetYaw = 0.f;
    float errorYaw = 0.f;
    float pitch = 0.f;
    float roll = 0.f;
    bool consumed = false;
    long long intentSentMs = 0;
    long long nextBirthMs = 0;
};
std::vector<RoachInterp> g_interp;
ue_wrap::CachedObjRef g_trackedMaster;
bool g_haveApplied = false;
std::vector<SnapEntry> g_pendingSnapshot;
std::vector<SnapEntry> g_latestSnapshot;
bool g_pendingApply = false;
long long g_intentWindowMs = 0, g_birthWindowMs = 0;
int g_intentsInWindow = 0, g_birthsInWindow = 0;
size_t g_birthCursor = 0;
uint32_t g_birthRefusals = 0;

void ClearClientState() {
    g_interp.clear();
    g_trackedMaster.Reset();
    g_haveApplied = false;
    g_pendingSnapshot.clear();
    g_latestSnapshot.clear();
    g_pendingApply = false;
    g_intentWindowMs = g_birthWindowMs = 0;
    g_intentsInWindow = g_birthsInWindow = 0;
    g_birthCursor = 0;
    g_birthRefusals = 0;
}

bool CurrentMaster(void* master) {
    return master && g_trackedMaster.Is(master) && g_trackedMaster.Alive() && Master() == master;
}

void AdvanceClientInterpolation(long long now) {
    if (!Resolved() || !g_haveApplied) return;
    void* master = Master();
    if (!master) return;  // Guard against transient master unready: do not wipe state on a single null frame
    if (!CurrentMaster(master)) { ClearClientState(); return; }
    const uint64_t nowU64 = static_cast<uint64_t>(now);
    for (auto& r : g_interp) {
        if (r.consumed || !r.window.IsOpen()) continue;
        void* comp = r.comp.Get();
        if (!comp) continue;

        bool arrived = false;
        const float dAlpha = r.window.Advance(nowU64, &arrived);
        if (dAlpha > 0.f) {
            r.curPos.x += r.errorPos.x * dAlpha;
            r.curPos.y += r.errorPos.y * dAlpha;
            r.curPos.z += r.errorPos.z * dAlpha;
            r.curScale += r.errorScale * dAlpha;
            r.curYaw += r.errorYaw * dAlpha;
        }
        if (arrived) {
            r.curPos = r.targetPos;
            r.curScale = r.targetScale;
            r.curYaw = r.targetYaw;
        }
        if (dAlpha > 0.f || arrived) {
            if (Dist2(r.errorPos, Vec3{0.f, 0.f, 0.f}) > 0.f || std::fabs(r.errorYaw) > 0.001f)
                SetCompLocationAndRotation(comp, r.curPos, FRotator3{r.pitch, r.curYaw, r.roll},
                                           /*bTeleport=*/false);
            if ((std::fabs(r.errorScale) > 0.001f || arrived) &&
                CurrentMaster(master) && r.comp.Is(comp))
                SetCompScale(comp, r.curScale);
        }
    }
}

// Native add appends and delete leaves a null hole. On growth, retain the old
// ordinal prefix; distance matching can mistake a nearby newborn for a moving
// survivor. On shrink, choose the closest ordered surviving prior samples.
// The wire has no IDs, so concurrent births/deletions remain ambiguous.
std::vector<int> MatchSamples(const std::vector<RoachInterp>& old, const SnapEntry* entries, int total) {
    const size_t n = old.size(), m = static_cast<size_t>(total), width = m + 1;
    if (m >= n) {
        std::vector<int> result(m, -1);
        for (size_t i = 0; i < n; ++i) result[i] = static_cast<int>(i);
        return result;
    }
    constexpr double gap = 100000000.0;  // larger than all capped match costs together
    std::vector<double> cost((n + 1) * width, 0.0);
    std::vector<uint8_t> step((n + 1) * width, 0);
    for (size_t i = 1; i <= n; ++i) { cost[i * width] = gap * i; step[i * width] = 1; }
    for (size_t j = 1; j <= m; ++j) { cost[j] = gap * j; step[j] = 2; }
    for (size_t i = 1; i <= n; ++i) for (size_t j = 1; j <= m; ++j) {
        const size_t cell = i * width + j;
        double best = cost[(i - 1) * width + j - 1] +
            std::min(Dist2(old[i - 1].hostPos, entries[j - 1].loc),
                     kTeleportSnapDist * kTeleportSnapDist);
        uint8_t choice = 0;
        const double del = cost[(i - 1) * width + j] + gap;
        const double add = cost[i * width + j - 1] + gap;
        if (del < best) { best = del; choice = 1; }
        if (add < best) { best = add; choice = 2; }
        cost[cell] = best; step[cell] = choice;
    }
    std::vector<int> result(m, -1);
    size_t i = n, j = m;
    while (i || j) {
        const uint8_t s = step[i * width + j];
        if (s == 0) { result[j - 1] = static_cast<int>(i - 1); --i; --j; }
        else if (s == 1) --i;
        else --j;
    }
    return result;
}

bool DetectLocalConsumption(coop::net::Session* s, long long now) {
    if (!g_haveApplied) return true;
    void* master = Master();
    if (!master) {
        if (!g_trackedMaster.Alive()) ClearClientState();
        return false;  // temporarily unavailable is not evidence of consumption
    }
    if (!CurrentMaster(master)) { ClearClientState(); return false; }
    if (!g_intentWindowMs || now - g_intentWindowMs >= kPollIntervalMs || now < g_intentWindowMs) {
        g_intentWindowMs = now; g_intentsInWindow = 0;
    }
    bool ready = true;
    for (auto& r : g_interp) {
        if (!r.consumed && r.comp.Raw() && !r.comp.Alive()) r.consumed = true;
        if (!r.consumed || r.intentSentMs) continue;
        if (g_intentsInWindow >= kMaxIntentsPerTick) { ready = false; continue; }
        coop::net::RoachConsumedPayload p{r.curPos.x, r.curPos.y, r.curPos.z};
        if (s->SendReliable(coop::net::ReliableKind::RoachConsumed, &p, sizeof(p))) {
            r.intentSentMs = now;
            ++g_intentsInWindow;
        } else ready = false;
    }
    return ready;
}

bool DeleteRoach(void* master, int rawIdx) {
    ue_wrap::ParamFrame del(g_delRoachFn);
    if (!del.valid() || !del.Set<int32_t>(L"IndexToRemove", rawIdx) ||
        !del.Set<bool>(L"crush", false)) return false;
    return ue_wrap::Call(master, del) && CurrentMaster(master);
}

bool AddRoach(void* master, const SnapEntry& entry) {
    ue_wrap::ParamFrame add(g_addRoachFn);
    const Vec3 size{entry.scale, entry.scale, entry.scale};
    if (!add.valid() || !add.SetRaw(L"Location", &entry.loc, sizeof(entry.loc)) ||
        !add.SetRaw(L"Size", &size, sizeof(size)) || !add.Set<bool>(L"bypassCheck", true)) return false;
    return ue_wrap::Call(master, add) && CurrentMaster(master);
}

void PrimeRoach(RoachInterp& r, void* comp, const SnapEntry& e) {
    r = RoachInterp{};
    r.comp.Set(comp);
    r.curPos = r.targetPos = r.hostPos = e.loc;
    r.curScale = r.targetScale = e.scale;
    FRotator3 rotation{};
    if (GetCompRotation(comp, rotation)) {
        r.pitch = rotation.Pitch; r.roll = rotation.Roll;
        r.curYaw = r.targetYaw = rotation.Yaw;
    }
    r.errorYaw = 0.f;
}

void TrySpawnMissing(void* master, long long now) {
    if (!g_haveApplied || g_interp.empty() || !CurrentMaster(master)) return;
    if (!g_birthWindowMs || now - g_birthWindowMs >= kPollIntervalMs || now < g_birthWindowMs) {
        g_birthWindowMs = now; g_birthsInWindow = 0;
    }
    const size_t count = g_interp.size();
    size_t examined = 0;
    while (examined++ < count && g_birthsInWindow < kBirthAttemptsPerSecond) {
        const size_t i = g_birthCursor++ % count;
        auto& r = g_interp[i];
        if (r.comp.Raw() || r.consumed || now < r.nextBirthMs) continue;
        ++g_birthsInWindow;
        r.nextBirthMs = now + kBirthRetryMs;
        std::vector<RoachSlot> before, after;
        if (!ReadRoaches(master, before)) return;
        const SnapEntry e{r.hostPos, r.targetScale};
        // Call() means dispatch, not that native add admitted this location:
        // the Blueprint also gates distance to the local camera and population.
        AddRoach(master, e);
        if (!CurrentMaster(master) || !ReadRoaches(master, after)) return;
        void* added = nullptr;
        int additions = 0;
        for (const auto& slot : after) {
            bool found = false;
            for (const auto& old : before) if (old.comp == slot.comp) { found = true; break; }
            if (!found) { added = slot.comp; ++additions; }
        }
        if (additions == 1 && after.size() == before.size() + 1) PrimeRoach(r, added, e);
        else if (additions || after.size() != before.size()) {
            // Ambiguous partial native work: re-prime on a later snapshot.
            g_interp.clear(); g_haveApplied = false; return;
        } else {
            ++g_birthRefusals;
            if ((g_birthRefusals & (g_birthRefusals - 1)) == 0)
                UE_LOGW("roach_sync: native add did not create a component (%u refusals); retrying, "
                        "camera-distance, count or native placement gates may reject it", g_birthRefusals);
        }
    }
}

bool ApplySnapshot(coop::net::Session* s, const SnapEntry* entries, int total, long long now) {
    void* master = Master();
    if (!master) return false;
    if (!g_trackedMaster.Is(master) || !g_trackedMaster.Alive()) {
        // Keep the incoming snapshot while retiring the old world.
        g_interp.clear(); g_haveApplied = false; g_latestSnapshot.clear();
        g_trackedMaster.Set(master);
    }
    if (!CurrentMaster(master)) return false;
    if (!DetectLocalConsumption(s, now)) return false;  // never erase unsent intent evidence
    AdvanceClientInterpolation(now);  // sample the displayed position before rebasing
    if (!CurrentMaster(master)) return false;

    std::vector<RoachSlot> slots;
    if (!ReadRoaches(master, slots)) return false;
    if (!g_haveApplied) {
        // At first contact there is no prior host sample to associate. Preserve
        // equal-sized native populations, otherwise retire the unknown local set.
        if (slots.size() != static_cast<size_t>(total)) {
            for (auto it = slots.rbegin(); it != slots.rend(); ++it)
                if (!DeleteRoach(master, it->rawIdx)) return false;
            if (!ReadRoaches(master, slots)) return false;
            if (!slots.empty()) return false;
        }
        g_interp.resize(total);
        for (int i = 0; i < total; ++i) {
            if (slots.empty()) {
                g_interp[i].hostPos = g_interp[i].curPos = g_interp[i].targetPos = entries[i].loc;
                g_interp[i].curScale = g_interp[i].targetScale = entries[i].scale;
                g_interp[i].curYaw = g_interp[i].targetYaw = 0.f;
                g_interp[i].errorYaw = 0.f;
            } else {
                PrimeRoach(g_interp[i], slots[i].comp, entries[i]);
                SetCompLocation(slots[i].comp, entries[i].loc, true);
                if (!CurrentMaster(master) || !g_interp[i].comp.Is(slots[i].comp)) return false;
                SetCompScale(slots[i].comp, entries[i].scale);
            }
        }
        g_haveApplied = true;
        TrySpawnMissing(master, now);
        return true;
    }

    const auto matches = MatchSamples(g_interp, entries, total);
    std::vector<bool> kept(g_interp.size(), false);
    std::vector<RoachInterp> next(static_cast<size_t>(total));
    for (int j = 0; j < total; ++j) {
        const int i = matches[j];
        if (i < 0) {
            next[j].hostPos = next[j].curPos = next[j].targetPos = entries[j].loc;
            next[j].curScale = next[j].targetScale = entries[j].scale;
            next[j].curYaw = next[j].targetYaw = 0.f;
            next[j].errorYaw = 0.f;
            continue;
        }
        kept[i] = true;
        next[j] = g_interp[i];
        auto& r = next[j];
        const Vec3 previousHost = r.hostPos;
        r.hostPos = entries[j].loc;
        if (r.consumed) {
            // No acknowledgement exists in this protocol. Hide the consumed
            // mirror briefly; an unaccepted intent eventually converges again.
            if (now - r.intentSentMs >= kConsumedGraceMs) {
                r = RoachInterp{};
                r.hostPos = r.curPos = r.targetPos = entries[j].loc;
                r.curScale = r.targetScale = entries[j].scale;
                r.curYaw = r.targetYaw = 0.f;
                r.errorYaw = 0.f;
            }
            continue;
        }
        if (!r.comp.Raw()) {
            r.curPos = r.targetPos = entries[j].loc;
            r.curScale = r.targetScale = entries[j].scale;
            r.curYaw = r.targetYaw = 0.f;
            r.errorYaw = 0.f;
            continue;
        }
        if (!r.comp.Alive()) return false;
        const Vec3 want = entries[j].loc;
        // The host-sample delta describes travel direction; the displayed
        // position can lag behind and point the wrong way after a reversal.
        const float dx = want.x - previousHost.x;
        const float dy = want.y - previousHost.y;
        if (Dist2(r.curPos, want) > kTeleportSnapDist * kTeleportSnapDist) {
            r.targetYaw = r.curYaw;
            r.errorYaw = 0.f;
            r.curPos = r.targetPos = want;
            r.curScale = r.targetScale = entries[j].scale;
            r.window.Close();
            void* comp = r.comp.Get();
            if (!comp) return false;
            SetCompLocation(comp, want, true);
            if (!CurrentMaster(master) || !r.comp.Is(comp)) {
                g_interp.clear(); g_haveApplied = false; return false;
            }
            SetCompScale(comp, entries[j].scale);
        } else {
            r.targetPos = want;
            r.errorPos = {want.x-r.curPos.x, want.y-r.curPos.y, want.z-r.curPos.z};
            r.targetScale = entries[j].scale;
            r.errorScale = entries[j].scale-r.curScale;
            if (dx * dx + dy * dy > 4.f) {
                r.targetYaw = std::atan2(dy, dx) * 57.2957795f;
                r.errorYaw = std::remainder(r.targetYaw - r.curYaw, 360.f);
            } else {
                r.targetYaw = r.curYaw;
                r.errorYaw = 0.f;
            }
            if (Dist2(r.curPos, want) > 0.f || r.errorScale != 0.f || std::fabs(r.errorYaw) > 0.001f)
                r.window.Open(static_cast<uint64_t>(now), static_cast<int>(kPollIntervalMs + kInterpCushionMs));
            else r.window.Close();
        }
    }
    // Delete only unmatched old local components, in reverse raw-index order.
    for (auto it = slots.rbegin(); it != slots.rend(); ++it) {
        bool preserve = false;
        for (size_t i = 0; i < g_interp.size(); ++i)
            if (kept[i] && g_interp[i].comp.Is(it->comp)) { preserve = true; break; }
        if (!preserve && !DeleteRoach(master, it->rawIdx)) {
            g_interp.clear(); g_haveApplied = false; return false;
        }
    }
    g_interp = std::move(next);
    TrySpawnMissing(master, now);
    return true;
}

void TryPendingSnapshot(coop::net::Session* s, long long now) {
    if (!g_pendingApply) return;
    if (ApplySnapshot(s, g_pendingSnapshot.data(), static_cast<int>(g_pendingSnapshot.size()), now)) {
        g_latestSnapshot = g_pendingSnapshot;
        g_pendingApply = false;
    }
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

void Tick() {
    if (!GT::IsGameThread()) return;
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected()) return;
    const long long now = NowMs();

    if (s->role() != coop::net::Role::Host) {
        // CLIENT: run continuous position interpolation every frame
        AdvanceClientInterpolation(now);

        // Client throttled work: 1 Hz liveness scan for consumption intents + resolve pass
        if (now - g_lastPollMs < kPollIntervalMs) return;
        g_lastPollMs = now;
        ResolvePass();
        if (!Resolved()) return;

        if (!DetectLocalConsumption(s, now)) return;
        if (!g_pendingApply) {
            for (const auto& r : g_interp) {
                if (r.consumed && r.intentSentMs && now - r.intentSentMs >= kConsumedGraceMs) {
                    g_pendingSnapshot = g_latestSnapshot;
                    g_pendingApply = true;
                    break;
                }
            }
        }
        TryPendingSnapshot(s, now);
        if (g_haveApplied) TrySpawnMissing(Master(), now);
        return;
    }

    // HOST: throttled 1 Hz work (population poll + paged broadcast)
    if (now - g_lastPollMs < kPollIntervalMs) return;
    g_lastPollMs = now;
    ResolvePass();
    if (!Resolved()) return;

    // HOST: poll -> paged broadcast on change / drift / keepalive.
    void* master = Master();
    if (!master) return;
    std::vector<SnapEntry> snap;
    if (!ReadSnapshot(master, snap)) return;
    if (!g_polledMaster.Is(master) || !g_polledMaster.Alive()) {
        // World/save reload minted a new master -- re-prime silently (the
        // serverbox re-prime rule: a prime never masquerades as an edge).
        g_polledMaster.Set(master);
        g_lastSent = snap;
        g_lastSendMs = now;
        return;
    }
    const bool changed   = SnapshotChanged(snap);
    const bool populationChanged = snap.size() != g_lastSent.size();
    const bool keepalive = (now - g_lastSendMs >= kKeepaliveMs) &&
                           (!snap.empty() || !g_lastSent.empty());
    if (!changed && !keepalive) return;
    if (!SendSnapshot(s, snap, -1)) return;
    g_lastSent = snap;
    g_lastSendMs = now;
    if (populationChanged)
        UE_LOGI("roach_sync: host broadcast %zu roach(es) (seq=%u)", snap.size(), g_seq);
}

void OnState(const coop::net::RoachStatePayload& payload) {
    if (!GT::IsGameThread()) { UE_LOGW("roach_sync: OnState off-game-thread -- dropping"); return; }
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() == coop::net::Role::Host) return;
    ResolvePass();
    if (!Resolved()) return;  // the keepalive re-delivers post-resolve
    const int expectedPages = std::max(1, (static_cast<int>(payload.totalCount) +
        coop::net::kRoachEntriesPerPage - 1) / coop::net::kRoachEntriesPerPage);
    const int base = payload.page * coop::net::kRoachEntriesPerPage;
    const int expectedEntries = std::min(coop::net::kRoachEntriesPerPage,
                                         std::max(0, static_cast<int>(payload.totalCount) - base));
    if (payload.totalCount > coop::net::kRoachSnapshotCap || payload.pageCount != expectedPages ||
        payload.page >= payload.pageCount || payload.entryCount != expectedEntries) {
        UE_LOGW("roach_sync: malformed RoachState (page %u/%u entries=%u total=%u) -- dropping",
                payload.page, payload.pageCount, payload.entryCount, payload.totalCount);
        return;
    }
    for (int i = 0; i < payload.entryCount; ++i) {
        const auto& e = payload.entries[i];
        if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z) || !std::isfinite(e.scale)) return;
    }
    if (!g_asmInitialized || payload.seq != g_asmSeq) {
        if (g_asmInitialized && static_cast<int32_t>(payload.seq - g_asmSeq) <= 0) return;
        g_asmInitialized = true;
        g_asmComplete = false;
        g_asmSeq = payload.seq;
        g_asmPagesGot = 0;
        g_asmPageCount = payload.pageCount;
        g_asmTotal = payload.totalCount;
    } else if (g_asmComplete || payload.pageCount != g_asmPageCount || payload.totalCount != g_asmTotal) return;
    for (int i = 0; i < payload.entryCount && base + i < coop::net::kRoachSnapshotCap; ++i) {
        const auto& e = payload.entries[i];
        g_asmEntries[base + i] = SnapEntry{{e.x, e.y, e.z}, e.scale};
    }
    g_asmPagesGot |= static_cast<uint16_t>(1u << payload.page);
    const uint16_t want = static_cast<uint16_t>((1u << g_asmPageCount) - 1u);
    if ((g_asmPagesGot & want) != want) return;  // more pages coming (in-order lane)
    g_asmComplete = true;
    g_pendingSnapshot.assign(g_asmEntries, g_asmEntries + g_asmTotal);
    g_pendingApply = true;
    TryPendingSnapshot(s, NowMs());
}

void OnConsumedIntent(const coop::net::RoachConsumedPayload& payload, int senderPeerSlot) {
    if (!GT::IsGameThread()) { UE_LOGW("roach_sync: OnConsumedIntent off-game-thread -- dropping"); return; }
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || s->role() != coop::net::Role::Host) return;
    if (senderPeerSlot <= 0) {
        UE_LOGW("roach_sync: RoachConsumed from slot=%d (not a client) -- dropping", senderPeerSlot);
        return;
    }
    ResolvePass();
    if (!Resolved()) return;
    void* master = Master();
    if (!master) return;
    std::vector<RoachSlot> slots;
    if (!ReadRoaches(master, slots)) return;
    const Vec3 at{payload.x, payload.y, payload.z};
    int32_t bestIdx = -1;
    float bestD2 = kAdjudicateRadius * kAdjudicateRadius;
    for (const auto& slot : slots) {
        Vec3 loc{};
        if (!GetCompLocation(slot.comp, loc)) continue;
        const float d2 = Dist2(loc, at);
        if (d2 <= bestD2) { bestD2 = d2; bestIdx = slot.rawIdx; }
    }
    if (bestIdx < 0) {
        // Already gone (host trim raced the intent) or a stale/duplicate
        // intent -- benign; the periodic snapshot has converged everyone.
        UE_LOGI("roach_sync: consumed-intent from slot %d matched no roach within %.0f -- no-op",
                senderPeerSlot, kAdjudicateRadius);
        return;
    }
    ue_wrap::ParamFrame del(g_delRoachFn);
    if (!del.valid()) return;
    del.Set<int32_t>(L"IndexToRemove", bestIdx);
    del.Set<bool>(L"crush", false);
    ue_wrap::Call(master, del);
    UE_LOGI("roach_sync: slot %d consumed roach rawIdx=%d (d=%.0f) -- deleted; next poll broadcasts",
            senderPeerSlot, bestIdx, std::sqrt(bestD2));
}

void QueueConnectBroadcastForSlot(int slot) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Host) return;
    if (slot < 0 || slot >= static_cast<int>(coop::players::kMaxPeers)) return;
    if (!Resolved()) return;  // no world yet -> the keepalive delivers state
    void* master = Master();
    if (!master) return;
    std::vector<SnapEntry> snap;
    if (!ReadSnapshot(master, snap)) return;
    if (snap.empty()) return;  // joiner starts at zero roaches natively
    if (SendSnapshot(s, snap, slot))
        UE_LOGI("roach_sync: connect-snapshot -- %zu roach(es) to slot %d", snap.size(), slot);
}

void OnDisconnect() {
    g_polledMaster.Reset();
    g_lastSent.clear();
    g_lastSendMs = 0; g_lastPollMs = 0;
    g_asmSeq = 0; g_asmPagesGot = 0; g_asmPageCount = 0; g_asmTotal = 0;
    g_asmInitialized = false; g_asmComplete = false;
    ClearClientState();
    g_session.store(nullptr, std::memory_order_release);
}

}  // namespace coop::roach_sync
