#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "roach_source.inc" // byte-for-byte production source copied by the runner

namespace test {
using namespace coop::roach_sync;
struct Obj { int idx, serial = 1; bool live = true; void* world; };
struct Comp { Vec3 loc; float scale; int writes = 0; bool failRead = false; FRotator3 rotation{}; int rotationWrites = 0; };
struct MasterData { FArrayRaw arr{}; std::vector<void*> slots; };
struct GameMode { void* master = nullptr; };
std::unordered_map<void*, Obj> objects;
std::vector<void*> indexes;
std::vector<std::unique_ptr<Comp>> comps;
MasterData master, secondMaster;
GameMode gm;
int world1, world2;
void* currentWorld = &world1;
bool failAdd = false, failDelete = false;
float addDistanceLimit = 1000000.f;
int adds = 0, deletes = 0, addAttempts = 0;

void Register(void* p) { objects.insert_or_assign(p, Obj{static_cast<int>(indexes.size()), 1, true, currentWorld}); indexes.push_back(p); }
void Refresh(MasterData& m) { m.arr = {m.slots.empty() ? nullptr : m.slots.data(), static_cast<int32_t>(m.slots.size()), static_cast<int32_t>(m.slots.size())}; }
Comp* NewComp(MasterData& m, Vec3 p, float scale = 1) {
    comps.emplace_back(std::make_unique<Comp>(Comp{p, scale}));
    auto* c = comps.back().get(); Register(c); m.slots.push_back(c); Refresh(m); return c;
}
void Reset() {
    coop::roach_sync::OnDisconnect();
    objects.clear(); indexes.clear(); comps.clear(); master = {}; secondMaster = {}; gm = {};
    currentWorld = &world1; failAdd = failDelete = false; ue_wrap::mockFailNewRotation = false;
    addDistanceLimit = 1000000.f; adds = deletes = addAttempts = 0;
    Register(&gm); Register(&master); gm.master = &master;
    g_offMaster = static_cast<int32_t>(offsetof(GameMode, master));
    g_offRoaches = static_cast<int32_t>(offsetof(MasterData, arr));
    g_addRoachFn = reinterpret_cast<void*>(1); g_delRoachFn = reinterpret_cast<void*>(2);
    g_getLocFn = reinterpret_cast<void*>(3); g_getScaleFn = reinterpret_cast<void*>(4);
    g_setLocFn = reinterpret_cast<void*>(5); g_setScaleFn = reinterpret_cast<void*>(6);
    g_setLocRotFn = reinterpret_cast<void*>(7); g_getRotFn = reinterpret_cast<void*>(8);
    g_resolveLatched = true;
}
void Fail(const char* msg, int line) { std::cerr << "FAIL line " << line << ": " << msg << '\n'; std::exit(1); }
#define CHECK(x) do { if (!(x)) test::Fail(#x, __LINE__); } while (0)
bool Near(float a, float b) { return std::fabs(a-b) < 0.01f; }
SnapEntry Entry(float x) { return {{x, 0, 0}, 1}; }
}

namespace ue_wrap::reflection {
void* FindClass(const wchar_t*) { return nullptr; }
void* FindFunction(void*, const wchar_t*) { return nullptr; }
int32_t FindPropertyOffset(void*, const wchar_t*) { return -1; }
bool IsLive(void* p) { auto it = test::objects.find(p); return it != test::objects.end() && it->second.live; }
bool IsLiveByIndex(void* p, int32_t i) { return i >= 0 && i < static_cast<int>(test::indexes.size()) && test::indexes[i] == p && IsLive(p); }
int32_t InternalIndexOf(void* p) { auto it = test::objects.find(p); return it == test::objects.end() ? -1 : it->second.idx; }
int32_t AllocateSlotSerial(int32_t i) { return i >= 0 && i < static_cast<int>(test::indexes.size()) ? test::objects[test::indexes[i]].serial : 0; }
int32_t SlotSerial(int32_t i) { return AllocateSlotSerial(i); }
}
namespace ue_wrap::world_identity {
void* WorldOf(void* p) { auto it = test::objects.find(p); return it == test::objects.end() ? nullptr : it->second.world; }
void* CurrentWorld() { return test::currentWorld; }
}
namespace ue_wrap::world_singleton { void* Gamemode() { return &test::gm; } }
namespace ue_wrap {
bool Call(void* target, ParamFrame& f) {
    using namespace test;
    if (!reflection::IsLive(target)) return false;
    const auto id = reinterpret_cast<uintptr_t>(f.fn);
    if (id == 1) {
        if (failAdd) return false;
        Vec3 loc{}, size{}; if (!f.GetRaw(L"Location", &loc, sizeof(loc)) || !f.GetRaw(L"Size", &size, sizeof(size))) return false;
        ++addAttempts;
        if (std::fabs(loc.x) >= addDistanceLimit) return true; // native dispatch succeeded, gate refused
        NewComp(*static_cast<MasterData*>(target), loc, size.x); ++adds; return true;
    }
    if (id == 2) {
        if (failDelete) return false;
        auto* m = static_cast<MasterData*>(target); const int i = f.Get<int32_t>(L"IndexToRemove");
        if (i < 0 || i >= static_cast<int>(m->slots.size()) || !m->slots[i]) return false;
        objects[m->slots[i]].live = false; m->slots[i] = nullptr; ++deletes; Refresh(*m); return true;
    }
    auto* c = static_cast<Comp*>(target);
    if (id == 3) return !c->failRead && f.SetRaw(L"ReturnValue", &c->loc, sizeof(c->loc));
    if (id == 4) { const Vec3 scale{c->scale,c->scale,c->scale}; return f.SetRaw(L"ReturnValue", &scale, sizeof(scale)); }
    if (id == 5) { ++c->writes; return f.GetRaw(L"NewLocation", &c->loc, sizeof(c->loc)); }
    if (id == 6) { Vec3 scale{}; if (!f.GetRaw(L"NewScale", &scale, sizeof(scale))) return false; c->scale=scale.x; return true; }
    if (id == 7) { ++c->writes; ++c->rotationWrites; return f.GetRaw(L"NewLocation", &c->loc, sizeof(c->loc)) && f.GetRaw(L"NewRotation", &c->rotation, sizeof(c->rotation)); }
    if (id == 8) return f.SetRaw(L"ReturnValue", &c->rotation, sizeof(c->rotation));
    return false;
}
}

int main() {
    using namespace test;
    using coop::net::Role;
    static_assert(kInterpCushionMs == 50, "fixture must exercise the production interpolation window");
    const float half = 500.f / (1000.f + kInterpCushionMs);
    coop::net::Session s;

    // Production host tick: movement below the former threshold still produces
    // one sample per one-second poll; a failed read/send never commits baseline.
    Reset(); s.currentRole = Role::Host; coop::roach_sync::Install(&s);
    Comp* host = NewComp(master, {0,0,0});
    g_lastPollMs = NowMs()-1000; coop::roach_sync::Tick();
    for (int i=1; i<=3; ++i) { host->loc.x = 10.f*i; g_lastPollMs=NowMs()-1000; coop::roach_sync::Tick(); }
    CHECK(s.states.size() == 3); CHECK(Near(s.states[0].entries[0].x, 10)); CHECK(Near(s.states[2].entries[0].x, 30));
    g_lastPollMs=NowMs()-1000; coop::roach_sync::Tick(); CHECK(s.states.size()==3);
    host->loc.x=40; host->failRead=true; g_lastPollMs=NowMs()-1000; coop::roach_sync::Tick(); CHECK(s.states.size()==3);
    host->failRead=false; s.refuse=true; g_lastPollMs=NowMs()-1000; coop::roach_sync::Tick(); CHECK(s.states.size()==3);
    s.refuse=false; g_lastPollMs=NowMs()-1000; coop::roach_sync::Tick(); CHECK(s.states.size()==4); CHECK(Near(s.states.back().entries[0].x,40));

    // Real client apply and per-frame advance. A deletion and a later birth
    // preserve the two surviving components and their interpolation state.
    Reset(); s = {}; s.currentRole=Role::Client; coop::roach_sync::Install(&s);
    Comp* a=NewComp(master,{0,0,0}); Comp* b=NewComp(master,{1000,0,0}); Comp* c=NewComp(master,{2000,0,0});
    SnapEntry initial[]{Entry(0),Entry(1000),Entry(2000)};
    CHECK(ApplySnapshot(&s,initial,3,1000));
    SnapEntry moving[]{Entry(10),Entry(1010),Entry(2010)};
    CHECK(ApplySnapshot(&s,moving,3,2000)); AdvanceClientInterpolation(2500);
    CHECK(Near(a->loc.x,10*half)); CHECK(Near(c->loc.x,2000+10*half));
    SnapEntry removed[]{Entry(20),Entry(2020)};
    CHECK(ApplySnapshot(&s,removed,2,3000)); CHECK(!objects[b].live); CHECK(objects[a].live && objects[c].live);
    const float at3000=10.f*1000.f/(1000.f+kInterpCushionMs);
    CHECK(deletes==1 && adds==0); AdvanceClientInterpolation(3500);
    CHECK(Near(a->loc.x,at3000+(20-at3000)*half)); CHECK(Near(c->loc.x,2000+at3000+(20-at3000)*half));
    SnapEntry born[]{Entry(30),Entry(2030),Entry(3000)};
    CHECK(ApplySnapshot(&s,born,3,4000)); CHECK(adds==1 && deletes==1); CHECK(g_interp[0].comp.Is(a)); CHECK(g_interp[1].comp.Is(c));
    CHECK(DetectLocalConsumption(&s,4001)); CHECK(s.intents.empty());

    // Local stomp is captured before a racing snapshot; an unsent intent blocks
    // reconciliation, and a successful one hides its tombstone until host removal.
    objects[c].live=false; master.slots[2]=nullptr; Refresh(master);
    s.refuse=true; CHECK(!ApplySnapshot(&s,born,3,5000)); CHECK(adds==1); CHECK(g_interp[1].consumed && !g_interp[1].intentSentMs);
    s.refuse=false; CHECK(ApplySnapshot(&s,born,3,5100)); CHECK(s.intents.size()==1); CHECK(adds==1);
    SnapEntry confirmed[]{Entry(40),Entry(3010)};
    CHECK(ApplySnapshot(&s,confirmed,2,6000)); CHECK(s.intents.size()==1); CHECK(g_interp.size()==2); CHECK(g_interp[0].comp.Is(a));
    CHECK(DetectLocalConsumption(&s,6100)); CHECK(s.intents.size()==1);

    // A refused or unacknowledged intent cannot hide a roach forever.
    void* victim=g_interp[1].comp.Raw(); objects[victim].live=false;
    for (auto& slot : master.slots) if (slot==victim) slot=nullptr; Refresh(master);
    CHECK(ApplySnapshot(&s,confirmed,2,7000)); CHECK(s.intents.size()==2);
    CHECK(ApplySnapshot(&s,confirmed,2,12000)); CHECK(adds==2); CHECK(!g_interp[1].consumed);

    // A recycled slot/address and a different world/master never receive writes.
    confirmed[0]=Entry(50); CHECK(ApplySnapshot(&s,confirmed,2,12100));
    const int writes=a->writes; ++objects[a].serial;
    AdvanceClientInterpolation(12500); CHECK(a->writes==writes);
    currentWorld=&world2; Register(&secondMaster); gm.master=&secondMaster;
    AdvanceClientInterpolation(13000); CHECK(!g_haveApplied); CHECK(a->writes==writes);

    // Native additions append: a newborn close to a survivor's previous pose
    // must not steal that survivor when both are moving in the same sample.
    Reset(); s = {}; coop::roach_sync::Install(&s);
    Comp* nearA=NewComp(master,{0,0,0}); Comp* nearB=NewComp(master,{10,0,0});
    SnapEntry closeInitial[]{Entry(0),Entry(10)};
    CHECK(ApplySnapshot(&s,closeInitial,2,1000));
    SnapEntry closeBirth[]{Entry(5),Entry(15),Entry(10)};
    CHECK(ApplySnapshot(&s,closeBirth,3,2000));
    CHECK(g_interp[0].comp.Is(nearA) && g_interp[1].comp.Is(nearB));
    CHECK(g_interp[2].comp.Alive() && !g_interp[2].comp.Is(nearB));
    AdvanceClientInterpolation(2500); CHECK(Near(nearB->loc.x,10+5*half));

    // A native camera/placement refusal is a silent successful dispatch. It
    // must not churn an admitted component or block it from moving, and the
    // missing component becomes eligible for a later retry.
    Reset(); s = {}; coop::roach_sync::Install(&s); addDistanceLimit=10000.f;
    SnapEntry mixed[]{Entry(0),Entry(20000)};
    CHECK(ApplySnapshot(&s,mixed,2,1000)); CHECK(g_haveApplied && adds==1 && addAttempts==2);
    CHECK(g_interp[0].comp.Alive() && !g_interp[1].comp.Raw());
    void* admitted=g_interp[0].comp.Raw();
    mixed[0]=Entry(10); CHECK(ApplySnapshot(&s,mixed,2,2000));
    CHECK(addAttempts==2 && deletes==0 && g_interp[0].comp.Is(admitted));
    AdvanceClientInterpolation(2500); CHECK(Near(static_cast<Comp*>(admitted)->loc.x,10*half));
    mixed[1]=Entry(9000); CHECK(ApplySnapshot(&s,mixed,2,6000));
    CHECK(adds==2 && g_interp[1].comp.Alive() && deletes==0 && s.intents.empty());

    // At most eight native additions are attempted in one second, including
    // repeated snapshot applications; the cursor reaches remaining entries.
    Reset(); s = {}; coop::roach_sync::Install(&s);
    std::vector<SnapEntry> many(20); for (int i=0; i<20; ++i) many[i]=Entry(static_cast<float>(i));
    CHECK(ApplySnapshot(&s,many.data(),20,1000)); CHECK(addAttempts==8);
    CHECK(ApplySnapshot(&s,many.data(),20,1001)); CHECK(addAttempts==8);
    TrySpawnMissing(&master,2000); CHECK(addAttempts==16);
    TrySpawnMissing(&master,3000); CHECK(addAttempts==20 && adds==20);

    // Host travel, rather than the lagging displayed location, determines
    // heading. The short arc across +/-180 and authored pitch/roll survive.
    Reset(); s = {}; coop::roach_sync::Install(&s);
    Comp* turn=NewComp(master,{0,0,0}); turn->rotation={12.f,179.f,17.f};
    SnapEntry pose[]{Entry(0)}; CHECK(ApplySnapshot(&s,pose,1,1000));
    CHECK(Near(turn->rotation.Pitch,12) && Near(turn->rotation.Yaw,179) && Near(turn->rotation.Roll,17));
    pose[0].loc={-9.9985f,-0.1745f,0.f}; CHECK(ApplySnapshot(&s,pose,1,2000));
    CHECK(Near(g_interp[0].errorYaw,2.f)); AdvanceClientInterpolation(2500);
    CHECK(Near(turn->rotation.Yaw,179.f+2.f*half));
    CHECK(Near(turn->rotation.Pitch,12) && Near(turn->rotation.Roll,17));

    // A new sample can reverse while the displayed roach is still moving
    // forward; its heading must follow the host's sample-to-sample delta.
    Reset(); s = {}; coop::roach_sync::Install(&s);
    turn=NewComp(master,{0,0,0}); pose[0]=Entry(0); CHECK(ApplySnapshot(&s,pose,1,1000));
    pose[0]=Entry(100); CHECK(ApplySnapshot(&s,pose,1,2000)); AdvanceClientInterpolation(2500);
    pose[0]=Entry(97); CHECK(ApplySnapshot(&s,pose,1,3000));
    CHECK(Near(std::fabs(g_interp[0].targetYaw),180));
    AdvanceClientInterpolation(3500); CHECK(std::fabs(turn->rotation.Yaw)>80);
    // A large correction is not interpreted as a travel heading.
    const float priorYaw=turn->rotation.Yaw; pose[0]=Entry(2000); CHECK(ApplySnapshot(&s,pose,1,3500));
    CHECK(Near(turn->rotation.Yaw,priorYaw));

    // Missing rotation parameters must fall back to the safe location-only call.
    pose[0]=Entry(2010); CHECK(ApplySnapshot(&s,pose,1,4000));
    const int priorRotations=turn->rotationWrites; ue_wrap::mockFailNewRotation=true;
    AdvanceClientInterpolation(4500); CHECK(turn->rotationWrites==priorRotations && turn->loc.x>2000);
    ue_wrap::mockFailNewRotation=false;

    // A transient missing master must retain tracked state without sending a
    // false consumption; a real master/world replacement was checked above.
    gm.master=nullptr; AdvanceClientInterpolation(4600); CHECK(g_haveApplied);
    CHECK(!DetectLocalConsumption(&s,4600)); CHECK(g_haveApplied && s.intents.empty());
    gm.master=&master; AdvanceClientInterpolation(4700); CHECK(g_haveApplied);

    // The same 1050 ms production window is still open at 1020 ms, but a
    // longer packet gap is bounded and eventually reaches the target.
    Reset(); s = {}; coop::roach_sync::Install(&s);
    turn=NewComp(master,{0,0,0}); pose[0]=Entry(0); CHECK(ApplySnapshot(&s,pose,1,1000));
    pose[0]=Entry(10); CHECK(ApplySnapshot(&s,pose,1,2000)); AdvanceClientInterpolation(3020);
    CHECK(g_interp[0].window.IsOpen() && turn->loc.x<10 && turn->loc.x>9);
    AdvanceClientInterpolation(3100); CHECK(!g_interp[0].window.IsOpen() && Near(turn->loc.x,10));

    // Reject short, malformed or non-finite pages before assembly.
    Reset(); s = {}; coop::roach_sync::Install(&s);
    coop::net::RoachStatePayload p{}; p.seq=1; p.pageCount=1; p.totalCount=1; p.entryCount=0;
    coop::roach_sync::OnState(p); CHECK(!g_asmInitialized);
    p.entryCount=1; p.entries[0]={NAN,0,0,1}; coop::roach_sync::OnState(p); CHECK(!g_asmInitialized);
    p.entries[0]={42,0,0,1}; coop::roach_sync::OnState(p); CHECK(g_haveApplied && g_interp.size()==1);
    p.seq=0; p.entries[0].x=99; coop::roach_sync::OnState(p); CHECK(Near(g_interp[0].hostPos.x,42));
    std::cout << "PASS production roach cadence, interpolation, population, consumption, lifecycle and page validation\n";
}
