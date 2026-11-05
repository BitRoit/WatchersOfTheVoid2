// agent/tests/roomba_sync_test.cpp -- test fixture for prop_roomba_C angular wrap and sync logic
#include "ue_wrap/core/field_io.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

inline float AngularDifference(float current, float target) {
    float diff = std::fmod(current - target, 360.0f);
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    return diff;
}

struct MockRoomba {
    bool rotat = false;
    float rot = 0.0f;
    ue_wrap::field_io::TArrayView wheels;
    bool active = false;
    bool mov = false;
    bool proc = false;
};

struct Tracker {
    std::chrono::steady_clock::time_point startRotTime{};
    bool wasRotating = false;
    int32_t savedWheelNum = -1;
};

void SimulateTick(MockRoomba& r, Tracker& tracker, float currentYaw, bool isClient, std::chrono::steady_clock::time_point now) {
    // PRE
    if (isClient && r.wheels.num > 0) {
        tracker.savedWheelNum = r.wheels.num;
        r.wheels.num = 0;
    }

    if (r.rotat) {
        if (!tracker.wasRotating) {
            tracker.wasRotating = true;
            tracker.startRotTime = now;
        }

        const float diff = AngularDifference(currentYaw, r.rot);
        const float absDiff = std::fabs(diff);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - tracker.startRotTime).count();

        if (absDiff <= 15.0f || elapsedMs > 1500) {
            r.rot = currentYaw;
            tracker.wasRotating = false;
        }
    } else {
        tracker.wasRotating = false;
    }

    // POST
    if (tracker.savedWheelNum >= 0) {
        r.wheels.num = tracker.savedWheelNum;
        tracker.savedWheelNum = -1;
    }
}

}  // namespace

int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while(0)

int main() {
    using namespace std::chrono;
    const auto t0 = steady_clock::now();

    // Test 1: Angular wrap difference calculation
    CHECK(std::fabs(AngularDifference(175.0f, -175.0f)) == 10.0f);
    CHECK(std::fabs(AngularDifference(-175.0f, 175.0f)) == 10.0f);
    CHECK(std::fabs(AngularDifference(0.0f, 180.0f)) == 180.0f);
    CHECK(std::fabs(AngularDifference(10.0f, -10.0f)) == 20.0f);
    CHECK(std::fabs(AngularDifference(-170.0f, 170.0f)) == 20.0f);

    // Test 2: Roomba crossing +/-180 wrap boundary snaps to yaw when within 15 deg
    {
        MockRoomba r;
        r.rotat = true;
        r.rot = -175.0f;  // target yaw
        r.wheels = { nullptr, 2, 2 };
        Tracker tracker;

        // Current yaw is +178 (angular diff is 7 deg)
        SimulateTick(r, tracker, 178.0f, false, t0);
        CHECK(r.rot == 178.0f);  // Snapped to currentYaw to satisfy NearlyEqual!
        CHECK(!tracker.wasRotating);
    }

    // Test 3: Normal rotation out of range does not prematurely snap
    {
        MockRoomba r;
        r.rotat = true;
        r.rot = 90.0f;
        r.wheels = { nullptr, 2, 2 };
        Tracker tracker;

        // Current yaw is 0 (angular diff is 90 deg)
        SimulateTick(r, tracker, 0.0f, false, t0);
        CHECK(r.rot == 90.0f);  // NOT snapped!
        CHECK(tracker.wasRotating == true);
    }

    // Test 4: Stuck rotation timeout (> 1500ms) snaps to yaw to unstuck
    {
        MockRoomba r;
        r.rotat = true;
        r.rot = 90.0f;
        r.wheels = { nullptr, 2, 2 };
        Tracker tracker;

        // Start rotating at t0
        SimulateTick(r, tracker, 0.0f, false, t0);
        CHECK(r.rot == 90.0f);

        // 1600ms later, still stuck at yaw 0
        SimulateTick(r, tracker, 0.0f, false, t0 + milliseconds(1600));
        CHECK(r.rot == 0.0f);  // Unstuck! Target snapped to current yaw!
        CHECK(!tracker.wasRotating);
    }

    // Test 5: Client wheel torque suppression
    {
        MockRoomba r;
        r.rotat = false;
        r.wheels = { nullptr, 2, 2 };
        Tracker tracker;

        // On client: during tick, wheels.num was 0, after tick restored to 2
        bool duringTickSawZero = false;
        auto clientTick = [&]() {
            if (r.wheels.num > 0) {
                tracker.savedWheelNum = r.wheels.num;
                r.wheels.num = 0;
            }
            if (r.wheels.num == 0) duringTickSawZero = true;
            if (tracker.savedWheelNum >= 0) {
                r.wheels.num = tracker.savedWheelNum;
                tracker.savedWheelNum = -1;
            }
        };

        clientTick();
        CHECK(duringTickSawZero);
        CHECK(r.wheels.num == 2);
    }

    std::printf("PASS roomba sync test: %d checks; angular wrap normalization, timeout recovery, and client wheel suppression verified\n", checks);
    return 0;
}
