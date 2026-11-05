// Production controller with mocked engine/roster boundaries; no rendered game claim.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#define UE_LOGI(...) ((void)0)
namespace ue_wrap {
struct FVector{float X=0,Y=0,Z=0;};struct FRotator{float Pitch=0,Yaw=0,Roll=0;};
namespace reflection {struct FName{uint32_t ComparisonIndex=0,Number=0;};}
}
namespace test {
struct Obj{Obj* cls=nullptr;const wchar_t* name=L"object";void* world=nullptr;ue_wrap::FVector pos{};bool looking=false,saw=false,stand=true;Obj* anim=nullptr;ue_wrap::reflection::FName key{};bool held=false,remoteHeld=false,angry=false;};
void* world=reinterpret_cast<void*>(1);uint64_t now=1000;int traceResult=0,staleReads=0;bool headOnly=false;
ue_wrap::FVector camera{0,0,60};ue_wrap::FRotator rotation{0,180,0};
std::unordered_map<void*,int> serials;std::vector<Obj*> walkers;
Obj walkerClass,regularClass,animClass,body,animation,viewer;
bool Live(void* p){return p&&serials.contains(p);}
bool nativeHostVisible=false;uint64_t sawUntil=0,invocation=0;int timerStarts=0,nativeCalls=0,damping=-1;
bool remoteSightKnown=false,remoteSight=false,hostStandKnown=false,hostStand=true;
bool nativeSightReadable=false,nativeSight=false,lastReportedSight=false,lastPublishedStand=true;
int reports=0,published=0;
}
uint64_t GetTickCount64(){return test::now;}
namespace ue_wrap::reflection {
bool IsLive(void* p){if(!test::Live(p))++test::staleReads;return test::Live(p);}
void* ClassOf(void* p){return static_cast<test::Obj*>(p)->cls;}
void* SuperStructOf(void*){return nullptr;}
const wchar_t* NameOf(void* p){return static_cast<test::Obj*>(p)->name;}
bool NameEquals(const wchar_t* a,const wchar_t* b){return std::wcscmp(a,b)==0;}
bool NameStartsWith(const wchar_t* a,const wchar_t* b){return std::wcsncmp(a,b,std::wcslen(b))==0;}
std::wstring ClassNameOf(void* p){return NameOf(ClassOf(p));}
bool FindBoolProperty(void*,const wchar_t* name,int32_t& off,uint8_t& mask){
 if(!std::wcscmp(name,L"looking"))off=offsetof(test::Obj,looking);
 else if(!std::wcscmp(name,L"saw"))off=offsetof(test::Obj,saw);
 else if(!std::wcscmp(name,L"stand"))off=offsetof(test::Obj,stand);
 else if(!std::wcscmp(name,L"angry"))off=offsetof(test::Obj,angry);else return false;
 mask=1;return true;
}
int32_t FindPropertyOffset(void*,const wchar_t* name){return !std::wcscmp(name,L"anim")?offsetof(test::Obj,anim):-1;}
}
namespace ue_wrap {
class CachedObjRef{void* p_=nullptr;int serial_=0;void* world_=nullptr;public:
 void Set(void* p){p_=p;serial_=test::serials.at(p);world_=static_cast<test::Obj*>(p)->world;}
 bool Is(void* p)const{auto it=test::serials.find(p);return p==p_&&it!=test::serials.end()&&it->second==serial_;}
 bool Alive()const{return Is(p_)&&(!world_||world_==test::world);}
 void* Get()const{return Alive()?p_:nullptr;}
 void* Raw()const{return p_;}void* StampedWorld()const{return world_;}void Reset(){p_=nullptr;serial_=0;world_=nullptr;}
};
namespace world_identity{void* CurrentWorld(){return test::world;}void* WorldOf(void* p){return static_cast<test::Obj*>(p)->world;}}
namespace object_index {
void* ClassByName(const wchar_t*){return &test::walkerClass;}
template<class Fn>void ForEachInstance(void*,Fn fn,void* ctx){for(auto* p:test::walkers)fn(ctx,p,0);}
}
namespace engine {
bool TryGetActorLocation(void* p,FVector& out){out=static_cast<test::Obj*>(p)->pos;return true;}
FVector GetCameraLocation(){return test::camera;}FRotator GetCameraRotation(){return test::rotation;}
}
namespace trace{int LineBlockedStatDyn(void*,const FVector&,const FVector& end){return test::headOnly?(end.Z==160?0:1):test::traceResult;}}
namespace mannequin{bool TryReadLocalSight(void*,void*,bool& seen){seen=test::nativeSight;return test::nativeSightReadable;}}
namespace prop{std::wstring GetInteractableKeyString(void*){return L"test-key";}}
namespace fname_utils{reflection::FName StringToFName(const wchar_t*){return {1,0};}}
namespace profile::off{constexpr size_t Aprop_Key=offsetof(test::Obj,key);}
namespace script_gate {
enum class Verdict{Run,Cancel};struct Call{void* object=nullptr;uint64_t invocation=0;};using PreFn=Verdict(*)(const Call&);using PostFn=void(*)(const Call&);
std::vector<std::wstring> watches;
std::unordered_set<uint64_t> active;
bool IsInvocationActive(uint64_t id){return active.contains(id);}
bool WatchClassName(const wchar_t*,const wchar_t* name,uint32_t,PreFn,PostFn){watches.emplace_back(name);return true;}
}
}
namespace coop::net {enum class Role{Host,Client};struct Session{Role role_=Role::Host;bool live=true,ready=true,slotConnected=true,slotReady=true;uint32_t generation=1;bool running(){return live;}bool connected(){return live;}Role role(){return role_;}bool IsSlotConnected(int){return slotConnected;}bool IsSlotReady(int){return slotReady;}bool IsSlotWorldReady(int){return ready;}uint32_t peerGenerationForSlot(int){return generation;}};}
namespace coop {
struct RemotePlayer{void* actor=&test::viewer;bool posed=true;float health=1;ue_wrap::FVector eye{0,0,60},aim{1,0,0};bool valid(){return test::Live(actor);}bool HasPose(){return posed;}void* GetActor(){return actor;}float GetHealth(){return health;}bool TryGetEyePosition(ue_wrap::FVector& out){out=eye;return true;}ue_wrap::FVector GetSyncedAimDirection(){return aim;}};
namespace players {constexpr int kMaxPeers=3;constexpr uint8_t kPeerIdUnknown=0xff,kPeerIdHost=0;struct Registry{RemotePlayer* peer=nullptr;static Registry& Get(){static Registry r;return r;}RemotePlayer* Puppet(int slot){return slot==1?peer:nullptr;}void* Local(){return &test::viewer;}};}
namespace local_prop_hold {bool IsHeld(void* p){return static_cast<test::Obj*>(p)->held;}}
namespace prop_drive_host {bool InAHand(void* p){return static_cast<test::Obj*>(p)->remoteHeld;}void Claim(void*,const char*){}}
namespace prop_synth_key {std::wstring EnsureKeyForBroadcast(void*,const std::wstring&,bool){return L"test-key";}}
namespace prop_element_tracker {enum class EnrollSource{kExpressSeam};void MarkProcessedInit(void*){}void MarkPropElement(void*,const std::wstring&,const std::wstring&,EnrollSource){}void TrackActorKey(void*,const std::wstring&){} }
namespace prop_snapshot {void ExpressIncrementalSpawn(void*){}}
namespace mannequin_sight_sync {
void Install(net::Session*){}void Tick(net::Session&){}
void ReportLocalSight(void*,bool seen){++test::reports;test::lastReportedSight=seen;}
bool TryGetRemoteSight(uint8_t,void*,bool& seen){seen=test::remoteSight;return test::remoteSightKnown;}
void PublishHostStand(void*,bool stand){++test::published;test::lastPublishedStand=stand;}
bool TryGetHostStand(void*,bool& stand){stand=test::hostStand;return test::hostStandKnown;}
void OnDisconnect(){}
}
namespace mannequin_interaction {void Tick(net::Session&){}void OnDisconnect(){}}
namespace mannequin_identity {void Tick(net::Session&,std::vector<void*>&){}void OnDisconnect(){}}
namespace mannequin_target {
#ifdef MANNEQUIN_TARGET_TEST
void Install(net::Session*);void Track(void*);void Tick();void OnDisconnect();
#else
void Install(net::Session*){}void Track(void*){}void Tick(){}void OnDisconnect(){}
#endif
}
}
#include "mannequin_controller.inc"
#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d %s\n",__func__,__LINE__,#x);return false;}}while(0)
namespace M=coop::mannequin_sync;
void Reset(){
 M::OnDisconnect();test::serials.clear();test::walkers.clear();test::now=1000;test::traceResult=0;test::headOnly=false;test::staleReads=0;test::rotation={0,180,0};
 test::nativeHostVisible=false;test::sawUntil=0;test::timerStarts=test::nativeCalls=0;test::damping=-1;ue_wrap::script_gate::active.clear();
 test::remoteSightKnown=test::remoteSight=test::hostStandKnown=test::nativeSightReadable=test::nativeSight=false;
 test::hostStand=true;test::reports=test::published=0;
 test::body={};test::animation={};test::viewer={};test::walkerClass.name=L"prop_wMannequin_C";test::regularClass.name=L"prop_mannequin_C";
 test::body.cls=&test::walkerClass;test::body.world=test::world;test::body.anim=&test::animation;test::body.pos={1000,0,0};test::animation.cls=&test::animClass;test::animation.world=test::world;test::viewer.world=test::world;
 for(void* p:{&test::body,&test::animation,&test::viewer,&test::walkerClass,&test::regularClass,&test::animClass})test::serials[p]=1;
 test::walkers.push_back(&test::body);coop::players::Registry::Get().peer=nullptr;
}
// Native transition model from the cooked graph: only a looking edge updates
// damping/stand; entering seen starts timer_saw at a random 10-20s. Use 15s here.
// It intentionally omits pathfinding/forces, which need rendered game testing.
void NativeStep(coop::net::Session& s){
 if(test::sawUntil&&test::now>=test::sawUntil){test::body.saw=false;test::sawUntil=0;}
 ue_wrap::script_gate::Call call{&test::body,++test::invocation};ue_wrap::script_gate::active.insert(call.invocation);
 if(M::OnClientWalkerTick(call)==ue_wrap::script_gate::Verdict::Run){
  ++test::nativeCalls;
  const bool seen=(test::nativeHostVisible||test::body.saw)&&!test::body.angry;
  if(seen!=test::body.looking){test::body.looking=seen;test::animation.stand=seen;test::damping=seen?0:40;if(seen){test::body.saw=true;test::sawUntil=test::now+15000;++test::timerStarts;}}
  M::OnWalkerTickPost(call);
 }
 ue_wrap::script_gate::active.erase(call.invocation);M::Tick(s);
}
bool GazeAndDisconnect(){
 Reset();coop::net::Session host;M::Install(&host);coop::RemotePlayer peer;auto& r=coop::players::Registry::Get();
 NativeStep(host);CHECK(!test::body.looking&&!test::body.saw);
 r.peer=&peer;NativeStep(host);CHECK(test::body.looking&&test::body.saw&&test::animation.stand);CHECK(test::timerStarts==1&&test::damping==0);
 peer.aim={-1,0,0};test::now+=1000;NativeStep(host);CHECK(test::body.looking&&test::body.saw);CHECK(test::timerStarts==1);
 test::now+=15000;NativeStep(host);CHECK(!test::body.looking&&!test::body.saw&&test::damping==40);
 peer.aim={1,0,0};NativeStep(host);CHECK(test::body.looking&&test::timerStarts==2);
 r.peer=nullptr;test::now+=1000;NativeStep(host);CHECK(test::body.looking); // native hesitation survives disconnect
 test::now+=15000;NativeStep(host);CHECK(!test::body.looking&&!test::body.saw);
 test::nativeHostVisible=true;NativeStep(host);CHECK(test::body.looking&&test::timerStarts==3);
 test::nativeHostVisible=false;test::now+=16000;NativeStep(host);CHECK(!test::body.looking);
 r.peer=&peer;test::traceResult=1;NativeStep(host);CHECK(!test::body.looking);
 test::traceResult=-1;NativeStep(host);CHECK(!test::body.looking);
 test::headOnly=true;NativeStep(host);CHECK(test::body.looking);
 return true;
}
bool LongGazeAndNativeEligibility(){
 Reset();coop::net::Session host;M::Install(&host);coop::RemotePlayer peer;auto& r=coop::players::Registry::Get();r.peer=&peer;
 NativeStep(host);CHECK(test::body.saw&&test::body.looking);
 test::now+=16000;NativeStep(host);CHECK(test::body.looking&&!test::body.saw);CHECK(test::timerStarts==1);
 test::now+=1000;NativeStep(host);CHECK(test::body.looking&&!test::body.saw);
 r.peer=nullptr;NativeStep(host);CHECK(!test::body.looking&&!test::body.saw);CHECK(M::g_remoteSightScopes.empty());
 test::body.angry=true;r.peer=&peer;NativeStep(host);CHECK(!test::body.looking&&!test::body.saw); // native angry rule, not forced by C++
 const int calls=test::nativeCalls;test::body.held=true;NativeStep(host);CHECK(test::nativeCalls==calls&&test::animation.stand);
 test::body.held=false;test::body.remoteHeld=true;NativeStep(host);CHECK(test::nativeCalls==calls);
 return true;
}
bool CancelledCallCleanup(){
 Reset();coop::net::Session host;M::Install(&host);coop::RemotePlayer peer;coop::players::Registry::Get().peer=&peer;
 ue_wrap::script_gate::Call call{&test::body,++test::invocation};ue_wrap::script_gate::active.insert(call.invocation);
 CHECK(M::OnClientWalkerTick(call)==ue_wrap::script_gate::Verdict::Run);CHECK(test::body.saw);
 ue_wrap::script_gate::active.erase(call.invocation);M::Tick(host);CHECK(!test::body.saw&&M::g_remoteSightScopes.empty());
 call.invocation=++test::invocation;CHECK(M::OnClientWalkerTick(call)==ue_wrap::script_gate::Verdict::Run);CHECK(test::body.saw);
 M::OnDisconnect();CHECK(!test::body.saw&&M::g_remoteSightScopes.empty());return true;
}
bool MotionAndCarry(){
 Reset();coop::net::Session client;client.role_=coop::net::Role::Client;M::Install(&client);
 test::body.pos={0,0,0};test::now=0;M::Tick(client);CHECK(test::animation.stand);
 test::body.pos.X=1;test::now=20;M::Tick(client);CHECK(!test::animation.stand);
 test::now=269;M::Tick(client);CHECK(!test::animation.stand);
 test::now=270;M::Tick(client);CHECK(test::animation.stand);
 for(bool remote:{false,true}){
  test::body.held=!remote;test::body.remoteHeld=remote;test::body.pos.X+=20;test::now+=20;M::Tick(client);CHECK(test::animation.stand);
  test::body.held=test::body.remoteHeld=false;test::body.pos.X+=100;test::now+=20;M::Tick(client);CHECK(test::animation.stand);
  test::body.pos.X+=1;test::now+=20;M::Tick(client);CHECK(!test::animation.stand);
 }
 return true;
}
bool RecycledAndDeadActors(){
 Reset();coop::net::Session client;client.role_=coop::net::Role::Client;M::Install(&client);
 M::Tick(client);test::body.pos.X+=10;test::now+=20;M::Tick(client);CHECK(!test::animation.stand);
 ++test::serials[&test::body];test::body.pos.X+=100;test::now+=20;M::Tick(client);CHECK(test::animation.stand);
 test::serials.erase(&test::body);test::walkers.clear();test::staleReads=0;M::Tick(client);CHECK(M::g_clientWalkers.empty());CHECK(test::staleReads==0);
 M::OnDisconnect();CHECK(M::OnClientWalkerTick({})==ue_wrap::script_gate::Verdict::Run);return true;
}
bool InteractionAdmission(){
 Reset();coop::net::Session session;session.role_=coop::net::Role::Client;ue_wrap::script_gate::watches.clear();M::Install(&session);
 CHECK(M::OnClientWalkerTick({&test::body})==ue_wrap::script_gate::Verdict::Cancel);
 session.role_=coop::net::Role::Host;CHECK(M::OnClientWalkerTick({&test::body})==ue_wrap::script_gate::Verdict::Run);
 for(auto& name:ue_wrap::script_gate::watches)CHECK(name!=L"ExecuteUbergraph_prop_wMannequin"&&name!=L"ExecuteUbergraph_prop_mannequin"&&name!=L"ReceiveBeginPlay"&&name!=L"K2_DestroyActor");
 return true;
}
bool NativeSightPrecedence(){
 Reset();coop::net::Session host;M::Install(&host);coop::RemotePlayer peer;coop::players::Registry::Get().peer=&peer;
 test::remoteSightKnown=test::remoteSight=true;peer.aim={-1,0,0};test::traceResult=1;
 NativeStep(host);CHECK(test::body.looking&&test::timerStarts==1); // actual client view overrides approximate cone/trace
 test::remoteSight=false;peer.aim={1,0,0};test::traceResult=0;test::now+=16000;
 NativeStep(host);CHECK(!test::body.looking&&!test::body.saw); // false is not absence
 test::remoteSightKnown=false;NativeStep(host);CHECK(test::body.looking); // old-peer fallback remains
 test::remoteSightKnown=test::remoteSight=true;host.slotConnected=false;test::now+=16000;
 NativeStep(host);CHECK(!test::body.looking&&!test::body.saw);
 return true;
}
bool NativeStandAndSampling(){
 Reset();coop::net::Session client;client.role_=coop::net::Role::Client;M::Install(&client);
 test::hostStandKnown=true;test::hostStand=true;test::nativeSightReadable=true;test::nativeSight=false;
 M::Tick(client);test::body.pos.X+=1;test::now+=20;M::Tick(client);
 CHECK(test::animation.stand&&test::reports==2&&!test::lastReportedSight); // no walking due to tiny pose correction
 test::hostStand=false;test::nativeSight=true;test::now+=500;M::Tick(client);
 CHECK(!test::animation.stand&&test::lastReportedSight); // host native walking wins even with stationary root
 test::body.held=true;M::Tick(client);CHECK(test::animation.stand);test::body.held=false;
 const int reports=test::reports;test::nativeSightReadable=false;M::Tick(client);CHECK(test::reports==reports);
 Reset();coop::net::Session host;M::Install(&host);test::body.looking=true;test::animation.stand=false;
 M::Tick(host);CHECK(test::published==1&&!test::lastPublishedStand); // actual AnimBP state, not inferred looking/position
 test::animation.stand=true;M::Tick(host);CHECK(test::lastPublishedStand);
 return true;
}
int main(){int bad=0;for(auto fn:{GazeAndDisconnect,LongGazeAndNativeEligibility,CancelledCallCleanup,MotionAndCarry,RecycledAndDeadActors,InteractionAdmission,NativeSightPrecedence,NativeStandAndSampling})if(!fn())++bad;if(bad)return 1;std::puts("PASS production mannequin controller: native hesitation/damping, actual-client sight precedence, native stand/sampling, disconnect/cancel cleanup, carry/history and interactions");return 0;}
