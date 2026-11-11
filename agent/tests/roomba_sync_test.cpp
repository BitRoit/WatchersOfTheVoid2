// Exercises the actual wrapper and coop callbacks with engine/session doubles.
#include "coop/net/session.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/ufunction_hook.h"
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace test {
struct Roomba { bool rotat = false, mov = false, active = true; float rot = 0, yaw = 0; int wheels = 2; };
Roomba actor;
int roombaClass, primitiveClass, graph, otherGraph, otherOwner, world, staleWorld;
bool classReady = false, rotationOk = true, held = false, hand = false;
bool parked = false, announced = true, currentWorld = true, stale = false, subclass = true;
bool wrongOwner = false;
int watchCount = 0, coastCount = 0, hookCount = 0;
ue_wrap::script_gate::PreFn pre = nullptr;
ue_wrap::script_gate::PostFn post = nullptr;
ue_wrap::ufunction_hook::PreNativeCallback torque = nullptr;
void* caller = &graph;
}

#ifndef ROOMBA_WRAPPER_SOURCE
#define ROOMBA_WRAPPER_SOURCE "../../src/votv-coop/src/ue_wrap/devices/roomba.cpp"
#endif
#ifndef ROOMBA_SYNC_SOURCE
#define ROOMBA_SYNC_SOURCE "../../src/votv-coop/src/coop/props/roomba_sync.cpp"
#endif
#include ROOMBA_WRAPPER_SOURCE
#include ROOMBA_SYNC_SOURCE

namespace ue_wrap::log { void Write(Level, const char*, ...) {} }
namespace ue_wrap::reflection {
int32_t InternalIndexOf(void* p) { return p ? 1 : -1; }
int32_t AllocateSlotSerial(int32_t) { return 1; }
int32_t SlotSerial(int32_t) { return 1; }
bool IsLiveByIndex(void* p, int32_t) { return p != nullptr; }
void* ClassOf(void* p) { return p == &test::actor && test::subclass ? &test::roombaClass : &test::otherOwner; }
bool IsDescendantOfAny(void* cls, void* const* bases, size_t n, int) { return n && cls == bases[0]; }
int32_t FindPropertyOffset(void*, const wchar_t* name) {
    if (!std::wcscmp(name, L"rotat")) return offsetof(test::Roomba, rotat);
    if (!std::wcscmp(name, L"mov")) return offsetof(test::Roomba, mov);
    if (!std::wcscmp(name, L"rot")) return offsetof(test::Roomba, rot);
    if (!std::wcscmp(name, L"active")) return offsetof(test::Roomba, active);
    return -1;
}
const FName& NameOf(void* obj) {
    static FName graph{1,0}, cls{2,0}, other{3,0};
    return obj == &test::graph ? graph : obj == &test::roombaClass ? cls : other;
}
void* OuterOf(void*) { return test::wrongOwner ? &test::otherOwner : &test::roombaClass; }
bool NameEquals(const FName& name, const wchar_t* text) {
    return (name.ComparisonIndex == 1 && !std::wcscmp(text, L"ExecuteUbergraph_prop_roomba")) ||
           (name.ComparisonIndex == 2 && !std::wcscmp(text, L"prop_roomba_C"));
}
}
namespace ue_wrap::world_identity {
void* CurrentWorld() { return test::currentWorld ? &test::world : nullptr; }
void* WorldOf(void* p) {
    if (p == &test::actor) return test::stale ? &test::staleWorld : &test::world;
    return nullptr;
}
}
namespace ue_wrap::object_index {
void* ClassByName(const wchar_t* name) {
    if (!std::wcscmp(name, L"prop_roomba_C")) return test::classReady ? &test::roombaClass : nullptr;
    return &test::primitiveClass;
}
}
namespace ue_wrap::engine {
bool TryGetActorRotation(void* p, FRotator& out) {
    if (!test::rotationOk) return false;
    out.Yaw = static_cast<test::Roomba*>(p)->yaw;
    return true;
}
}
namespace ue_wrap::facts {
Function::Function(detail::MacroToken, const char* id, const char* feat, const wchar_t* cls,
                   const wchar_t* fn, Mode mode) : ident_(id), feature_(feat), cls_(cls), fn_(fn), mode_(mode) {}
Resolved ResolveOn(const Function&, void*) { return {}; }
}
namespace ue_wrap::script_gate {
bool WatchName(const facts::Function& row, int, PreFn pre, PostFn post) {
    ++test::watchCount;
    if (std::strcmp(row.Feature(), "roomba") == 0) test::pre = pre;
    else test::post = post;
    return true;
}
}
namespace ue_wrap::ufunction_hook {
bool InstallPreHook(const facts::Resolved&, PreNativeCallback cb, bool armed) {
    ++test::hookCount;
    test::torque = armed ? cb : nullptr;
    return true;
}
CallerFrame CurrentCallerFrame() { return { test::caller, nullptr, -1 }; }
}
namespace coop::local_prop_hold { bool IsHeld(void*) { return test::held; } }
namespace coop::prop_drive_host {
bool InAHand(void*) { return test::hand; }
void Coast(void*, const char*, bool) { ++test::coastCount; }
}
namespace coop::prop_drive_stream { bool IsParked(void*) { return test::parked; } }
namespace coop::net_pump { bool IsInAnnouncedWorld(void*) { return test::announced; } }

int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
    using namespace test;
    using V = ue_wrap::script_gate::Verdict;
    namespace S = coop::roomba_sync;
    namespace W = ue_wrap::roomba;
    ue_wrap::script_gate::Call call{}; call.object = &actor;
    coop::net::Session session;

    // Registration does not resolve the class on the boot thread or recurse.
    CHECK(W::Install()); CHECK(W::Install()); CHECK(watchCount == 1 && pre);
    CHECK(!W::EnsureResolved()); classReady = true;
    CHECK(W::EnsureResolved()); CHECK(W::EnsureResolved()); CHECK(watchCount == 1);
    actor.rotat = true; actor.yaw = 179; actor.rot = -179;
    CHECK(pre(call) == V::Run); CHECK(actor.rot == 181 && actor.wheels == 2);
    // Emulate the native success branch. The next direction survives; no post restores the old target.
    CHECK(std::fabs(actor.yaw - actor.rot) <= 10.f);
    actor.rotat = false; actor.mov = true; actor.rot = -89;
    CHECK(actor.rot == -89);
    actor.yaw = -179; actor.rot = 179; CHECK(pre(call) == V::Run); CHECK(actor.rot == -181);
    actor.yaw = 179; actor.rot = 539; CHECK(pre(call) == V::Run); CHECK(actor.rot == 179);
    actor.yaw = -179; actor.rot = -539; CHECK(pre(call) == V::Run); CHECK(actor.rot == -179);
    actor.yaw = 175; actor.rot = -175; CHECK(pre(call) == V::Run); CHECK(actor.rot == 185);
    actor.yaw = 174.9f; actor.rot = -175; CHECK(pre(call) == V::Run); CHECK(actor.rot == -175);
    actor.yaw = 0; actor.rot = 90; CHECK(pre(call) == V::Run); CHECK(actor.rot == 90);
    for (int i = 0; i < 1000; ++i) CHECK(pre(call) == V::Run && actor.rot == 90);
    actor.mov = false; actor.yaw = 179; actor.rot = -179; CHECK(pre(call) == V::Run); CHECK(actor.rot == -179);
    actor.rotat = true; call.fromOurCode = true; CHECK(pre(call) == V::Run); CHECK(actor.rot == -179);
    call.fromOurCode = false; rotationOk = false; CHECK(pre(call) == V::Run); CHECK(actor.rot == -179);
    rotationOk = true; actor.yaw = std::numeric_limits<float>::infinity(); CHECK(pre(call) == V::Run); CHECK(actor.rot == -179);
    actor.yaw = 179; actor.rot = std::numeric_limits<float>::quiet_NaN(); CHECK(pre(call) == V::Run); CHECK(std::isnan(actor.rot));

    // Actual host callback feeds only active, current-world, unheld actors.
    S::Install(&session); S::Install(&session); CHECK(post && watchCount == 2);
    actor.active = true; post(call); CHECK(coastCount == 1);
    actor.active = false; post(call); CHECK(coastCount == 1); actor.active = true;
    held = true; post(call); CHECK(coastCount == 1); held = false;
    hand = true; post(call); CHECK(coastCount == 1); hand = false;
    stale = true; post(call); CHECK(coastCount == 1); stale = false;
    currentWorld = false; post(call); CHECK(coastCount == 1); currentWorld = true;
    subclass = false; post(call); CHECK(coastCount == 1); subclass = true;
    call.fromOurCode = true; post(call); CHECK(coastCount == 1); call.fromOurCode = false;
    session.linked = false; post(call); CHECK(coastCount == 1); session.linked = true;
    post(call); CHECK(coastCount == 2);

    // The actual client native hook is armed and refuses only host-parked wheel calls.
    session.mode = coop::net::Role::Client; S::Install(&session); CHECK(hookCount == 1 && torque);
    S::Install(&session); CHECK(hookCount == 1); post(call); CHECK(coastCount == 2);
    CHECK(torque(nullptr, &actor, nullptr) == V::Run);
    parked = true; CHECK(torque(nullptr, &actor, nullptr) == V::Cancel);
    announced = false; CHECK(torque(nullptr, &actor, nullptr) == V::Run); announced = true;
    stale = true; CHECK(torque(nullptr, &actor, nullptr) == V::Run); stale = false;
    caller = &otherGraph; CHECK(torque(nullptr, &actor, nullptr) == V::Run); caller = &graph;
    wrongOwner = true; CHECK(torque(nullptr, &actor, nullptr) == V::Run); wrongOwner = false;
    subclass = false; CHECK(torque(nullptr, &actor, nullptr) == V::Run); subclass = true;
    S::OnDisconnect(); CHECK(torque(nullptr, &actor, nullptr) == V::Run); CHECK(actor.wheels == 2);
    std::printf("PASS roomba production callbacks: %d checks; native boundaries mocked\n", checks);
}
