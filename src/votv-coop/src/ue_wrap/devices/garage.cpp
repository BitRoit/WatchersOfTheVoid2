// ue_wrap/devices/garage.cpp -- see ue_wrap/devices/garage.h. Engine access for the base garage
// door (Agarage_C). Offsets and verbs are resolved from the live class by name; a class whose Open or
// acivae does not resolve leaves the lane off, said once, since a guessed offset would write whatever a
// newer build keeps there.

#include "ue_wrap/devices/garage.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/facts/function_row.h"

#include <atomic>
#include <cstdint>

namespace ue_wrap::garage {
namespace {

namespace R = reflection;
namespace sg = ue_wrap::script_gate;

UE_FACT_FN(kRowSetTime, "garage", L"garage_C", L"settime", Declared);
constexpr int kTagSetTime = 0x47525354;  // 'GRST'

std::atomic<bool> g_resolved{false};
std::atomic<bool> g_setTimeWatched{false};

int32_t g_openOff    = -1;       // Agarage_C::Open
int32_t g_movOff     = -1;       // Agarage_C::mov, true mid-swing (optional: only a drill reads it)
int32_t g_moveOff    = -1;       // Agarage_C::move (UTimelineComponent*)
int32_t g_skelMeshOff= -1;       // Agarage_C::SkeletalMesh (USkeletalMeshComponent*)
int32_t g_boxOff     = -1;       // Agarage_C::Box (UBoxComponent*)
int32_t g_loopOff    = -1;       // Agarage_C::loop (UAudioComponent*)
int32_t g_navModOff  = -1;       // Agarage_C::NavModifier (UNavModifierComponent*)

// The verbs, resolved per instance (Verb below): runTrigger(owner, index), the wall button's call (a drill's),
// and acivae() -- the NATIVE animated swing (the montage from position 0 at half rate, and the move timeline
// over its full length). NOT settime, which runs the same timeline and then JUMPS it: the montage at full
// rate from position 100, then move.SetNewTime to second 0 or 1 of a six-second track.
// No Key offset: identity is the level-export FName (GetNameKey), not the save key -- see
// garage.h for why the key cannot serve as one.

// A garage class whose Open or acivae did not resolve: said once, asked again only for another class
// object.
void* g_failedCls = nullptr;

// garage_C as this world holds it: kept only while its slot and serial still hold it, looked up by name again
// after a world gave the class a new object. Game thread.
ue_wrap::CachedObjRef g_garageClsRef;
void* GarageClass() {
    if (void* c = g_garageClsRef.Get()) return c;
    void* c = ue_wrap::object_index::ClassByName(L"garage_C");
    if (c) g_garageClsRef.Set(c);
    return c;
}

// One of the garage's verbs on this instance: its class's own, memoised by the reflection layer.
void* Verb(void* g, const wchar_t* name) {
    void* cls = g ? R::ClassOf(g) : nullptr;
    return cls ? R::FindDispatchFunctionCached(cls, name) : nullptr;
}

sg::Verdict OnSetTimePre(const sg::Call& c) {
    if (!c.object || !IsGarage(c.object)) return sg::Verdict::Run;
    if (c.fromOurCode) return sg::Verdict::Run;
    static int32_t s_openParamOff = -1;
    if (s_openParamOff < 0 && c.function) {
        s_openParamOff = R::FindParamOffset(c.function, L"open");
    }
    bool open = false;
    if (s_openParamOff >= 0 && c.locals) {
        open = *reinterpret_cast<const bool*>(c.locals + s_openParamOff);
    } else {
        TryReadOpen(c.object, open);
    }
    SnapOpen(c.object, open);
    return sg::Verdict::Cancel;
}

}  // namespace

bool Install() {
    EnsureResolved();
    if (!g_setTimeWatched.load(std::memory_order_relaxed)) {
        if (sg::WatchName(kRowSetTime, kTagSetTime, &OnSetTimePre, nullptr)) {
            g_setTimeWatched.store(true, std::memory_order_release);
            UE_LOGI("garage: hooked garage_C::settime for save-load snap");
        }
    }
    return g_setTimeWatched.load(std::memory_order_acquire);
}

bool EnsureResolved() {
    if (g_resolved.load(std::memory_order_acquire)) {
        if (!g_setTimeWatched.load(std::memory_order_relaxed)) Install();
        return true;
    }

    // One object-index lookup a call until the class loads, which costs nothing while it has not.
    void* cls = GarageClass();
    if (!cls || cls == g_failedCls) return false;
    const int32_t openOff = R::FindPropertyOffset(cls, L"Open");
    void* acivaeFn = R::FindFunction(cls, L"acivae");
    if (openOff < 0 || !acivaeFn) {
        g_failedCls = cls;
        UE_LOGE("garage: garage_C did not resolve by name (Open@%d acivae=%p) -- the garage lane stays off for "
                "this class", openOff, acivaeFn);
        return false;
    }

    g_openOff    = openOff;
    g_movOff     = R::FindPropertyOffset(cls, L"mov");
    g_moveOff    = R::FindPropertyOffset(cls, L"move");
    g_skelMeshOff= R::FindPropertyOffset(cls, L"SkeletalMesh");
    g_boxOff     = R::FindPropertyOffset(cls, L"Box");
    g_loopOff    = R::FindPropertyOffset(cls, L"loop");
    g_navModOff  = R::FindPropertyOffset(cls, L"NavModifier");

    void* const runTriggerFn = R::FindFunction(cls, L"runTrigger");  // asked here only to say its absence
    g_resolved.store(true, std::memory_order_release);
    UE_LOGI("garage: resolved garage_C=%p Open@0x%04X acivae=%p (identity=level-export FName)",
            cls, openOff, acivaeFn);
    if (g_movOff < 0 || !runTriggerFn)
        UE_LOGW("garage: mov@%d runTrigger=%p did not both resolve -- a drill cannot wait for a garage's rest "
                "or toggle it", g_movOff, runTriggerFn);

    Install();
    return true;
}

bool IsGarage(void* obj) {
    if (!obj || !g_resolved.load(std::memory_order_acquire)) return false;
    void* garageCls = GarageClass();
    void* cls = R::ClassOf(obj);
    return garageCls && cls && R::IsDescendantOfAny(cls, &garageCls, 1);
}

std::wstring GetNameKey(void* g) {
    // Identity is the garage's level-export FName, baked into the cooked package and so
    // deterministic and cross-peer stable, NOT the save key. Mirrors ue_wrap::door_box::GetNameKey,
    // the proven author for keyless placed actors; garage.h says why the save key is unreliable.
    if (!g) return std::wstring();
    return R::ToString(R::NameOf(g));
}

bool TryReadOpen(void* g, bool& open) {
    if (!g || g_openOff < 0) return false;
    open = *reinterpret_cast<const bool*>(
        reinterpret_cast<const char*>(g) + g_openOff);
    return true;
}

bool ApplyOpen(void* g, bool open) {
    void* const acivae = Verb(g, L"acivae");
    if (!acivae) return false;
    // Idempotent: if already in the target state, do nothing (skip the re-trigger + the echo).
    bool cur = false;
    if (TryReadOpen(g, cur) && cur == open) {
        bool moving = false;
        if (!TryReadMoving(g, moving) || !moving) return true;
    }
    // Two facts about the blueprint drive this:
    //   (1) Neither settime() nor acivae() writes the `Open` bool. The only writers
    //       are runTrigger's E-press toggle and the game's own loadTriggerData
    //       (`open := value; settime`). So we must set the field ourselves, or the
    //       mirror's poll baseline goes stale and the symmetric Channel re-broadcasts
    //       the opposite -- an open/close oscillation.
    //   (2) settime() JUMPS: it plays the timeline, then puts the montage at position
    //       100 at full rate and calls move.SetNewTime with second 0 or 1 of a
    //       six-second track -- a close snaps shut, an open lurches and runs on.
    //       acivae() ANIMATES: the montage from 0 at half rate and move.Play/Reverse
    //       over the full timeline, direction read from the `Open` field.
    // So: write Open := target FIRST -- fixing the oscillation and giving acivae its direction --
    // THEN call acivae() for the native animated swing. acivae has no `mov` guard; that sits in
    // runTrigger, which ignores an E-press mid-swing, so a mid-swing opposite packet re-aims the
    // door, last writer wins. This is the local E-press path minus the toggle and that guard.
    if (g_openOff >= 0)
        *reinterpret_cast<bool*>(reinterpret_cast<char*>(g) + g_openOff) = open;
    ParamFrame f(acivae);  // acivae() takes no params -- it reads the Open field for direction
    if (!f.valid()) return false;
    return Call(g, f);
}

bool SnapOpen(void* g, bool open) {
    if (!g) return false;
    EnsureResolved();

    if (g_openOff >= 0)
        *reinterpret_cast<bool*>(reinterpret_cast<char*>(g) + g_openOff) = open;
    if (g_movOff >= 0)
        *reinterpret_cast<bool*>(reinterpret_cast<char*>(g) + g_movOff) = false;

    // 1. Move collision Box via native set(open ? 1.0f : 0.0f)
    if (void* setFn = Verb(g, L"set")) {
        ParamFrame f(setFn);
        if (f.valid() && f.Set<float>(L"InputPin", open ? 1.0f : 0.0f)) {
            Call(g, f);
        }
    }

    // 2. Timeline 'move': set time to 6.0s (open) or 0.0s (closed) and stop ticking
    if (g_moveOff >= 0) {
        void* moveComp = *reinterpret_cast<void**>(reinterpret_cast<char*>(g) + g_moveOff);
        if (moveComp) {
            void* cls = R::ClassOf(moveComp);
            if (void* setNewTimeFn = R::FindDispatchFunctionCached(cls, L"SetNewTime")) {
                ParamFrame f(setNewTimeFn);
                if (f.valid() && f.Set<float>(L"NewTime", open ? 6.0f : 0.0f)) {
                    Call(moveComp, f);
                }
            }
            if (void* stopFn = R::FindDispatchFunctionCached(cls, L"Stop")) {
                ParamFrame f(stopFn);
                if (f.valid()) {
                    Call(moveComp, f);
                }
            }
        }
    }

    // 3. SkeletalMesh: play garageDoor_Skeleton_Montage at openLoop or closeLoop
    if (g_skelMeshOff >= 0) {
        void* skelMesh = *reinterpret_cast<void**>(reinterpret_cast<char*>(g) + g_skelMeshOff);
        if (skelMesh) {
            void* getAnimFn = R::FindDispatchFunctionCached(R::ClassOf(skelMesh), L"GetAnimInstance");
            if (getAnimFn) {
                ParamFrame get(getAnimFn);
                void* anim = nullptr;
                if (get.valid() && Call(skelMesh, get) && get.GetRaw(L"ReturnValue", &anim, sizeof(anim)) && anim) {
                    void* montage = R::FindObject(L"garageDoor_Skeleton_Montage", L"AnimMontage");
                    if (montage) {
                        void* playFn = R::FindDispatchFunctionCached(R::ClassOf(anim), L"Montage_Play");
                        if (playFn) {
                            ParamFrame play(playFn);
                            if (play.valid() &&
                                play.Set<void*>(L"MontageToPlay", montage) &&
                                play.Set<float>(L"InPlayRate", 1.0f) &&
                                play.Set<float>(L"InTimeToStartMontageAt", open ? 4.0f : 12.0f)) {
                                Call(anim, play);
                            }
                        }
                        void* jumpFn = R::FindDispatchFunctionCached(R::ClassOf(anim), L"Montage_JumpToSection");
                        if (jumpFn) {
                            ParamFrame jump(jumpFn);
                            const auto sec = ue_wrap::fname_utils::StringToFName(open ? L"openLoop" : L"closeLoop");
                            if (jump.valid() &&
                                jump.SetRaw(L"SectionName", &sec, sizeof(sec)) &&
                                jump.Set<void*>(L"Montage", montage)) {
                                Call(anim, jump);
                            }
                        }
                    }
                }
            }
        }
    }

    // 4. Audio loop: stop
    if (g_loopOff >= 0) {
        void* loopComp = *reinterpret_cast<void**>(reinterpret_cast<char*>(g) + g_loopOff);
        if (loopComp) {
            if (void* stopFn = R::FindDispatchFunctionCached(R::ClassOf(loopComp), L"Stop")) {
                ParamFrame f(stopFn);
                if (f.valid()) Call(loopComp, f);
            }
        }
    }

    // 5. NavModifier: clear if open, set NavArea_Obstacle if closed
    if (g_navModOff >= 0) {
        void* navComp = *reinterpret_cast<void**>(reinterpret_cast<char*>(g) + g_navModOff);
        if (navComp) {
            if (void* setAreaFn = R::FindDispatchFunctionCached(R::ClassOf(navComp), L"SetAreaClass")) {
                ParamFrame f(setAreaFn);
                void* areaCls = open ? nullptr : R::FindClass(L"NavArea_Obstacle");
                if (f.valid() && f.Set<void*>(L"AreaClass", areaCls)) {
                    Call(navComp, f);
                }
            }
        }
    }

    return true;
}

bool TryReadMoving(void* g, bool& moving) {
    if (!g || g_movOff < 0) return false;
    moving = *reinterpret_cast<const bool*>(reinterpret_cast<const char*>(g) + g_movOff);
    return true;
}

bool CallRunTrigger(void* g, void* owner, int32_t index) {
    void* const fn = Verb(g, L"runTrigger");
    if (!fn) return false;
    ParamFrame f(fn);
    if (!f.valid() || !f.Set<void*>(L"owner", owner) || !f.Set<int32_t>(L"index", index)) return false;
    return Call(g, f);
}

}  // namespace ue_wrap::garage
