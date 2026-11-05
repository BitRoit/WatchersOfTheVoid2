#include "coop/creatures/npc_spawn_pending.h"
#include <cstdio>
#include <cwchar>
#include <cstdlib>
struct Actor { const wchar_t* name; bool live=true, npc=false, parked=false; };
namespace ue_wrap::reflection {
struct FName { const wchar_t* text; };
bool IsLive(void* p) { return p && static_cast<Actor*>(p)->live; }
void* ClassOf(void* p) { return p; }
const FName& NameOf(void* p) { static FName name; name.text=static_cast<Actor*>(p)->name; return name; }
bool NameEquals(const FName& n,const wchar_t* s) { return !wcscmp(n.text,s); }
}
namespace coop::element {
using ElementId=uint32_t; constexpr ElementId kInvalidId=0;
enum class ElementType { Npc, WorldActor };
struct Element { Actor* actor; ElementType GetType() const {return actor->npc?ElementType::Npc:ElementType::WorldActor;} };
struct Registry {
 static Registry& Get(){static Registry r;return r;}
 ElementId EidForActor(void* p) const {return p?1:0;}
 const Element* Get(ElementId) const {static Element e; e.actor=current;return current?&e:nullptr;}
 static inline Actor* current=nullptr;
};
}
namespace coop::npc_mirror_brain { bool IsParkedClass(void* p){return static_cast<Actor*>(p)->parked;} }
#include "creature_hit.inc"
void Check(bool pass,const char* name){if(!pass){printf("FAIL %s\n",name);exit(1);}}
int main(){
 using namespace coop::npc_spawn_pending;
 Queue q; coop::net::EntitySpawnPayload p{}; p.elementId=1;p.locX=10;
 Check(q.Put(p,100),"initial pending spawn");
 coop::net::EntityPoseSnapshot pose{};pose.elementId=1;pose.x=99;pose.yaw=70;
 q.Pose(pose);q.Put(p,5000);
 Check(q.Size()==1 && q.Find(1)->expires==15100,"duplicate bounded expiry");
 Check(q.Find(1)->spawn.locX==99 && q.Find(1)->spawn.rotYaw==70 && q.Find(1)->hasPose,"latest pending pose");
 for(uint32_t i=2;i<=64;++i){p.elementId=i;Check(q.Put(p,100),"capacity admission");}
 p.elementId=65;Check(!q.Put(p,100),"bounded capacity");
 Check(q.Ready(500).size()==4,"per-pump work bound");
 q.Erase(1);Check(!q.Find(1),"destroy cancels retry");
 Check(q.Ready(15100).empty() && !q.Size(),"expiry cancels retry");
 q.Put(p,20000);q.Clear();Check(q.Ready(20001).empty(),"world and disconnect cleanup");
 Check(ClassPath(L"kavotia_C") && ClassPath(L"npc_krampus_C"),"verified canonical loading");
 Check(!ClassPath(L"/Game/evil") && !ClassPath(L"Character") && !ClassPath(L"geomOcta_C"),"untrusted load rejection");
 Actor shot{L"kavotiaShot_C"};coop::element::Registry::current=&shot;
 Check(coop::creature_hit::IsDelivered(&shot),"host Kavotia shot admission");
 shot.name=L"tentacleBallShot_C";Check(coop::creature_hit::IsDelivered(&shot),"host tentacle shot admission");
 shot.name=L"hoelUfoAttack_C";Check(coop::creature_hit::IsDelivered(&shot),"host lightning admission");
 shot.live=false;Check(!coop::creature_hit::IsDelivered(&shot),"dead source rejection");shot.live=true;
 shot.name=L"explosion_C";Check(!coop::creature_hit::IsDelivered(&shot),"separate explosion lane cannot double deliver");
 shot.name=L"npc_arirGunStealer_C";shot.npc=true;Check(!coop::creature_hit::IsDelivered(&shot),"unparked damage cannot double deliver");
 shot.parked=true;Check(coop::creature_hit::IsDelivered(&shot),"existing parked native creature delivery");
 Check(!coop::creature_hit::IsDelivered(nullptr),"null source rejection");
 puts("PASS entity pending lifecycle, canonical load trust and native damage classification");
}
