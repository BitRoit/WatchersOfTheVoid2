// Execute the production native-view wrapper with mocked reflection/dispatch.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#define UE_LOGI(...) ((void)0)
namespace ue_wrap {struct FVector{float X=0,Y=0,Z=0;};}
namespace test {
struct Obj{Obj* cls=nullptr;void* world=nullptr;Obj* mesh=nullptr;};
struct Bounds{void* component=nullptr;ue_wrap::FVector origin{},extent{};float radius=0;};
struct Frustum{void* controller=nullptr;ue_wrap::FVector origin{},extent{};bool result=false;};
struct Render{float tolerance=0;bool result=false;};
Obj walker,player,mesh,controller,system,code,boundsFn,frustumFn,renderFn,otherMesh;
void* world=reinterpret_cast<void*>(1);
std::unordered_map<void*,int> serials;
bool gameThread=true,inFrustum=true,recent=true,badSize=false,badOverlap=false,readOK=true,setOK=true;
bool recycle=false,swapMesh=false,travel=false;
void* failCall=nullptr;int calls=0;
int cdoLookups=0;bool noCodeCdo=false;
ue_wrap::FVector origin{321,456,789},extent{11,22,33};
Bounds capturedBounds;Frustum capturedFrustum;Render capturedRender;
bool Live(void* p){return p&&serials.contains(p);}
}
namespace ue_wrap::reflection {
namespace cpf{constexpr uint64_t ReturnParm=0x400;}
struct ParamInfo{std::wstring name;int32_t offset=0,size=0;uint64_t flags=0;};
bool IsLive(void* p){return test::Live(p);}
void* ClassOf(void* p){return static_cast<test::Obj*>(p)->cls;}
bool PropertyOffsetAndClass(void*,const wchar_t* field,int32_t& offset,std::wstring& type){
 if(std::wcscmp(field,L"SkeletalMesh"))return false;offset=offsetof(test::Obj,mesh);type=L"ObjectProperty";return true;
}
void* FindClass(const wchar_t* name){return !std::wcscmp(name,L"KismetSystemLibrary")?&test::system:&test::code;}
void* FindClassDefaultObject(const wchar_t* name){++test::cdoLookups;return !std::wcscmp(name,L"KismetSystemLibrary")?&test::system:test::noCodeCdo?nullptr:&test::code;}
void* FindDispatchFunctionCached(void*,const wchar_t* name){
 if(!std::wcscmp(name,L"GetComponentBounds"))return &test::boundsFn;
 if(!std::wcscmp(name,L"isBoxInFrustum"))return &test::frustumFn;
 if(!std::wcscmp(name,L"WasRecentlyRendered"))return &test::renderFn;return nullptr;
}
std::vector<ParamInfo> FunctionParams(void* fn){
 if(fn==&test::boundsFn)return {{L"Component",offsetof(test::Bounds,component),8,0},{L"Origin",offsetof(test::Bounds,origin),12,0},{L"BoxExtent",offsetof(test::Bounds,extent),12,0},{L"SphereRadius",offsetof(test::Bounds,radius),4,0}};
 if(fn==&test::renderFn)return {{L"Tolerance",offsetof(test::Render,tolerance),4,0},{L"ReturnValue",offsetof(test::Render,result),1,cpf::ReturnParm}};
 auto fields=std::vector<ParamInfo>{{L"ActualControllerName",offsetof(test::Frustum,controller),8,0},{L"ActualCenterName",offsetof(test::Frustum,origin),12,0},{L"ActualExtentName",offsetof(test::Frustum,extent),12,0},{L"ActualResultName",offsetof(test::Frustum,result),1,cpf::ReturnParm}};
 if(test::badSize)fields[1].size=4;if(test::badOverlap)fields[2].offset=fields[1].offset;return fields;
}
int32_t FunctionFrameSize(void* fn){return fn==&test::boundsFn?sizeof(test::Bounds):fn==&test::renderFn?sizeof(test::Render):sizeof(test::Frustum);}
int32_t FindParamOffset(void* fn,const wchar_t* name){for(const auto& p:FunctionParams(fn))if(p.name==name)return p.offset;return -1;}
int32_t FindParamSize(void* fn,const wchar_t* name){for(const auto& p:FunctionParams(fn))if(p.name==name)return p.size;return -1;}
}
namespace ue_wrap {
namespace profile::cpf{constexpr uint64_t ReturnParm=reflection::cpf::ReturnParm;}
class CachedObjRef{void* p_=nullptr;int serial_=0;public:
 void Set(void* p){p_=p;serial_=p?test::serials.at(p):0;}
 void* Get()const{auto it=test::serials.find(p_);return it!=test::serials.end()&&it->second==serial_?p_:nullptr;}
 bool Is(void* p)const{return p&&p==Get();}void Reset(){p_=nullptr;serial_=0;}
};
namespace game_thread{bool IsGameThread(){return test::gameThread;}}
namespace world_identity{void* CurrentWorld(){return test::world;}void* WorldOf(void* p){return static_cast<test::Obj*>(p)->world;}}
namespace engine{void* GetController(void*){return &test::controller;}}
struct ParamFrame{void* fn;std::vector<uint8_t> bytes;explicit ParamFrame(void* f):fn(f),bytes(reflection::FunctionFrameSize(f)){}
 bool valid()const{return fn!=nullptr;}
 template<class T>bool Set(const wchar_t* name,const T& value){const int off=reflection::FindParamOffset(fn,name);if(!test::setOK||off<0||reflection::FindParamSize(fn,name)!=sizeof(T))return false;std::memcpy(bytes.data()+off,&value,sizeof(value));return true;}
 bool GetRaw(const wchar_t* name,void* out,size_t size)const{const int off=reflection::FindParamOffset(fn,name);if(!test::readOK||off<0||reflection::FindParamSize(fn,name)!=size)return false;std::memcpy(out,bytes.data()+off,size);return true;}
};
bool Call(void*,ParamFrame& frame){
 ++test::calls;if(frame.fn==test::failCall)return false;
 if(frame.fn==&test::boundsFn){auto& p=*reinterpret_cast<test::Bounds*>(frame.bytes.data());test::capturedBounds=p;p.origin=test::origin;p.extent=test::extent;
  if(test::recycle)++test::serials[&test::walker];if(test::swapMesh)test::walker.mesh=&test::otherMesh;if(test::travel)test::world=reinterpret_cast<void*>(2);
 }else if(frame.fn==&test::frustumFn){auto& p=*reinterpret_cast<test::Frustum*>(frame.bytes.data());test::capturedFrustum=p;p.result=test::inFrustum;
 }else{auto& p=*reinterpret_cast<test::Render*>(frame.bytes.data());test::capturedRender=p;p.result=test::recent;}return true;
}
}
#include "mannequin_native_sight.inc"
#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d %s\n",__func__,__LINE__,#x);return false;}}while(0)
void Reset(){
 ue_wrap::mannequin::g_systemCdo.Reset();ue_wrap::mannequin::g_codeCdo.Reset();ue_wrap::mannequin::g_systemMiss.Reset();ue_wrap::mannequin::g_codeMiss.Reset();
 test::world=reinterpret_cast<void*>(1);test::serials.clear();test::calls=0;
 test::cdoLookups=0;test::noCodeCdo=false;
 test::gameThread=test::inFrustum=test::recent=test::readOK=test::setOK=true;
 test::badSize=test::badOverlap=test::recycle=test::swapMesh=test::travel=false;test::failCall=nullptr;
 test::origin={321,456,789};test::extent={11,22,33};
 for(auto* p:{&test::walker,&test::player,&test::mesh,&test::controller,&test::system,&test::code,&test::boundsFn,&test::frustumFn,&test::renderFn,&test::otherMesh}){*p={};p->cls=p;p->world=test::world;test::serials[p]=1;}
 test::walker.mesh=&test::mesh;
}
bool PredicateAndInputs(){
 Reset();bool seen=false;
 CHECK(ue_wrap::mannequin::TryReadLocalSight(&test::walker,&test::player,seen)&&seen&&test::calls==3);
 CHECK(test::capturedBounds.component==&test::mesh&&test::capturedFrustum.controller==&test::controller);
 CHECK(test::capturedFrustum.origin.X==321&&test::capturedFrustum.origin.Z==789&&test::capturedFrustum.extent.Y==22&&test::capturedRender.tolerance==3.f);
 test::inFrustum=false;CHECK(ue_wrap::mannequin::TryReadLocalSight(&test::walker,&test::player,seen)&&!seen);
 test::inFrustum=true;test::recent=false;CHECK(ue_wrap::mannequin::TryReadLocalSight(&test::walker,&test::player,seen)&&!seen);
 CHECK(test::cdoLookups==2); // no full object-array walks on the per-frame path
 Reset();test::noCodeCdo=true;
 CHECK(!ue_wrap::mannequin::TryReadLocalSight(&test::walker,&test::player,seen));
 CHECK(!ue_wrap::mannequin::TryReadLocalSight(&test::walker,&test::player,seen)&&test::cdoLookups==2); // misses latch to class incarnation
 ++test::serials[&test::code];test::noCodeCdo=false;
 CHECK(ue_wrap::mannequin::TryReadLocalSight(&test::walker,&test::player,seen)&&test::cdoLookups==3);
 return true;
}
bool RefusalAndLifetimes(){
 for(int fault=0;fault<11;++fault){
  Reset();bool seen=true;
  if(fault==0)test::gameThread=false;if(fault==1)test::player.world=reinterpret_cast<void*>(2);
  if(fault==2)test::badSize=true;if(fault==3)test::badOverlap=true;if(fault==4)test::failCall=&test::frustumFn;
  if(fault==5)test::readOK=false;if(fault==6)test::setOK=false;if(fault==7)test::origin.X=std::numeric_limits<float>::quiet_NaN();
  if(fault==8)test::recycle=true;if(fault==9)test::swapMesh=true;if(fault==10)test::travel=true;
  CHECK(!ue_wrap::mannequin::TryReadLocalSight(&test::walker,&test::player,seen)&&!seen);
  if(fault<4||fault==6)CHECK(test::calls==0);
  if(fault>=7)CHECK(test::calls==1);
 }
 return true;
}
int main(){if(!PredicateAndInputs()||!RefusalAndLifetimes())return 1;std::puts("PASS actual native mannequin sight wrapper: real bounds/controller, native frustum AND recent-render(3s), schema/dispatch failures, world/actor/mesh lifetime guards");}
