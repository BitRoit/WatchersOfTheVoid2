// coop/world/power_panel.cpp -- see coop/world/power_panel.h.

#include "coop/world/power_panel.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/element/intent_authority.h"
#include "coop/element/registry.h"
#include "coop/net/intent_bucket.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/player/roster_ledger.h"
#include "coop/session/net_pump.h"  // IsInAnnouncedWorld: a client's own world load runs natively

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/devices/laptop.h"
#include "ue_wrap/devices/portable_pc.h"
#include "ue_wrap/devices/power_control.h"
#include "ue_wrap/engine/umg_build.h"
#include "ue_wrap/facts/function_row.h"
#include "ue_wrap/world/upgrades.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#include <random>
#include <utility>
#include <vector>

namespace coop::power_panel {
namespace {

namespace EL = coop::element;
namespace PC = ue_wrap::power_control;
namespace sg = ue_wrap::script_gate;
using coop::net::PowerPanelPayload;

static_assert(coop::net::kMaxPeers <= 32, "PowerPanelPayload::ack holds all slots");
static_assert(coop::net::kMaxPeers <= 32, "the owed canonicals are one bit a slot");

constexpr uint8_t kMaskBits = 0x1F;
constexpr uint8_t kCalcBit = 0x08;       // press_calc: the virus lockout's end switches the servers with it
constexpr uint8_t kFlagPage = 0x01;      // a press: the laptop's breaker page pressed it
constexpr uint8_t kFlagDisabled = 0x01;  // the canonical: the panel is disabled
constexpr uint8_t kNoSlot = 0xFF;
// A lever is pressed within the look-at trace and the breaker page at its terminal: twice the default armLength,
// the door and keypad lanes' reach, which coop/element/intent_authority pads with the target's bounds and the
// puppet's lag.
constexpr float kPressReachUU = 400.0f;
// A slot's presses wait, in arrival order, while the panel has not resolved or their presser has no body here
// yet (a joiner's first pose follows its world-ready by seconds, 3 s measured on the LAN rig). Each is taken as
// refused once it has waited this long, and answered while the host's panel can say its state: unlike a door verb,
// which waits for its body without a bound, a press is a prediction its presser already shows.
constexpr uint64_t kWaitMs = 10000;
constexpr size_t   kMaxQueued = 32;
// A player flips a lever or clicks the page a few times a second at most, and each press runs the panel's whole
// apply; a sender's presses run at this rate, the rest waiting their turn (door_verb_intent's bound).
constexpr coop::net::IntentBudget kPressBudget{4.0f, 4.0f};

std::atomic<coop::net::Session*> g_session{nullptr};

// The press bodies' functions, recorded as each begins, for IsBodyActive; held by slot and serial, since a world
// can load the class anew and put another function at a recorded address.
ue_wrap::CachedObjRef g_leverFn;
ue_wrap::CachedObjRef g_pageFn;
// Scoped flag for local page clicks driving buttonsVisibility
bool g_pagePressActive = false;
EL::ElementId g_pageTerminal = EL::kInvalidId;
struct ScopedPagePress {
    explicit ScopedPagePress(EL::ElementId terminal)
        : previousActive(g_pagePressActive), previousTerminal(g_pageTerminal) {
        g_pagePressActive = true;
        g_pageTerminal = terminal;
    }
    ~ScopedPagePress() { g_pagePressActive = previousActive; g_pageTerminal = previousTerminal; }
    ScopedPagePress(const ScopedPagePress&) = delete;
    ScopedPagePress& operator=(const ScopedPagePress&) = delete;
    bool previousActive;
    EL::ElementId previousTerminal;
};
// What the panel's breakers were as each press body began, so the seam can tell what the press flipped.
uint8_t g_leverSnap = 0;
uint8_t g_pageSnap = 0;

// CLIENT: my presses the host has not taken, oldest first; the last canonical, its acknowledgements already
// taken; one that came before the panel.
struct Pending { uint16_t seq; uint8_t bits; };
std::vector<Pending> g_pending;
uint16_t g_seq = 0;
PowerPanelPayload g_canonical{};
bool g_haveCanonical = false;
PowerPanelPayload g_parked{};
bool g_haveParked = false;
uint64_t g_refusedBlackouts = 0, g_refusedLockouts = 0, g_pressesSent = 0, g_canonicalsApplied = 0;

// HOST: the last press seq taken from each slot, what the last broadcast said, the presses that wait, each
// sender's rate, and the joiners owed their canonical.
coop::roster_ledger::PerSlotState<uint16_t> g_ack;
bool    g_ackDirty = false;
uint8_t g_sentMask = 0;
bool    g_sentDisabled = false;
bool    g_haveSent = false;
uint8_t g_leverBits = 0;          // the next canonical's lever clicks
uint8_t g_leverSlot = kNoSlot;    // and whose lever made them
uint8_t g_takingFor = 0;          // the slot whose press the host's lever is running, 0 for its own
struct Waiting { PowerPanelPayload p; uint64_t arrivedMs; };
std::deque<Waiting> g_waiting[coop::net::kMaxPeers];
coop::net::IntentBucket g_rate[coop::net::kMaxPeers];
uint32_t g_owed = 0;
uint64_t g_pressesTaken = 0, g_pressesRefused = 0;

// [dev] grid_drill=red: a client applies the canonical raw, as the old mirror did, with no apply behind it.
bool RedApply() {
    static const bool red = coop::config::ResolveString(::coop::config_registry::rows::grid_drill) == "red";
    return red;
}

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// `a` is later than `b` in a 16-bit sequence that wraps.
bool SeqAfter(uint16_t a, uint16_t b) { return static_cast<int16_t>(static_cast<uint16_t>(a - b)) > 0; }

coop::net::Session* Connected() {
    auto* s = g_session.load(std::memory_order_acquire);
    return (s && s->running() && s->connected()) ? s : nullptr;
}

uint8_t LocalSlot() { return coop::players::Registry::Get().LocalPeerId(); }

bool TakeToken(uint8_t slot) { return g_rate[slot].Take(kPressBudget, NowMs()); }

// ---- the host ------------------------------------------------------------------------------------------------

void HostBroadcast(coop::net::Session* s, void* panel, int onlySlot = -1) {
    uint8_t mask = 0;
    bool disabled = false;
    if (!PC::ReadPress(panel, mask) || !PC::ReadDisabled(panel, disabled)) return;
    PowerPanelPayload p{};
    p.op = coop::net::kPowerPanelOpCanonical;
    p.bits = static_cast<uint8_t>(mask & kMaskBits);
    p.flags = disabled ? kFlagDisabled : 0;
    p.leverSlot = kNoSlot;
    p.terminal = EL::kInvalidId;
    for (int i = 0; i < coop::net::kMaxPeers; ++i) p.ack[i] = g_ack[i];
    if (onlySlot >= 0) {
        s->SendReliableToSlot(onlySlot, coop::net::ReliableKind::PowerControlState, &p, sizeof(p));
        return;
    }
    p.leverBits = g_leverBits;
    p.leverSlot = g_leverSlot;
    s->SendReliable(coop::net::ReliableKind::PowerControlState, &p, sizeof(p));
    g_sentMask = p.bits;
    g_sentDisabled = disabled;
    g_haveSent = true;
    g_ackDirty = false;
    g_leverBits = 0;
    g_leverSlot = kNoSlot;
}

// Where a press is judged from: its lever's panel, or for a page press the terminal it was worked through, the
// laptop or a portable PC its presser named by element id.
EL::IntentSubject ReachOf(coop::net::Session* s, const PowerPanelPayload& p, uint8_t sender, void* panel) {
    const EL::IntentTarget target = EL::IntentTarget::ForClientIntent(*s, sender, kPressReachUU);
    if (!(p.flags & kFlagPage)) return target.Authorize(panel);
    if (p.terminal == coop::net::kPowerPanelTerminalUnnamed) return EL::IntentSubject{};
    if (p.terminal == EL::kInvalidId) {
        void* laptop = ue_wrap::laptop::Instance();
        return laptop ? target.Authorize(laptop) : EL::IntentSubject{};
    }
    EL::IntentSubject subject = target.Resolve(p.terminal, EL::ElementType::Prop);
    if (subject && !ue_wrap::portable_pc::IsPortablePc(subject.actor)) subject.outcome = EL::IntentOutcome::WrongType;
    return subject;
}

enum class Take : uint8_t { Done, Wait };

Take HostTakePress(coop::net::Session* s, const Waiting& w, uint8_t sender) {
    const PowerPanelPayload& p = w.p;
    const bool waited = NowMs() - w.arrivedMs >= kWaitMs;
    void* panel = PC::EnsureResolved() ? PC::Panel() : nullptr;
    if (!panel && !waited) return Take::Wait;
    if (!SeqAfter(p.seq, g_ack[sender])) {
        // Already taken: its presser still shows it only if no canonical has answered it yet.
        UE_LOGW("power_panel: host dropped slot %u's press seq %u (last taken %u)", sender, p.seq, g_ack[sender]);
        if (panel) HostBroadcast(s, panel, sender);
        return Take::Done;
    }
    const bool page = (p.flags & kFlagPage) != 0;
    const uint8_t bits = static_cast<uint8_t>(p.bits & kMaskBits);
    const EL::IntentSubject reach = panel ? ReachOf(s, p, sender, panel) : EL::IntentSubject{};
    if (reach.outcome == EL::IntentOutcome::NoBody && !waited) return Take::Wait;
    g_ack[sender] = p.seq;
    g_ackDirty = true;
    if (!panel) {
        UE_LOGW("power_panel: host has no panel -- slot %u's press seq %u taken and dropped", sender, p.seq);
        return Take::Done;
    }
    bool waterlogged = false;
    PC::ReadWaterlogged(panel, waterlogged);
    const bool unnamed = page && p.terminal == coop::net::kPowerPanelTerminalUnnamed;
    const char* refusal = !bits                  ? "it flips no breaker"
                          : unnamed              ? "its portable PC has no identity here yet"
                          : !reach               ? EL::OutcomeName(reach.outcome)
                          : (page && waterlogged) ? "the panel is waterlogged"  // the page's own refusal
                                                  : nullptr;
    if (refusal) {
        ++g_pressesRefused;
        UE_LOGW("power_panel: host refused slot %u's %s press 0x%02X (seq %u) -- %s (%.0f uu, allowed %.0f)", sender,
                page ? "page" : "lever", bits, p.seq, refusal, reach.distUU, reach.reachUU);
        HostBroadcast(s, panel, sender);  // its acknowledgement reverts the presser's prediction
        return Take::Done;
    }
    uint8_t cur = 0, next = 0;
    PC::ReadPress(panel, cur);
    if (page) {
        // The page's own flip, whose wait of 1.5 to 25 s the presser already served before its press came.
        PC::WritePress(panel, static_cast<uint8_t>((cur ^ bits) & kMaskBits));
        PC::ButtonsVisibility(panel);  // its seam broadcasts the canonical
    } else {
        // The game's own lever press, by the presser's puppet: its tutorial gate, its flip, its click and its
        // apply, whose seam broadcasts the canonical with this slot's click.
        coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(sender);
        void* body = (rp && rp->valid()) ? rp->GetActor() : nullptr;
        g_takingFor = sender;
        for (int b = 0; b < 5; ++b)
            if (bits & (1u << b)) PC::PressLever(panel, body, b);
        g_takingFor = 0;
    }
    PC::ReadPress(panel, next);
    if (g_ackDirty) HostBroadcast(s, panel);  // the lever's route refused it (the tutorial), or the seam is dead
    if (next == cur) {
        ++g_pressesRefused;
        UE_LOGW("power_panel: host ran slot %u's %s press 0x%02X (seq %u) and its own route flipped nothing (the "
                "tutorial's gate)", sender, page ? "page" : "lever", bits, p.seq);
        return Take::Done;
    }
    ++g_pressesTaken;
    UE_LOGI("power_panel: host took slot %u's %s press 0x%02X (seq %u): 0x%02X -> 0x%02X", sender,
            page ? "page" : "lever", bits, p.seq, cur, next);
    return Take::Done;
}

// Every waiting press of `slot` refused with its newest, whose acknowledgement answers them all: its queue is full.
void HostRefuseQueue(coop::net::Session* s, uint8_t slot, uint16_t newestSeq) {
    const size_t n = g_waiting[slot].size();
    g_waiting[slot].clear();
    if (SeqAfter(newestSeq, g_ack[slot])) g_ack[slot] = newestSeq;
    g_ackDirty = true;
    g_pressesRefused += n + 1;
    UE_LOGE("power_panel: host refused slot %u's %zu waiting presses and seq %u -- its queue is full", slot, n,
            newestSeq);
    if (void* panel = PC::Panel()) HostBroadcast(s, panel, slot);
}

// A press from the wire is taken at once when its slot has none waiting and a token to spend.
void HostOffer(coop::net::Session* s, const PowerPanelPayload& p, uint8_t sender) {
    if (sender == 0 || sender >= coop::net::kMaxPeers) return;
    auto& q = g_waiting[sender];
    const Waiting w{p, NowMs()};
    if (q.empty() && TakeToken(sender)) {
        if (HostTakePress(s, w, sender) == Take::Done) return;
        g_rate[sender].Refund(kPressBudget);  // a wait runs nothing, so it spends no token
    }
    if (q.size() >= kMaxQueued) {
        HostRefuseQueue(s, sender, p.seq);
        return;
    }
    q.push_back(w);
}

void HostDrainWaiting(coop::net::Session* s) {
    for (uint8_t slot = 1; slot < coop::net::kMaxPeers; ++slot) {
        auto& q = g_waiting[slot];
        while (!q.empty() && TakeToken(slot)) {
            if (HostTakePress(s, q.front(), slot) == Take::Wait) {
                g_rate[slot].Refund(kPressBudget);
                break;
            }
            q.pop_front();
        }
    }
}

// The canonical each owed joiner waits for, once the panel resolves.
void HostServeOwed(coop::net::Session* s) {
    if (!g_owed || !PC::EnsureResolved()) return;
    void* panel = PC::Panel();
    if (!panel) return;
    for (int slot = 1; slot < coop::net::kMaxPeers; ++slot)
        if (g_owed & (1u << slot)) HostBroadcast(s, panel, slot);
    g_owed = 0;
}

// ---- breaker component restoration (R3: upgrade duration timer & throbber) -----------------

constexpr int kBreakerCompCount = 5;

// Helper to set switcher_button on ui_breakerComp_C (0 = idle button, 1 = throbber)
bool SetBreakerCompSwitcher(void* widget, int32_t index) {
    if (index == 0) return PC::ResetBreakerCompSwitcher(widget);
    static ue_wrap::reflection::InstanceOffset s_offBreakerSwitcher{L"switcher_button"};
    if (!widget || !ue_wrap::reflection::IsLive(widget)) return false;
    const int32_t off = s_offBreakerSwitcher.Of(widget);
    if (off < 0) return false;
    void* sw = *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(widget) + off);
    if (!sw || !ue_wrap::reflection::IsLive(sw)) return false;
    return ue_wrap::umg::SwitcherSetIndex(sw, index);
}

// ui_breakerComp uses EaseIn on a uniform random value, with an exponent that drops from 6 to 2.
// Its endpoints lerp from 10–25s down to 1–1.5s as upg_compTime_ reaches level 16.
float ComputeBreakerDuration(int32_t level, float randomUnit) {
    const float alpha = std::clamp(static_cast<float>(level) / 16.0f, 0.0f, 1.0f);
    const float minSec = 10.0f + (1.0f - 10.0f) * alpha;
    const float maxSec = 25.0f + (1.5f - 25.0f) * alpha;
    const float exponent = 6.0f + (2.0f - 6.0f) * alpha;
    return minSec + (maxSec - minSec) * std::pow(std::clamp(randomUnit, 0.0f, 1.0f), exponent);
}

float ComputeBreakerDuration(int32_t level) {
    static thread_local std::mt19937 gen{static_cast<unsigned int>(NowMs())};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return ComputeBreakerDuration(level, dist(gen));
}

EL::ElementId CurrentPageTerminal() {
    void* term = ue_wrap::laptop::TerminalInUse();
    if (term && ue_wrap::portable_pc::IsPortablePc(term)) {
        const EL::ElementId eid = EL::Registry::Get().EidForActor(term);
        return eid == EL::kInvalidId ? coop::net::kPowerPanelTerminalUnnamed : eid;
    }
    return EL::kInvalidId;  // the fixed laptop
}

struct BreakerRestoreState {
    bool active = false;
    ue_wrap::CachedObjRef panelRef;
    std::vector<ue_wrap::CachedObjRef> widgets;
    uint8_t bit = 0;
    bool startOn = false;
    EL::ElementId terminal = EL::kInvalidId;
    uint64_t expireMs = 0;
};
BreakerRestoreState g_restores[kBreakerCompCount];

// If a widget cannot be reset immediately, keep its weak identity until a later safe retry. A fresh
// native click discards this debt before its own latent action can take ownership of the switcher.
struct BreakerWidgetCleanup {
    ue_wrap::CachedObjRef panelRef;
    ue_wrap::CachedObjRef widgetRef;
};
std::vector<BreakerWidgetCleanup> g_breakerCleanup;
std::vector<ue_wrap::CachedObjRef> g_breakerCleanupInFlight;
constexpr size_t kMaxBreakerWidgets = 128;

void PruneBreakerCleanup() {
    for (auto it = g_breakerCleanup.begin(); it != g_breakerCleanup.end();) {
        if (!it->widgetRef.Get()) it = g_breakerCleanup.erase(it);
        else ++it;
    }
}

bool IsActiveBreakerWidget(void* widget) {
    for (const auto& r : g_restores)
        if (r.active)
            for (const auto& ref : r.widgets)
                if (ref.Is(widget)) return true;
    return false;
}

void QueueBreakerWidgetCleanup(const ue_wrap::CachedObjRef& panelRef, const ue_wrap::CachedObjRef& widgetRef) {
    void* widget = widgetRef.Get();
    if (!widget || IsActiveBreakerWidget(widget)) return;
    for (const auto& debt : g_breakerCleanup)
        if (debt.widgetRef.Is(widget)) return;
    g_breakerCleanup.push_back({panelRef, widgetRef});
}

// An upd/click supplies a freshly entered widget, so it can clean its own debt even if the panel
// changed. Background retries, by contrast, require the original panel still to be current.
void TryBreakerWidgetCleanup(void* widget, bool fresh, bool refresh) {
    for (size_t i = 0; i < g_breakerCleanup.size(); ++i) {
        if (!g_breakerCleanup[i].widgetRef.Is(widget)) continue;
        const auto debt = g_breakerCleanup[i];
        void* panel = debt.panelRef.Get();
        if (!fresh && (!panel || PC::Panel() != panel)) return;
        g_breakerCleanup.erase(g_breakerCleanup.begin() + i);  // engine calls can re-enter
        g_breakerCleanupInFlight.push_back(debt.widgetRef);
        const bool reset = PC::ResetBreakerCompSwitcher(widget);
        bool stillOwned = false;
        for (auto it = g_breakerCleanupInFlight.begin(); it != g_breakerCleanupInFlight.end();) {
            if (it->Is(widget)) {
                stillOwned = true;
                it = g_breakerCleanupInFlight.erase(it);
            } else ++it;
        }
        if (!stillOwned) return;  // a re-entrant native click or disconnect took ownership
        if (!reset) QueueBreakerWidgetCleanup(debt.panelRef, debt.widgetRef);
        else if (refresh && debt.widgetRef.Is(widget) && debt.panelRef.Is(panel) && PC::Panel() == panel)
            PC::RefreshBreakerCompVisual(widget);
        return;
    }
}

void ForgetBreakerWidgetCleanup(void* widget) {
    for (auto it = g_breakerCleanup.begin(); it != g_breakerCleanup.end();) {
        if (it->widgetRef.Is(widget)) it = g_breakerCleanup.erase(it);
        else ++it;
    }
    for (auto it = g_breakerCleanupInFlight.begin(); it != g_breakerCleanupInFlight.end();) {
        if (it->Is(widget)) it = g_breakerCleanupInFlight.erase(it);
        else ++it;
    }
}

void RetryBreakerWidgetCleanup() {
    PruneBreakerCleanup();
    const auto debts = g_breakerCleanup;  // resets may invoke an upd, changing the live list
    for (const auto& debt : debts)
        if (void* widget = debt.widgetRef.Get()) TryBreakerWidgetCleanup(widget, false, true);
}

// A panel may be shown by a laptop and a portable PC; remember every widget whose throbber we set.
// Weak identities avoid retaining widgets through a world change. Never show an untracked throbber.
bool TrackBreakerWidget(BreakerRestoreState& r, void* widget) {
    for (auto it = r.widgets.begin(); it != r.widgets.end();) {
        if (it->Is(widget)) return true;
        if (!it->Get()) it = r.widgets.erase(it);
        else ++it;
    }
    if (r.widgets.size() >= 16) return false;
    // A newly managed widget will be cleaned by this restore, not by an older debt.
    ForgetBreakerWidgetCleanup(widget);
    PruneBreakerCleanup();
    size_t knownWidgets = g_breakerCleanup.size() + g_breakerCleanupInFlight.size();
    for (const auto& state : g_restores) knownWidgets += state.widgets.size();
    if (knownWidgets >= kMaxBreakerWidgets) return false;
    r.widgets.emplace_back();
    r.widgets.back().Set(widget);
    if (r.widgets.back().Is(widget)) return true;
    r.widgets.pop_back();
    return false;
}

void ResetBreakerWidgets(const ue_wrap::CachedObjRef& panelRef, std::vector<ue_wrap::CachedObjRef>& widgets) {
    // Queue every moved reference before the first engine call, which can re-enter the panel code.
    for (const auto& ref : widgets) QueueBreakerWidgetCleanup(panelRef, ref);
    for (const auto& ref : widgets)
        if (void* widget = ref.Get()) TryBreakerWidgetCleanup(widget, false, true);
}

void ResetBreakerRestore(int compIndex) {
    if (compIndex >= 0 && compIndex < kBreakerCompCount && g_restores[compIndex].active) {
        auto& r = g_restores[compIndex];
        const auto panelRef = r.panelRef;
        auto widgets = std::move(r.widgets);
        r.active = false;
        r.panelRef.Reset();
        r.widgets.clear();
        ResetBreakerWidgets(panelRef, widgets);
    }
}

void ResetAllBreakerRestores() {
    for (int i = 0; i < kBreakerCompCount; ++i) {
        ResetBreakerRestore(i);
    }
}

// Cancel only when the panel or the effective state of this breaker has changed since the click.
// This distinguishes a host flip from acknowledgement of a client's already-predicted press.
void CancelChangedBreakerRestores(void* panel, uint8_t mask) {
    for (int i = 0; i < kBreakerCompCount; ++i) {
        const auto& r = g_restores[i];
        if (r.active && (!r.panelRef.Is(panel) || ((mask & r.bit) != 0) != r.startOn)) {
            ResetBreakerRestore(i);
        }
    }
}

void TickBreakerRestores() {
    // A host may remain running after its last peer leaves, without a session teardown.
    if (!Connected()) {
        ResetAllBreakerRestores();
        RetryBreakerWidgetCleanup();
        return;
    }
    RetryBreakerWidgetCleanup();
    if (std::none_of(std::begin(g_restores), std::end(g_restores),
                     [](const BreakerRestoreState& r) { return r.active; })) return;
    const uint64_t now = NowMs();
    void* panel = PC::EnsureResolved() ? PC::Panel() : nullptr;

    for (int i = 0; i < kBreakerCompCount; ++i) {
        auto& r = g_restores[i];
        if (!r.active) continue;

        if (!panel || !r.panelRef.Is(panel)) {
            ResetBreakerRestore(i);
            continue;
        }

        uint8_t cur = 0;
        if (!PC::ReadPress(panel, cur) || ((cur & r.bit) != 0) != r.startOn) {
            ResetBreakerRestore(i);
            continue;
        }

        if (now >= r.expireMs) {
            const uint8_t bit = r.bit;
            const EL::ElementId terminal = r.terminal;
            const auto panelRef = r.panelRef;
            auto widgets = std::move(r.widgets);
            r.active = false;
            r.panelRef.Reset();
            r.widgets.clear();

            // Native completion resets the switcher, checks waterlogging, reports its hint or
            // flips the selected breaker, and calls buttonsVisibility in the game's own order.
            // Keep the original panel snapshot and terminal for the client-intent seam.
            g_pageSnap = cur;
            auto result = PC::BreakerCompFinish::Unavailable;
            for (auto& ref : widgets) {
                void* widget = ref.Get();
                int32_t currentIndex = -1;
                if (!widget || !PC::ReadBreakerCompIndex(widget, currentIndex) || currentIndex != i) continue;
                ScopedPagePress mark(terminal);
                result = PC::FinishBreakerComp(widget, panel);
                if (result != PC::BreakerCompFinish::Unavailable) break;
            }
            // The player can close the breaker page before its countdown ends.
            // With no usable widget, keep the previous guarded panel-only restore.
            bool usedPanelOnly = false;
            if (result == PC::BreakerCompFinish::Unavailable && Connected() &&
                panelRef.Is(PC::Panel())) {
                uint8_t before = 0;
                bool waterlogged = false, disabled = false;
                if (PC::ReadPress(panel, before) && ((before ^ cur) & bit) == 0 &&
                    PC::ReadWaterlogged(panel, waterlogged) && !waterlogged &&
                    PC::ReadDisabled(panel, disabled) && !disabled &&
                    PC::WritePress(panel, static_cast<uint8_t>((before ^ bit) & kMaskBits))) {
                    g_pageSnap = before;
                    ScopedPagePress mark(terminal);
                    usedPanelOnly = true;
                    result = PC::ButtonsVisibility(panel) ? PC::BreakerCompFinish::Ran : PC::BreakerCompFinish::Faulted;
                }
            }
            ResetBreakerWidgets(panelRef, widgets);

            const char* const route = usedPanelOnly ? "panel-only restore" : "native continuation";
            if (result == PC::BreakerCompFinish::Ran) {
                uint8_t after = cur;
                PC::ReadPress(panelRef.Get(), after);
                UE_LOGI("power_panel: breaker comp %d (bit 0x%02X) %s ran: 0x%02X -> 0x%02X",
                        i, bit, route, cur, after);
            } else if (result == PC::BreakerCompFinish::Faulted) {
                UE_LOGW("power_panel: breaker comp %d %s faulted; its effects are uncertain", i, route);
            } else {
                UE_LOGW("power_panel: breaker comp %d native continuation became unavailable; press again", i);
            }
        }
    }
}

// ---- a client ------------------------------------------------------------------------------------------------

// The canonical with my untaken presses on top, onto the panel when they differ. A fresh canonical also plays the
// clicks of another player's lever; a re-assertion plays nothing.
void ClientApplyModel(void* panel, const PowerPanelPayload& p, bool fresh) {
    uint8_t model = static_cast<uint8_t>(p.bits & kMaskBits);
    for (const Pending& d : g_pending) model ^= d.bits;
    const bool disabled = (p.flags & kFlagDisabled) != 0;
    CancelChangedBreakerRestores(panel, model);
    uint8_t cur = 0;
    bool curDisabled = false;
    if (!PC::ReadPress(panel, cur) || !PC::ReadDisabled(panel, curDisabled)) return;
    if (cur != model || curDisabled != disabled) {
        PC::WriteDisabled(panel, disabled);
        PC::WritePress(panel, model);
        if (!RedApply()) {
            PC::ButtonsVisibility(panel);
            // The desk virus's lockout (virus_pb) switches the servers off as it starts, and as it ends switches them
            // on with the calc breaker and plays the turn-on cue; the host's 60 s is its only clock, so each half
            // runs here as its edge arrives.
            if (disabled != curDisabled) {
                PC::SetServersActive(panel, !disabled && (model & kCalcBit) != 0);
                if (!disabled) PC::PlayTurnOnCue(panel);
            }
        }
        ++g_canonicalsApplied;
        UE_LOGI("power_panel: canonical 0x%02X%s %s, the panel was 0x%02X%s (%zu press(es) of mine on top)", p.bits,
                disabled ? " disabled" : "", fresh ? "applied" : "re-asserted", cur, curDisabled ? " disabled" : "",
                g_pending.size());
    }
    if (fresh && p.leverBits && p.leverSlot != LocalSlot())
        for (int b = 0; b < 5; ++b)
            if (p.leverBits & (1u << b)) PC::PlayLeverSound(panel, (model >> b) & 1);
}

void ClientTakeCanonical(const PowerPanelPayload& p) {
    const uint8_t me = LocalSlot();
    const uint16_t ack = me < coop::net::kMaxPeers ? p.ack[me] : 0;
    size_t kept = 0;
    for (const Pending& d : g_pending)
        if (SeqAfter(d.seq, ack)) g_pending[kept++] = d;
    g_pending.resize(kept);
    g_canonical = p;
    g_haveCanonical = true;
    void* panel = PC::EnsureResolved() ? PC::Panel() : nullptr;
    if (!panel) {
        g_parked = p;
        g_haveParked = true;
        return;
    }
    g_haveParked = false;
    ClientApplyModel(panel, p, true);
}

// ---- the watches ---------------------------------------------------------------------------------------------

sg::Verdict OnLeverPre(const sg::Call& call) {
    if (!g_leverFn.Is(call.function)) g_leverFn.Set(call.function);
    if (call.object) PC::ReadPress(call.object, g_leverSnap);
    return sg::Verdict::Run;
}

// Keep observing the native latent continuation when the replacement cannot safely start.
sg::Verdict OnPagePre(const sg::Call& call) {
    if (!g_pageFn.Is(call.function)) g_pageFn.Set(call.function);
    if (void* panel = PC::Panel()) PC::ReadPress(panel, g_pageSnap);
    return sg::Verdict::Run;
}

// A click on any breaker component's toggle button on the laptop / portable PC breaker page.
// Displays the throbber (switcher_button index 1) and enforces the upgrade duration timer
// computed from upg_compTime_ (index 26, scaling from 10-25s down to 1-1.5s). Breakers flip only
// upon timer completion, preventing premature (0.0s) restoration and respecting upgrades.
sg::Verdict OnBreakerButtonClickPre(const sg::Call& call) {
    // The watch outlives a session. With no pump to advance our timer, leave the native button alone.
    if (call.object) {
        TryBreakerWidgetCleanup(call.object, true, false);
        // If cleanup failed, a native fallback may now start its own delay on this widget.
        ForgetBreakerWidgetCleanup(call.object);
    }
    if (!Connected()) {
        ResetAllBreakerRestores();
        ForgetBreakerWidgetCleanup(call.object);
        return sg::Verdict::Run;
    }
    if (!call.object) return sg::Verdict::Run;

    void* panel = PC::EnsureResolved() ? PC::Panel() : nullptr;
    if (!panel) return sg::Verdict::Run;

    int32_t compIndex = -1;
    if (!PC::ReadBreakerCompIndex(call.object, compIndex)) return sg::Verdict::Run;
    if (compIndex < 0 || compIndex >= kBreakerCompCount) return sg::Verdict::Run;
    const uint8_t bit = PC::BreakerBitForCompIndex(compIndex);
    if (!bit) return sg::Verdict::Run;

    // If already restoring this breaker component, ignore repeated clicks
    auto& r = g_restores[compIndex];
    if (r.active) {
        if (r.panelRef.Is(panel)) {
            if (TrackBreakerWidget(r, call.object)) SetBreakerCompSwitcher(call.object, 1);
            return sg::Verdict::Cancel;
        }
        ResetBreakerRestore(compIndex);
        ForgetBreakerWidgetCleanup(call.object);
    }

    // If the exact native completion cannot be called safely, leave the whole click and its
    // native latent delay alone, including its waterlogged hint and generator/lockout rules.
    if (!PC::CanFinishBreakerComp(call.object, panel)) return sg::Verdict::Run;

    // Read upgrade level for upg_compTime_ (panel index 26)
    int32_t level = 0;
    uint8_t cur = 0;
    if (!ue_wrap::upgrades::ReadLevel(26, &level)) return sg::Verdict::Run;
    const EL::ElementId terminal = CurrentPageTerminal();
    if (!Connected() || PC::Panel() != panel || !PC::ReadPress(panel, cur)) return sg::Verdict::Run;

    const float durationSec = ComputeBreakerDuration(level);
    const uint64_t durationMs = static_cast<uint64_t>(durationSec * 1000.0f);

    // Publish the timer before the engine call: an update or repeated click can re-enter it.
    r.panelRef.Set(panel);
    if (!r.panelRef.Is(panel) || !TrackBreakerWidget(r, call.object)) {
        r.panelRef.Reset();
        r.widgets.clear();
        return sg::Verdict::Run;
    }
    r.active = true;
    r.bit = bit;
    r.startOn = (cur & bit) != 0;
    r.terminal = terminal;
    r.expireMs = NowMs() + durationMs;

    if (!SetBreakerCompSwitcher(call.object, 1)) {
        ResetBreakerRestore(compIndex);
        ForgetBreakerWidgetCleanup(call.object);
        return sg::Verdict::Run;  // native event and its observed latent continuation
    }
    if (!r.active || !r.panelRef.Is(panel) || !Connected()) return sg::Verdict::Cancel;
    PC::PlayBreakerButtonSound(call.object);

    UE_LOGI("power_panel: breaker comp %d (bit 0x%02X) restoration started: level %d, duration %.2f s",
            compIndex, bit, level, durationSec);

    // Cancel the native Blueprint event body so ExecuteUbergraph_ui_breakerComp(1264) never runs,
    // avoiding duplicate unmanaged FLatentActionManager Delays.
    return sg::Verdict::Cancel;
}

// Maintain managed throbbers and clean only known managed debt; native fallback may also be spinning.
sg::Verdict OnBreakerUpdPre(const sg::Call& call) {
    if (!call.object) return sg::Verdict::Run;
    if (!Connected()) {
        TryBreakerWidgetCleanup(call.object, true, false);
        return sg::Verdict::Run;
    }
    int32_t compIndex = -1;
    if (PC::ReadBreakerCompIndex(call.object, compIndex) && compIndex >= 0 && compIndex < kBreakerCompCount) {
        auto& r = g_restores[compIndex];
        if (r.active && r.panelRef.Is(PC::Panel()) && TrackBreakerWidget(r, call.object)) {
            SetBreakerCompSwitcher(call.object, 1);
            return sg::Verdict::Run;
        }
    }
    TryBreakerWidgetCleanup(call.object, true, false);
    return sg::Verdict::Run;
}

// The seam: every press and every blackout ends in the panel's buttonsVisibility.
sg::Verdict OnSeamPre(const sg::Call& call) {
    auto* s = Connected();
    if (!s || !call.object) return sg::Verdict::Run;
    void* const leverFn = g_leverFn.Get();
    void* const pageFn = g_pageFn.Get();
    const bool lever = leverFn && sg::IsBodyActive(leverFn);
    const bool page = !lever && (g_pagePressActive || (pageFn && sg::IsBodyActive(pageFn)));
    uint8_t cur = 0;
    if (!PC::ReadPress(call.object, cur)) return sg::Verdict::Run;
    const bool currentPanel = PC::Panel() == call.object;
    if (currentPanel) CancelChangedBreakerRestores(call.object, cur);
    if (s->role() == coop::net::Role::Host) {
        if (lever) {  // a lever press, the host's own or a client's it runs: every other peer plays its click
            g_leverBits = static_cast<uint8_t>((cur ^ g_leverSnap) & kMaskBits);
            g_leverSlot = g_takingFor;
        }
        bool disabled = false;
        PC::ReadDisabled(call.object, disabled);
        if (!g_haveSent || cur != g_sentMask || disabled != g_sentDisabled || g_ackDirty)
            HostBroadcast(s, call.object);
        return sg::Verdict::Run;
    }
    if (!lever && !page) return sg::Verdict::Run;  // a client's panel moves only by its own press or the host
    uint8_t& snap = lever ? g_leverSnap : g_pageSnap;
    const uint8_t delta = static_cast<uint8_t>((cur ^ snap) & kMaskBits);
    snap = cur;
    if (!delta) return sg::Verdict::Run;
    PowerPanelPayload p{};
    p.op = coop::net::kPowerPanelOpPress;
    p.bits = delta;
    p.flags = page ? kFlagPage : 0;
    p.leverSlot = kNoSlot;
    p.terminal = EL::kInvalidId;
    if (page) p.terminal = g_pagePressActive ? g_pageTerminal : CurrentPageTerminal();
    p.seq = ++g_seq;
    g_pending.push_back({p.seq, delta});
    s->SendReliableToSlot(0, coop::net::ReliableKind::PowerControlState, &p, sizeof(p));
    ++g_pressesSent;
    UE_LOGI("power_panel: local %s press flipped 0x%02X (seq %u) -- sent to the host", page ? "page" : "lever",
            delta, p.seq);
    return sg::Verdict::Run;
}

// A client never blacks its panel out nor locks it on its own: the host's blackout and the host's lockout arrive
// as its canonical. A blackout inside a verb of ours (a generator's break the host's rows run here) is let through
// for what else solar() does, and its breakers are put back to the canonical after (ReassertCanonical).
sg::Verdict OnOwnEdgePre(const sg::Call& call) {
    auto* s = Connected();
    if (!s) return sg::Verdict::Run;
    const bool lockout = call.tag == 0x50505650 /*'PPVP'*/;
    if (s->role() != coop::net::Role::Client || call.fromOurCode) {
        if (!lockout) ResetAllBreakerRestores();
        return sg::Verdict::Run;
    }
    if (!coop::net_pump::IsInAnnouncedWorld(call.object)) return sg::Verdict::Run;
    if (++(lockout ? g_refusedLockouts : g_refusedBlackouts) == 1)
        UE_LOGI("power_panel: this client refuses its own %s -- the host's panel is its author",
                lockout ? "virus_pb lockout" : "solar() blackout");
    return sg::Verdict::Cancel;
}

UE_FACT_FN(kRowButtonsVisibility, "power_panel", L"powerControl_C", L"buttonsVisibility", Declared);
UE_FACT_FN(kRowActionOptionIndex, "power_panel", L"powerControl_C", L"actionOptionIndex", Declared);
UE_FACT_FN(kRowBreakerButtonClick, "power_panel", L"ui_breakerComp_C",
           L"BndEvt__ui_breakerComp_button_toggle_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature",
           Declared);
UE_FACT_FN(kRowBreakerUpd, "power_panel", L"ui_breakerComp_C", L"upd", Declared);
UE_FACT_FN(kRowUbergraphBreakerComp, "power_panel", L"ui_breakerComp_C", L"ExecuteUbergraph_ui_breakerComp",
           Declared);
UE_FACT_FN(kRowSolar, "power_panel", L"powerControl_C", L"solar", Declared);
UE_FACT_FN(kRowVirusPb, "power_panel", L"powerControl_C", L"virus_pb", Declared);

struct WatchDef {
    const ue_wrap::facts::Function* row;
    int tag;
    sg::PreFn pre;
};
constexpr WatchDef kWatches[] = {
    { &kRowButtonsVisibility,    0x50504256 /*'PPBV'*/, &OnSeamPre },
    { &kRowActionOptionIndex,    0x5050414F /*'PPAO'*/, &OnLeverPre },
    { &kRowBreakerButtonClick,   0x50504243 /*'PPBC'*/, &OnBreakerButtonClickPre },
    { &kRowBreakerUpd,           0x50505550 /*'PPUP'*/, &OnBreakerUpdPre },
    { &kRowUbergraphBreakerComp, 0x50505047 /*'PPPG'*/, &OnPagePre },
    { &kRowSolar,                0x5050534C /*'PPSL'*/, &OnOwnEdgePre },
    { &kRowVirusPb,              0x50505650 /*'PPVP'*/, &OnOwnEdgePre },
};
constexpr int kWatchCount = static_cast<int>(sizeof(kWatches) / sizeof(kWatches[0]));
bool g_registered = false;
bool g_settled = false;

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    if (g_registered) return;
    g_registered = true;
    for (const WatchDef& w : kWatches)
        if (!sg::WatchName(*w.row, w.tag, w.pre, nullptr))
            UE_LOGE("power_panel: the script-body gate refused the watch on %ls::%ls", w.row->Class(),
                    w.row->Name());
}

void Tick() {
    TickBreakerRestores();
    auto* s = Connected();
    if (s && s->role() == coop::net::Role::Host) {
        HostDrainWaiting(s);
        HostServeOwed(s);
    } else if (s) {
        // A client's panel resolves before its first canonical, so a press made before one is caught too.
        if (PC::EnsureResolved() && g_haveParked && PC::Panel()) ClientTakeCanonical(g_parked);
    }
    if (g_settled || !g_registered) return;
    sg::ResolvePendingNames();
    int live = 0, settled = 0;
    for (const WatchDef& w : kWatches) {
        if (sg::NameWatchLive(*w.row, w.tag)) ++live;
        if (sg::NameWatchSettled(*w.row, w.tag)) ++settled;
    }
    if (live == kWatchCount) {
        g_settled = true;
        UE_LOGI("power_panel: the panel's gates are live (buttonsVisibility, the lever, the breaker page, solar, "
                "the virus lockout)");
    } else if (settled == kWatchCount) {
        g_settled = true;
        UE_LOGE("power_panel: %d of %d panel gates are dead -- presses on those surfaces do not reach the host",
                kWatchCount - live, kWatchCount);
    }
}

void OnReliable(const coop::net::PowerPanelPayload& payload, uint8_t senderSlot) {
    auto* s = Connected();
    if (!s) return;
    if (s->role() == coop::net::Role::Host) {
        if (payload.op == coop::net::kPowerPanelOpPress) HostOffer(s, payload, senderSlot);
        return;
    }
    if (payload.op == coop::net::kPowerPanelOpCanonical && senderSlot == 0) ClientTakeCanonical(payload);
}

void QueueConnectBroadcastForSlot(int slot) {
    auto* s = Connected();
    if (!s || s->role() != coop::net::Role::Host || slot <= 0 || slot >= coop::net::kMaxPeers) return;
    g_owed |= static_cast<uint32_t>(1u << slot);
    HostServeOwed(s);
    if (g_owed & (1u << slot))
        UE_LOGI("power_panel: slot %d's canonical waits for the host's panel to resolve", slot);
}

void OnPeerLeft(uint8_t slot) {
    if (slot == 0 || slot >= coop::net::kMaxPeers) return;
    g_waiting[slot].clear();
    g_rate[slot].Reset();
    g_owed = static_cast<uint32_t>(g_owed & ~(1u << slot));
}

void ReassertCanonical() {
    auto* s = Connected();
    if (!s || s->role() != coop::net::Role::Client || !g_haveCanonical || RedApply()) return;
    if (void* panel = PC::Panel()) ClientApplyModel(panel, g_canonical, false);
}

size_t PendingPresses() { return g_pending.size(); }
uint64_t ClientPressesSent() { return g_pressesSent; }
uint64_t HostPressesTaken() { return g_pressesTaken; }

bool LastCanonical(uint8_t& bits, bool& disabled) {
    if (!g_haveCanonical) return false;
    bits = static_cast<uint8_t>(g_canonical.bits & kMaskBits);
    disabled = (g_canonical.flags & kFlagDisabled) != 0;
    return true;
}

void OnDisconnect() {
    ResetAllBreakerRestores();
    g_breakerCleanup.clear();
    g_breakerCleanupInFlight.clear();
    if (g_pressesSent || g_canonicalsApplied || g_refusedBlackouts || g_refusedLockouts || g_pressesTaken ||
        g_pressesRefused)
        UE_LOGI("power_panel: session end -- presses sent %llu, canonicals applied %llu, own blackouts refused %llu, "
                "own lockouts refused %llu; as host: presses taken %llu, refused %llu",
                static_cast<unsigned long long>(g_pressesSent), static_cast<unsigned long long>(g_canonicalsApplied),
                static_cast<unsigned long long>(g_refusedBlackouts),
                static_cast<unsigned long long>(g_refusedLockouts), static_cast<unsigned long long>(g_pressesTaken),
                static_cast<unsigned long long>(g_pressesRefused));
    g_pending.clear();
    g_seq = 0;
    g_haveCanonical = false;
    g_haveParked = false;
    g_haveSent = false;
    g_ackDirty = false;
    g_leverBits = 0;
    g_leverSlot = kNoSlot;
    g_takingFor = 0;
    g_leverFn.Reset();
    g_pageFn.Reset();
    g_pagePressActive = false;
    g_pageTerminal = EL::kInvalidId;
    for (int slot = 0; slot < coop::net::kMaxPeers; ++slot) {
        g_waiting[slot].clear();
        g_rate[slot].Reset();
    }
    g_owed = 0;
    g_pressesSent = g_canonicalsApplied = g_refusedBlackouts = g_refusedLockouts = 0;
    g_pressesTaken = g_pressesRefused = 0;
}

}  // namespace coop::power_panel
