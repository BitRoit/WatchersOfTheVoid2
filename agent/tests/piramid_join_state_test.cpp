#include "coop/net/protocol.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#define UE_LOGI(...) ((void)0)
#define UE_LOGW(...) ((void)0)
namespace coop::net { enum class Role {Host,Client}; }
namespace coop::element { struct Registry {static bool IsAllowedHostAllocatedEid(uint32_t id){return id>0 && id<32768;}}; }
namespace coop::piramid_sync {
struct Session {bool online=true;net::Role mode=net::Role::Client; bool connected(){return online;} net::Role role(){return mode;}} session;
Session* LoadSession(){return &session;}
long long NowMs(){return 1000;}
constexpr long long kReplayDeadlineMs=5000;
#include "piramid_pending.inc"
PendingGather g_pending;
#include "piramid_receive.inc"
}
void Check(bool yes,const char* label){if(!yes){printf("FAIL %s\n",label);exit(1);}}
int main(){using namespace coop::piramid_sync;
 coop::net::PyramidGatherStatePayload p{};p.gather={1,2};p.montagePosition=7.5f;p.playRate=1;p.timelinePosition[0]=1.8f;p.timelinePosition[1]=0.4f;
 OnPyramidGatherState(p);Check(g_pending.active&&g_pending.hasState&&g_pending.state.montagePosition==7.5f&&g_pending.receivedMs==1000,"native progress retained without ID packing");
 auto bad=p;bad.montagePosition=std::numeric_limits<float>::quiet_NaN();g_pending.active=false;OnPyramidGatherState(bad);Check(!g_pending.active,"NaN refusal");
 bad=p;bad.playRate=0;OnPyramidGatherState(bad);Check(!g_pending.active,"invalid rate refusal");
 bad=p;bad.timelineFlags=128;OnPyramidGatherState(bad);Check(!g_pending.active,"unknown flags refusal");
 bad=p;bad.reserved=1;OnPyramidGatherState(bad);Check(!g_pending.active,"reserved wire refusal");
 bad=p;bad.timelinePosition[2]=-1;OnPyramidGatherState(bad);Check(!g_pending.active,"negative timeline refusal");
 bad=p;bad.gather.wispEid=0;OnPyramidGatherState(bad);Check(!g_pending.active,"missing live wisp refusal");
 p.consumed=1;p.gather.wispEid=0;OnPyramidGatherState(p);Check(g_pending.active&&g_pending.state.consumed&&!g_pending.wispEid,"post-consumption tail without resurrecting wisp");
 g_pending.active=false;session.mode=coop::net::Role::Host;OnPyramidGatherState(p);Check(!g_pending.active,"host refusal");session.mode=coop::net::Role::Client;
 session.online=false;OnPyramidGatherState(p);Check(!g_pending.active,"offline refusal");session.online=true;
 OnPyramidGather({1,2});Check(g_pending.active&&!g_pending.hasState&&g_pending.wispEid==2,"legacy eight-byte relay compatibility");
 puts("PASS production pyramid join receiver: native phase, consumed tail, trust validation and legacy relay");
}
