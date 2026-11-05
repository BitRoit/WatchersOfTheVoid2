// ue_wrap/devices/roomba.cpp -- see ue_wrap/devices/roomba.h. Engine access and native bug mitigation
// for the Broomba (prop_roomba_C / prop_roomba_mx_C). Offsets are resolved from the live class by name.

#include "ue_wrap/devices/roomba.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/facts/function_row.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace ue_wrap::roomba {
namespace {

namespace R = reflection;
namespace sg = ue_wrap::script_gate;

UE_FACT_FN(kRowRoombaTick, "roomba", L"prop_roomba_C", L"ReceiveTick", Declared);
constexpr int kTagRoombaTick = 0x524D4254;  // 'RMBT'

std::atomic<bool> g_resolved{false};
std::atomic<bool> g_tickWatched{false};
std::atomic<bool> g_isClient{false};

int32_t g_rotatOff  = -1; // bool rotat
int32_t g_rotOff    = -1; // float rot
int32_t g_wheelsOff = -1; // TArray wheels
int32_t g_activeOff = -1; // bool active
int32_t g_movOff    = -1; // bool mov
int32_t g_procOff   = -1; // bool proc
int32_t g_locOff    = -1; // FVector loc

void* g_failedCls = nullptr;
ue_wrap::CachedObjRef g_roombaClsRef;

void* RoombaClass() {
    if (void* c = g_roombaClsRef.Get()) return c;
    void* c = ue_wrap::object_index::ClassByName(L"prop_roomba_C");
    if (c) g_roombaClsRef.Set(c);
    return c;
}

inline float AngularDifference(float current, float target) {
    float diff = std::fmod(current - target, 360.0f);
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    return diff;
}

struct SpinTracker {
    std::chrono::steady_clock::time_point startRotTime{};
    bool wasRotating = false;
    int32_t savedWheelNum = -1;
};

std::unordered_map<void*, SpinTracker> g_spinTrackers;

sg::Verdict OnRoombaTickPre(const sg::Call& c) {
    if (!c.object || !IsRoomba(c.object) || c.fromOurCode) return sg::Verdict::Run;
    if (g_rotatOff < 0 || g_rotOff < 0) return sg::Verdict::Run;

    if (g_spinTrackers.size() > 64) {
        g_spinTrackers.clear();
    }
    auto& tracker = g_spinTrackers[c.object];

    // 1. Client wheel torque suppression in multiplayer
    if (g_isClient.load(std::memory_order_relaxed) && g_wheelsOff >= 0) {
        auto* arr = reinterpret_cast<ue_wrap::field_io::TArrayView*>(reinterpret_cast<char*>(c.object) + g_wheelsOff);
        if (arr && arr->num > 0) {
            tracker.savedWheelNum = arr->num;
            arr->num = 0;  // suppress local torque; host drives position
        }
    }

    // 2. Angular wrap & stuck rotation fix
    bool* pRotat = reinterpret_cast<bool*>(reinterpret_cast<char*>(c.object) + g_rotatOff);
    float* pRot  = reinterpret_cast<float*>(reinterpret_cast<char*>(c.object) + g_rotOff);

    if (pRotat && *pRotat && pRot) {
        const auto now = std::chrono::steady_clock::now();
        if (!tracker.wasRotating) {
            tracker.wasRotating = true;
            tracker.startRotTime = now;
        }

        ue_wrap::FRotator currentRot{};
        if (ue_wrap::engine::TryGetActorRotation(c.object, currentRot)) {
            const float diff = AngularDifference(currentRot.Yaw, *pRot);
            const float absDiff = std::fabs(diff);
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - tracker.startRotTime).count();

            // If within 15 degrees of target OR stuck rotating for > 1500ms:
            // Snap target rot to current Yaw so native NearlyEqual(Yaw, rot, 10.0f) evaluates to TRUE!
            if (absDiff <= 15.0f || elapsedMs > 1500) {
                *pRot = currentRot.Yaw;
                tracker.wasRotating = false;
            }
        }
    } else {
        tracker.wasRotating = false;
    }

    return sg::Verdict::Run;
}

void OnRoombaTickPost(const sg::Call& c) {
    if (!c.object || !IsRoomba(c.object) || c.fromOurCode) return;

    auto it = g_spinTrackers.find(c.object);
    if (it != g_spinTrackers.end() && it->second.savedWheelNum >= 0 && g_wheelsOff >= 0) {
        auto* arr = reinterpret_cast<ue_wrap::field_io::TArrayView*>(reinterpret_cast<char*>(c.object) + g_wheelsOff);
        if (arr) {
            arr->num = it->second.savedWheelNum;
        }
        it->second.savedWheelNum = -1;
    }
}

}  // namespace

void SetClientRole(bool isClient) {
    g_isClient.store(isClient, std::memory_order_relaxed);
}

bool Install() {
    EnsureResolved();
    if (!g_tickWatched.load(std::memory_order_relaxed)) {
        if (sg::WatchName(kRowRoombaTick, kTagRoombaTick, &OnRoombaTickPre, &OnRoombaTickPost)) {
            g_tickWatched.store(true, std::memory_order_release);
            UE_LOGI("roomba: hooked prop_roomba_C::ReceiveTick for angular wrap and torque fix");
        }
    }
    return g_tickWatched.load(std::memory_order_acquire);
}

bool EnsureResolved() {
    if (g_resolved.load(std::memory_order_acquire)) {
        if (!g_tickWatched.load(std::memory_order_relaxed)) Install();
        return true;
    }

    void* cls = RoombaClass();
    if (!cls || cls == g_failedCls) return false;

    const int32_t rotatOff = R::FindPropertyOffset(cls, L"rotat");
    const int32_t rotOff   = R::FindPropertyOffset(cls, L"rot");
    if (rotatOff < 0 || rotOff < 0) {
        g_failedCls = cls;
        UE_LOGE("roomba: prop_roomba_C properties rotat@%d rot@%d failed to resolve", rotatOff, rotOff);
        return false;
    }

    g_rotatOff  = rotatOff;
    g_rotOff    = rotOff;
    g_wheelsOff = R::FindPropertyOffset(cls, L"wheels");
    g_activeOff = R::FindPropertyOffset(cls, L"active");
    g_movOff    = R::FindPropertyOffset(cls, L"mov");
    g_procOff   = R::FindPropertyOffset(cls, L"proc");
    g_locOff    = R::FindPropertyOffset(cls, L"loc");

    g_resolved.store(true, std::memory_order_release);
    UE_LOGI("roomba: resolved prop_roomba_C rotat@0x%04X rot@0x%04X wheels@0x%04X active@0x%04X",
            rotatOff, rotOff, g_wheelsOff, g_activeOff);

    Install();
    return true;
}

bool IsRoomba(void* obj) {
    if (!obj || !g_resolved.load(std::memory_order_acquire)) return false;
    void* roombaCls = RoombaClass();
    void* cls = R::ClassOf(obj);
    return roombaCls && cls && R::IsDescendantOfAny(cls, &roombaCls, 1);
}

}  // namespace ue_wrap::roomba
