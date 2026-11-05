// Production exact-identity reconciliation; mocked UE destroy/render boundaries.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define UE_LOGI(...) ((void)0)
namespace test {
struct Obj {const wchar_t* name=L"object";Obj* cls=nullptr;void* world=(void*)1;std::wstring key=L"shared-walker-key";int serial=1;bool live=true,held=false,remoteHeld=false,handAxis=false,frozen=false,isStatic=false,hidden=false,collision=true;};
Obj walkerClass,regularClass,subClass,canonical,twin,other;
void* world=(void*)1;uint32_t generation=1;uint64_t now=10000;bool settled=true,gameThread=true,flagsReady=true,destroyWorks=true,recycleOnDestroy=false,travelOnDestroy=false;int destroys=0,unmarks=0,tracks=0,broadcasts=0;
std::unordered_map<std::wstring,void*> tracked;
}
uint64_t GetTickCount64(){return test::now;}
namespace ue_wrap {
namespace reflection {
bool IsLive(void* p){return p&&static_cast<test::Obj*>(p)->live;}bool IsLiveByIndex(void* p,int){return IsLive(p);}void* ClassOf(void* p){return static_cast<test::Obj*>(p)->cls;}
int InternalIndexOf(void* p){return p==&test::canonical?1:p==&test::twin?2:p==&test::other?3:-1;}
void* ObjectAt(int index){return index==1?(void*)&test::canonical:index==2?(void*)&test::twin:index==3?(void*)&test::other:nullptr;}
int SlotSerial(int index){void* p=ObjectAt(index);return p?static_cast<test::Obj*>(p)->serial:0;}
void* SuperStructOf(void* p){return p==&test::subClass?&test::walkerClass:nullptr;}
const wchar_t* NameOf(void* p){return static_cast<test::Obj*>(p)->name;}bool NameEquals(const wchar_t* a,const wchar_t* b){return std::wcscmp(a,b)==0;}
bool FindBoolProperty(void*,const wchar_t* name,int& offset,uint8_t& mask){if(!test::flagsReady)return false;mask=1;if(!std::wcscmp(name,L"bHidden"))offset=offsetof(test::Obj,hidden);else if(!std::wcscmp(name,L"bActorEnableCollision"))offset=offsetof(test::Obj,collision);else return false;return true;}
}
namespace world_identity {void* CurrentWorld(){return test::world;}uint32_t Generation(){return test::generation;}void* WorldOf(void* p){return static_cast<test::Obj*>(p)->world;}}
namespace game_thread {bool IsGameThread(){return test::gameThread;}}
namespace prop {bool IsFrozen(void* p){return static_cast<test::Obj*>(p)->frozen;}bool IsStatic(void* p){return static_cast<test::Obj*>(p)->isStatic;}std::wstring GetInteractableKeyString(void* p){return static_cast<test::Obj*>(p)->key;}}
namespace engine {bool SetActorHiddenInGame(void* p,bool value){static_cast<test::Obj*>(p)->hidden=value;return true;}bool SetActorEnableCollision(void* p,bool value){static_cast<test::Obj*>(p)->collision=value;return true;}}
class CachedObjRef {test::Obj* p_=nullptr;int serial_=0;void* world_=nullptr;public:void Set(void* p){p_=static_cast<test::Obj*>(p);serial_=p_->serial;world_=p_->world;}bool Is(void* p)const{return p&&p==p_&&p_->live&&p_->serial==serial_;}bool Alive()const{return Is(p_)&&world_==test::world;}void* Raw()const{return p_;}void* Get()const{return Alive()?p_:nullptr;}int Idx()const{return reflection::InternalIndexOf(p_);}};
}
namespace coop {
namespace net {enum class Role{Host,Client};struct Session{Role role_=Role::Client;bool live=true;uint64_t stops=0;bool running(){return live;}bool connected(){return live;}Role role(){return role_;}uint64_t StopCount(){return stops;}};}
namespace element {enum class ElementType{Prop,Other};struct Element {test::Obj* actor=nullptr;bool mirror=false;std::string name="shared-walker-key";int serial=1;ElementType type=ElementType::Prop;
bool IsMirror(){return mirror;}ElementType GetType(){return type;}void* LiveActor(){return actor&&actor->live?actor:nullptr;}const std::string& GetName(){return name;}};
struct Registry {std::unordered_map<uint32_t,Element> entries;static Registry& Get(){static Registry r;return r;}Element* Get(uint32_t eid){auto it=entries.find(eid);return it==entries.end()?nullptr:&it->second;}uint32_t EidForActor(void* actor){for(auto& [eid,e]:entries)if(e.actor==actor&&e.actor->serial==e.serial)return eid;return UINT32_MAX;}static bool IsAllowedHostAllocatedEid(uint32_t eid){return eid>0&&eid<1000;}static bool IsLocalId(uint32_t eid){return eid>=1000&&eid<100000;}
struct ActorIdPair{void* actor;uint32_t id;int internalIdx;bool mirror;};size_t SnapshotActorsByType(ElementType type,std::vector<ActorIdPair>& out){out.clear();for(auto& [id,e]:entries)if(e.type==type)out.push_back({e.actor,id,1,e.mirror});return out.size();}};}
namespace hand_item {bool IsHandAxisActor(void* p){return static_cast<test::Obj*>(p)->handAxis;}}
namespace local_prop_hold {bool IsHeld(void* p){return static_cast<test::Obj*>(p)->held;}}
namespace prop_drive_host {bool InAHand(void* p){return static_cast<test::Obj*>(p)->remoteHeld;}}
namespace net_pump {bool IsWorldSettled(){return test::settled;}}
namespace remote_prop {void ConsumeLocalActor(void* p){++test::destroys;auto* actor=static_cast<test::Obj*>(p);if(test::destroyWorks)actor->live=false;if(test::recycleOnDestroy){++actor->serial;actor->key=L"successor";actor->live=true;element::Registry::Get().entries[33887].serial=actor->serial;}if(test::travelOnDestroy){test::world=(void*)2;++test::generation;}}}
namespace prop_element_tracker {
void UnmarkKnownKeyedProp(void* p){++test::unmarks;auto& registry=element::Registry::Get();for(auto it=registry.entries.begin();it!=registry.entries.end();)if(it->second.actor==p&&!it->second.mirror)it=registry.entries.erase(it);else ++it;}
void UnmarkProcessedInit(void*){}void TrackActorKey(void* p,const std::wstring& key){++test::tracks;test::tracked[key]=p;}
}
namespace trash_collect_sync {bool EnsureHeldItemBroadcast(void*,net::Session*){++test::broadcasts;return true;}}
}
namespace coop::mannequin_identity {void OnDisconnect();}
#include "mannequin_identity_controller.inc"
namespace M=coop::mannequin_identity;
coop::net::Session session;
std::vector<void*> walkers;
int checks=0;
uint32_t g_lastHeldEid=0;
namespace coop::element {inline constexpr uint32_t kInvalidId=UINT32_MAX;}
uint32_t ResolveHeldPropEid(void*){return 33887;}
void ProductionHeldEdge(void* heldActor){
auto& session=::session;uint32_t adoptedEid=coop::element::kInvalidId;
#include "mannequin_held_edge.inc"
}
std::wstring ProductionHeldPose(void* heldActor){auto& session=::session;
#include "mannequin_held_pose.inc"
return keyW;
}
void Check(bool ok,const char* name){++checks;if(!ok){std::printf("FAIL %s\n",name);std::exit(1);}}
void Reset(){M::OnDisconnect();session={};test::world=(void*)1;++test::generation;test::canonical={};test::twin={};test::other={};test::walkerClass.name=L"prop_wMannequin_C";test::regularClass.name=L"prop_mannequin_C";test::subClass.name=L"custom_walker_C";
test::canonical.cls=test::twin.cls=test::other.cls=&test::walkerClass;test::destroys=test::unmarks=test::tracks=test::broadcasts=0;g_lastHeldEid=0;test::settled=test::gameThread=test::flagsReady=test::destroyWorks=true;test::recycleOnDestroy=test::travelOnDestroy=false;test::tracked.clear();walkers={&test::canonical,&test::twin};
coop::element::Registry::Get().entries={{1,{&test::canonical,true}},{33887,{&test::twin,false}}};}
bool HeldIdentity(void* actor,uint32_t expected=1){uint32_t eid=0;std::wstring key;return M::TryGetHeldIdentity(session,actor,eid,key)&&eid==expected&&key==L"shared-walker-key";}
int main(){
Reset();M::Tick(session,walkers);Check(test::canonical.live&&!test::twin.live&&test::destroys==1,"fresh host mirror plus late native twin converges to one actor");
Check(walkers.size()==1&&walkers[0]==&test::canonical,"retired pointer removed before controller animation/sight loop");
Check(!coop::element::Registry::Get().Get(33887)&&coop::element::Registry::Get().Get(1)->actor==&test::canonical,"only duplicate local row drains; canonical mirror identity kept");
Check(test::tracked[L"shared-walker-key"]==&test::canonical,"tracked key repaired onto canonical actor");test::canonical.held=true;Check(HeldIdentity(&test::canonical),"first grab streams canonical host eid");test::canonical.held=false;M::Tick(session,walkers);Check(test::destroys==1,"release/reassert does not create a second actor");
// The native twin can arrive already held. Preserve that real handle/body,
// alias its outbound identity, and hide only the positively identified mirror.
Reset();test::twin.held=true;M::Tick(session,walkers);Check(test::destroys==0&&test::twin.live&&test::twin.held,"already held twin is never deleted or force-dropped");
Check(test::canonical.hidden&&!test::canonical.collision&&!test::twin.hidden&&test::twin.collision,"one visible/interactable rendering during held deferral");
Check(HeldIdentity(&test::twin),"held local33887 alias outputs canonical host1 identity");
Check(coop::element::Registry::Get().Get(33887)->actor==&test::twin&&coop::element::Registry::Get().Get(1)->actor==&test::canonical,"held alias never steals or rewrites Registry identities");
// Equivalent of the local_streams new-held gate and pose/release fields.
uint32_t wireEid=0;std::wstring wireKey;const bool alias=M::TryGetHeldIdentity(session,&test::twin,wireEid,wireKey);int newSpawns=alias?0:1;Check(newSpawns==0&&wireEid==1&&wireKey==test::canonical.key,"held alias does not express another PropSpawn or local identity");
M::Tick(session,walkers);Check(HeldIdentity(&test::twin)&&test::destroys==0,"repeated host reassert keeps canonical and held alias stable");
test::twin.held=false;test::twin.remoteHeld=true;M::Tick(session,walkers);Check(test::destroys==0,"release-edge cached hand owner defers retirement until release packet can be emitted");
test::twin.remoteHeld=false;M::Tick(session,walkers);Check(!test::twin.live&&!test::canonical.hidden&&test::canonical.collision&&walkers.size()==1,"released alias retires and canonical presentation restores");
Reset();test::canonical.hidden=true;test::canonical.collision=false;test::twin.held=true;M::Tick(session,walkers);M::OnDisconnect();Check(test::canonical.hidden&&!test::canonical.collision,"disconnect restores original flags rather than assuming visible/collidable");
Reset();test::twin.held=true;M::Tick(session,walkers);M::OnDisconnect();Check(!test::canonical.hidden&&test::canonical.collision&&test::twin.live,"disconnect restores canonical without deleting held twin");
Reset();test::twin.frozen=true;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"frozen exact-key local prop preserved");test::twin.frozen=false;test::twin.isStatic=true;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"static exact-key local prop preserved");
Reset();test::twin.key=L"different-key";M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"different key at any proximity is not a twin");test::twin.held=true;Check(!HeldIdentity(&test::twin),"different-key held object never aliases canonical identity");
Reset();test::twin.cls=&test::regularClass;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"ordinary mannequin sharing a key remains untouched");
Reset();test::twin.cls=&test::subClass;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"different walking subclass sharing a key remains untouched");
Reset();test::twin.world=(void*)2;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"foreign world twin remains untouched");
Reset();test::twin.handAxis=true;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"hand-axis display actor remains untouched");
Reset();auto& entries=coop::element::Registry::Get().entries;entries.erase(33887);entries[2]={&test::twin,true};M::Tick(session,walkers);Check(test::twin.live&&test::canonical.live&&test::destroys==0,"two actual host mirror identities are ambiguous and neither retires");
Reset();entries[1].name="different-host-key";M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"mirror canonical key must match native twin key exactly");
Reset();entries[1].mirror=false;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"local actor cannot impersonate canonical host mirror");
Reset();entries[33887].mirror=true;M::Tick(session,walkers);Check(test::twin.live&&test::destroys==0,"distinct peer mirror never retired as local twin");
Reset();test::destroyWorks=false;M::Tick(session,walkers);Check(test::twin.live&&test::unmarks==0&&walkers.size()==2,"failed native destroy does not drain live identity or cached controller pointer");
Reset();session.role_=coop::net::Role::Host;M::Tick(session,walkers);Check(test::destroys==0,"host-owned actors never reconciled client-side");
Reset();test::settled=false;M::Tick(session,walkers);Check(test::destroys==0,"client load window not reconciled prematurely");
Reset();test::twin.held=true;M::Tick(session,walkers);++session.stops;entries.erase(1);Check(!HeldIdentity(&test::twin),"session stop invalidates cached alias and requires current mirror");
Reset();test::twin.held=true;M::Tick(session,walkers);++test::generation;entries.erase(1);Check(!HeldIdentity(&test::twin),"world generation invalidates cached alias and requires current mirror");
Reset();test::twin.held=true;M::Tick(session,walkers);++test::canonical.serial;Check(!HeldIdentity(&test::twin),"recycled canonical actor cannot inherit alias binding");
Reset();test::twin.held=true;M::Tick(session,walkers);entries[1].name="changed";Check(!HeldIdentity(&test::twin),"canonical rekey invalidates alias immediately");
Reset();test::twin.held=true;Check(HeldIdentity(&test::twin),"first NEW-held lookup before any helper Tick resolves current authoritative mirror");Check(test::canonical.hidden&&!test::canonical.collision&&test::destroys==0,"pre-Tick held alias hides canonical without deleting held body");
Reset();test::twin.held=true;test::flagsReady=false;Check(HeldIdentity(&test::twin),"missing presentation flags cannot emit an unknown local identity");Check(!test::canonical.hidden&&test::twin.live&&test::destroys==0,"missing flag metadata defers presentation change safely");test::flagsReady=true;Check(HeldIdentity(&test::twin)&&test::canonical.hidden,"presentation hide retries when reflected flags become readable");
Reset();test::canonical.key=L"different-native-canonical-key";test::canonical.held=true;Check(HeldIdentity(&test::canonical),"canonical host mirror streams mirror key even if native key differs");
Reset();test::twin.held=true;ProductionHeldEdge(&test::twin);Check(test::broadcasts==0&&g_lastHeldEid==1,"actual production NEW-held block before helper Tick emits no local33887 spawn");Check(ProductionHeldPose(&test::twin)==L"shared-walker-key"&&g_lastHeldEid==1,"actual production pose block retains canonical eid/key for deferred held alias");
Reset();test::canonical.held=true;test::canonical.key=L"different-local-key";ProductionHeldEdge(&test::canonical);Check(test::broadcasts==0&&g_lastHeldEid==1&&ProductionHeldPose(&test::canonical)==L"shared-walker-key","actual production mirror carry uses canonical key despite native-key mismatch");
Reset();test::twin.held=true;test::twin.cls=&test::regularClass;ProductionHeldEdge(&test::twin);Check(test::broadcasts==1&&g_lastHeldEid==33887,"actual production ordinary prop express behavior remains unchanged");
Reset();test::twin.held=true;M::Tick(session,walkers);coop::element::Registry::Get().entries[2]={&test::other,true};Check(!HeldIdentity(&test::twin),"host mirror conflict arriving after Tick cannot use cached alias uniqueness");Check(!test::canonical.hidden&&test::canonical.collision,"new canonical ambiguity immediately restores prior hidden rendering");
Reset();test::destroyWorks=false;M::Tick(session,walkers);Check(test::twin.live&&test::unmarks==0,"delayed native destruction keeps live identity intact");test::twin.live=false;M::Tick(session,walkers);Check(test::unmarks==1&&!coop::element::Registry::Get().Get(33887)&&walkers.size()==1,"later pending-kill twin drains old local identity and leaves controller list");
Reset();test::destroyWorks=false;M::Tick(session,walkers);M::Tick(session,walkers);Check(test::destroys==1&&test::unmarks==0,"failed native destroy retries are throttled without draining live row");test::now+=1001;M::Tick(session,walkers);Check(test::destroys==2&&test::unmarks==0,"failed destroy remains retryable after bounded wait");
Reset();test::destroyWorks=false;M::Tick(session,walkers);++test::twin.serial;coop::element::Registry::Get().entries[33887].serial=test::twin.serial;test::twin.key=L"new-object-key";M::Tick(session,walkers);Check(test::unmarks==0&&test::twin.live&&coop::element::Registry::Get().Get(33887),"delayed cleanup never unmarks new object at recycled twin address");
Reset();test::recycleOnDestroy=true;M::Tick(session,walkers);Check(test::twin.live&&test::twin.key==L"successor"&&test::unmarks==0&&coop::element::Registry::Get().Get(33887)&&walkers.size()==2,"slot and address recycled inside native destroy preserve successor identity and controller membership");
Reset();test::travelOnDestroy=true;M::Tick(session,walkers);Check(test::unmarks==0&&walkers.empty(),"world change inside native destroy drops old proof without unmarking or processing old-world actors");
std::printf("PASS mannequin exact identity: %d checks\n",checks);
}
