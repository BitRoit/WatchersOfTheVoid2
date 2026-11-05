#include "coop/creatures/mannequin_interaction.h"
#include "coop/net/session.h"
#include "coop/element/registry.h"
#include "coop/player/players_registry.h"
#include "coop/props/prop_drive_stream.h"
#include "coop/props/prop_park.h"
#include "coop/props/remote_prop.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/ufunction_hook.h"
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/facts/function_row.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <unordered_map>

namespace coop::mannequin_interaction {
namespace {
namespace R=ue_wrap::reflection;
namespace H=ue_wrap::ufunction_hook;
namespace WI=ue_wrap::world_identity;
namespace P=ue_wrap::prop;
using Verdict=ue_wrap::script_gate::Verdict;
UE_FACT_FN(kQuery,"mannequin_interaction",L"PrimitiveComponent",L"IsSimulatingPhysics",Dispatch);
UE_FACT_FN(kGrab,"mannequin_interaction",L"PhysicsHandleComponent",L"GrabComponentAtLocation",Declared);
UE_FACT_FN(kGrabRotation,"mannequin_interaction",L"PhysicsHandleComponent",L"GrabComponentAtLocationWithRotation",Declared);
UE_FACT_FN(kConstraint,"mannequin_interaction",L"PhysicsConstraintComponent",L"SetConstrainedComponents",Declared);
net::Session* g_session=nullptr;
ue_wrap::CachedObjRef g_player;
uint32_t g_worldGeneration=0;
uint64_t g_stopCount=0,g_retryAt=0;
bool g_query=false,g_grab=false,g_rotation=false,g_constraint=false;
std::unordered_map<void*,std::array<int,2>> g_components;
bool Client(){
    return ue_wrap::game_thread::IsGameThread()&&!R::InCoopDispatch()&&g_session&&g_session->running()&&g_session->connected()&&
           g_session->role()==net::Role::Client&&g_session->StopCount()==g_stopCount&&
           WI::Generation()==g_worldGeneration&&net_pump::IsWorldSettled()&&g_player.Get();
}
bool Walker(void* actor){
    if(!actor||!R::IsLive(actor)||WI::WorldOf(actor)!=WI::CurrentWorld())return false;
    for(void* cls=R::ClassOf(actor);cls;cls=R::SuperStructOf(cls))
        if(R::NameEquals(R::NameOf(cls),L"prop_wMannequin_C"))return true;
    return false;
}
void* Eligible(void* component){
    if(!Client()||!component||!R::IsLive(component))return nullptr;
    void* actor=R::OuterOf(component);
    if(!Walker(actor)||P::GetStaticMesh(actor)!=component||P::IsFrozen(actor)||P::IsStatic(actor)||
       !prop_drive_stream::IsParked(actor)||remote_prop::IsActorUnderAnyDrive(actor))return nullptr;
    auto& registry=element::Registry::Get();const auto eid=registry.EidForActor(actor);auto* entity=registry.Get(eid);
    if(!element::Registry::IsAllowedHostAllocatedEid(eid)||!entity||!entity->IsMirror()||
       entity->GetType()!=element::ElementType::Prop||entity->LiveActor()!=actor)return nullptr;
    return actor;
}
bool PlayerCall(void* source,void* function){
    if(source!=g_player.Get()||!function)return false;
    const auto name=R::NameOf(function);
    return R::NameEquals(name,L"ExecuteUbergraph_mainPlayer")||R::NameEquals(name,L"useAction")||
           R::NameEquals(name,L"buildActionList")||R::NameEquals(name,L"useSelectedAction")||
           R::NameEquals(name,L"pickupObject");
}
void Query(void* component,void* source,void*){
    void* actor=Eligible(component);if(!actor)return;
    const auto frame=H::CurrentCallerFrame();
    // Aprop.lookAt's inherited bytecode and the local player's actual native
    // use/grab graph must see the movable body represented by this network park.
    // Other physics readers still observe the real kinematic state.
    const bool propHover=source==actor&&frame.function&&R::NameEquals(R::NameOf(frame.function),L"lookAt");
    if(!propHover&&!PlayerCall(source,frame.function))return;
    if(void* result=H::CurrentResult())*static_cast<bool*>(result)=true;
}
Verdict Grab(void* context,void* source,const uint8_t* parms){
    if(!Client()||!context||!parms||!PlayerCall(source,H::CurrentCallerFrame().function)||R::OuterOf(context)!=g_player.Get())return Verdict::Run;
    const auto it=g_components.find(H::CurrentNativeFunction());if(it==g_components.end())return Verdict::Run;
    for(int offset:it->second){
        if(offset<0)continue;void* component=nullptr;std::memcpy(&component,parms+offset,sizeof(component));
        if(void* actor=Eligible(component)){
            // Admission alone is insufficient: native physics handles require a
            // simulating body. Wake only at the real grab, never while hovering.
            // The existing held-prop stream yields this generation to the hand.
            prop_park::Unpark(actor,component,true);
            UE_LOGI("mannequin interaction: local native grab admitted actor=%p",actor);
        }
    }
    return Verdict::Run;
}
bool InputOffset(void* fn,const wchar_t* name,int& offset){
    offset=R::FindParamOffset(fn,name);const int size=R::FindParamSize(fn,name),total=R::FunctionFrameSize(fn);
    return offset>=0&&size==sizeof(void*)&&offset<=total&&size<=total-offset;
}
bool HookGrab(const ue_wrap::facts::Function& row,bool constraint){
    const auto fn=ue_wrap::facts::Resolve(row);if(!fn)return false;
    std::array<int,2> offsets{-1,-1};
    if(!InputOffset(fn.Fn(),constraint?L"Component1":L"Component",offsets[0])||
       (constraint&&!InputOffset(fn.Fn(),L"Component2",offsets[1])))return false;
    if(!H::InstallPreHook(fn,&Grab,true))return false;
    g_components[fn.Fn()]=offsets;return true;
}
} // namespace
void Tick(net::Session& session){
    g_session=&session;g_stopCount=session.StopCount();g_worldGeneration=WI::Generation();
    void* player=players::Registry::Get().Local();if(player&&R::IsLive(player))g_player.Set(player);else g_player.Reset();
    if(!Client()||(g_query&&g_grab&&g_rotation&&g_constraint)||GetTickCount64()<g_retryAt)return;
    g_retryAt=GetTickCount64()+2000;
    if(!g_query)g_query=H::InstallPostHook(ue_wrap::facts::Resolve(kQuery),&Query);
    if(!g_grab)g_grab=HookGrab(kGrab,false);
    if(!g_rotation)g_rotation=HookGrab(kGrabRotation,false);
    if(!g_constraint)g_constraint=HookGrab(kConstraint,true);
    UE_LOGI("mannequin interaction: native admission seams query=%u grab=%u rotation=%u constraint=%u",unsigned(g_query),unsigned(g_grab),unsigned(g_rotation),unsigned(g_constraint));
}
void OnDisconnect(){g_session=nullptr;g_player.Reset();g_retryAt=0;}
}
