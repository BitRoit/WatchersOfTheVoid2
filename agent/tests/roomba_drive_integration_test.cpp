// Real Roomba feeder + real host/receiver prop-drive code; engine and transport are doubles.
#define main existing_prop_drive_fixture_main
#include "prop_drive_2123_test.cpp"
#undef main
#include "../../src/votv-coop/src/coop/props/roomba_sync.cpp"

namespace test {
bool roombaActive = true;
int graph, roombaClass, primitiveClass;
ue_wrap::ufunction_hook::PreNativeCallback torque = nullptr;
}
namespace ue_wrap::roomba {
bool EnsureResolved() { return true; }
bool IsRoomba(void* actor) { return actor == &test::actor; }
bool IsActive(void*) { return test::roombaActive; }
}
namespace ue_wrap::reflection {
const FName& NameOf(void* obj) {
    static FName graph{1,0}, cls{2,0};
    return obj == &test::graph ? graph : cls;
}
bool NameEquals(const FName& name, const wchar_t* expected) {
    return (name.ComparisonIndex == 1 && !std::wcscmp(expected, L"ExecuteUbergraph_prop_roomba")) ||
           (name.ComparisonIndex == 2 && !std::wcscmp(expected, L"prop_roomba_C"));
}
}
namespace ue_wrap::object_index { void* ClassByName(const wchar_t*) { return &test::primitiveClass; } }
namespace ue_wrap::facts {
Function::Function(detail::MacroToken, const char* id, const char* feat, const wchar_t* cls,
                   const wchar_t* fn, Mode mode) : ident_(id), feature_(feat), cls_(cls), fn_(fn), mode_(mode) {}
Resolved ResolveOn(const Function&, void*) { return {}; }
}
namespace ue_wrap::script_gate { bool WatchName(const facts::Function&, int, PreFn, PostFn) { return true; } }
namespace ue_wrap::ufunction_hook {
bool InstallPreHook(const facts::Resolved&, PreNativeCallback cb, bool armed) {
    test::torque = armed ? cb : nullptr;
    return true;
}
CallerFrame CurrentCallerFrame() { return { &test::graph, nullptr, -1 }; }
}
namespace coop::net_pump { bool IsInAnnouncedWorld(void*) { return true; } }

int main() {
    namespace S = coop::roomba_sync;
    namespace H = coop::prop_drive_host;
    namespace D = coop::prop_drive_stream;
    namespace AD = coop::active_drive;
    coop::net::Session session;
    test::worldEnabled = true;
    test::outerToken = &test::roombaClass;
    test::x = 0;
    ue_wrap::script_gate::Call call{}; call.object = &test::actor;
    S::Install(&session); S::AfterTick(call); H::Tick(session);
    assert(H::g_driven.size() == 1 && session.published.size() == 1);

    // Deliver the real host pose to the real receiver, which parks the copy.
    session.mode = coop::net::Role::Client;
    session.incoming.push_back(session.published.back());
    D::TickApplyAndDrive(session);
    assert(D::IsParked(&test::actor) && !test::simulates);
    S::Install(&session);
    assert(test::torque != nullptr);
    assert(test::torque(nullptr, &test::actor, nullptr) == ue_wrap::script_gate::Verdict::Cancel);

    // The second real host sample reaches the receiver's interpolation buffer.
    session.mode = coop::net::Role::Host; test::x = 80;
    S::AfterTick(call); H::Tick(session);
    assert(session.published.back().x == 80);
    session.mode = coop::net::Role::Client; test::x = 0;
    session.incoming.push_back(session.published.back()); D::TickApplyAndDrive(session);
    auto& d = D::g_drives.at(1).d;
    assert(d.lastLoc.X == 80 && d.haveTwoSnaps);
    auto now = AD::NowMs(); d.prevPoseMs = now - 200; d.lastPoseMs = now - 100;
    D::TickApplyAndDrive(session); assert(test::x == 80);

    // Turning it off stops refreshing the host row; its rest edge unparks the receiver.
    session.mode = coop::net::Role::Host; test::roombaActive = false;
    S::AfterTick(call); H::g_driven[0].lastMoveMs = AD::NowMs() - 2000; H::Tick(session);
    assert(H::g_driven.empty() && !session.ends.empty());
    session.mode = coop::net::Role::Client; D::OnEnd(session.ends.back());
    assert(!D::IsParked(&test::actor) && test::simulates);
    assert(test::torque(nullptr, &test::actor, nullptr) == ue_wrap::script_gate::Verdict::Run);

    // A newly grabbed Roomba cannot open a conflicting host drive.
    session.mode = coop::net::Role::Host; test::roombaActive = true;
    test::grab.grabbingActor = &test::actor; S::AfterTick(call);
    assert(H::g_driven.empty());
    S::OnDisconnect(); D::OnDisconnect(); H::OnDisconnect();
    std::puts("PASS Roomba feeder -> host pose -> client park/interpolate -> rest end (engine/transport mocked)");
}
