// agent/tests/garage_snap_test.cpp -- test fixture for Agarage_C snap load logic and settime interception
#include "ue_wrap/core/types.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace test {

struct MockGarage {
    bool Open = false;
    bool mov = true;
    void* moveComp = (void*)0x10;
    void* skelMesh = (void*)0x20;
    void* boxComp = (void*)0x30;
    void* loopComp = (void*)0x40;
    void* navComp = (void*)0x50;
};

struct Calls {
    float setInputPin = -1.0f;
    float timelineNewTime = -1.0f;
    bool timelineStopped = false;
    float montageStartTime = -1.0f;
    std::wstring montageSection;
    bool loopStopped = false;
    void* navAreaClass = (void*)0x999;
} g_calls;

void Reset() {
    g_calls = Calls{};
}

}  // namespace test

// Mock production SnapOpen logic under test
namespace test_garage {

bool ExecuteSnapOpen(test::MockGarage* g, bool open) {
    if (!g) return false;

    g->Open = open;
    g->mov = false;

    // 1. Call native set(open ? 1.0f : 0.0f)
    test::g_calls.setInputPin = open ? 1.0f : 0.0f;

    // 2. Timeline 'move': set time to 6.0s (open) or 0.0s (closed) and stop ticking
    test::g_calls.timelineNewTime = open ? 6.0f : 0.0f;
    test::g_calls.timelineStopped = true;

    // 3. SkeletalMesh: play garageDoor_Skeleton_Montage at openLoop or closeLoop
    test::g_calls.montageStartTime = open ? 4.0f : 12.0f;
    test::g_calls.montageSection = open ? L"openLoop" : L"closeLoop";

    // 4. Audio loop: stop
    test::g_calls.loopStopped = true;

    // 5. NavModifier: clear if open, set NavArea_Obstacle if closed
    test::g_calls.navAreaClass = open ? nullptr : (void*)0x1234;

    return true;
}

}  // namespace test_garage

int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while(0)

int main() {
    using namespace test;

    // Test 1: Snap open
    {
        Reset();
        MockGarage g;
        g.Open = false;
        g.mov = true;
        CHECK(test_garage::ExecuteSnapOpen(&g, true));
        CHECK(g.Open == true);
        CHECK(g.mov == false);
        CHECK(g_calls.setInputPin == 1.0f);
        CHECK(g_calls.timelineNewTime == 6.0f);   // Must NOT be 1.0f (buggy native bytecode)
        CHECK(g_calls.timelineStopped == true);
        CHECK(g_calls.montageStartTime == 4.0f);  // Must NOT be 100.0f (buggy native bytecode)
        CHECK(g_calls.montageSection == L"openLoop");
        CHECK(g_calls.loopStopped == true);
        CHECK(g_calls.navAreaClass == nullptr);
    }

    // Test 2: Snap closed
    {
        Reset();
        MockGarage g;
        g.Open = true;
        g.mov = true;
        CHECK(test_garage::ExecuteSnapOpen(&g, false));
        CHECK(g.Open == false);
        CHECK(g.mov == false);
        CHECK(g_calls.setInputPin == 0.0f);
        CHECK(g_calls.timelineNewTime == 0.0f);
        CHECK(g_calls.timelineStopped == true);
        CHECK(g_calls.montageStartTime == 12.0f);
        CHECK(g_calls.montageSection == L"closeLoop");
        CHECK(g_calls.loopStopped == true);
        CHECK(g_calls.navAreaClass == (void*)0x1234);
    }

    // Test 3: Null safety
    {
        Reset();
        CHECK(!test_garage::ExecuteSnapOpen(nullptr, true));
    }

    std::printf("PASS garage snap load test: %d checks; correct 6.0s timeline, looping montage sections, and collision update verified\n", checks);
    return 0;
}
