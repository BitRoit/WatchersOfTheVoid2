#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/log.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace test {
struct Class {int index=0,serial=1,offset=0;bool live=true;} classes[80];
struct Actor {Class* cls=nullptr;} actor;
int resolutions=0,infos=0,warnings=0;
std::function<void()> duringLookup;
}
namespace ue_wrap::reflection {
void* ClassOf(void* obj){return obj?static_cast<test::Actor*>(obj)->cls:nullptr;}
int32_t InternalIndexOf(void* cls){return static_cast<test::Class*>(cls)->index;}
int32_t AllocateSlotSerial(int32_t i){return test::classes[i].serial;}
int32_t SlotSerial(int32_t i){return i>=0&&i<80?test::classes[i].serial:0;}
bool IsLiveByIndex(void* cls,int32_t i){return i>=0&&i<80&&cls==&test::classes[i]&&test::classes[i].live;}
int32_t FindPropertyOffset(void* cls,const wchar_t*){
 ++test::resolutions;int offset=static_cast<test::Class*>(cls)->offset;
 auto fn=std::move(test::duringLookup);test::duringLookup={};if(fn)fn();return offset;
}
std::wstring ClassNameOf(void*){return L"test_class";}
}
namespace ue_wrap::log {void Write(Level level,const char*,...){if(level==Level::Info)++test::infos;else ++test::warnings;}}
// Generated from the actual method in reflection_props.cpp by the runner.
#include "instance_offset_production.inc"
int main(){
 using namespace test;using ue_wrap::reflection::InstanceOffset;
 static_assert(sizeof(InstanceOffset)<1024,"Instance cache must remain bounded");
 for(int i=0;i<80;++i)classes[i]={i,1,100+i,true};
 InstanceOffset cache(L"growth");CHECK(cache.Of(nullptr)==-1);CHECK(cache.Of(&actor)==-1);
 for(int round=0;round<1000;++round)for(int i=0;i<32;++i){actor.cls=&classes[i];CHECK(cache.Of(&actor)==100+i);}
 CHECK(resolutions==32&&infos==0&&warnings==0);
 // A cached class at the same pointer/index can be a different incarnation.
 actor.cls=&classes[5];++classes[5].serial;classes[5].offset=800;
 CHECK(cache.Of(&actor)==800&&resolutions==33);
 classes[5].live=false;CHECK(cache.Of(&actor)==-1);classes[5].live=true;
 classes[5].offset=801;++classes[5].serial;CHECK(cache.Of(&actor)==801);
 InstanceOffset missing(L"missing");classes[6].offset=-1;classes[7].offset=-1;
 int start=resolutions,oldWarnings=warnings;
 for(int i=0;i<1000;++i){actor.cls=&classes[6+i%2];CHECK(missing.Of(&actor)==-1);}
 CHECK(resolutions==start+2&&warnings==oldWarnings+2);
 ++classes[6].serial;classes[6].offset=66;actor.cls=&classes[6];CHECK(missing.Of(&actor)==66);
 // A replacement while metadata is read cannot publish an old offset.
 actor.cls=&classes[8];duringLookup=[]{++classes[8].serial;classes[8].offset=888;};
 CHECK(missing.Of(&actor)==-1);CHECK(missing.Of(&actor)==888);
 // More distinct classes than the bounded working set still returns each layout;
 // eviction permits another lookup but never a wrong retained offset or INFO flood.
 InstanceOffset overflow(L"growth");
 for(int round=0;round<5;++round)for(int i=0;i<80;++i){actor.cls=&classes[i];CHECK(overflow.Of(&actor)==classes[i].offset);}
 CHECK(infos==0);
 std::puts("PASS production InstanceOffset: 32000 mixed-class reads use32 lookups; cached negative results and bounded overflow");
 std::puts("PASS class serial/address reuse, dead class refusal, retirement during resolution, null input and no success-log flood");
}
