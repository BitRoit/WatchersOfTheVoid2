// Direct regression for the production USceneComponent::SetAbsolute wrapper used by remote
// flashlight puppets. Each command-line case runs in a fresh process so the wrapper's intentional
// resolved-function cache cannot hide a malformed reflected parameter layout.
#include "../../src/votv-coop/src/ue_wrap/engine/engine_component.cpp"
#include "../../src/votv-coop/src/ue_wrap/core/call.cpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace test {
struct Obj {
    int id = -1;
    int serial = 1;
    bool live = true;
    ue_wrap::reflection::FName name{0, 0};
};

Obj objects[]{{0}, {1}, {2}};
Obj& target = objects[0];
Obj& sceneClass = objects[1];
Obj& function = objects[2];
bool classReady = true;
bool functionReady = true;
bool callOk = true;
int calls = 0;
std::wstring missing;
bool expectedLoc = false;
bool expectedRot = true;
bool expectedScale = true;
}  // namespace test

namespace ue_wrap::reflection {
bool IsLive(void* object) {
    return object && static_cast<test::Obj*>(object)->live;
}
int32_t InternalIndexOf(void* object) {
    return object ? static_cast<test::Obj*>(object)->id : -1;
}
int32_t SlotSerial(int32_t index) {
    return index >= 0 && index <= 2 ? test::objects[index].serial : 0;
}
int32_t AllocateSlotSerial(int32_t index) { return SlotSerial(index); }
void* ObjectAt(int32_t index) {
    return index >= 0 && index <= 2 ? &test::objects[index] : nullptr;
}
void* FindClass(const wchar_t* name) {
    CHECK(!std::wcscmp(name, profile::name::SceneComponentClass));
    return test::classReady ? &test::sceneClass : nullptr;
}
void* FindFunction(void* owner, const wchar_t* name) {
    CHECK(owner == &test::sceneClass);
    CHECK(!std::wcscmp(name, profile::name::SetAbsoluteFn));
    return test::functionReady ? &test::function : nullptr;
}
void* ClassOf(void*) { return &test::sceneClass; }
int32_t FindPropertyOffset(void*, const wchar_t*) { return -1; }
void* FindDispatchFunctionCached(void*, const wchar_t*) { return nullptr; }
std::vector<ObjectRef> ChildObjectsOf(void*) { return {}; }
bool IsDescendantOfAny(void*, void* const*, size_t, int) { return false; }
int32_t FunctionFrameSize(void* function) {
    CHECK(function == &test::function);
    return 3;
}
std::vector<ParamInfo> FunctionParams(void* function) {
    CHECK(function == &test::function);
    std::vector<ParamInfo> params{
        {L"bNewAbsoluteLocation", 0, 1, 0},
        {L"bNewAbsoluteRotation", 1, 1, 0},
        {L"bNewAbsoluteScale", 2, 1, 0},
    };
    if (!test::missing.empty()) {
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it->name == test::missing) {
                params.erase(it);
                break;
            }
        }
    }
    return params;
}
bool CallFunction(void* object, void* function, void* params) {
    CHECK(object == &test::target);
    CHECK(function == &test::function);
    CHECK(params != nullptr);
    const auto* values = static_cast<const uint8_t*>(params);
    CHECK(bool(values[0]) == test::expectedLoc);
    CHECK(bool(values[1]) == test::expectedRot);
    CHECK(bool(values[2]) == test::expectedScale);
    ++test::calls;
    return test::callOk;
}
const FName& NameOf(void* object) { return static_cast<test::Obj*>(object)->name; }
std::wstring ToString(const FName&) { return L"flashlight_absolute_fixture"; }
}  // namespace ue_wrap::reflection

namespace ue_wrap::log {
void Write(Level, const char*, ...) {}
}  // namespace ue_wrap::log

namespace ue_wrap::fname_utils {
reflection::FName StringToFName(const std::wstring&) { return {}; }
}  // namespace ue_wrap::fname_utils

int main(int argc, char** argv) {
    using namespace test;
    const std::string mode = argc == 2 ? argv[1] : "valid";
    if (mode == "unresolved-class") classReady = false;
    else if (mode == "unresolved-function") functionReady = false;
    else if (mode == "dead") target.live = false;
    else if (mode == "missing-location") missing = L"bNewAbsoluteLocation";
    else if (mode == "missing-rotation") missing = L"bNewAbsoluteRotation";
    else if (mode == "missing-scale") missing = L"bNewAbsoluteScale";
    else if (mode == "call-failure") callOk = false;
    else if (mode != "valid") {
        std::fprintf(stderr, "unknown case: %s\n", mode.c_str());
        return 2;
    }

    const bool result = ue_wrap::engine::SetComponentAbsolute(
        &target, expectedLoc, expectedRot, expectedScale);
    const bool shouldPass = mode == "valid";
    CHECK(result == shouldPass);
    if (shouldPass || mode == "call-failure") CHECK(calls == 1);
    else CHECK(calls == 0);
    std::printf("PASS flashlight absolute wrapper %s\n", mode.c_str());
    return 0;
}
