// Engine access and angular-wrap mitigation for prop_roomba_C and its subclasses.
#include "ue_wrap/devices/roomba.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/facts/function_row.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace ue_wrap::roomba {
namespace {
namespace R = reflection;
namespace sg = script_gate;

UE_FACT_FN(kRowRoombaTick, "roomba", L"prop_roomba_C", L"ReceiveTick", Declared);
constexpr int kTagRoombaTick = 0x524D4254;

std::atomic<bool> g_resolved{false};
std::atomic<bool> g_tickWatched{false};
int32_t g_rotatOff = -1;
int32_t g_rotOff = -1;
int32_t g_movOff = -1;
int32_t g_activeOff = -1;
void* g_failedCls = nullptr;
CachedObjRef g_roombaClsRef;

void* RoombaClass() {
    if (void* c = g_roombaClsRef.Get()) return c;
    void* c = object_index::ClassByName(L"prop_roomba_C");
    if (c) g_roombaClsRef.Set(c);
    return c;
}

sg::Verdict OnRoombaTickPre(const sg::Call& c) {
    if (!c.object || c.fromOurCode || !EnsureResolved() || !IsRoomba(c.object)) return sg::Verdict::Run;
    const auto* base = reinterpret_cast<const char*>(c.object);
    if (!*reinterpret_cast<const bool*>(base + g_rotatOff) &&
        !*reinterpret_cast<const bool*>(base + g_movOff)) return sg::Verdict::Run;

    auto* target = reinterpret_cast<float*>(reinterpret_cast<char*>(c.object) + g_rotOff);
    FRotator current{};
    if (!engine::TryGetActorRotation(c.object, current) ||
        !std::isfinite(current.Yaw) || !std::isfinite(*target)) return sg::Verdict::Run;

    float diff = std::fmod(current.Yaw - *target, 360.0f);
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    if (std::isfinite(diff) && std::fabs(diff) <= 10.0f &&
        std::fabs(current.Yaw - *target) > 10.0f) {
        // The Blueprint compares scalars, then uses this angle to derive the next
        // direction. Keep the same physical direction on the yaw's 360-degree
        // branch. No post restore: its success branch writes the next target, and
        // a cancelled/faulted script body is not guaranteed to call a post hook.
        *target = current.Yaw - diff;
    }
    return sg::Verdict::Run;
}
}  // namespace

bool Install() {
    // Name registration is safe before the class is loaded or on the boot thread.
    // Reflection happens on the game thread in EnsureResolved / the tick callback.
    if (!g_tickWatched.load(std::memory_order_acquire) &&
        sg::WatchName(kRowRoombaTick, kTagRoombaTick, &OnRoombaTickPre, nullptr)) {
        g_tickWatched.store(true, std::memory_order_release);
        UE_LOGI("roomba: registered prop_roomba_C::ReceiveTick angular wrap watch");
    }
    return g_tickWatched.load(std::memory_order_acquire);
}

bool EnsureResolved() {
    if (g_resolved.load(std::memory_order_acquire)) return true;
    void* cls = RoombaClass();
    if (!cls || cls == g_failedCls) return false;

    const int32_t rotatOff = R::FindPropertyOffset(cls, L"rotat");
    const int32_t rotOff = R::FindPropertyOffset(cls, L"rot");
    const int32_t movOff = R::FindPropertyOffset(cls, L"mov");
    const int32_t activeOff = R::FindPropertyOffset(cls, L"active");
    if (rotatOff < 0 || rotOff < 0 || movOff < 0 || activeOff < 0) {
        g_failedCls = cls;
        UE_LOGE("roomba: properties rotat@%d rot@%d mov@%d active@%d failed to resolve",
                rotatOff, rotOff, movOff, activeOff);
        return false;
    }
    g_rotatOff = rotatOff;
    g_rotOff = rotOff;
    g_movOff = movOff;
    g_activeOff = activeOff;
    g_resolved.store(true, std::memory_order_release);
    UE_LOGI("roomba: resolved rotat@0x%04X rot@0x%04X mov@0x%04X active@0x%04X",
            rotatOff, rotOff, movOff, activeOff);
    return true;
}

bool IsRoomba(void* obj) {
    if (!obj || !g_resolved.load(std::memory_order_acquire)) return false;
    void* roombaCls = RoombaClass();
    void* cls = R::ClassOf(obj);
    return roombaCls && cls && R::IsDescendantOfAny(cls, &roombaCls, 1);
}

bool IsActive(void* obj) {
    return obj && g_resolved.load(std::memory_order_acquire) && g_activeOff >= 0 &&
           *reinterpret_cast<const bool*>(reinterpret_cast<const char*>(obj) + g_activeOff);
}
}  // namespace ue_wrap::roomba
