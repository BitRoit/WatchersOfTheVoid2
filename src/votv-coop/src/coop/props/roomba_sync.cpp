#include "coop/props/roomba_sync.h"

#include "coop/net/session.h"
#include "coop/player/local_prop_hold.h"
#include "coop/props/prop_drive_host.h"
#include "coop/props/prop_drive_stream.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/ufunction_hook.h"
#include "ue_wrap/devices/roomba.h"
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/facts/function_row.h"

#include <chrono>
#include <cstdint>

namespace coop::roomba_sync {
namespace {
namespace R = ue_wrap::reflection;
namespace SG = ue_wrap::script_gate;
namespace H = ue_wrap::ufunction_hook;
namespace W = ue_wrap::world_identity;
UE_FACT_FN(kRowTick, "roomba_sync", L"prop_roomba_C", L"ReceiveTick", Declared);
UE_FACT_FN(kRowTorque, "roomba_sync", L"PrimitiveComponent", L"AddTorqueInDegrees", Declared);
constexpr int kTickTag = 0x524d4253;
net::Session* g_session = nullptr;
bool g_tickWatch = false;
bool g_torqueHook = false;
std::chrono::steady_clock::time_point g_nextTorqueAttempt{};

bool Current() {
    return g_session && g_session->running() && g_session->connected();
}

bool WorldRoomba(void* actor) {
    void* const world = W::CurrentWorld();
    return actor && world && W::WorldOf(actor) == world && ue_wrap::roomba::IsRoomba(actor);
}

void AfterTick(const SG::Call& c) {
    if (!Current() || g_session->role() != net::Role::Host || c.fromOurCode || !c.object ||
        !ue_wrap::roomba::EnsureResolved() || !WorldRoomba(c.object) ||
        !ue_wrap::roomba::IsActive(c.object) || local_prop_hold::IsHeld(c.object) ||
        prop_drive_host::InAHand(c.object)) return;
    // A rolling Roomba refreshes the ordinary coasting stream. When it turns off,
    // refresh stops and the stream closes after rest; another subsystem's claimed
    // drive is never released here. The stream already yields to hands.
    prop_drive_host::Coast(c.object, "roomba");
}

SG::Verdict BeforeTorque(void*, void* source, const uint8_t*) {
    if (!Current() || g_session->role() != net::Role::Client || !source) return SG::Verdict::Run;
    const auto caller = H::CurrentCallerFrame();
    if (!caller.function || !R::NameEquals(R::NameOf(caller.function), L"ExecuteUbergraph_prop_roomba"))
        return SG::Verdict::Run;
    void* const owner = R::OuterOf(caller.function);
    if (!owner || !R::NameEquals(R::NameOf(owner), L"prop_roomba_C")) return SG::Verdict::Run;
    if (!WorldRoomba(source) || !net_pump::IsInAnnouncedWorld(source) ||
        !prop_drive_stream::IsParked(source)) return SG::Verdict::Run;
    // Only this Blueprint's own wheel torques are suppressed, and only while a
    // host pose actually owns this copy. Other physics and unstreamed Roombas run.
    return SG::Verdict::Cancel;
}
}  // namespace

void Install(net::Session* session) {
    g_session = session;
    if (!g_tickWatch)
        g_tickWatch = SG::WatchName(kRowTick, kTickTag, nullptr, &AfterTick);
    if (g_torqueHook || !session || session->role() != net::Role::Client) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextTorqueAttempt) return;
    g_nextTorqueAttempt = now + std::chrono::seconds(1);
    void* const cls = ue_wrap::object_index::ClassByName(L"PrimitiveComponent");
    if (!cls) return;
    g_torqueHook = H::InstallPreHook(ue_wrap::facts::ResolveOn(kRowTorque, cls), &BeforeTorque, true);
    if (g_torqueHook) UE_LOGI("roomba_sync: client wheel torque hook installed");
}

void OnDisconnect() { g_session = nullptr; }
}  // namespace coop::roomba_sync
