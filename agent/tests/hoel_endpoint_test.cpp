#include "ue_wrap/actors/hoel_ufo.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 using namespace ue_wrap;using hoel_ufo::DecodeEndpoint;
 FVector p{123.5f,-4000.f,98000.f},out{1,2,3};
 CHECK(DecodeEndpoint(&p,sizeof(p),out));CHECK(out.X==p.X&&out.Y==p.Y&&out.Z==p.Z);
 CHECK(!DecodeEndpoint(nullptr,12,out));CHECK(!DecodeEndpoint(&p,0,out));
 CHECK(!DecodeEndpoint(&p,11,out));CHECK(!DecodeEndpoint(&p,13,out));
 for(int axis=0;axis<3;++axis)for(float bad:{std::numeric_limits<float>::quiet_NaN(),
     std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),1000001.f,-1000001.f}){
   p={1,2,3};(&p.X)[axis]=bad;out={7,8,9};
   CHECK(!DecodeEndpoint(&p,sizeof(p),out));CHECK(out.X==7&&out.Y==8&&out.Z==9);
 }
 p={1000000.f,-1000000.f,0};CHECK(DecodeEndpoint(&p,12,out));
 puts("PASS Hoel beam endpoint: exact size, finite bounds, all axes, untouched output on refusal");
}
