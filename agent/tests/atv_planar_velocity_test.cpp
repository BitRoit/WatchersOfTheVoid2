// Actual production wrapper with reflected engine calls mocked at the boundary.
#include "ue_wrap/devices/atv.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/engine/engine_physics.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
namespace test {
using ue_wrap::FVector;
struct Obj {int index=0,serial=1;bool live=true,primitive=false;void* cls=nullptr;void* parts[4]{};FVector velocity{};} objects[10];
int writes=0,reads=0,missingPart=-1;uint32_t generation=1;void* world=(void*)101;
std::wstring badField;bool badSize=false,badFrame=false;std::function<void(int,bool)> callback;
struct Frame{FVector velocity{};ue_wrap::reflection::FName bone{};bool add=false;};
void Reset(){for(int i=0;i<10;++i){int serial=objects[i].serial+1;objects[i]={};objects[i].index=i;objects[i].serial=serial;objects[i].cls=&objects[6];}objects[0].cls=&objects[5];for(int i=0;i<4;++i){objects[0].parts[i]=&objects[1+i];objects[1+i].primitive=true;objects[1+i].velocity={20.f+i,-10.f+i,-30.f+i};}writes=reads=0;missingPart=-1;badField.clear();badSize=badFrame=false;callback={};++generation;world=(void*)(uintptr_t)(100+generation);}
}
namespace ue_wrap::reflection {
bool IsLive(void* p){return p&&static_cast<test::Obj*>(p)->live;}
int32_t InternalIndexOf(void* p){return p?static_cast<test::Obj*>(p)->index:-1;}
int32_t SlotSerial(int32_t i){return i>=0&&i<10?test::objects[i].serial:0;}
int32_t AllocateSlotSerial(int32_t i){return SlotSerial(i);}
bool IsLiveByIndex(void* p,int32_t i){return i>=0&&i<10&&p==&test::objects[i]&&test::objects[i].live;}
void* ClassOf(void* p){return p?static_cast<test::Obj*>(p)->cls:nullptr;}
int32_t FindPropertyOffset(void*,const wchar_t* name){const wchar_t* names[]={L"mesh",L"frontWheel_L",L"frontWheel_R",L"backWheelRoot"};for(int i=0;i<4;++i)if(!wcscmp(name,names[i]))return i==test::missingPart?-1:int32_t(offsetof(test::Obj,parts)+i*sizeof(void*));return -1;}
void* FindDispatchFunctionCached(void*,const wchar_t* name){return !wcscmp(name,L"GetPhysicsLinearVelocity")?&test::objects[7]:&test::objects[8];}
int32_t FindParamSize(void*,const wchar_t* name){if(test::badField==name)return test::badSize?99:-1;if(!wcscmp(name,L"BoneName"))return sizeof(FName);if(!wcscmp(name,L"bAddToCurrent"))return sizeof(bool);return sizeof(FVector);}
}
namespace ue_wrap::world_identity {void* CurrentWorld(){return test::world;}uint32_t Generation(){return test::generation;}void* WorldOf(void*){return test::world;}}
namespace ue_wrap::engine {bool IsPrimitiveComponent(void* p){return reflection::IsLive(p)&&static_cast<test::Obj*>(p)->primitive;}}
namespace ue_wrap {
ParamFrame::ParamFrame(void* fn):fn_(fn){if(!test::badFrame)buf_.resize(sizeof(test::Frame));else fn_=nullptr;}
bool ParamFrame::SetRaw(const wchar_t* name,const void* p,int32_t size){if(test::badField==name)return false;auto* frame=static_cast<test::Frame*>(data());if(!wcscmp(name,L"BoneName"))std::memcpy(&frame->bone,p,size);else if(!wcscmp(name,L"bAddToCurrent"))std::memcpy(&frame->add,p,size);else std::memcpy(&frame->velocity,p,size);return true;}
bool ParamFrame::GetRaw(const wchar_t* name,void* p,int32_t size)const{if(test::badField==name)return false;std::memcpy(p,&reinterpret_cast<const test::Frame*>(buf_.data())->velocity,size);return true;}
bool Call(void* object,ParamFrame& frame){auto* obj=static_cast<test::Obj*>(object);auto* args=static_cast<test::Frame*>(frame.data());if(!obj||!obj->live)return false;bool write=frame.function()==&test::objects[8];if(write){if(args->add||args->bone.ComparisonIndex||args->bone.Number)return false;obj->velocity=args->velocity;++test::writes;}else{args->velocity=obj->velocity;++test::reads;}if(test::callback)test::callback(obj->index,write);return true;}
}
namespace ue_wrap::atv {namespace R=reflection;bool EnsureResolved(){return true;}bool IsAtv(void* p){return p==&test::objects[0]&&R::ClassOf(p)==&test::objects[5];}
#include "atv_planar_velocity.inc"
}
int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){using namespace test;FVector before{};int count=0;auto call=[&](FVector desired=FVector{1,2,0}){return ue_wrap::atv::MatchRigPlanarVelocity(&objects[0],desired,before,count);};
 Reset();CHECK(call());CHECK(writes==4&&count==4&&before.X==20);for(int i=0;i<4;++i){CHECK(objects[i+1].velocity.X==1+i);CHECK(objects[i+1].velocity.Y==2+i);CHECK(objects[i+1].velocity.Z==-30+i);}
 Reset();CHECK(call({20,-10,999}));CHECK(writes==0&&count==0); // desired Z is never applied
 for(int i=0;i<4;++i){Reset();missingPart=i;CHECK(!call());CHECK(writes==0);}
 for(int i=0;i<4;++i){Reset();objects[i+1].velocity.Y=NAN;CHECK(!call());CHECK(writes==0);}
 for(auto field:{L"NewVel",L"ReturnValue",L"BoneName",L"bAddToCurrent"})for(bool size:{false,true}){Reset();badField=field;badSize=size;CHECK(!call());CHECK(writes==0);}
 Reset();badFrame=true;CHECK(!call());CHECK(writes==0);
 Reset();objects[0].parts[3]=objects[0].parts[1];CHECK(!call());CHECK(writes==0);
 Reset();objects[4].primitive=false;CHECK(!call());CHECK(writes==0);
 Reset();callback=[](int i,bool write){if(i==4&&!write)++generation;};CHECK(!call());CHECK(writes==0);
 Reset();callback=[](int i,bool write){if(i==4&&!write)++objects[1].serial;};CHECK(!call());CHECK(writes==0);
 Reset();callback=[](int i,bool write){if(i==2&&write)++generation;};CHECK(!call());CHECK(writes==2);
 Reset();callback=[](int i,bool write){if(i==4&&write)objects[4].live=false;};CHECK(!call());CHECK(writes==4);
 std::printf("PASS production planar wrapper: %d checks; coherent XY delta, preserved per-part Z/relative velocities, preflight and lifetime failures\n",checks);
}
