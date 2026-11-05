// Production callbacks with mocked engine/native transport boundaries. Native
// admission and handle requirements follow the extracted 0.9.0n cooked graph.
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#define UE_LOGI(...) ((void)0)
namespace test {
struct Obj{const wchar_t* name=L"object";Obj* outer=nullptr;Obj* cls=nullptr;Obj* mesh=nullptr;void* world=(void*)1;int serial=1;bool alive=true,sim=false,parked=false,frozen=false,isStatic=false,remoteHeld=false,held=false;};
Obj walkerClass,regularClass,actor,mesh,player,orphan,handle,constraint,otherComponent,lookAt,useAction,ubergraph,unrelated;
void* world=(void*)1;uint32_t generation=1;uint64_t now=10000;bool settled=true,gameThread=true,replay=false,mirror=true,reverse=true,propType=true,badSize=false;int wakes=0;
}
uint64_t GetTickCount64(){return test::now;}
namespace ue_wrap {
namespace reflection {
bool IsLive(void* p){return p&&static_cast<test::Obj*>(p)->alive;}bool InCoopDispatch(){return test::replay;}
void* ClassOf(void* p){return static_cast<test::Obj*>(p)->cls;}void* SuperStructOf(void*){return nullptr;}
void* OuterOf(void* p){return static_cast<test::Obj*>(p)->outer;}
const wchar_t* NameOf(void* p){return static_cast<test::Obj*>(p)->name;}bool NameEquals(const wchar_t* a,const wchar_t* b){return std::wcscmp(a,b)==0;}
int FindParamOffset(void*,const wchar_t* name){return !std::wcscmp(name,L"Component2")?24:8;}
int FindParamSize(void*,const wchar_t*){return test::badSize?4:8;}int FunctionFrameSize(void*){return 48;}
}
namespace game_thread {bool IsGameThread(){return test::gameThread;}}
namespace world_identity{void* CurrentWorld(){return test::world;}uint32_t Generation(){return test::generation;}void* WorldOf(void* p){return static_cast<test::Obj*>(p)->world;}}
namespace prop {void* GetStaticMesh(void* p){return static_cast<test::Obj*>(p)->mesh;}bool IsFrozen(void* p){return static_cast<test::Obj*>(p)->frozen;}bool IsStatic(void* p){return static_cast<test::Obj*>(p)->isStatic;}}
class CachedObjRef {test::Obj* p_=nullptr;int serial_=0;public:void Set(void* p){p_=static_cast<test::Obj*>(p);serial_=p_?p_->serial:0;}void Reset(){p_=nullptr;}void* Get()const{return p_&&p_->alive&&p_->serial==serial_&&p_->world==test::world?p_:nullptr;}};
namespace script_gate {enum class Verdict{Run,Cancel};}
namespace facts {struct Function{const wchar_t* name;};struct Resolved{void* p=nullptr;void* Fn()const{return p;}explicit operator bool()const{return p!=nullptr;}};Resolved Resolve(const Function& row){return {(void*)&row};}}
namespace ufunction_hook {
using PreNativeCallback=script_gate::Verdict(*)(void*,void*,const uint8_t*);
using PostNativeCallback=void(*)(void*,void*,void*);
struct CallerFrame{void* function;uint8_t* locals;int codeOffset;};
CallerFrame frame{};void* result=nullptr;void* native=nullptr;
std::unordered_map<void*,PreNativeCallback> pre;PostNativeCallback query=nullptr;
CallerFrame CurrentCallerFrame(){return frame;}void* CurrentResult(){return result;}void* CurrentNativeFunction(){return native;}
bool InstallPostHook(facts::Resolved,PostNativeCallback callback,bool=true){query=callback;return true;}
bool InstallPreHook(facts::Resolved fn,PreNativeCallback callback,bool){pre[fn.Fn()]=callback;return true;}
}
}
#define UE_FACT_FN(ident,feature,cls,fn,mode) const ue_wrap::facts::Function ident{fn}
namespace coop {
namespace net {enum class Role{Host,Client};struct Session {Role role_=Role::Client;bool live=true;uint64_t stops=0;bool running(){return live;}bool connected(){return live;}Role role(){return role_;}uint64_t StopCount(){return stops;}};}
namespace players {struct Registry {static Registry& Get(){static Registry r;return r;}void* Local(){return &test::player;}};}
namespace prop_drive_stream {bool IsParked(void* p){return static_cast<test::Obj*>(p)->parked;}}
namespace remote_prop {bool IsActorUnderAnyDrive(void* p){return static_cast<test::Obj*>(p)->remoteHeld;}}
namespace net_pump {bool IsWorldSettled(){return test::settled;}}
namespace prop_park {void Unpark(void* actor,void* component,bool on){if(actor!=&test::actor||component!=test::actor.mesh||!on)std::abort();static_cast<test::Obj*>(component)->sim=on;++test::wakes;}}
namespace element {enum class ElementType{Prop,Other};struct Element {bool IsMirror(){return test::mirror;}ElementType GetType(){return test::propType?ElementType::Prop:ElementType::Other;}void* LiveActor(){return &test::actor;}};
struct Registry{static Registry& Get(){static Registry r;return r;}uint32_t EidForActor(void* p){return test::reverse&&p==&test::actor?773:UINT32_MAX;}Element* Get(uint32_t eid){static Element e;return eid==773?&e:nullptr;}static bool IsAllowedHostAllocatedEid(uint32_t eid){return eid>0&&eid<1000;}};}
}
#include "mannequin_interaction_controller.inc"
namespace M=coop::mannequin_interaction;
namespace H=ue_wrap::ufunction_hook;
int checks=0;
void Check(bool b,const char* name){++checks;if(!b){std::printf("FAIL %s\n",name);std::exit(1);}}
coop::net::Session session;
void Reset(){
M::OnDisconnect();session={};test::actor={};test::mesh={};test::player={};test::orphan={};test::handle={};test::constraint={};test::otherComponent={};
test::world=(void*)1;++test::generation;test::settled=test::gameThread=test::mirror=test::reverse=test::propType=true;test::replay=test::badSize=false;test::wakes=0;
test::walkerClass.name=L"prop_wMannequin_C";test::regularClass.name=L"prop_mannequin_C";
test::actor.cls=&test::walkerClass;test::actor.mesh=&test::mesh;test::actor.parked=true;test::mesh.outer=&test::actor;test::otherComponent.outer=&test::actor;
test::handle.outer=test::constraint.outer=&test::player;test::lookAt.name=L"lookAt";test::useAction.name=L"useAction";test::ubergraph.name=L"ExecuteUbergraph_mainPlayer";test::unrelated.name=L"timer_damage";
H::frame={&test::useAction,nullptr,20};M::Tick(session);
}
bool NativeQuery(void* component,void* source,void* function){
H::frame={function,nullptr,20};bool answer=static_cast<test::Obj*>(component)->sim;H::result=&answer;H::query(component,source,nullptr);H::result=nullptr;return answer;
}
bool NativeGrab(bool heavy=false,bool rotated=true){
H::frame={&test::ubergraph,nullptr,20};const auto fn=ue_wrap::facts::Resolve(heavy?M::kConstraint:(rotated?M::kGrabRotation:M::kGrab));H::native=fn.Fn();
std::array<uint8_t,48> params{};void* target=&test::mesh;void* helper=&test::otherComponent;
std::memcpy(params.data()+8,heavy?&helper:&target,sizeof(void*));if(heavy)std::memcpy(params.data()+24,&target,sizeof(void*));
const auto verdict=H::pre.at(fn.Fn())(heavy?(void*)&test::constraint:(void*)&test::handle,&test::player,params.data());
Check(verdict==ue_wrap::script_gate::Verdict::Run,"native handle/constraint call remains native");
// Native PHC/PCC cannot hold a kinematic target. The callback must wake the
// body before this point, rather than merely spoofing the earlier read.
test::actor.held=test::mesh.sim;return test::actor.held;
}
void StreamStep(){if(test::actor.held){test::actor.parked=false;}else{test::actor.parked=true;test::mesh.sim=false;}}
int main(){
Reset();Check(H::query&&H::pre.size()==3,"all reflected native seams installed");
Check(!test::mesh.sim&&NativeQuery(&test::mesh,&test::actor,&test::lookAt),"parked walker passes inherited native hover predicate");
Check(!test::mesh.sim&&test::wakes==0,"hover does not enable physics or fight host drive");
Check(NativeQuery(&test::mesh,&test::player,&test::useAction),"actual local useAction can enter native pickupObject");
Check(!test::mesh.sim,"pickup admission read alone keeps body parked");
Check(NativeGrab(),"native PHC grab receives simulating walker body");Check(test::wakes==1,"only actual grab wakes body");StreamStep();Check(!test::actor.parked&&test::mesh.sim&&test::actor.held,"held hand owns physics after host stream yields");
test::actor.held=false;StreamStep();Check(test::actor.parked&&!test::mesh.sim,"release permits authoritative stalking stream to resume");
Check(NativeQuery(&test::mesh,&test::player,&test::useAction)&&NativeGrab(true),"heavy native constraint grab works after release");StreamStep();Check(test::actor.held&&test::mesh.sim,"heavy hand owns simulating body");
Reset();Check(NativeGrab(false,false),"nonrotating native PHC path also wakes target");
Reset();Check(!NativeQuery(&test::mesh,&test::orphan,&test::useAction),"remote puppet cannot gain local pickup admission");
Check(!NativeQuery(&test::mesh,&test::player,&test::unrelated),"unrelated player physics query sees actual kinematic state");
Check(!NativeQuery(&test::mesh,&test::actor,&test::unrelated),"walker AI query sees actual kinematic state");
Check(!NativeQuery(&test::otherComponent,&test::player,&test::useAction),"unrelated component of walker is not admitted");
test::actor.cls=&test::regularClass;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"ordinary mannequin retains original admission and physics");test::actor.cls=&test::walkerClass;
test::actor.frozen=true;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"frozen walker not made movable");test::actor.frozen=false;
test::actor.isStatic=true;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"static walker not made movable");test::actor.isStatic=false;
test::actor.remoteHeld=true;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"remote hand cannot be stolen by park admission");test::actor.remoteHeld=false;
test::actor.parked=false;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"genuine nonsimulating walker remains unchanged");test::actor.parked=true;
test::replay=true;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"mod network replay cannot trigger local pickup override");test::replay=false;
test::handle.outer=&test::orphan;Check(!NativeGrab(),"foreign physics handle cannot wake local body");test::handle.outer=&test::player;
test::mirror=false;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"unmirrored local spawn not admitted");test::mirror=true;
test::reverse=false;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"stale reverse identity not admitted");test::reverse=true;
test::propType=false;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"wrong registry type not admitted");test::propType=true;
test::actor.world=(void*)2;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"foreign world walker not admitted");test::actor.world=test::world;
test::settled=false;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"loading client cannot gain admission");test::settled=true;
session.role_=coop::net::Role::Host;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction)&&!NativeGrab(),"host native behavior unaffected");session.role_=coop::net::Role::Client;
test::gameThread=false;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"worker thread cannot inspect/admit native body");test::gameThread=true;
++session.stops;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"same session restart invalidates callback context");M::Tick(session);
++test::generation;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"world generation invalidates callback context");M::Tick(session);
++test::player.serial;Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"recycled local player cannot inherit callback admission");M::Tick(session);
test::badSize=true;Check(!M::HookGrab(M::kGrab,false),"malformed reflected native component parameter rejected");test::badSize=false;
M::OnDisconnect();Check(!NativeQuery(&test::mesh,&test::player,&test::useAction),"disconnect disables installed native callbacks");
std::printf("PASS mannequin native interaction: %d checks\n",checks);
}
