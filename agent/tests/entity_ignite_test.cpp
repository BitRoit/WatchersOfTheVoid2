#include "coop/net/protocol.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace coop::net {enum class Role{Host,Client};struct Session{Role mode=Role::Host;bool online=true;bool connected(){return online;}Role role(){return mode;}};}
namespace ue_wrap::script_gate {
struct Call {void* function=nullptr;uint8_t* locals=nullptr;void* object=nullptr;void* callerObject=nullptr;uintptr_t* scopeMark=nullptr;};
uintptr_t ScopeBitFor(const void*){return 1;}
void MarkScope(const Call& c,uintptr_t bit){if(c.scopeMark)*c.scopeMark|=bit;}
}
namespace ue_wrap::reflection {struct ParamOffset{const wchar_t* name;int Of(void* p){return p?0:-1;}};}
namespace coop::players {constexpr uint8_t kPeerIdHost=0,kMaxPeers=4;struct Registry{static Registry& Get(){static Registry r;return r;}uint8_t PeerIdOfActor(void* p){return p==reinterpret_cast<void*>(10)?1:0;}};}
namespace coop::creature_hit {bool IsDelivered(void* p){return p==reinterpret_cast<void*>(20);}}
namespace coop::player_hit {std::vector<net::PlayerHitPayload> sent;bool Deliver(uint8_t,const net::PlayerHitPayload& p){sent.push_back(p);return true;}}
namespace damage_fixture {
namespace sg=ue_wrap::script_gate;namespace R=ue_wrap::reflection;
coop::net::Session session;std::atomic<coop::net::Session*> g_session{&session};uint32_t g_delivered=0;
float ReadFloat(const sg::Call& c,R::ParamOffset&){return *reinterpret_cast<const float*>(c.locals);}
#include "entity_ignite.inc"
}
void Check(bool yes,const char* label){if(!yes){printf("FAIL %s\n",label);exit(1);}}
int main(){using namespace damage_fixture;
 float fuel=30;uintptr_t outer=0,nested=0;sg::Call c;c.function=&fuel;c.locals=reinterpret_cast<uint8_t*>(&fuel);c.object=reinterpret_cast<void*>(10);c.callerObject=reinterpret_cast<void*>(20);c.scopeMark=&outer;
 DeliverIgnite(c);Check(coop::player_hit::sent.size()==1 && coop::player_hit::sent.back().damage==30 && coop::player_hit::sent.back().verb==coop::net::kPlayerHitVerbIgnite,"native fuel retained");
 auto inner=c;inner.scopeMark=&nested;DeliverIgnite(inner);DeliverIgnite(c);
 Check(coop::player_hit::sent.size()==2,"nested calls cannot double-deliver outer ignite");
 outer=0;c.callerObject=nullptr;DeliverIgnite(c);Check(!outer&&coop::player_hit::sent.size()==2,"unowned fire excluded");c.callerObject=reinterpret_cast<void*>(20);
 c.object=nullptr;DeliverIgnite(c);Check(coop::player_hit::sent.size()==2,"host target excluded");c.object=reinterpret_cast<void*>(10);
 c.function=nullptr;DeliverIgnite(c);Check(coop::player_hit::sent.size()==2,"unresolved fuel metadata excluded");c.function=&fuel;
 c.scopeMark=nullptr;DeliverIgnite(c);Check(coop::player_hit::sent.size()==2,"missing dispatch mark fails closed");c.scopeMark=&outer;
 session.mode=coop::net::Role::Client;DeliverIgnite(c);Check(coop::player_hit::sent.size()==2,"client cannot author fire");
 puts("PASS production ignite delivery: native fuel, nested dual-watch deduplication and authority guards");
}
