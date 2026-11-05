#define MANNEQUIN_TARGET_TEST
#define main UnusedMannequinControllerMain
#include "mannequin_sync_test.cpp"
#undef main
#include <cstring>
#include <limits>
#define UE_LOGE(...) ((void)0)
namespace test {
Obj moveFn,distanceFn,ubergraph,cameraActor,cameraClass,pawn,clientBody,proxy;
struct Args{void* world=nullptr;void* pawn=nullptr;ue_wrap::FVector destination{};void* target=nullptr;float radius=50;bool overlap=false;void* result=nullptr;};
Args captured;void* result=nullptr;float distanceResult=0,hostDead=0;int dispatches=0;
bool gameThread=true,hookFail=false,callOK=true,setOK=true,returnOK=true,badSignature=false,distanceArmed=false,moveArmed=false;
void* caller=nullptr;
}
namespace ue_wrap::game_thread{bool IsGameThread(){return test::gameThread;}}
namespace ue_wrap::vitals{enum class Field{Dead};bool Read(Field,float* out){*out=test::hostDead;return true;}}
namespace ue_wrap::facts {
struct Function{const wchar_t* name;};struct Resolved{void* p=nullptr;void* Fn()const{return p;}explicit operator bool()const{return p!=nullptr;}};
Resolved Resolve(const Function& row){return {std::wcscmp(row.name,L"GetDistanceTo")==0?&test::distanceFn:&test::moveFn};}
}
#define UE_FACT_FN(id,feature,cls,name,mode) const ue_wrap::facts::Function id{name}
namespace ue_wrap::reflection {
int32_t FindParamOffset(void* fn,const wchar_t* name){
 if(!std::wcscmp(name,L"ReturnValue"))return fn==&test::distanceFn?0:offsetof(test::Args,result);
 if(!std::wcscmp(name,L"WorldContextObject"))return offsetof(test::Args,world);
 if(!std::wcscmp(name,L"Pawn"))return offsetof(test::Args,pawn);
 if(!std::wcscmp(name,L"Destination"))return offsetof(test::Args,destination);
 if(!std::wcscmp(name,L"TargetActor"))return offsetof(test::Args,target);
 if(!std::wcscmp(name,L"AcceptanceRadius"))return offsetof(test::Args,radius);
 if(!std::wcscmp(name,L"bStopOnOverlap"))return offsetof(test::Args,overlap);return -1;
}
int32_t FindParamSize(void* fn,const wchar_t* name){
 if(test::badSignature&&!std::wcscmp(name,L"Pawn"))return 0;
 if(!std::wcscmp(name,L"ReturnValue"))return fn==&test::distanceFn?sizeof(float):sizeof(void*);
 if(!std::wcscmp(name,L"Destination"))return sizeof(FVector);
 if(!std::wcscmp(name,L"AcceptanceRadius"))return sizeof(float);
 if(!std::wcscmp(name,L"bStopOnOverlap"))return sizeof(bool);
 return FindParamOffset(fn,name)>=0?sizeof(void*):-1;
}
int32_t FunctionFrameSize(void*){return sizeof(test::Args);}
}
namespace ue_wrap {
class ParamFrame{void* fn_;public:test::Args args{};explicit ParamFrame(void* fn):fn_(fn){}bool valid(){return fn_!=nullptr;}
 template<class T>bool Set(const wchar_t* name,const T& value){const int off=reflection::FindParamOffset(fn_,name);if(!test::setOK||off<0||reflection::FindParamSize(fn_,name)!=sizeof(T))return false;std::memcpy(reinterpret_cast<uint8_t*>(&args)+off,&value,sizeof(T));return true;}
 bool GetRaw(const wchar_t* name,void* out,int size){if(!test::returnOK||std::wcscmp(name,L"ReturnValue")||size!=sizeof(void*))return false;std::memcpy(out,&args.result,size);return true;}
};
bool Call(void*,ParamFrame& f){++test::dispatches;test::captured=f.args;if(test::callOK)f.args.result=&test::proxy;return test::callOK;}
namespace ufunction_hook {
using PostNativeCallback=void(*)(void*,void*,void*);using PreNativeCallback=script_gate::Verdict(*)(void*,void*,const uint8_t*);
struct CallerFrame{void* function;uint8_t* locals=nullptr;int codeOffset=-1;};
CallerFrame CurrentCallerFrame(){return {test::caller};}void* CurrentResult(){return test::result;}
bool InstallPostHook(const facts::Resolved&,PostNativeCallback,bool){return true;}
bool InstallReturningPreHook(const facts::Resolved&,PreNativeCallback,int,bool){return !test::hookFail;}
bool SetArmed(void*,PostNativeCallback,bool on){test::distanceArmed=on;return true;}
bool SetArmed(void*,PreNativeCallback,bool on){test::moveArmed=on;return true;}
}
}
#include "mannequin_target.inc"
namespace T=coop::mannequin_target;
void TargetReset(coop::net::Session& session,coop::RemotePlayer& peer){
 Reset();T::g_attempted=T::g_ready=T::g_armed=false;T::g_moveFn.Reset();T::g_distanceFn.Reset();
 test::hostDead=0;test::dispatches=0;test::hookFail=false;test::callOK=test::setOK=test::returnOK=true;test::badSignature=false;test::gameThread=true;
 test::distanceArmed=test::moveArmed=false;test::caller=&test::ubergraph;test::ubergraph.name=L"ExecuteUbergraph_prop_wMannequin";
 test::moveFn.world=test::distanceFn.world=nullptr;test::cameraClass.name=L"PlayerCameraManager";test::cameraActor.cls=&test::cameraClass;
 test::clientBody={};test::clientBody.world=test::world;test::clientBody.pos={900,0,0};test::pawn.world=test::world;
 for(void* p:{&test::moveFn,&test::distanceFn,&test::clientBody,&test::pawn,&test::cameraActor,&test::cameraClass})test::serials[p]=1;
 session={};peer={};peer.actor=&test::clientBody;coop::players::Registry::Get().peer=&peer;
 T::Install(&session);T::Track(&test::body);T::Tick();
}
bool NearestAndHysteresis(){
 coop::net::Session s;coop::RemotePlayer peer;TargetReset(s,peer);auto& state=T::g_targets.at(&test::body);
 auto pick=[&]{return T::Select(&test::body,state).slot;};CHECK(pick()==1);
 state.chosen=T::Candidate{};test::clientBody.pos={2000,0,0};CHECK(pick()==0); // fresh equal-distance tie
 test::clientBody.pos.X=1900;CHECK(pick()==0); // only 10% closer
 test::clientBody.pos.X=1700;CHECK(pick()==1); // 20% AND 2m closer
 test::viewer.pos.X=400;CHECK(pick()==1); // host only 1m closer
 test::viewer.pos.X=600;CHECK(pick()==0);
 test::body.angry=true;test::clientBody.pos.X=1010;CHECK(pick()==0); // retain valid angry target
 test::hostDead=1;CHECK(pick()==1); // invalid target overrides stickiness
 return true;
}
bool EligibilityAndReuse(){
 coop::net::Session s;coop::RemotePlayer peer;TargetReset(s,peer);auto& state=T::g_targets.at(&test::body);
 auto pick=[&]{return T::Select(&test::body,state).slot;};CHECK(pick()==1);
 peer.health=0;CHECK(pick()==0);peer.health=1;CHECK(pick()==1);
 peer.posed=false;CHECK(pick()==0);peer.posed=true;s.ready=false;CHECK(pick()==0);s.ready=true;
 s.slotConnected=false;CHECK(pick()==0);s.slotConnected=true;CHECK(pick()==1); // world-ready can briefly outlive a connection
 s.slotReady=false;CHECK(pick()==0);s.slotReady=true;CHECK(pick()==1);
 s.generation=0;CHECK(pick()==0);s.generation=1;CHECK(pick()==1);
 test::clientBody.world=reinterpret_cast<void*>(2);CHECK(pick()==0);test::clientBody.world=test::world;
 peer.health=std::numeric_limits<float>::quiet_NaN();CHECK(pick()==0);peer.health=1;CHECK(pick()==1);
 ++s.generation;CHECK(pick()==1&&state.chosen.generation==s.generation);
 ++test::serials[&test::clientBody];test::clientBody.pos.X=2000;CHECK(pick()==0); // successor cannot inherit sticky target
 s.ready=false;test::hostDead=1;CHECK(pick()==coop::players::kPeerIdUnknown);
 return true;
}
bool NativeDispatchAndScope(){
 coop::net::Session s;coop::RemotePlayer peer;TargetReset(s,peer);CHECK(T::g_ready&&test::distanceArmed&&test::moveArmed);
 float distance=1000;test::result=&distance;T::OnDistance(&test::cameraActor,&test::body,nullptr);CHECK(distance==100);
 test::Args args;args.world=&test::body;args.pawn=&test::pawn;args.target=&test::cameraActor;args.radius=50;
 void* result=nullptr;test::result=&result;
 auto move=[&]{return T::OnMove(&test::cameraActor,&test::body,reinterpret_cast<uint8_t*>(&args));};
 CHECK(move()==ue_wrap::script_gate::Verdict::Cancel);CHECK(test::dispatches==1&&result==&test::proxy);
 CHECK(test::captured.target==&test::clientBody&&test::captured.pawn==args.pawn&&test::captured.world==args.world&&test::captured.radius==50&&!test::captured.overlap);
 test::caller=&test::viewer;CHECK(move()==ue_wrap::script_gate::Verdict::Run&&test::dispatches==1);
 test::caller=&test::ubergraph;test::body.held=true;CHECK(move()==ue_wrap::script_gate::Verdict::Run&&test::dispatches==1);test::body.held=false;
 test::gameThread=false;CHECK(move()==ue_wrap::script_gate::Verdict::Run);test::gameThread=true;
 test::callOK=false;result=&test::proxy;CHECK(move()==ue_wrap::script_gate::Verdict::Cancel&&test::dispatches==2&&result==nullptr);
 test::callOK=true;test::returnOK=false;result=nullptr;CHECK(move()==ue_wrap::script_gate::Verdict::Cancel&&test::dispatches==3&&result==nullptr);
 test::setOK=false;CHECK(move()==ue_wrap::script_gate::Verdict::Run&&test::dispatches==3);test::setOK=true;
 s.ready=false;test::hostDead=1;distance=1;test::result=&distance;T::OnDistance(&test::cameraActor,&test::body,nullptr);CHECK(distance>20000);
 test::result=&result;result=&test::proxy;CHECK(move()==ue_wrap::script_gate::Verdict::Cancel&&result==nullptr&&test::dispatches==3);
 T::OnDisconnect();CHECK(T::g_targets.empty()&&!test::distanceArmed&&!test::moveArmed);return true;
}
bool PairReadinessAndWorldCleanup(){
 coop::net::Session s;coop::RemotePlayer peer;TargetReset(s,peer);T::OnDisconnect();T::g_attempted=false;test::hookFail=true;T::Install(&s);T::Track(&test::body);T::Tick();CHECK(!T::g_ready&&!test::distanceArmed&&!test::moveArmed);
 T::OnDisconnect();T::g_attempted=false;test::hookFail=false;test::badSignature=true;T::Install(&s);CHECK(!T::g_ready);
 test::badSignature=false;T::g_attempted=false;T::Install(&s);T::Track(&test::body);T::Tick();CHECK(T::g_ready);
 test::serials.erase(&test::body);T::Tick();CHECK(T::g_targets.empty()&&!test::moveArmed);return true;
}
int main(){int bad=0;for(auto fn:{NearestAndHysteresis,EligibilityAndReuse,NativeDispatchAndScope,PairReadinessAndWorldCleanup})if(!fn())++bad;if(bad)return 1;std::puts("PASS mannequin targeting: nearest/hysteresis, angry/invalid exceptions, health/readiness/world/generation guards, scoped native distance+single move, pair readiness and cleanup");}
