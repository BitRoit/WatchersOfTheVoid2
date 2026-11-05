#include "coop/creatures/mannequin_target.h"
#include "coop/net/session.h"
#include "coop/player/local_prop_hold.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/prop_drive_host.h"
#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/ufunction_hook.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/facts/function_row.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace coop::mannequin_target {
namespace {
namespace R=ue_wrap::reflection;
namespace E=ue_wrap::engine;
namespace WI=ue_wrap::world_identity;
namespace UH=ue_wrap::ufunction_hook;
namespace SG=ue_wrap::script_gate;
UE_FACT_FN(kDistance, "mannequin_target", L"Actor", L"GetDistanceTo", Declared);
UE_FACT_FN(kMove, "mannequin_target", L"AIBlueprintHelperLibrary", L"CreateMoveToProxyObject", Declared);
std::atomic<net::Session*> g_session{nullptr};
ue_wrap::CachedObjRef g_moveFn,g_distanceFn;
bool g_attempted=false,g_ready=false,g_armed=false;
struct Candidate {
    uint8_t slot=players::kPeerIdUnknown;
    uint32_t generation=0;
    ue_wrap::CachedObjRef body;
    float distance=0;
};
struct Target {ue_wrap::CachedObjRef walker;Candidate chosen;};
std::unordered_map<void*,Target> g_targets;
struct Layout {int world=-1,pawn=-1,destination=-1,target=-1,radius=-1,overlap=-1;} g_layout;

bool OnHost(){auto* s=g_session.load();return s&&s->running()&&s->connected()&&s->role()==net::Role::Host;}
bool Finite(const ue_wrap::FVector& p){return std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z);}
bool Held(void* p){return local_prop_hold::IsHeld(p)||prop_drive_host::InAHand(p);}
float Distance(const ue_wrap::FVector& a,const ue_wrap::FVector& b){const float x=a.X-b.X,y=a.Y-b.Y,z=a.Z-b.Z;return std::sqrt(x*x+y*y+z*z);}
bool Angry(void* actor){int off=-1;uint8_t mask=0;return R::FindBoolProperty(R::ClassOf(actor),L"angry",off,mask)&&(*reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(actor)+off)&mask)!=0;}

Candidate Select(void* actor,Target& state){
    Candidate result;
    auto* s=g_session.load();void* world=WI::CurrentWorld();ue_wrap::FVector origin{};
    if(!OnHost()||!world||state.walker.Get()!=actor||WI::WorldOf(actor)!=world||!E::TryGetActorLocation(actor,origin)||!Finite(origin))return result;
    Candidate candidates[players::kMaxPeers];int n=0;
    auto add=[&](uint8_t slot,void* body,uint32_t gen,const ue_wrap::FVector& pos){
        if(!body||!R::IsLive(body)||WI::WorldOf(body)!=world||!Finite(pos))return;
        const float dist=Distance(origin,pos);if(!std::isfinite(dist))return;
        Candidate c;c.slot=slot;c.generation=gen;c.body.Set(body);c.distance=dist;candidates[n++]=c;
    };
    auto& registry=players::Registry::Get();
    void* local=registry.Local();float dead=1;
    if(local&&R::IsLive(local)&&ue_wrap::vitals::Read(ue_wrap::vitals::Field::Dead,&dead)&&dead==0){
        ue_wrap::FVector pos{};
        if(E::TryGetActorLocation(local,pos))add(players::kPeerIdHost,local,0,pos);
    }
    for(uint8_t slot=1;slot<players::kMaxPeers;++slot){
        if(!s->IsSlotConnected(slot)||!s->IsSlotReady(slot)||!s->IsSlotWorldReady(slot))continue;
        const uint32_t generation=s->peerGenerationForSlot(slot);
        if(!generation)continue;
        auto* rp=registry.Puppet(slot);
        if(!rp||!rp->valid()||!rp->HasPose()||!std::isfinite(rp->GetHealth())||rp->GetHealth()<=0)continue;
        void* body=rp->GetActor();ue_wrap::FVector pos{};
        if(body&&E::TryGetActorLocation(body,pos))add(slot,body,generation,pos);
    }
    if(!n){state.chosen=result;return result;}
    int nearest=0,current=-1;
    for(int i=0;i<n;++i){
        if(candidates[i].distance<candidates[nearest].distance)nearest=i;
        if(candidates[i].slot==state.chosen.slot&&candidates[i].generation==state.chosen.generation&&state.chosen.body.Is(candidates[i].body.Raw()))current=i;
    }
    int pick=nearest;
    // Nearest is the preference, not a demand to swap victims at every noisy tie.
    // Keep an eligible angry target; otherwise a challenger must be 20% AND 2m closer.
    if(current>=0&&(Angry(actor)||!(candidates[nearest].distance<candidates[current].distance*.8f&&candidates[current].distance-candidates[nearest].distance>200.f)))pick=current;
    result=candidates[pick];
    if(result.slot!=state.chosen.slot||!state.chosen.body.Is(result.body.Raw())||result.generation!=state.chosen.generation)
        UE_LOGI("mannequin_target: walker=%p targets slot=%u distance=%.0fcm",actor,result.slot,result.distance);
    state.chosen=result;return result;
}

Target* Source(void* actor){
    if(!ue_wrap::game_thread::IsGameThread()||!g_ready||!OnHost())return nullptr;
    auto it=g_targets.find(actor);if(it==g_targets.end()||it->second.walker.Get()!=actor||Held(actor))return nullptr;
    const auto frame=UH::CurrentCallerFrame();
    if(!frame.function||!R::NameEquals(R::NameOf(frame.function),L"ExecuteUbergraph_prop_wMannequin"))return nullptr;
    return &it->second;
}

void OnDistance(void* context,void* source,void*){
    Target* state=Source(source);if(!state||!context||!R::IsLive(context))return;
    bool camera=false;
    for(void* cls=R::ClassOf(context);cls;cls=R::SuperStructOf(cls))if(R::NameEquals(R::NameOf(cls),L"PlayerCameraManager")){camera=true;break;}
    if(!camera)return;
    float* out=static_cast<float*>(UH::CurrentResult());if(!out)return;
    const Candidate chosen=Select(source,*state);
    // No living player must not silently turn into a dead-host fallback.
    if(chosen.slot==players::kPeerIdUnknown)*out=1.0e8f;
    else if(chosen.slot!=players::kPeerIdHost)*out=chosen.distance;
}

template<class T>T Read(const uint8_t* p,int offset){T result{};std::memcpy(&result,p+offset,sizeof(T));return result;}
SG::Verdict OnMove(void* context,void* source,const uint8_t* parms){
    Target* state=Source(source);void** out=static_cast<void**>(UH::CurrentResult());
    if(!state||!out||!parms||!context)return SG::Verdict::Run;
    const Candidate chosen=Select(source,*state);
    if(chosen.slot==players::kPeerIdUnknown){*out=nullptr;return SG::Verdict::Cancel;}
    if(chosen.slot==players::kPeerIdHost)return SG::Verdict::Run;
    void* target=chosen.body.Get();void* fn=g_moveFn.Get();
    void* pawn=Read<void*>(parms,g_layout.pawn);
    if(!target||!fn||!pawn||!R::IsLive(pawn)||WI::WorldOf(pawn)!=WI::CurrentWorld())return SG::Verdict::Run;
    ue_wrap::ParamFrame call(fn);
    if(!call.valid()||!call.Set<void*>(L"WorldContextObject",Read<void*>(parms,g_layout.world))||
       !call.Set<void*>(L"Pawn",pawn)||!call.Set(L"Destination",Read<ue_wrap::FVector>(parms,g_layout.destination))||
       !call.Set<void*>(L"TargetActor",target)||!call.Set(L"AcceptanceRadius",Read<float>(parms,g_layout.radius))||
       !call.Set(L"bStopOnOverlap",Read<bool>(parms,g_layout.overlap)))return SG::Verdict::Run;
    // The native-hook recursion guard forwards this one replacement directly.
    // Return its real proxy so native completion/path delegates still bind to it.
    *out=nullptr;
    if(ue_wrap::Call(context,call))call.GetRaw(L"ReturnValue",out,sizeof(void*));
    // A dispatch failure may follow partial effects. Never issue a second move.
    return SG::Verdict::Cancel;
}

bool Offset(void* fn,const wchar_t* name,int size,int& offset){offset=R::FindParamOffset(fn,name);const int total=R::FunctionFrameSize(fn);return offset>=0&&R::FindParamSize(fn,name)==size&&offset<=total&&size<=total-offset;}
void Arm(bool want){
    if(!g_ready||want==g_armed)return;
    const bool a=UH::SetArmed(g_distanceFn.Raw(),&OnDistance,want);
    const bool b=UH::SetArmed(g_moveFn.Raw(),&OnMove,want);
    if(a&&b){g_armed=want;return;}
    UH::SetArmed(g_distanceFn.Raw(),&OnDistance,false);UH::SetArmed(g_moveFn.Raw(),&OnMove,false);g_armed=false;g_ready=false;
    UE_LOGE("mannequin_target: native target hooks could not arm together -- native targeting retained");
}
} // namespace

void Install(net::Session* session){
    g_session.store(session);
    if(!OnHost()||g_attempted)return;
    const auto distance=ue_wrap::facts::Resolve(kDistance),move=ue_wrap::facts::Resolve(kMove);
    if(!distance||!move)return;
    g_attempted=true;
    void* fn=move.Fn();
    const bool layout=Offset(fn,L"WorldContextObject",sizeof(void*),g_layout.world)&&Offset(fn,L"Pawn",sizeof(void*),g_layout.pawn)&&
        Offset(fn,L"Destination",sizeof(ue_wrap::FVector),g_layout.destination)&&Offset(fn,L"TargetActor",sizeof(void*),g_layout.target)&&
        Offset(fn,L"AcceptanceRadius",sizeof(float),g_layout.radius)&&Offset(fn,L"bStopOnOverlap",sizeof(bool),g_layout.overlap)&&
        R::FindParamSize(fn,L"ReturnValue")==sizeof(void*)&&R::FindParamSize(distance.Fn(),L"ReturnValue")==sizeof(float);
    if(!layout){UE_LOGE("mannequin_target: native navigation signature mismatch -- targeting disabled");return;}
    g_moveFn.Set(fn);g_distanceFn.Set(distance.Fn());
    const bool a=UH::InstallPostHook(distance,&OnDistance,false);
    const bool b=UH::InstallReturningPreHook(move,&OnMove,sizeof(void*),false);
    g_ready=a&&b;
    if(!g_ready)UE_LOGE("mannequin_target: both native hooks are required -- targeting disabled");
}
void Track(void* walker){
    if(!OnHost()||!walker||!R::IsLive(walker)||WI::WorldOf(walker)!=WI::CurrentWorld())return;
    auto& state=g_targets[walker];if(!state.walker.Is(walker)){state=Target{};state.walker.Set(walker);}
}
void Tick(){
    void* world=WI::CurrentWorld();
    for(auto it=g_targets.begin();it!=g_targets.end();){
        if(!OnHost()||!world||!it->second.walker.Alive()||it->second.walker.StampedWorld()!=world)it=g_targets.erase(it);else ++it;
    }
    Arm(OnHost()&&!g_targets.empty());
}
void OnDisconnect(){
    g_session.store(nullptr);g_targets.clear();Arm(false);
}
} // namespace coop::mannequin_target
