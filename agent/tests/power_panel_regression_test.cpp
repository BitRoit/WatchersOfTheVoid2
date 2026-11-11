// The runner extracts these function bodies and the restore state from the current production source.
// Engine, object-array, and wire surfaces here are fakes; no Unreal process is running.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
#define UE_LOGI(...) ((void)0)
#define UE_LOGW(...) ((void)0)

struct Switcher { int index = 0; };
struct Widget { Switcher* switcher = nullptr; int32_t index = 0; };
static std::unordered_map<void*, int> slots;

namespace ue_wrap {
class CachedObjRef {
    void* ptr = nullptr;
    int serial = 0;
public:
    void Set(void* p) { ptr = p; serial = p && slots.count(p) ? slots[p] : 0; }
    void Reset() { ptr = nullptr; serial = 0; }
    bool Is(void* p) const { return p && p == ptr && slots.count(p) && slots[p] == serial; }
    void* Get() const { return Is(ptr) ? ptr : nullptr; }
};
namespace reflection {
struct InstanceOffset { explicit InstanceOffset(const wchar_t*) {} int32_t Of(void*) { return offsetof(Widget, switcher); } };
bool IsLive(void* p) { return p && slots.count(p); }
}
namespace umg {
bool switchWorks = true;
std::function<void()> onReset;
bool SwitcherSetIndex(void* p, int32_t index) {
    if (p && index == 0 && onReset) { auto callback = std::move(onReset); onReset = {}; callback(); }
    if (!p || !switchWorks) return false;
    static_cast<Switcher*>(p)->index = index;
    return true;
}
}
namespace script_gate {
enum class Verdict : uint8_t { Run, Cancel };
struct Call { void* object = nullptr; void* function = nullptr; bool fromOurCode = false; int tag = 0; };
void* activeFunction = nullptr;
bool IsBodyActive(void* fn) { return fn && fn == activeFunction; }
}
namespace upgrades {
bool available = true;
int32_t level = 0;
bool ReadLevel(int index, int32_t* out) { CHECK(index == 26); if (!available) return false; *out = level; return true; }
}
namespace laptop { void* terminal = nullptr; void* TerminalInUse() { return terminal; } }
namespace portable_pc { bool IsPortablePc(void* p) { return p && p == laptop::terminal; } }
}

namespace coop::element {
using ElementId = uint32_t;
constexpr ElementId kInvalidId = 0xFFFFFFFF;
struct Registry {
    static Registry& Get() { static Registry value; return value; }
    ElementId EidForActor(void*) const { return 42; }
};
}
namespace coop::net {
constexpr int kMaxPeers = 4;
constexpr uint32_t kPowerPanelTerminalUnnamed = 0xFFFFFFFE;
constexpr uint8_t kPowerPanelOpPress = 0;
enum class Role { Host, Client };
enum class ReliableKind { PowerControlState };
struct PowerPanelPayload {
    uint8_t op = 0, bits = 0, flags = 0, leverBits = 0, leverSlot = 0;
    uint16_t seq = 0, ack[kMaxPeers]{};
    uint32_t terminal = 0;
};
struct Session {
    bool isRunning = true, isConnected = true;
    Role currentRole = Role::Client;
    int sent = 0;
    PowerPanelPayload last{};
    bool running() const { return isRunning; }
    bool connected() const { return isConnected; }
    Role role() const { return currentRole; }
    void SendReliableToSlot(int slot, ReliableKind, const void* data, size_t size) {
        CHECK(slot == 0 && size == sizeof(last));
        std::memcpy(&last, data, size);
        ++sent;
    }
};
}
namespace coop::net_pump { bool IsInAnnouncedWorld(void*) { return true; } }

namespace ue_wrap::power_control {
void* panel = nullptr;
uint8_t state = 0;
bool disabled = false, waterlogged = false, readOk = true;
int sounds = 0, resets = 0, refreshes = 0, applies = 0;
int finishes = 0, waterloggedHints = 0;
enum class BreakerCompFinish { Unavailable, Ran, Faulted };
BreakerCompFinish nativeFinish = BreakerCompFinish::Ran;
bool EnsureResolved() { return true; }
void* Panel() { return panel; }
bool ReadPress(void*, uint8_t& out) { if (!readOk) return false; out = state; return true; }
bool WritePress(void*, uint8_t next) { if (!readOk) return false; state = next; return true; }
bool ReadDisabled(void*, bool& out) { if (!readOk) return false; out = disabled; return true; }
bool WriteDisabled(void*, bool value) { disabled = value; return true; }
bool ReadWaterlogged(void*, bool& out) { if (!readOk) return false; out = waterlogged; return true; }
bool ReadBreakerCompIndex(void* widget, int32_t& out) { out = static_cast<Widget*>(widget)->index; return true; }
#include "breaker_mapping.inc"
#include "breaker_layout.inc"
bool ResetBreakerCompSwitcher(void* widget) {
    ++resets;
    return ue_wrap::umg::SwitcherSetIndex(static_cast<Widget*>(widget)->switcher, 0);
}
bool RefreshBreakerCompVisual(void*) { ++refreshes; return true; }
bool PlayBreakerButtonSound(void*) { ++sounds; return true; }
bool ButtonsVisibility(void*);
bool CanFinishBreakerComp(void*, void*) { return nativeFinish != BreakerCompFinish::Unavailable; }
BreakerCompFinish FinishBreakerComp(void* widget, void* target) {
    ++finishes;
    if (nativeFinish == BreakerCompFinish::Unavailable) return nativeFinish;
    auto* component = static_cast<Widget*>(widget);
    if (ue_wrap::umg::switchWorks) component->switcher->index = 0;
    if (waterlogged) ++waterloggedHints;
    else {
        state ^= BreakerBitForCompIndex(component->index);
        ButtonsVisibility(target);
    }
    return nativeFinish;
}
bool SetServersActive(void*, bool) { return true; }
bool PlayTurnOnCue(void*) { return true; }
bool PlayLeverSound(void*, bool) { return true; }
}

namespace coop::power_panel {
namespace EL = coop::element;
namespace PC = ue_wrap::power_control;
namespace sg = ue_wrap::script_gate;
using coop::net::PowerPanelPayload;
constexpr uint8_t kMaskBits = 0x1F, kCalcBit = 0x08, kFlagPage = 1, kFlagDisabled = 1, kNoSlot = 0xFF;
std::atomic<coop::net::Session*> g_session{nullptr};
ue_wrap::CachedObjRef g_leverFn, g_pageFn;
bool g_pagePressActive = false;
EL::ElementId g_pageTerminal = EL::kInvalidId;
#include "breaker_scope.inc"
uint8_t g_leverSnap = 0, g_pageSnap = 0;
struct Pending { uint16_t seq; uint8_t bits; };
std::vector<Pending> g_pending;
uint16_t g_seq = 0;
PowerPanelPayload g_canonical{}, g_parked{};
bool g_haveCanonical = false, g_haveParked = false, g_haveSent = false, g_sentDisabled = false, g_ackDirty = false;
uint8_t g_sentMask = 0, g_leverBits = 0, g_leverSlot = 0, g_takingFor = 0;
uint64_t g_canonicalsApplied = 0, g_pressesSent = 0, g_refusedLockouts = 0, g_refusedBlackouts = 0;
uint64_t now = 100;
int broadcasts = 0;
uint64_t NowMs() { return now; }
uint8_t LocalSlot() { return 1; }
bool RedApply() { return false; }
void HostBroadcast(coop::net::Session*, void*) { ++broadcasts; }
#include "breaker_helpers.inc"
#include "breaker_restores.inc"
#include "breaker_clients.inc"
#include "breaker_watches.inc"
}

bool ue_wrap::power_control::ButtonsVisibility(void* object) {
    ++applies;
    return coop::power_panel::OnSeamPre({object}) == ue_wrap::script_gate::Verdict::Run;
}

namespace test {
namespace P = coop::power_panel;
namespace PC = ue_wrap::power_control;
namespace sg = ue_wrap::script_gate;
coop::net::Session session;
int panelA, panelB, pageFunction, portable;
Switcher switches[3];
Widget widgets[3];

void Reset() {
    slots.clear();
    for (void* p : {static_cast<void*>(&panelA), static_cast<void*>(&panelB), static_cast<void*>(&pageFunction),
                    static_cast<void*>(&portable)}) slots[p] = 1;
    for (int i = 0; i < 3; ++i) {
        switches[i] = {};
        widgets[i] = {&switches[i], i == 2 ? 1 : 0};
        slots[&switches[i]] = slots[&widgets[i]] = 1;
    }
    for (auto& r : P::g_restores) r = {};
    P::g_breakerCleanup.clear();
    P::g_breakerCleanupInFlight.clear();
    session = {};
    P::g_session = &session;
    P::g_pending.clear(); P::g_seq = 0; P::g_pagePressActive = false; P::g_pageTerminal = coop::element::kInvalidId;
    P::g_pageFn.Reset(); P::g_leverFn.Reset(); P::g_pageSnap = P::g_leverSnap = 0;
    P::g_haveCanonical = P::g_haveParked = P::g_haveSent = P::g_ackDirty = false;
    P::g_canonicalsApplied = P::g_pressesSent = P::g_refusedLockouts = P::g_refusedBlackouts = 0;
    P::now = 100; P::broadcasts = 0;
    PC::panel = &panelA; PC::state = 0; PC::disabled = PC::waterlogged = false; PC::readOk = true;
    PC::sounds = PC::resets = PC::refreshes = PC::applies = 0;
    PC::finishes = PC::waterloggedHints = 0; PC::nativeFinish = PC::BreakerCompFinish::Ran;
    ue_wrap::upgrades::available = true; ue_wrap::upgrades::level = 0;
    ue_wrap::umg::switchWorks = true; ue_wrap::umg::onReset = {};
    ue_wrap::laptop::terminal = nullptr; sg::activeFunction = nullptr;
}
sg::Call Click(int i = 0) { return {&widgets[i]}; }
bool Close(float a, float b) { return std::fabs(a - b) < 0.0001f; }

void DurationAndMapping() {
    Reset();
    CHECK(PC::BreakerBitForCompIndex(0) == 8 && PC::BreakerBitForCompIndex(1) == 2);
    CHECK(PC::BreakerBitForCompIndex(2) == 1 && PC::BreakerBitForCompIndex(3) == 4);
    CHECK(PC::BreakerBitForCompIndex(4) == 16 && PC::BreakerBitForCompIndex(5) == 0);
    CHECK(Close(P::ComputeBreakerDuration(0, 0), 10) && Close(P::ComputeBreakerDuration(0, 1), 25));
    CHECK(Close(P::ComputeBreakerDuration(16, 0), 1) && Close(P::ComputeBreakerDuration(16, 1), 1.5f));
    CHECK(Close(P::ComputeBreakerDuration(0, .5f), 10.f + 15.f / 64.f));
    CHECK(Close(P::ComputeBreakerDuration(8, .5f), 5.5f + 7.75f / 16.f));
    CHECK(Close(P::ComputeBreakerDuration(16, .5f), 1.125f));
}
void NativeContinuationLayout() {
    std::array<uint8_t, 1814> script{};
    script[0] = 0x4c; script[5] = 0x4e;
    script[182] = 0x19; script[1769] = 0x5b; script[1795] = 0x4d;
    script[1811] = 0x04; script[1812] = 0x0b; script[1813] = 0x53;
    const int32_t exit = 1811;
    std::memcpy(script.data() + 1, &exit, sizeof(exit));
    script[1770] = 182;
    CHECK(PC::BreakerFinishLayoutMatches(script.data(), static_cast<int32_t>(script.size())));
    script[1770] = 183;
    CHECK(!PC::BreakerFinishLayoutMatches(script.data(), static_cast<int32_t>(script.size())));
    script[1770] = 182; script[5] = 0x06;
    CHECK(!PC::BreakerFinishLayoutMatches(script.data(), static_cast<int32_t>(script.size())));
    CHECK(!PC::BreakerFinishLayoutMatches(script.data(), 182));
}
void OfflineAndFallback() {
    Reset(); session.isRunning = false; switches[0].index = 1;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run);
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run && switches[0].index == 1);
    CHECK(!P::g_restores[0].active);
    session.isRunning = true; session.isConnected = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run);
    session.isConnected = true; ue_wrap::upgrades::available = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run && !P::g_restores[0].active);
    sg::Call page{&widgets[0], &pageFunction};
    CHECK(P::OnPagePre(page) == sg::Verdict::Run);
    sg::activeFunction = &pageFunction; PC::state = 8;
    CHECK(P::OnSeamPre({&panelA}) == sg::Verdict::Run);
    CHECK(session.sent == 1 && session.last.bits == 8 && session.last.flags == P::kFlagPage);
    Reset(); ue_wrap::umg::switchWorks = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run && !P::g_restores[0].active);
    Reset(); PC::nativeFinish = PC::BreakerCompFinish::Unavailable;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run && !P::g_restores[0].active);
    Reset(); session.currentRole = coop::net::Role::Host;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    session.isConnected = false; P::TickBreakerRestores();
    CHECK(!P::g_restores[0].active && switches[0].index == 0 && PC::state == 0);
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    session.isConnected = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run);
    CHECK(!P::g_restores[0].active && switches[0].index == 0);
}
void ClientHostRace() {
    Reset();
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    P::g_restores[0].expireMs = P::now;
    coop::net::PowerPanelPayload p{}; p.bits = 8;
    P::ClientApplyModel(&panelA, p, true);
    CHECK(PC::state == 8 && !P::g_restores[0].active && switches[0].index == 0);
    P::TickBreakerRestores();
    CHECK(PC::state == 8 && session.sent == 0);
}
void HostRemotePageRace() {
    Reset(); session.currentRole = coop::net::Role::Host;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    P::g_restores[0].expireMs = P::now; PC::state = 8;
    CHECK(P::OnSeamPre({&panelA}) == sg::Verdict::Run);
    CHECK(!P::g_restores[0].active && P::broadcasts == 1);
    P::TickBreakerRestores(); CHECK(PC::state == 8);
}
void AckAndOtherBit() {
    Reset(); PC::state = 8; P::g_pending.push_back({1, 8});
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    coop::net::PowerPanelPayload p{}; p.bits = 8; p.ack[1] = 1;
    P::ClientTakeCanonical(p);
    CHECK(P::g_pending.empty() && P::g_restores[0].active && PC::state == 8);
    p.bits = 10; P::ClientTakeCanonical(p);
    CHECK(P::g_restores[0].active && PC::state == 10);
    P::g_restores[0].expireMs = P::now;
    P::TickBreakerRestores();
    CHECK(PC::state == 2 && session.sent == 1 && session.last.bits == 8);
}
void WidgetsAndIdentity() {
    Reset();
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    const uint64_t expire = P::g_restores[0].expireMs;
    CHECK(P::OnBreakerButtonClickPre(Click(1)) == sg::Verdict::Cancel);
    CHECK(PC::sounds == 1 && P::g_restores[0].expireMs == expire);
    CHECK(switches[0].index == 1 && switches[1].index == 1);
    P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(switches[0].index == 0 && switches[1].index == 0 && PC::state == 8);
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    PC::panel = &panelB; P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(!P::g_restores[0].active && PC::state == 0 && session.sent == 0 && PC::resets == 0);
}
void ManagedWidgetCleanup() {
    // An actual native fallback can be spinning while no managed restore exists.
    Reset(); ue_wrap::upgrades::available = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run);
    switches[0].index = 1;
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run && switches[0].index == 1);

    // A widget from a replaced panel is not touched until it is freshly entered by upd.
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    PC::panel = &panelB; P::TickBreakerRestores();
    CHECK(!P::g_restores[0].active && P::g_breakerCleanup.size() == 1 && switches[0].index == 1);
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run);
    CHECK(P::g_breakerCleanup.empty() && switches[0].index == 0 && PC::state == 0);

    // The widget can also be cleaned from a fresh update after the last peer leaves.
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    PC::panel = &panelB; session.isConnected = false; P::TickBreakerRestores();
    CHECK(P::g_breakerCleanup.size() == 1 && switches[0].index == 1);
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run);
    CHECK(P::g_breakerCleanup.empty() && switches[0].index == 0);

    // A failed switcher reset is retried without repeating the breaker press.
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    ue_wrap::umg::switchWorks = false; P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(PC::state == 8 && session.sent == 1 && switches[0].index == 1 && P::g_breakerCleanup.size() == 1);
    ue_wrap::umg::switchWorks = true; P::TickBreakerRestores();
    CHECK(P::g_breakerCleanup.empty() && switches[0].index == 0 && PC::state == 8 && session.sent == 1);

    // A native click claims the widget after a failed cleanup, so later updates must leave it alone.
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    ue_wrap::umg::switchWorks = false; P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    ue_wrap::upgrades::available = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run && P::g_breakerCleanup.empty());
    ue_wrap::umg::switchWorks = true; switches[0].index = 1;
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run && switches[0].index == 1);

    // The same handoff is safe when the last peer leaves before the old panel is retired.
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    PC::panel = &panelB; session.isConnected = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run && P::g_breakerCleanup.empty());
    session.isConnected = true; switches[0].index = 1;
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run && switches[0].index == 1);

    // The panel can also change while connected, before a native fallback is selected.
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    PC::panel = &panelB; ue_wrap::upgrades::available = false;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run && P::g_breakerCleanup.empty());
    switches[0].index = 1;
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run && switches[0].index == 1);

    // Re-entrant native ownership during a failed reset must not recreate stale cleanup debt.
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    ue_wrap::umg::switchWorks = false;
    ue_wrap::umg::onReset = [] {
        ue_wrap::upgrades::available = false;
        CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Run);
        switches[0].index = 1;
    };
    P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(P::g_breakerCleanup.empty() && P::g_breakerCleanupInFlight.empty());
    ue_wrap::umg::switchWorks = true; P::TickBreakerRestores();
    CHECK(P::OnBreakerUpdPre(Click()) == sg::Verdict::Run && switches[0].index == 1);
}
void CancellationAndTerminal() {
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    PC::waterlogged = true; P::TickBreakerRestores();
    CHECK(P::g_restores[0].active && switches[0].index == 1 && PC::waterloggedHints == 0);
    P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(!P::g_restores[0].active && switches[0].index == 0 && PC::state == 0);
    CHECK(PC::finishes == 1 && PC::waterloggedHints == 1 && session.sent == 0);
    Reset(); PC::waterlogged = true;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel && switches[0].index == 1);
    PC::waterlogged = false;
    P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(PC::state == 8 && PC::finishes == 1 && session.sent == 1);
    Reset(); PC::disabled = true;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(PC::state == 8 && PC::finishes == 1 && PC::disabled);
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    PC::nativeFinish = PC::BreakerCompFinish::Faulted;
    P::g_restores[0].expireMs = P::now; P::TickBreakerRestores(); P::TickBreakerRestores();
    CHECK(PC::state == 8 && PC::finishes == 1 && session.sent == 1);
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    slots.erase(&widgets[0]);
    P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(PC::state == 8 && PC::finishes == 0 && session.sent == 1);
    Reset(); CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    CHECK(P::OnOwnEdgePre({&panelA, nullptr, false, 0x5050534C}) == sg::Verdict::Cancel);
    CHECK(P::g_restores[0].active);
    session.currentRole = coop::net::Role::Host;
    CHECK(P::OnOwnEdgePre({&panelA, nullptr, false, 0x5050534C}) == sg::Verdict::Run);
    CHECK(!P::g_restores[0].active);
    Reset(); ue_wrap::laptop::terminal = &portable;
    CHECK(P::OnBreakerButtonClickPre(Click()) == sg::Verdict::Cancel);
    ue_wrap::laptop::terminal = nullptr; P::g_restores[0].expireMs = P::now; P::TickBreakerRestores();
    CHECK(session.sent == 1 && session.last.terminal == 42 && session.last.bits == 8);
}
}

int main() {
    test::DurationAndMapping(); test::NativeContinuationLayout(); test::OfflineAndFallback(); test::ClientHostRace();
    test::HostRemotePageRace(); test::AckAndOtherBit(); test::WidgetsAndIdentity();
    test::ManagedWidgetCleanup(); test::CancellationAndTerminal();
    std::puts("PASS breaker production-body fixture: timing, fallback, races, acknowledgement, widget cleanup and native ownership, lifetime, cancellation, terminal");
}
