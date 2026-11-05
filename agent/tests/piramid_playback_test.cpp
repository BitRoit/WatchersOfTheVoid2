#include "coop/net/protocol.h"
#include <algorithm>
#include <any>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
struct Object { void* target=nullptr; void* mesh=nullptr; void* anim=nullptr; void* timeline[3]{}; int generation=1; bool live=true; bool gathering=false,walking=false,consumed=false; float position=0,rate=1; bool playing=true; } actor,mesh,anim,montage,timeline[3],wisp;
int plays=0,deletes=0,completed=0,calls=0,notifies=0,reached=0; long long now=1000;
std::function<void(const std::wstring&)> fault;
namespace R {
bool IsLive(void* p){return p && static_cast<Object*>(p)->live;}
bool IsLiveByIndex(void* p,int){return IsLive(p);}
void* ClassOf(void* p){return p;}
int FindPropertyOffset(void*,const wchar_t* n){std::wstring s(n);if(s==L"mesh")return offsetof(Object,mesh);if(s.substr(0,9)==L"Timeline_")return offsetof(Object,timeline)+sizeof(void*)*(s.back()-L'0');return -1;}
bool FindBoolProperty(void*,const wchar_t*,int& o,uint8_t& m){o=offsetof(Object,consumed);m=1;return true;}
const wchar_t* FindDispatchFunctionCached(void*,const wchar_t* n){return n;}
void* FindObject(const wchar_t*,const wchar_t*){return &montage;}
}
namespace ue_wrap {
struct FVector {float X=0,Y=0,Z=0;};
struct CachedObjRef {Object* p=nullptr;int gen=0;void Set(void* v){p=static_cast<Object*>(v);gen=p?p->generation:0;}void Reset(){p=nullptr;}void* Get()const{return p&&p->live&&p->generation==gen?p:nullptr;}bool Is(void* v)const{return Get()==v;}};
namespace fname_utils {std::wstring StringToFName(const wchar_t* n){return n;}}
struct ParamFrame { std::wstring name;std::unordered_map<std::wstring,std::any> args; explicit ParamFrame(const wchar_t* n):name(n?n:L""){}bool valid(){return !name.empty();}template<class T>bool Set(const wchar_t* n,T v){args[n]=v;return true;}template<class T>T Get(const wchar_t* n){return std::any_cast<T>(args[n]);}bool GetRaw(const wchar_t* n,void* p,size_t z){auto& v=args[n];if(v.type()==typeid(void*)) *static_cast<void**>(p)=std::any_cast<void*>(v);else if(z==sizeof(bool))*static_cast<bool*>(p)=std::any_cast<bool>(v);else *static_cast<float*>(p)=std::any_cast<float>(v);return true;}};
bool Call(void* v,ParamFrame& f){if(!R::IsLive(v))return false;++calls;auto* o=static_cast<Object*>(v);auto n=f.name;
 if(n==L"GetAnimInstance")f.args[L"ReturnValue"]=o->anim;
 else if(n==L"GetCurrentActiveMontage")f.args[L"ReturnValue"]=static_cast<void*>(&montage);
 else if(n==L"Montage_Play"){++plays;anim.rate=f.Get<float>(L"InPlayRate");anim.playing=true;f.args[L"ReturnValue"]=10.f;}
 else if(n==L"Montage_SetPlayRate")anim.rate=f.Get<float>(L"NewPlayRate");
 else if(n==L"Montage_SetPosition")anim.position=f.Get<float>(L"NewPosition");
 else if(n==L"Montage_IsPlaying")f.args[L"ReturnValue"]=anim.playing;
 else if(n==L"SetPlaybackPosition")o->position=f.Get<float>(L"NewPosition");
 else if(n==L"Play"||n==L"Reverse")o->playing=true;
 else if(n==L"Stop")o->playing=false;
 else if(n==L"checkIfReached"){++reached;actor.gathering=true;}
 else if(n.find(L"OnNotifyBegin_")==0){++notifies;auto s=f.Get<std::wstring>(L"NotifyName");if(s==L"del"){if(actor.target)++deletes;actor.target=nullptr;actor.consumed=true;}}
 else if(n.find(L"OnCompleted_")==0){++completed;actor.gathering=false;}
 if(fault)fault(n);return true;
}
}
namespace coop::net {enum class Role{Host,Client};}
#define UE_LOGW(...) ((void)0)
#define UE_LOGI(...) ((void)0)
namespace E {bool TryGetActorLocation(void*,ue_wrap::FVector& v){v={};return true;}void SetActorTickEnabled(void*,bool){}}
namespace coop::element {
struct WorldActor {bool IsMirror(){return true;}void* GetActor(){return &actor;}int GetInternalIdx(){return 0;}};
struct Npc {void* GetActor(){return &wisp;}int GetInternalIdx(){return 0;}};
struct WaTable {WorldActor value;WorldActor* Get(uint32_t id){return id==1?&value:nullptr;}} wa;
struct NpcTable {Npc value;Npc* Get(uint32_t id){return id==2?&value:nullptr;}} npcs;
WaTable& WaMirrors(){return wa;}NpcTable& NpcMirrors(){return npcs;}
}
namespace coop::piramid_sync {
struct Session {bool online=true;bool connected(){return online;} net::Role role(){return net::Role::Client;}} session;
Session* LoadSession(){return &session;}long long NowMs(){return now;}
int g_offWispTarget=offsetof(Object,target),g_gatheringOff=offsetof(Object,gathering),g_isWalkingOff=offsetof(Object,walking);uint8_t g_gatheringMask=1,g_isWalkingMask=1;
void WriteBoolAt(void* v,int o,uint8_t,bool b){*reinterpret_cast<bool*>(static_cast<char*>(v)+o)=b;}
bool ReadBoolAt(void* v,int o,uint8_t){return *reinterpret_cast<bool*>(static_cast<char*>(v)+o);}
#include "piramid_pending.inc"
PendingGather g_pending;
constexpr float kReplayAttemptRadius=1000;constexpr long long kReplayDeadlineMs=5000;
const wchar_t* g_fnCheckIfReached=L"checkIfReached";bool t_allowCheckIfReached=false;
ue_wrap::CachedObjRef g_tailActor,g_tailMontage,g_tailAnimation;
#include "piramid_playback.inc"
}
void Check(bool b,const char* s){if(!b){printf("FAIL %s\n",s);exit(1);}}
void Reset(){actor=Object{};mesh=Object{};anim=Object{};montage=Object{};for(auto& t:timeline)t=Object{};actor.mesh=&mesh;mesh.anim=&anim;for(int i=0;i<3;++i)actor.timeline[i]=&timeline[i];actor.target=&wisp;plays=deletes=completed=calls=notifies=reached=0;fault={};coop::piramid_sync::g_tailActor.Reset();coop::piramid_sync::g_tailMontage.Reset();coop::piramid_sync::g_tailAnimation.Reset();coop::piramid_sync::session.online=true;}
int main(){using namespace coop::piramid_sync;PendingGather p{};p.receivedMs=0;p.state.playRate=2;p.state.montagePosition=3;p.state.timelinePosition[0]=.5f;p.state.timelineFlags=1;
 Reset();Check(RestoreGatherState(&actor,p,false),"native active restore");Check(anim.rate==2&&anim.position==5,"rate and elapsed position applied to actual playback calls");Check(timeline[0].position==1.5f,"timeline elapsed seek");
 Reset();Check(RestoreGatherState(&actor,p,true),"consumed tail restore");Check(!deletes&&!actor.target&&actor.consumed,"tail never destroys stale target");anim.playing=false;TickGatherTail();TickGatherTail();Check(completed==1&&!actor.gathering,"tail completion once");
 Reset();actor.timeline[1]=nullptr;Check(!RestoreGatherState(&actor,p,true)&&!plays&&!deletes,"missing component preflight before native effects");
 Reset();fault=[](auto n){if(n==L"Montage_SetPlayRate")++timeline[1].generation;};Check(!RestoreGatherState(&actor,p,false)&&anim.position==0,"child recycle cancels remaining seek");
 Reset();fault=[](auto n){if(n==L"Montage_SetPlayRate")actor.timeline[1]=&wisp;};Check(!RestoreGatherState(&actor,p,false)&&anim.position==0,"replaced live child cancels remaining seek");
 Reset();fault=[](auto n){if(n==L"Montage_SetPlayRate")mesh.anim=&wisp;};Check(!RestoreGatherState(&actor,p,false)&&anim.position==0,"still-live animation replacement cancels remaining seek");
 Reset();int animationQueries=0;fault=[&](auto n){if(n==L"GetAnimInstance"&&++animationQueries==2)session.online=false;};Check(!RestoreGatherState(&actor,p,true)&&!plays&&anim.position==0&&anim.rate==1&&!notifies,"disconnect during binding query cancels native effects");
 Reset();fault=[](auto n){if(n==L"Montage_SetPlayRate")session.online=false;};Check(!RestoreGatherState(&actor,p,false)&&anim.position==0,"disconnect cancels remaining seek");
 Reset();Check(RestoreGatherState(&actor,p,true),"tail setup for recycle");anim.playing=false;fault=[](auto n){if(n==L"Montage_IsPlaying")++anim.generation;};TickGatherTail();Check(!completed,"reentrant recycled animation cannot complete actor");
 Reset();Check(RestoreGatherState(&actor,p,true),"tail setup for between-tick recycle");++anim.generation;anim.playing=false;TickGatherTail();Check(!completed,"tail animation incarnation retained across ticks");
 Reset();g_pending=p;g_pending.active=g_pending.hasState=true;g_pending.pyramidEid=1;g_pending.wispEid=2;g_pending.deadlineMs=5000;actor.gathering=true;TryReplayPendingGather();Check(!g_pending.active&&anim.position==5&&!notifies&&!reached,"matching active gather seeks without repeated notifies or reached-check");
 Reset();g_pending=p;g_pending.active=g_pending.hasState=true;g_pending.pyramidEid=1;g_pending.wispEid=2;g_pending.deadlineMs=5000;actor.gathering=true;actor.target=&montage;TryReplayPendingGather();Check(g_pending.active&&anim.position==0&&!notifies&&!reached,"different active gather retains pending without guessing");
 Reset();g_pending=p;g_pending.active=true;g_pending.pyramidEid=1;g_pending.wispEid=2;g_pending.deadlineMs=5000;actor.gathering=true;TryReplayPendingGather();Check(!g_pending.active&&anim.position==0&&!notifies&&!reached,"legacy duplicate retains native behavior");
 Reset();g_pending=p;g_pending.active=g_pending.hasState=true;g_pending.state.consumed=1;g_pending.pyramidEid=1;g_pending.deadlineMs=5000;TryReplayPendingGather();Check(plays==1&&notifies==1,"first consumed dispatch starts native tail once");g_pending=p;g_pending.active=g_pending.hasState=true;g_pending.state.consumed=1;g_pending.state.montagePosition=4;g_pending.pyramidEid=1;g_pending.deadlineMs=5000;TryReplayPendingGather();Check(!g_pending.active&&plays==1&&notifies==1&&anim.position==6,"duplicate bound tail seeks without play or del");
 ++anim.generation;g_pending=p;g_pending.active=g_pending.hasState=true;g_pending.state.consumed=1;g_pending.pyramidEid=1;g_pending.deadlineMs=5000;TryReplayPendingGather();Check(plays==2&&notifies==2,"new animation incarnation starts distinct tail");
 puts("PASS production Pyramid playback: native rate/seek, consumed tail, missing component, child/actor recycle, disconnect, completion");
}
