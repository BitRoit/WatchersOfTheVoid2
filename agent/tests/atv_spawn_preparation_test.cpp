// Production spawn/teleport bodies; engine and reflected-call boundaries doubled.
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/devices/atv_rig_pose.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <algorithm>
#include <limits>
namespace test {
struct Obj {int id=0,serial=1,kind=-1;bool live=true;void* cls=nullptr;void* world=nullptr;ue_wrap::reflection::FName key{};} objects[16];
void *actorClass,*baseClass,*serviceClass,*service,*context,*beginFunction,*finishFunction,*teleportFunction,*beginResult,*partsFunction,*advancedFunction;
int calls[5]{},destroyed=0,failFunction=-1,invalidFunction=-1,badSizeFunction=-1,finishMode=0;
int keyOffset=offsetof(Obj,key);
uint32_t generation=1;
uintptr_t worldToken=101;
bool lineage=true,beginReadOk=true,callOk[5]{true,true,true,true,true};
void* destroyedActor=nullptr;
std::wstring failField,nameValue;
std::function<void()> onName,onKey;
std::function<void(int)> onFrame,onNative;
ue_wrap::FTransform lastTransform{};
ue_wrap::FVector lastTeleport{};
ue_wrap::FRotator lastFacing{};
void* lastActorClass=nullptr;
struct Begin {void* ctx;void* cls;ue_wrap::FTransform transform;uint8_t collision;void* owner;void* result;};
struct Finish {void* actor;ue_wrap::FTransform transform;void* result;};
struct Teleport {ue_wrap::FVector location;ue_wrap::FRotator rotation;};
using Part=ue_wrap::atv::detail::RigPartPose;
Part rig[4]{};
size_t PartOffset(const wchar_t* name,bool setter) {
 const wchar_t* locations[]={L"body_location",L"frontLeft_location",L"frontRight_location",L"back_location"};
 const wchar_t* rotations[]={L"body_rotation",setter?L"frontLeft_rotation":L"frontLeftrotation",L"frontRight_rotation",L"back_rotation"};
 for(int i=0;i<4;++i){if(!wcscmp(name,locations[i]))return i*sizeof(Part)+offsetof(Part,pos);if(!wcscmp(name,rotations[i]))return i*sizeof(Part)+offsetof(Part,rot);}
 return size_t(-1);
}
void* World(){return reinterpret_cast<void*>(worldToken);}
void Callback(std::function<void()>& f){auto copy=std::move(f);f={};if(copy)copy();}
void Callback(std::function<void(int)>& f,int stage){auto copy=f;if(copy)copy(stage);}
void Reset(){
 onName=onKey={};onFrame=onNative={};++generation;worldToken=generation+100;
 for(int i=0;i<16;++i){const int serial=objects[i].serial+1;objects[i]={};objects[i].id=i;objects[i].serial=serial;objects[i].cls=&objects[15];}
 actorClass=baseClass=&objects[2];serviceClass=&objects[4];service=&objects[5];context=&objects[6];
 beginFunction=&objects[7];finishFunction=&objects[8];teleportFunction=&objects[9];beginResult=&objects[0];
 partsFunction=&objects[11];advancedFunction=&objects[12];
 objects[0].cls=actorClass;objects[0].world=World();objects[1].world=World();objects[1].key={777,0};objects[5].cls=serviceClass;objects[14].cls=&objects[13];
 objects[7].kind=objects[10].kind=0;objects[8].kind=1;objects[9].kind=2;
 objects[11].kind=3;objects[12].kind=4;
 std::memset(calls,0,sizeof(calls));destroyed=0;destroyedActor=nullptr;failFunction=invalidFunction=badSizeFunction=-1;finishMode=0;
 failField.clear();nameValue=L"coopatv1";lineage=beginReadOk=true;for(auto& ok:callOk)ok=true;
 lastTransform={};lastTeleport={};lastFacing={};lastActorClass=nullptr;keyOffset=offsetof(Obj,key);
 for(auto& part:rig)part={};
}
}
namespace ue_wrap::log {void Write(Level,const char*,...) {}}
namespace ue_wrap::reflection {
void* FindClass(const wchar_t* n){if(!_wcsicmp(n,L"GameplayStatics"))return test::serviceClass;return !_wcsicmp(n,L"ATV_C")?test::actorClass:nullptr;}
void* FindClassDefaultObject(const wchar_t*){return test::service;}
void* FindFunction(void*,const wchar_t* n){return !_wcsicmp(n,L"BeginDeferredActorSpawnFromClass")?test::beginFunction:test::finishFunction;}
void* FindDispatchFunctionCached(void*,const wchar_t* name){if(!wcscmp(name,L"vehicleGetParts"))return test::partsFunction;if(!wcscmp(name,L"teleportVehicleAdvanced"))return test::advancedFunction;return test::teleportFunction;}
void* ClassOf(void* p){return p?static_cast<test::Obj*>(p)->cls:nullptr;}
bool IsDescendantOfAny(void* cls,void* const*,size_t,int){return test::lineage && (cls==&test::objects[2] || cls==&test::objects[11]);}
int32_t FindPropertyOffset(void*,const wchar_t*){return test::keyOffset;}
int32_t FindParamSize(void* fn,const wchar_t* name){return static_cast<test::Obj*>(fn)->kind==test::badSizeFunction && test::failField==name?4:12;}
bool IsLive(void* p){return p && static_cast<test::Obj*>(p)->live;}
int32_t InternalIndexOf(void* p){return p?static_cast<test::Obj*>(p)->id:-1;}
int32_t SlotSerial(int32_t i){return test::objects[i].serial;}
int32_t AllocateSlotSerial(int32_t i){return SlotSerial(i);}
bool IsLiveByIndex(void* p,int32_t i){return i>=0 && i<16 && p==&test::objects[i] && test::objects[i].live;}
std::wstring ToString(const FName& name){test::Callback(test::onKey);return name.ComparisonIndex==123?test::nameValue:L"mismatch";}
}
namespace ue_wrap::world_identity {
uint32_t Generation(){return test::generation;}
void* CurrentWorld(){return test::World();}
void* WorldOf(void* p){return p?static_cast<test::Obj*>(p)->world:nullptr;}
}
namespace ue_wrap::fname_utils {reflection::FName StringToFName(const std::wstring& key){test::nameValue=key;test::Callback(test::onName);return {123,0};}}
namespace ue_wrap::engine {
void* GetWorldContext(){return test::context;}
#include "atv_engine_math.inc"
}
namespace ue_wrap {
ParamFrame::ParamFrame(void* fn):fn_(fn){
 if(!fn)return;
 const int stage=static_cast<test::Obj*>(fn)->kind;test::Callback(test::onFrame,stage);
 if(stage==test::invalidFunction){fn_=nullptr;return;}
 buf_.resize(stage==0?sizeof(test::Begin):stage==1?sizeof(test::Finish):stage==2?sizeof(test::Teleport):sizeof(test::rig));
}
bool ParamFrame::SetRaw(const wchar_t* name,const void* data,int32_t size){
 const int stage=static_cast<test::Obj*>(fn_)->kind;if(stage==test::failFunction && test::failField==name)return false;
 size_t off=0;
 if(stage==0){if(!wcscmp(name,L"ActorClass"))off=offsetof(test::Begin,cls);if(!wcscmp(name,L"SpawnTransform"))off=offsetof(test::Begin,transform);if(!wcscmp(name,L"CollisionHandlingOverride"))off=offsetof(test::Begin,collision);if(!wcscmp(name,L"Owner"))off=offsetof(test::Begin,owner);}
 if(stage==1 && !wcscmp(name,L"SpawnTransform"))off=offsetof(test::Finish,transform);
 if(stage==2 && !wcscmp(name,L"NewRotation"))off=offsetof(test::Teleport,rotation);
 if(stage>=3){off=test::PartOffset(name,true);if(off==size_t(-1))return false;}
 assert(off+size<=buf_.size());std::memcpy(buf_.data()+off,data,size);return true;
}
bool ParamFrame::GetRaw(const wchar_t* name,void* data,int32_t size)const{
 const int kind=static_cast<test::Obj*>(fn_)->kind;
 if(kind>=3){if(kind==test::failFunction && test::failField==name)return false;const auto off=test::PartOffset(name,false);if(off==size_t(-1)||off+size>buf_.size())return false;std::memcpy(data,buf_.data()+off,size);return true;}
 assert(!wcscmp(name,L"ReturnValue") && size==sizeof(void*));const int stage=static_cast<test::Obj*>(fn_)->kind;
 if(stage==0 && !test::beginReadOk)return false;if(stage==1 && test::finishMode==1)return false;
 const size_t offset=stage==0?offsetof(test::Begin,result):offsetof(test::Finish,result);
 std::memcpy(data,buf_.data()+offset,size);return true;
}
bool Call(void* object,ParamFrame& frame){
 const int stage=static_cast<test::Obj*>(frame.function())->kind;++test::calls[stage];
 if(stage<2)assert(object==test::service);
 if(stage==0){
  auto* args=static_cast<test::Begin*>(frame.data());assert(args->ctx==test::context && args->collision==1 && !args->owner);
  test::lastActorClass=args->cls;test::lastTransform=args->transform;args->result=test::beginResult;
  if(args->result==&test::objects[0]){test::objects[0].cls=args->cls;test::objects[0].world=test::World();}
 }
 if(stage==1){
  auto* args=static_cast<test::Finish*>(frame.data());args->result=test::finishMode==2?nullptr:test::finishMode==3?&test::objects[1]:args->actor;
 }
 if(stage==2){assert(object==&test::objects[0]);auto* args=static_cast<test::Teleport*>(frame.data());test::lastTeleport=args->location;test::lastFacing=args->rotation;}
 if(stage==3)std::memcpy(frame.data(),test::rig,sizeof(test::rig));
 if(stage==4 && test::callOk[stage])std::memcpy(test::rig,frame.data(),sizeof(test::rig));
 test::Callback(test::onNative,stage);return test::callOk[stage];
}
}
namespace ue_wrap::atv {
namespace R=ue_wrap::reflection;
bool EnsureResolved(){return true;}
std::wstring GetKeyString(void* actor){assert(static_cast<test::Obj*>(actor)->live);return L"fixture";}
void* AtvClass(){return test::baseClass;}
bool IsAtv(void* p){void* base=AtvClass();return R::IsDescendantOfAny(R::ClassOf(p),&base,1);}
bool DestroyMirror(void* p){++test::destroyed;test::destroyedActor=p;static_cast<test::Obj*>(p)->live=false;return true;}
#include "atv_spawn.inc"
}
void ShapePreservingCorrections(){
 using namespace test;namespace A=ue_wrap::atv;
 const auto near=[](float a,float b){return std::fabs(a-b)<.025f;};
 // Independently known UE axes: positive yaw X->Y, pitch X->Z,
 // roll Y->-Z. These are not round trips through the helper under test.
 for(int axis=0;axis<3;++axis){
  Reset();rig[0]={{0,0,0},{}};rig[1]={{axis==2?0.f:100.f,axis==2?100.f:0.f,-40.f},{}};
  rig[2]={{-50,30,-60},{}};rig[3]={{-80,0,-70},{}};
  const ue_wrap::FRotator facing{axis==1?90.f:0.f,axis==0?90.f:0.f,axis==2?90.f:0.f};
  assert(A::TeleportRigPreservingShape(&objects[0],{10,20,30},facing));
  assert(calls[3]==2 && calls[4]==1 && calls[2]==0);
  const ue_wrap::FVector expected=axis==0?ue_wrap::FVector{10,120,-10}:axis==1?ue_wrap::FVector{50,20,130}:ue_wrap::FVector{10,-20,-70};
  assert(near(rig[1].pos.X,expected.X)&&near(rig[1].pos.Y,expected.Y)&&near(rig[1].pos.Z,expected.Z));
 }
 // Non-identity source rotation: remove an existing +90 pitch, then apply
 // +90 roll. The old local +X wheel offset is world +Z, and must become +X.
 Reset();rig[0]={{0,0,0},{90,0,0}};rig[1]={{0,0,100},{90,0,0}};
 assert(A::TeleportRigPreservingShape(&objects[0],{0,0,0},{0,0,90}));
 assert(near(rig[1].pos.X,100)&&near(rig[1].pos.Y,0)&&near(rig[1].pos.Z,0));
 Reset();rig[0]={{120,220,6282.5f},{}};rig[1]={{220,280,6222.5f},{0,10,25}};
 rig[2]={{220,160,6222.5f},{0,-10,-25}};rig[3]={{20,220,6212.5f},{20,0,15}};
 const Part original[4]={rig[0],rig[1],rig[2],rig[3]};
 // Forty seconds at the actual idle cadence, matching the 6282.5 root height
 // in the new logs; settled suspension offsets survive 200 small corrections.
 for(int packet=0;packet<200;++packet){
  const ue_wrap::FVector target{120.f+(packet%2),220.f,6282.5f-(packet%3)};
  assert(A::TeleportRigPreservingShape(&objects[0],target,{}));
  for(int i=1;i<4;++i){
   assert(near(rig[i].pos.X-target.X,original[i].pos.X-original[0].pos.X));
   assert(near(rig[i].pos.Y-target.Y,original[i].pos.Y-original[0].pos.Y));
   assert(near(rig[i].pos.Z-target.Z,original[i].pos.Z-original[0].pos.Z));
   assert(near(rig[i].rot.Pitch,original[i].rot.Pitch)&&near(rig[i].rot.Yaw,original[i].rot.Yaw)&&near(rig[i].rot.Roll,original[i].rot.Roll));
  }
 }
 for(const int stage:{3,4})for(const auto field:{L"body_location",L"body_rotation",L"frontLeft_location",stage==3?L"frontLeftrotation":L"frontLeft_rotation",L"frontRight_location",L"frontRight_rotation",L"back_location",L"back_rotation"}){
  Reset();failFunction=stage;failField=field;assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(calls[4]==0 && calls[2]==0);
  Reset();badSizeFunction=stage;failField=field;assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(!calls[3]&&!calls[4]&&!calls[2]);
 }
 for(const int stage:{3,4}){Reset();invalidFunction=stage;assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(!calls[4]&&!calls[2]);}
 Reset();advancedFunction=nullptr;assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(!calls[4]&&!calls[2]);
 Reset();callOk[3]=false;assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(!calls[4]&&!calls[2]);
 Reset();rig[1].pos.X=std::numeric_limits<float>::quiet_NaN();assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(!calls[4]);
 Reset();onNative=[](int stage){if(stage==3)++generation;};assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(!calls[4]);
 Reset();onNative=[](int stage){if(stage==3)++objects[0].serial;};assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(!calls[4]);
 for(int fault=0;fault<3;++fault){
  Reset();onNative=[fault](int stage){if(stage==3 && calls[3]==2){if(fault==0)objects[0].live=false;else if(fault==1)++objects[0].serial;else ++generation;}};
  assert(!A::TeleportRigPreservingShape(&objects[0],{1,2,3},{}));assert(calls[4]==1 && calls[3]==2);
 }
 std::puts("PASS ATV Advanced: independent yaw/pitch/roll, 40s settled suspension geometry, 16 missing + 16 wrong-size fields, unavailable/retired/read failures leave rig untouched");
}
int main(){
 namespace A=ue_wrap::atv;using F=A::SpawnFail;using namespace test;
 const ue_wrap::FVector loc{100,200,50};const ue_wrap::FRotator rot{};F why;
 const auto spawn=[&]{return A::SpawnMirror(L"ATV_C",loc,rot,L"coopatv1",&why);};
 for(const auto field:{L"WorldContextObject",L"ActorClass",L"SpawnTransform",L"CollisionHandlingOverride",L"Owner"}){
  Reset();failFunction=0;failField=field;assert(!spawn() && why==F::BeginFailed && !calls[0] && !calls[1] && !destroyed);
 }
 for(const auto field:{L"Actor",L"SpawnTransform"}){
  Reset();failFunction=1;failField=field;assert(!spawn() && why==F::FinishFailed && calls[0]==1 && !calls[1] && destroyed==1);
 }
 for(int mode=1;mode<=3;++mode){
  Reset();finishMode=mode;assert(!spawn() && why==F::FinishFailed && calls[1]==1 && !calls[2] && destroyed==1);
 }
 Reset();invalidFunction=1;assert(!spawn() && why==F::FinishFailed && destroyed==1);
 for(const auto field:{L"NewLocation",L"NewRotation"}){
  Reset();failFunction=2;failField=field;assert(!A::TeleportRig(&objects[0],loc,rot) && !calls[2]);
 }
 Reset();assert(spawn()==&objects[0] && why==F::None && !destroyed);
 assert(calls[0]==1 && calls[1]==1 && calls[2]==1 && objects[0].key.ComparisonIndex==123);
 std::puts("PASS ATV spawn argument failures, finish result/cleanup, teleport arguments and successful mint");
 ShapePreservingCorrections();return 0;
}
