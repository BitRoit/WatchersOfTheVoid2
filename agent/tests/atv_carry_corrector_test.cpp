// Executes the unchanged production namespace with engine/clock boundaries mocked.
// The simple gravity integration is a regression model, not a PhysX rig simulation.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>

#define UE_LOGI(...) ((void)0)
#define UE_LOGW(...) ((void)0)
namespace ue_wrap {
struct FVector { float X=0,Y=0,Z=0; };
struct FRotator { float Pitch=0,Yaw=0,Roll=0; };
float NormalizeAxis(float x){x=std::fmod(x,360.f);return x>180?x-360:(x<-180?x+360:x);}
}
namespace test {
struct Rig {ue_wrap::FVector pos{},lin{},ang{};ue_wrap::FRotator rot{};int writes=0,angularWrites=0,teleports=0,planarWrites=0;bool teleportWorks=true,planarWorks=true;};
uint64_t ms=1000;
}
namespace coop::config_registry::rows {constexpr int atv_corrector=1;}
namespace coop::config {bool ResolveFlag(int){return true;}}
namespace coop::net {
constexpr uint8_t kAtvStateBitGrabbed=0x04;
constexpr uint8_t kAtvStateBitBrake=0x02;
struct AtvStatePayload {float x=0,y=0,z=0,pitch=0,yaw=0,roll=0,linVelX=0,linVelY=0,linVelZ=0,angVelX=0,angVelY=0,angVelZ=0;uint8_t stateBits=0,occupantSlot=0xFF,syncerSlot=0;};
}
namespace coop::atv_sync {
struct AtvEntry {void* actor=nullptr;uint64_t lastPktMs=0,lastRestPlaceMs=0,restAnchorSinceMs=0,lastCorrectionDecisionMs=0,lastPlanarCorrectionLogMs=0;uint8_t restAnchorSyncer=0xFF;ue_wrap::FVector restAnchorPos{},lastCorrectionWirePos{};bool haveCorrectionWirePos=false;ue_wrap::FRotator restAnchorRot{};float lastErrCm=-1;int stallPackets=0,restReplaces=0,seatedReplaces=0;};
uint64_t NowMs(){return test::ms;}
float Len(const ue_wrap::FVector& a){return std::sqrt(a.X*a.X+a.Y*a.Y+a.Z*a.Z);}
}
namespace ue_wrap::atv {
bool GetRootTransform(void* p,FVector& pos,FRotator& rot){auto& r=*static_cast<test::Rig*>(p);pos=r.pos;rot=r.rot;return true;}
std::wstring GetKeyString(void*){return L"fixture";}
bool TeleportRigPreservingShape(void* p,const FVector& pos,const FRotator& rot){auto& r=*static_cast<test::Rig*>(p);if(!r.teleportWorks)return false;r.pos=pos;r.rot=rot;++r.teleports;return true;}
bool MatchRigPlanarVelocity(void* p,const FVector& desired,FVector& before,int& writes){auto& r=*static_cast<test::Rig*>(p);before=r.lin;writes=0;if(!r.planarWorks)return false;if(std::fabs(r.lin.X-desired.X)>0.01f||std::fabs(r.lin.Y-desired.Y)>0.01f){r.lin.X=desired.X;r.lin.Y=desired.Y;++r.planarWrites;writes=4;}return true;}
}
namespace ue_wrap::engine {
void SetActorRootPhysicsAngularVelocity(void* p,const FVector& a){auto& r=*static_cast<test::Rig*>(p);r.ang=a;++r.angularWrites;}
void SetActorRootPhysicsVelocity(void* p,const FVector& v,const FVector& a){auto& r=*static_cast<test::Rig*>(p);r.lin=v;r.ang=a;++r.writes;}
}
namespace coop::atv_corrector {struct Counters{uint64_t corrections,warps,stallWarps;unsigned long long restPlaces;};}
#include "atv_corrector_body.inc"

#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);return false;}}while(0)
using coop::atv_corrector::ApplyCorrection;
using coop::atv_sync::AtvEntry;
using coop::net::AtvStatePayload;

bool HeldStill() {
    test::Rig r;r.pos.Z=160;AtvEntry e;e.actor=&r;e.restReplaces=3;e.lastRestPlaceMs=test::ms;
    AtvStatePayload p;p.z=160;p.stateBits=coop::net::kAtvStateBitGrabbed;
    float maxError=0;
    for(int packet=0;packet<400;++packet){
        test::ms+=50;ApplyCorrection(e,p,false);
        // Ten physics steps between packets; floor clamps the old falling mirror.
        for(int step=0;step<10;++step){r.lin.Z-=980.f*.005f;r.pos.Z+=r.lin.Z*.005f;if(r.pos.Z<0){r.pos.Z=0;r.lin.Z=0;}}
        maxError=std::max(maxError,std::fabs(p.z-r.pos.Z));
    }
    std::printf("HeldStill: max height error %.2f cm across 20 seconds; linear writes=%d restReplaces=%d\n",maxError,r.writes,e.restReplaces);
    CHECK(maxError<10.f);CHECK(r.writes==400);CHECK(e.restReplaces==0);return true;
}
bool ParkedProtection() {
    test::Rig r;AtvEntry e;e.actor=&r;AtvStatePayload p;p.z=8;
    for(int i=0;i<450;++i){r.pos.Z=0;test::ms+=200;ApplyCorrection(e,p,false);}
    CHECK(r.teleports==3);CHECK(r.writes==0);CHECK(e.restReplaces==3);
    // A real new author location starts a fresh episode, unlike elapsed time.
    p.x=20;test::ms+=200;ApplyCorrection(e,p,false);CHECK(r.teleports==4);return true;
}
bool ParkedReadinessRetry() {
    test::Rig r;r.teleportWorks=false;AtvEntry e;e.actor=&r;AtvStatePayload p;p.z=8;
    for(int i=0;i<30;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    CHECK(e.restReplaces==0);CHECK(r.teleports==0);
    r.teleportWorks=true;test::ms+=200;ApplyCorrection(e,p,false);
    CHECK(e.restReplaces==1);CHECK(r.teleports==1);return true;
}
bool GrabReleaseAndThrow() {
    test::Rig r;r.pos.Z=160;AtvEntry e;e.actor=&r;AtvStatePayload p;p.z=160;p.stateBits=coop::net::kAtvStateBitGrabbed;
    test::ms+=50;ApplyCorrection(e,p,false);CHECK(r.writes==1);
    p.stateBits=0;test::ms+=50;ApplyCorrection(e,p,false);CHECK(r.writes==1);
    p.linVelZ=-80;test::ms+=50;ApplyCorrection(e,p,false);CHECK(r.writes==2);CHECK(r.lin.Z==-80);
    p.linVelZ=0;p.z=0;r.pos.Z=0;test::ms+=50;ApplyCorrection(e,p,false);CHECK(r.writes==2);return true;
}
bool FailedWarpDoesNotWrite() {
    test::Rig r;r.teleportWorks=false;AtvEntry e;e.actor=&r;AtvStatePayload p;p.z=500;p.stateBits=coop::net::kAtvStateBitGrabbed;
    test::ms+=50;ApplyCorrection(e,p,true);CHECK(r.writes==0);CHECK(r.teleports==0);return true;
}
bool ParkedSuspensionNoise() {
    test::Rig r;AtvEntry e;e.actor=&r;AtvStatePayload p;
    // Same bounded pose, velocity spikes matching the client log (up to 72 cm/s).
    for(int i=0;i<8;++i){test::ms+=200;p.z=(i%2)*4.f;p.linVelZ=i%2?72.f:-13.f;ApplyCorrection(e,p,false);}
    const int settledWrites=r.writes;
    for(int i=0;i<80;++i){test::ms+=200;p.z=(i%2)*4.f;p.linVelZ=i%2?72.f:-13.f;p.angVelY=12.f;ApplyCorrection(e,p,false);}
    CHECK(r.writes==settledWrites);CHECK(r.angularWrites==0);
    // Actual movement out of that fixed anchor resumes physics replication immediately.
    test::ms+=200;p.x=20;p.linVelX=100;ApplyCorrection(e,p,false);CHECK(r.writes==settledWrites+1);
    std::printf("ParkedSuspensionNoise: no root writes across 16s of parked velocity spikes; movement resumes\n");return true;
}
bool SlowCoastAndDriver() {
    test::Rig r;AtvEntry e;e.actor=&r;AtvStatePayload p;p.linVelX=6;
    for(int i=0;i<30;++i){test::ms+=200;p.x+=1.2f;ApplyCorrection(e,p,false);}
    CHECK(r.writes==30); // cumulative travel crosses 5 cm before the 1 s confirmation
    p.occupantSlot=1;
    for(int i=0;i<10;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    CHECK(r.writes==40);CHECK(e.restAnchorSinceMs==0);return true;
}
bool PoseHistoryReset() {
    test::Rig r;AtvEntry e;e.actor=&r;AtvStatePayload p;p.linVelX=30;
    for(int i=0;i<8;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    int writes=r.writes;
    test::ms+=4000;ApplyCorrection(e,p,false);CHECK(r.writes==writes+1);
    for(int i=0;i<8;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    writes=r.writes;test::ms+=200;ApplyCorrection(e,p,true);CHECK(r.writes==writes+1);
    for(int i=0;i<8;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    writes=r.writes;p.syncerSlot=1;test::ms+=200;ApplyCorrection(e,p,false);CHECK(r.writes==writes+1);
    for(int i=0;i<8;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    writes=r.writes;p.yaw=10;test::ms+=200;ApplyCorrection(e,p,false);CHECK(r.writes==writes+1);
    for(int i=0;i<8;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    writes=r.writes;p.stateBits=coop::net::kAtvStateBitGrabbed;test::ms+=50;ApplyCorrection(e,p,false);
    CHECK(r.writes==writes+1);CHECK(e.restAnchorSinceMs==0);
    p.stateBits=0;test::ms+=50;ApplyCorrection(e,p,false);CHECK(r.writes==writes+2);return true;
}
bool UnbrakedPlanarDrift(){
    test::Rig r;r.pos.X=30;r.lin={20,-12,-17};AtvEntry e;e.actor=&r;AtvStatePayload p;
    for(int i=0;i<300;++i){
        test::ms+=200;ApplyCorrection(e,p,false);
        for(int step=0;step<10;++step){r.lin.X+=2.f*.02f;r.pos.X+=r.lin.X*.02f;r.pos.Y+=r.lin.Y*.02f;}
    }
    CHECK(std::hypot(r.pos.X,r.pos.Y)<5.5f);CHECK(r.teleports==0);CHECK(r.planarWrites>0);
    CHECK(r.lin.Z==-17);CHECK(r.writes==0);CHECK(p.stateBits==0);
    // Below the old 5cm/s threshold: slow host coasting must still travel 120cm.
    for(int i=0;i<600;++i){p.x+=.2f;p.linVelX=1;test::ms+=200;ApplyCorrection(e,p,false);r.pos.X+=r.lin.X*.2f;r.pos.Y+=r.lin.Y*.2f;}
    CHECK(p.x>119);CHECK(std::hypot(r.pos.X-p.x,r.pos.Y-p.y)<5.5f);CHECK(r.teleports==0);CHECK(p.stateBits==0);
    std::printf("UnbrakedPlanarDrift: 60s divergent mirror + 120s 1cm/s coast converge without teleports; Z retained\n");return true;
}
bool IdlePacketCadence(){
    for(uint64_t interval:{2000,2500}){
        test::Rig r;AtvEntry e;e.actor=&r;AtvStatePayload p;
        test::ms+=interval;ApplyCorrection(e,p,false);r.pos.X=100;r.lin.X=30;
        for(int i=0;i<50;++i){test::ms+=interval;ApplyCorrection(e,p,false);r.pos.X+=r.lin.X*(interval/1000.f);if(i==0)CHECK(std::fabs(r.pos.X-50)<.1f);}
        CHECK(std::fabs(r.pos.X)<5.5f);CHECK(r.teleports==0);
    }
    return true;
}
bool RestPoseVelocityNoise(){
    test::Rig r;AtvEntry e;e.actor=&r;AtvStatePayload p;p.linVelX=30;
    for(int i=0;i<10;++i){test::ms+=200;ApplyCorrection(e,p,false);}
    r.lin.X=25;r.pos.X=20;
    for(int i=0;i<200;++i){test::ms+=200;ApplyCorrection(e,p,false);r.pos.X+=r.lin.X*.2f;}
    CHECK(std::fabs(r.pos.X)<5.5f);CHECK(std::fabs(r.lin.X)<.1f);return true;
}
bool IdleWarpClearsResidual(){
    test::Rig r;r.pos.X=400;r.lin={80,-50,-7};AtvEntry e;e.actor=&r;AtvStatePayload p;p.linVelX=1;
    test::ms+=200;ApplyCorrection(e,p,true);CHECK(r.teleports==1);CHECK(r.lin.X==1&&r.lin.Y==0&&r.lin.Z==-7);return true;
}
bool SeatedRestProtection() {
    test::Rig r;AtvEntry e;e.actor=&r;AtvStatePayload p;p.occupantSlot=1;p.z=8;
    for(int i=0;i<400;++i){r.pos.Z=0;test::ms+=50;ApplyCorrection(e,p,false);}
    CHECK(r.teleports==3);CHECK(r.writes==0);CHECK(e.seatedReplaces==3);
    // Driving away resets seatedReplaces and applies continuous correction
    p.linVelX=20;test::ms+=50;ApplyCorrection(e,p,false);
    CHECK(e.seatedReplaces==0);CHECK(r.writes==1);
    // Stopping again allows another 3 re-places for settling
    p.linVelX=0;
    for(int i=0;i<400;++i){r.pos.Z=0;test::ms+=50;ApplyCorrection(e,p,false);}
    CHECK(r.teleports==6);CHECK(e.seatedReplaces==3);
    return true;
}
int main(){int failed=0;for(auto fn:{HeldStill,ParkedProtection,ParkedReadinessRetry,GrabReleaseAndThrow,FailedWarpDoesNotWrite,ParkedSuspensionNoise,SlowCoastAndDriver,PoseHistoryReset,UnbrakedPlanarDrift,IdlePacketCadence,RestPoseVelocityNoise,IdleWarpClearsResidual,SeatedRestProtection})if(!fn())++failed;if(failed)return 1;std::puts("PASS production ATV corrector: 13 scenarios including ATVS1 seated rest bound, unbraked drift/slow coast, 2s cadence, stable-pose noise and snapshot residual momentum");}
