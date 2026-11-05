#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <unordered_map>
#include <vector>
namespace ue_wrap {struct FVector{float X=0,Y=0,Z=0;};}
struct Actor{const wchar_t* name;ue_wrap::FVector pos;bool live=true;void* world=reinterpret_cast<void*>(1);};
Actor host{L"mainPlayer_C",{1000,0,0}},client{L"pawn_phys_C",{100,0,0}},creature{L"tentacleBall_C",{0,0,0}};
namespace ue_wrap::reflection {struct FName{const wchar_t* name;};bool IsLive(void* p){return p&&static_cast<Actor*>(p)->live;} void* ClassOf(void* p){return p;} const FName& NameOf(void* p){static FName n;n.name=static_cast<Actor*>(p)->name;return n;}bool NameEquals(const FName& n,const wchar_t* p){return !wcscmp(n.name,p);}}
namespace ue_wrap::world_identity {void* CurrentWorld(){return reinterpret_cast<void*>(1);}void* WorldOf(void* a){return static_cast<Actor*>(a)->world;}}
namespace ue_wrap::engine {bool TryGetActorLocation(void* a,FVector& p){if(!a)return false;p=static_cast<Actor*>(a)->pos;return true;}}
namespace ue_wrap::vitals {enum class Field{Dead};float dead=0;bool Read(Field,float* p){*p=dead;return true;}}
namespace coop::players {
constexpr uint8_t kMaxPeers=4,kPeerIdHost=0;
struct Peer{float health=1;bool valid(){return true;}bool HasPose(){return true;}float GetHealth(){return health;}void* GetActor(){return &client;}} peer;
struct Registry{static Registry& Get(){static Registry r;return r;}void* Local(){return &host;}Peer* Puppet(uint8_t i){return i==1?&peer:nullptr;}};
}
namespace coop::element {
struct Npc{bool mirror=false;bool IsMirror(){return mirror;}void* LiveActor(){return creature.live?&creature:nullptr;}} npc;
struct Manager{void Snapshot(std::vector<Npc*>& out){out={&npc};}} manager;
Manager& NpcMirrors(){return manager;}
}
namespace coop::served_player {
namespace R=ue_wrap::reflection;
struct Session {bool ready=true;bool connected(){return true;}bool IsSlotConnected(uint8_t i){return i==1&&ready;}bool IsSlotReady(uint8_t i){return IsSlotConnected(i);}bool IsSlotWorldReady(uint8_t i){return IsSlotConnected(i);}} session;
std::atomic<Session*> g_session{&session};uint64_t g_nextTargetMs=0,now=1000;
uint64_t TargetNowMs(){return now;}bool OnHost(){return true;}
constexpr uint8_t kPlayerPawn=1,kPlayerCharacter=2;
std::unordered_map<void*,uint8_t> records;
uint8_t ServedSlot(void* actor){auto it=records.find(actor);return it==records.end()?0:it->second;}
void Unserve(void* actor){records.erase(actor);}
bool Serve(void* actor,uint8_t slot,uint8_t){if(slot)records[actor]=slot;else Unserve(actor);return slot!=0;}
#include "creature_targets.inc"
void Step(){now+=101;UpdateCreatureTargets();}
}
void Check(bool yes,const char* label){if(!yes){printf("FAIL %s\n",label);exit(1);}}
int main(){using namespace coop::served_player;
 Step();Check(ServedSlot(&creature)==1,"closer client preference");
 host.pos.X=80;Step();Check(ServedSlot(&creature)==1,"stable distance tie");
 coop::players::peer.health=0;Step();Check(ServedSlot(&creature)==0,"dead client excluded");coop::players::peer.health=1;host.pos.X=1000;
 session.ready=false;Step();Check(ServedSlot(&creature)==0,"departed or unready client excluded");session.ready=true;
 client.world=nullptr;Step();Check(ServedSlot(&creature)==0,"different world excluded");client.world=reinterpret_cast<void*>(1);
 creature.name=L"kavotia_C";Step();Check(ServedSlot(&creature)==1,"Kavotia player read redirect");
 creature.name=L"antibreather_C";Step();Check(ServedSlot(&creature)==1,"Antibreather player read redirect");
 records.clear();creature.name=L"insomniac_C";Step();Check(records.empty(),"personal entity excluded");
 creature.name=L"kavotia_C";coop::element::npc.mirror=true;Step();Check(records.empty(),"client mirror excluded");coop::element::npc.mirror=false;
 Step();Check(ServedSlot(&creature)==1,"host native actor retained");
 coop::players::peer.health=0;ue_wrap::vitals::dead=1;Step();Check(records.empty(),"no living candidates clears preference");
 puts("PASS production creature target preference: nearer peer, stable tie, life/world/readiness and personal exclusions");
}
