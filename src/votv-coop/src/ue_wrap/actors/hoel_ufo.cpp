#include "ue_wrap/actors/hoel_ufo.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
namespace ue_wrap::hoel_ufo {
namespace R=reflection;
namespace {
void* ObjectField(void* actor,const wchar_t* field) {
    if(!actor || !R::IsLive(actor)) return nullptr;
    int32_t off=-1; std::wstring type;
    if(!R::PropertyOffsetAndClass(R::ClassOf(actor),field,off,type) || off<0 || type!=L"ObjectProperty")return nullptr;
    void* value=*reinterpret_cast<void**>(static_cast<unsigned char*>(actor)+off);
    return value && R::IsLive(value) ? value : nullptr;
}
}
bool ReadBeamEndpoint(void* actor,FVector& out) {
    CachedObjRef owner;owner.Set(actor);
    void* target=ObjectField(actor,L"target");
    if(!target)return false;
    CachedObjRef victim;victim.Set(target);
    // The native graph uses the bounds of the target's root component.
    void* rootFn=R::FindFunction(R::ClassOf(target),L"K2_GetRootComponent");
    if(!rootFn)return false;
    ParamFrame root(rootFn);
    if(!Call(target,root) || !owner.Get() || !victim.Get())return false;
    void* component=root.Get<void*>(L"ReturnValue");
    void* cdo=R::FindClassDefaultObject(L"KismetSystemLibrary");
    void* fn=cdo?R::FindFunction(R::ClassOf(cdo),L"GetComponentBounds"):nullptr;
    if(!component || !R::IsLive(component) || !fn)return false;
    ParamFrame bounds(fn);if(!bounds.Set<void*>(L"Component",component))return false;
    if(!Call(cdo,bounds) || !owner.Get() || !victim.Get())return false;
    FVector endpoint;if(!bounds.GetRaw(L"Origin",&endpoint,sizeof(endpoint)))return false;
    return DecodeEndpoint(&endpoint,sizeof(endpoint),out);
}
bool PaintBeam(void* actor,const FVector& endpoint) {
    FVector checked;if(!DecodeEndpoint(&endpoint,sizeof(endpoint),checked))return false;
    void* component=ObjectField(actor,L"ParticleSystem");
    void* fn=component?R::FindFunction(R::ClassOf(component),L"SetBeamTargetPoint"):nullptr;
    if(!fn)return false;
    ParamFrame frame(fn);
    if(!frame.Set<int32_t>(L"EmitterIndex",0) || !frame.Set<FVector>(L"NewTargetPoint",checked) ||
       !frame.Set<int32_t>(L"TargetIndex",0))return false;
    return Call(component,frame);
}
void TickBeamPresentation(void* actor) {
    if(!actor || !R::IsLive(actor))return;
    void* fn=R::FindFunction(R::ClassOf(actor),L"GetGameTimeSinceCreation");
    if(!fn)return;
    ParamFrame frame(fn);
    if(Call(actor,frame) && frame.Get<float>(L"ReturnValue")>=0.5f)
        engine::SetActorHiddenInGame(actor,true);
}
}
