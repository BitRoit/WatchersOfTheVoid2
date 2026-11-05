#include "ue_wrap/actors/mannequin.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/sdk_profile.h"
#include "ue_wrap/engine/engine_pawn.h"
#include "ue_wrap/engine/world_identity.h"
#include <cmath>

namespace ue_wrap::mannequin {
namespace {
namespace R=reflection;
CachedObjRef g_systemCdo,g_codeCdo,g_systemMiss,g_codeMiss;
void* Library(const wchar_t* name,CachedObjRef& cdo,CachedObjRef& miss){
    if(void* object=cdo.Get())return object;
    void* cls=R::FindClass(name);
    if(!cls||miss.Is(cls))return nullptr;
    cdo.Set(R::FindClassDefaultObject(name));
    if(!cdo.Get())miss.Set(cls);
    else miss.Reset();
    return cdo.Get();
}
bool Finite(const FVector& p){return std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z);}
void* ObjectField(void* actor,const wchar_t* field){
    int32_t offset=-1;std::wstring type;
    if(!actor||!R::IsLive(actor)||!R::PropertyOffsetAndClass(R::ClassOf(actor),field,offset,type)||offset<0||type!=L"ObjectProperty")return nullptr;
    void* object=*reinterpret_cast<void**>(static_cast<uint8_t*>(actor)+offset);
    return object&&R::IsLive(object)?object:nullptr;
}
bool Parameter(void* fn,const wchar_t* name,int size){
    const int offset=R::FindParamOffset(fn,name),total=R::FunctionFrameSize(fn);
    return offset>=0&&R::FindParamSize(fn,name)==size&&offset<=total&&size<=total-offset;
}
}
bool TryReadLocalSight(void* walker,void* localPlayer,bool& seen){
    seen=false;
    if(!game_thread::IsGameThread()||!walker||!localPlayer||!R::IsLive(walker)||!R::IsLive(localPlayer))return false;
    void* world=world_identity::CurrentWorld();
    if(!world||world_identity::WorldOf(walker)!=world||world_identity::WorldOf(localPlayer)!=world)return false;
    CachedObjRef actor,player,mesh,controller;actor.Set(walker);player.Set(localPlayer);
    mesh.Set(ObjectField(walker,L"SkeletalMesh"));controller.Set(engine::GetController(localPlayer));
    if(!mesh.Get()||!controller.Get()||world_identity::WorldOf(controller.Get())!=world)return false;
    const auto current=[&]{return world_identity::CurrentWorld()==world&&actor.Get()&&player.Get()&&mesh.Get()&&controller.Get()&&ObjectField(walker,L"SkeletalMesh")==mesh.Get();};
    void* system=Library(L"KismetSystemLibrary",g_systemCdo,g_systemMiss);
    void* code=Library(L"bpCodeLib",g_codeCdo,g_codeMiss);
    if(!system||!code||!current())return false;
    CachedObjRef systemRef,codeRef;systemRef.Set(system);codeRef.Set(code);
    void* boundsFn=R::FindDispatchFunctionCached(R::ClassOf(system),L"GetComponentBounds");
    void* frustumFn=R::FindDispatchFunctionCached(R::ClassOf(code),L"isBoxInFrustum");
    void* renderedFn=R::FindDispatchFunctionCached(R::ClassOf(mesh.Get()),L"WasRecentlyRendered");
    if(!boundsFn||!frustumFn||!renderedFn||!current())return false;
    if(!Parameter(boundsFn,L"Component",sizeof(void*))||!Parameter(boundsFn,L"Origin",sizeof(FVector))||
       !Parameter(boundsFn,L"BoxExtent",sizeof(FVector))||!Parameter(renderedFn,L"Tolerance",sizeof(float))||
       !Parameter(renderedFn,L"ReturnValue",sizeof(bool)))return false;
    // The native bpCodeLib import takes (PlayerController, Origin, BoxExtent).
    // Use declaration-order metadata for its input names, with exact POD sizes
    // and frame bounds; these names are not hardcoded into the cooked call site.
    const auto fields=R::FunctionParams(frustumFn);
    const int sizes[]={sizeof(void*),sizeof(FVector),sizeof(FVector),sizeof(bool)};
    if(fields.size()!=4)return false;
    for(size_t i=0;i<fields.size();++i){
        if(!Parameter(frustumFn,fields[i].name.c_str(),sizes[i]))return false;
        const bool returns=(fields[i].flags&profile::cpf::ReturnParm)!=0;
        if(returns!=(i==3))return false;
        for(size_t j=0;j<i;++j)if(fields[i].offset<fields[j].offset+fields[j].size&&fields[j].offset<fields[i].offset+fields[i].size)return false;
    }
    ParamFrame bounds(boundsFn);
    if(!bounds.valid()||!bounds.Set<void*>(L"Component",mesh.Get())||!Call(system,bounds)||!systemRef.Get()||!codeRef.Get()||!current())return false;
    FVector origin{},extent{};
    if(!bounds.GetRaw(L"Origin",&origin,sizeof(origin))||!bounds.GetRaw(L"BoxExtent",&extent,sizeof(extent))||
       !Finite(origin)||!Finite(extent)||extent.X<0||extent.Y<0||extent.Z<0)return false;
    ParamFrame frustum(frustumFn);
    if(!frustum.valid()||!frustum.Set<void*>(fields[0].name.c_str(),controller.Get())||
       !frustum.Set(fields[1].name.c_str(),origin)||!frustum.Set(fields[2].name.c_str(),extent)||
       !Call(code,frustum)||!systemRef.Get()||!codeRef.Get()||!current())return false;
    bool inFrustum=false;
    if(!frustum.GetRaw(fields[3].name.c_str(),&inFrustum,sizeof(inFrustum)))return false;
    ParamFrame rendered(renderedFn);
    if(!rendered.valid()||!rendered.Set(L"Tolerance",3.f)||!Call(mesh.Get(),rendered)||!current())return false;
    bool recent=false;
    if(!rendered.GetRaw(L"ReturnValue",&recent,sizeof(recent)))return false;
    seen=inFrustum&&recent;
    static bool said=false;
    if(!said){said=true;UE_LOGI("mannequin_sight: native local-view sampling active (mesh bounds, actual frustum, recent render 3s)");}
    return true;
}
}
