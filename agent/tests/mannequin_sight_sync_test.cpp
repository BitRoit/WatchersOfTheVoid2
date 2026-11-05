// Executes the production network module with mocked engine/transport edges.
#include "coop/net/protocol.h"
#include "coop/net/wire_key_util.h"
#include "client_authorship.h"
#include "session_lanes.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#define UE_LOGI(...) ((void)0)
namespace test {
struct Actor {std::wstring key=L"walker";void* world=(void*)1;int serial=1;bool alive=true,walker=true;uint32_t eid=1;};
Actor actor,other;void* world=(void*)1;uint32_t worldGeneration=1;uint64_t now=10000;
bool settled=true,reverseBroken=false;int collections=0;std::vector<Actor*> tracked;
}
uint64_t GetTickCount64(){return test::now;}
namespace ue_wrap {
namespace reflection {
bool IsLive(void* p){return p&&static_cast<test::Actor*>(p)->alive;}
bool IsLiveByIndex(void* p,int){return IsLive(p);}
void* ClassOf(void* p){return p;}void* SuperStructOf(void*){return nullptr;}
const wchar_t* NameOf(void* p){return static_cast<test::Actor*>(p)->walker?L"prop_wMannequin_C":L"prop_C";}
bool NameEquals(const wchar_t* a,const wchar_t* b){return std::wcscmp(a,b)==0;}
}
namespace world_identity {void* CurrentWorld(){return test::world;}uint32_t Generation(){return test::worldGeneration;}void* WorldOf(void* p){return static_cast<test::Actor*>(p)->world;}}
namespace prop {std::wstring GetInteractableKeyString(void* p){return static_cast<test::Actor*>(p)->key;}}
class CachedObjRef {void* p_=nullptr;int serial_=0;void* world_=nullptr;public:
void Set(void* p){p_=p;serial_=static_cast<test::Actor*>(p)->serial;world_=static_cast<test::Actor*>(p)->world;}
bool Is(void* p)const{return p&&p==p_&&reflection::IsLive(p)&&static_cast<test::Actor*>(p)->serial==serial_;}
bool Alive()const{return Is(p_)&&world_==test::world;}void* Get()const{return Alive()?p_:nullptr;}
void* Raw()const{return p_;}void* StampedWorld()const{return world_;}
};
}
namespace coop::net {
inline constexpr uint8_t kMaxPeers=kPeerCapacity;
enum class Role{Host,Client};
class Session {public:
Role role_=Role::Host;bool live=true,accept=true;
uint64_t stopCount=0;uint64_t StopCount(){return stopCount;}
std::array<bool,kMaxPeers> connected_{},ready{},worldReady{};
std::array<uint32_t,kMaxPeers> generation{};
struct Sent {int slot;MannequinSightPayload p;};std::vector<Sent> sent;
bool running(){return live;}bool connected(){return live;}Role role(){return role_;}
bool IsSlotConnected(int slot){return connected_[slot];}bool IsSlotReady(int slot){return ready[slot];}
bool IsSlotWorldReady(int slot){return worldReady[slot];}uint32_t peerGenerationForSlot(int slot){return generation[slot];}
bool SendReliableToSlot(int slot,ReliableKind k,const void* p,int len){
if(!accept)return false;if(k!=ReliableKind::MannequinSight||len!=64)std::abort();
sent.push_back({slot,*static_cast<const MannequinSightPayload*>(p)});return true;}
bool SendReliable(ReliableKind k,const void* p,int len){return SendReliableToSlot(0,k,p,len);}
};
}
namespace coop::net_pump {bool IsWorldSettled(){return test::settled;}}
namespace coop::prop_element_tracker {
struct TrackedKeyEntry{void* actor;int internalIdx;std::wstring key;};
void CollectTrackedKeyEntries(std::vector<TrackedKeyEntry>& out){++test::collections;for(auto* a:test::tracked)out.push_back({a,1,a->key});}
uint32_t GetPropElementIdForActor(void* a){return static_cast<test::Actor*>(a)->eid;}
}
namespace coop::element {
enum class ElementType{Prop,Other};
struct Element {void* actor=nullptr;bool mirror=false;ElementType type=ElementType::Prop;std::string name="walker";int serial=1;
ElementType GetType(){return type;}bool IsMirror(){return mirror;}const std::string& GetName(){return name;}
void* LiveActor(){return ue_wrap::reflection::IsLive(actor)?actor:nullptr;}};
class Registry {public:std::unordered_map<uint32_t,Element> entries;
static Registry& Get(){static Registry r;return r;}static bool IsHostId(uint32_t id){return id>0&&id<1000;}
uint32_t EidForActor(void* actor){if(test::reverseBroken)return UINT32_MAX;for(auto& [eid,e]:entries)if(e.LiveActor()==actor&&static_cast<test::Actor*>(actor)->serial==e.serial)return eid;return UINT32_MAX;}
Element* Get(uint32_t id){auto it=entries.find(id);return it==entries.end()?nullptr:&it->second;}};
}
namespace coop::mannequin_sight_sync {void OnDisconnect();}
#include "mannequin_sight_controller.inc"
namespace M=coop::mannequin_sight_sync;
namespace N=coop::net;
int checks=0;
void Check(bool b,const char* name){++checks;if(!b){std::printf("FAIL %s\n",name);std::exit(1);}}
void Setup(N::Session& s,bool host=true){
M::OnDisconnect();test::actor={};test::other={};test::tracked={&test::actor};test::settled=true;test::reverseBroken=false;
test::world=(void*)1;++test::worldGeneration;test::actor.world=test::other.world=test::world;
coop::element::Registry::Get().entries={{1,{&test::actor,!host}}};
s={};s.role_=host?N::Role::Host:N::Role::Client;int slot=host?1:0;
s.connected_[slot]=s.ready[slot]=s.worldReady[slot]=true;s.generation[slot]=host?11:0;M::Tick(s);
}
N::MannequinSightPayload Ack(const N::MannequinSightPayload& hello){auto p=hello;p.op=2;return p;}
N::MannequinSightPayload Hello(uint64_t world=77,uint64_t challenge=88){N::MannequinSightPayload p{};p.version=1;p.op=1;p.world=world;p.challenge=challenge;return p;}
N::MannequinSightPayload Stand(const N::MannequinSightPayload& h,uint64_t token=99){auto p=h;p.op=4;p.token=token;p.eid=1;p.value=1;N::WireKeyFromString(L"walker",p.key);return p;}
int main(){
Check(N::kProtocolVersion==2217&&sizeof(N::MannequinSightPayload)==64,"wire identity retained");
Check(N::AuthorshipOf(N::ReliableKind::MannequinSight)==N::ClientAuthorship::Client,"client intent admitted");
Check(!N::IsClientRelayableReliableKind(N::ReliableKind::MannequinSight),"no blind relay");
Check(!N::IsPreWorldSendableKind(N::ReliableKind::MannequinSight),"world gate retained");
N::Session s;Setup(s);auto hello=s.sent.back().p;bool value=true;
int idleCollections=test::collections;M::Tick(s);Check(test::collections==idleCollections,"idle pump without walker rows does not collect tracked props");
M::PublishHostStand(&test::actor,false);M::Tick(s);
Check(s.sent.size()==1,"no stand to unnegotiated peer");
auto bad=Ack(hello);++bad.challenge;M::Receive(s,bad,1);M::Tick(s);Check(s.sent.size()==1,"wrong handshake challenge rejected");
M::Receive(s,Ack(hello),1);M::Tick(s);auto stand=s.sent.back().p;
Check(stand.op==4&&stand.value==0&&stand.token&&stand.eid==1,"host publishes actual false stand");
auto sight=stand;sight.op=3;sight.value=0;
Check(!M::TryGetRemoteSight(1,&test::actor,value),"absence distinct from false");
M::Receive(s,sight,1);Check(M::TryGetRemoteSight(1,&test::actor,value)&&!value,"fresh false observation accepted");
auto original=sight;
for(int field=0;field<9;++field){
auto p=original;p.value=1;
switch(field){case 0:++p.world;break;case 1:++p.challenge;break;case 2:++p.token;break;case 3:++p.eid;break;case 4:p.key.data[0]='X';break;case 5:p.reserved=1;break;case 6:p.version=2;break;case 7:p.op=4;break;case 8:p.key.data[30]='x';break;}
M::Receive(s,p,1);Check(M::TryGetRemoteSight(1,&test::actor,value)&&!value,"invalid observation cannot overwrite false");}
M::Receive(s,original,0);Check(!M::TryGetRemoteSight(0,&test::actor,value),"host rejects slot zero observation");
s.connected_[2]=s.ready[2]=s.worldReady[2]=true;s.generation[2]=22;M::Tick(s);M::Receive(s,original,2);Check(!M::TryGetRemoteSight(2,&test::actor,value),"another occupant cannot borrow negotiated peer capability");
s.worldReady[1]=false;Check(!M::TryGetRemoteSight(1,&test::actor,value),"host load gate immediately refuses observation");s.worldReady[1]=true;
test::now+=2001;Check(!M::TryGetRemoteSight(1,&test::actor,value),"remote observation expires");
M::Receive(s,original,1);test::actor.key=L"changed";Check(!M::TryGetRemoteSight(1,&test::actor,value),"native rekey invalidates cached index immediately");auto rekeyed=original;rekeyed.value=1;M::Receive(s,rekeyed,1);test::actor.key=L"walker";Check(M::TryGetRemoteSight(1,&test::actor,value)&&!value,"native key change refuses incoming observation before next tick");
coop::element::Registry::Get().entries[1].actor=&test::other;Check(!M::TryGetRemoteSight(1,&test::actor,value),"registry rebind invalidates cached observation immediately");coop::element::Registry::Get().entries[1].actor=&test::actor;
int collections=test::collections;for(int i=0;i<100;++i){M::PublishHostStand(&test::actor,false);M::TryGetRemoteSight(1,&test::actor,value);}Check(test::collections==collections,"callbacks reuse tick uniqueness index");M::Tick(s);Check(test::collections==collections+1,"one tracker collection per tick");
test::other.walker=false;test::tracked.push_back(&test::other);M::Tick(s);Check(!M::TryGetRemoteSight(1,&test::actor,value),"nonwalker duplicate key rejects observation");
Setup(s);hello=s.sent.back().p;M::Receive(s,Ack(hello),1);M::PublishHostStand(&test::actor,true);M::Tick(s);sight=s.sent.back().p;sight.op=3;M::Receive(s,sight,1);++test::actor.serial;Check(!M::TryGetRemoteSight(1,&test::actor,value),"same pointer recycled actor rejected");
Setup(s);hello=s.sent.back().p;M::Receive(s,Ack(hello),1);M::PublishHostStand(&test::actor,true);M::Tick(s);sight=s.sent.back().p;sight.op=3;M::Receive(s,sight,1);Check(M::TryGetRemoteSight(1,&test::actor,value),"generation test has a fresh observation");++s.generation[1];Check(!M::TryGetRemoteSight(1,&test::actor,value),"occupant generation invalidates observation");M::Tick(s);auto hello2=s.sent.back().p;Check(hello2.op==1&&hello2.challenge!=hello.challenge,"replacement occupant negotiates fresh challenge");M::Receive(s,Ack(hello),1);Check(!M::CurrentPeer(1),"old occupant ack rejected");
Setup(s);for(int i=0;i<8;++i){test::now+=2100;M::Tick(s);}Check(s.sent.size()==3,"old peer limited to three feature probes");
Setup(s,false);Check(s.generation[0]==0,"client host slot has no generation");auto h=Hello();M::Receive(s,Stand(h),0);M::ReportLocalSight(&test::actor,true);M::Tick(s);Check(s.sent.empty()&&!M::TryGetHostStand(&test::actor,value),"client silent without negotiated host feature");test::settled=false;M::Tick(s);M::Receive(s,h,0);Check(s.sent.empty(),"slot0 world bit cannot bypass local load readiness");test::settled=true;M::Tick(s);M::Receive(s,h,0);Check(s.sent.size()==1&&s.sent.back().p.op==2,"settled client acknowledges feature");
stand=Stand(h);M::Receive(s,stand,0);Check(M::TryGetHostStand(&test::actor,value)&&value,"client accepts host actual stand");M::ReportLocalSight(&test::actor,false);M::Tick(s);sight=s.sent.back().p;Check(sight.op==3&&sight.value==0&&sight.world==h.world&&sight.challenge==h.challenge&&sight.token==stand.token,"native false sight echoes complete host binding");
auto sent=s.sent.size();M::Tick(s);Check(s.sent.size()==sent,"stable sample throttled");test::now+=400;M::Tick(s);Check(s.sent.size()==sent+1,"fresh sample heartbeat");test::now+=1001;M::Tick(s);Check(s.sent.size()==sent+1,"old local sample not kept alive by heartbeat");test::now+=1000;Check(!M::TryGetHostStand(&test::actor,value),"host stand expires");
M::Receive(s,stand,1);Check(!M::TryGetHostStand(&test::actor,value),"client refuses nonhost sender");
M::Receive(s,stand,0);M::ReportLocalSight(&test::actor,true);s.accept=false;M::Tick(s);sent=s.sent.size();s.accept=true;M::Tick(s);Check(s.sent.size()==sent+1,"refused send retried");
h=Hello(78,89);M::Receive(s,h,0);M::Receive(s,stand,0);Check(!M::TryGetHostStand(&test::actor,value),"old world challenge stand refused");
stand=Stand(h);M::Receive(s,stand,0);++test::worldGeneration;Check(!M::TryGetHostStand(&test::actor,value),"same pointer world generation invalidates state");M::Tick(s);Check(!M::TryGetHostStand(&test::actor,value),"world change requires renegotiation");
M::OnDisconnect();Check(!M::TryGetHostStand(&test::actor,value),"disconnect clears state");
Setup(s,false);M::OnDisconnect();M::Receive(s,Hello(),0);Check(s.sent.size()==1&&s.sent.back().p.op==2,"hello accepted before first subsystem tick");M::Tick(s);stand=Stand(Hello());M::Receive(s,stand,0);Check(M::TryGetHostStand(&test::actor,value),"pre-tick handshake remains valid after indexing");
++s.stopCount;Check(!M::TryGetHostStand(&test::actor,value),"same session stop count immediately invalidates old binding");M::Receive(s,stand,0);Check(!M::TryGetHostStand(&test::actor,value),"restart without intervening tick cannot accept old stand");M::Receive(s,Hello(90,91),0);M::Tick(s);stand=Stand(Hello(90,91));M::Receive(s,stand,0);Check(M::TryGetHostStand(&test::actor,value),"same session restart renegotiates without stale state");
++test::actor.serial;M::Tick(s);M::Receive(s,stand,0);Check(!M::TryGetHostStand(&test::actor,value),"old token cannot rebind recycled client actor without authoritative mirror reassert");coop::element::Registry::Get().entries[1].serial=test::actor.serial;M::Receive(s,stand,0);Check(M::TryGetHostStand(&test::actor,value),"authoritative mirror reassert permits same host token on replacement rendering");auto newer=stand;++newer.token;M::Receive(s,newer,0);Check(M::TryGetHostStand(&test::actor,value),"new host incarnation binds replacement actor");stand.value=0;M::Receive(s,stand,0);Check(M::TryGetHostStand(&test::actor,value)&&value,"old token cannot replace newer host incarnation");
// Production fuzzy-drive adoption layout from the 3-Nov client log: the host
// mirror carries key/eid while native/tracker keys remain client-local. No
// tracker alias is inserted; the channel must wait for the real mirror bind.
Setup(s,false);h=Hello();M::Receive(s,h,0);auto& registry=coop::element::Registry::Get();registry.entries.clear();
test::actor.key=L"client-local-native-key";stand=Stand(h);stand.eid=773;N::WireKeyFromString(L"VB947f6qd0_9h5r8-caAog",stand.key);
M::Receive(s,stand,0);Check(!M::TryGetHostStand(&test::actor,value),"host stand waits for authoritative PropSpawn adoption");
registry.entries[773]={&test::actor,true,coop::element::ElementType::Prop,"VB947f6qd0_9h5r8-caAog"};M::Tick(s);M::Receive(s,stand,0);
Check(M::TryGetHostStand(&test::actor,value)&&value,"fuzzy driven mirror accepts host stand despite divergent native key");
M::ReportLocalSight(&test::actor,false);M::Tick(s);sight=s.sent.back().p;Check(sight.op==3&&sight.eid==773&&sight.token==stand.token&&N::StringFromWireKey(sight.key)==N::StringFromWireKey(stand.key)&&sight.value==0,"fuzzy mirror native sight echoes canonical host key and eid");
Check(test::actor.key==L"client-local-native-key","binding does not rewrite native key during carry");
test::tracked.clear();M::Tick(s);Check(M::TryGetHostStand(&test::actor,value),"authoritative mirror does not need tracker alias");
auto& mirror=registry.entries[773];mirror.mirror=false;Check(!M::TryGetHostStand(&test::actor,value),"local registry row cannot impersonate host mirror");mirror.mirror=true;
mirror.type=coop::element::ElementType::Other;Check(!M::TryGetHostStand(&test::actor,value),"wrong element type cannot bind mannequin channel");mirror.type=coop::element::ElementType::Prop;
mirror.name="different-host-key";Check(!M::TryGetHostStand(&test::actor,value),"wrong authoritative mirror key rejected");M::Receive(s,stand,0);mirror.name="VB947f6qd0_9h5r8-caAog";
test::reverseBroken=true;Check(!M::TryGetHostStand(&test::actor,value),"missing reverse identity refuses mirror binding");auto brokenReverse=stand;brokenReverse.value=0;M::Receive(s,brokenReverse,0);test::reverseBroken=false;Check(M::TryGetHostStand(&test::actor,value)&&value,"broken reverse prevents incoming stand overwrite");
mirror.actor=&test::other;Check(!M::TryGetHostStand(&test::actor,value),"mirror rebound away from native actor invalidates prior observation");mirror.actor=&test::actor;
test::actor.world=(void*)2;Check(!M::TryGetHostStand(&test::actor,value),"foreign world mirror cannot bind channel");test::actor.world=test::world;
Setup(s);h=s.sent.back().p;M::Receive(s,Ack(h),1);test::actor.key=L"VB947f6qd0_9h5r8-caAog";test::actor.eid=773;registry.entries.clear();registry.entries[773]={&test::actor,false};M::Tick(s);M::PublishHostStand(&test::actor,true);M::Tick(s);auto canonical=s.sent.back().p;
// Switch between endpoint snapshots so the unmodified bytes emitted by this
// host traverse client receipt/sampling and return to that exact host binding.
auto hostRows=M::g_rows;auto hostPeers=M::g_peers;auto hostEpoch=M::g_epoch;
M::OnDisconnect();s.role_=N::Role::Client;s.connected_[0]=s.ready[0]=s.worldReady[0]=true;M::Tick(s);
h=canonical;h.op=1;h.token=0;h.eid=0;h.value=0;h.key={};M::Receive(s,h,0);
test::other.key=L"different-client-native-key";test::tracked={&test::other};registry.entries.clear();registry.entries[773]={&test::other,true,coop::element::ElementType::Prop,"VB947f6qd0_9h5r8-caAog"};M::Receive(s,canonical,0);
Check(M::TryGetHostStand(&test::other,value)&&value,"actual host packet drives divergent-key client mirror");M::ReportLocalSight(&test::other,false);M::Tick(s);sight=s.sent.back().p;
Check(sight.op==3&&sight.world==canonical.world&&sight.challenge==canonical.challenge&&sight.token==canonical.token&&sight.eid==canonical.eid&&std::memcmp(&sight.key,&canonical.key,sizeof(sight.key))==0,"round-trip observation preserves every host binding byte");
M::OnDisconnect();s.role_=N::Role::Host;test::tracked={&test::actor};registry.entries.clear();registry.entries[773]={&test::actor,false};M::Tick(s);M::g_rows=hostRows;M::g_peers=hostPeers;M::g_epoch=hostEpoch;
M::Receive(s,sight,1);Check(M::TryGetRemoteSight(1,&test::actor,value)&&!value,"unmodified client native sight accepted by originating host");
std::printf("PASS mannequin sight production network: %d checks\n",checks);
}
