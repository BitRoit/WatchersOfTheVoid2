#include "coop/creatures/mannequin_sight_sync.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "coop/session/net_pump.h"
#include "coop/props/prop_element_tracker.h"
#include "coop/element/registry.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/world_identity.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace coop::mannequin_sight_sync {
namespace {
namespace R=ue_wrap::reflection;
namespace WI=ue_wrap::world_identity;
using Packet=net::MannequinSightPayload;
enum Op:uint8_t{Hello=1,Ack=2,Sight=3,Stand=4};
constexpr uint64_t kHeartbeat=400,kExpiry=2000,kSampleExpiry=1000;
constexpr size_t kMaxWalkers=128;
struct Peer {uint32_t generation=0;uint64_t challenge=0,lastHello=0;unsigned tries=0;bool supported=false;};
struct Sample {bool known=false,value=false;uint64_t at=0;};
struct Row {
    ue_wrap::CachedObjRef actor;
    uint32_t eid=0;uint64_t token=0;
    Sample local,host;
    std::array<Sample,net::kMaxPeers> remote{};
    std::array<uint64_t,net::kMaxPeers> sent{};
    std::array<int8_t,net::kMaxPeers> sentValue{};
    Row(){sentValue.fill(-1);}
};
net::Session* g_session=nullptr;
void* g_world=nullptr;
uint64_t g_epoch=0,g_serial=0,g_remoteEpoch=0;
uint64_t g_stopCount=0;
uint32_t g_worldGeneration=0;
bool g_reportedLongKey=false;
std::array<Peer,net::kMaxPeers> g_peers{};
std::unordered_map<std::wstring,Row> g_rows;
struct IndexedActor {ue_wrap::CachedObjRef actor;bool duplicate=false;};
std::unordered_map<std::wstring,IndexedActor> g_index;
bool g_indexFresh=false;
uint64_t Now(){return GetTickCount64();}
uint64_t Mint(){if(!g_serial)g_serial=(Now()<<16)|1;return ++g_serial;}
bool Active(){return g_session&&g_session->StopCount()==g_stopCount&&g_session->running()&&g_session->connected()&&g_world&&WI::CurrentWorld()==g_world&&WI::Generation()==g_worldGeneration;}
bool Host(){return Active()&&g_session->role()==net::Role::Host;}
bool Ready(int slot){
    if(!Active()||slot<0||slot>=net::kMaxPeers||!g_session->IsSlotConnected(slot)||!g_session->IsSlotReady(slot))return false;
    // Slot 0's world-ready bit opens the send gate toward the host at connect,
    // before the client's save has loaded. The announce owner knows local readiness.
    return Host()?g_session->IsSlotWorldReady(slot):(slot==0&&net_pump::IsWorldSettled());
}
bool Fresh(const Sample& s,uint64_t limit=kExpiry){const auto now=Now();return s.known&&now>=s.at&&now-s.at<=limit;}
bool Walker(void* actor){
    if(!actor||!R::IsLive(actor)||WI::WorldOf(actor)!=g_world)return false;
    for(void* cls=R::ClassOf(actor);cls;cls=R::SuperStructOf(cls))
        if(R::NameEquals(R::NameOf(cls),L"prop_wMannequin_C"))return true;
    return false;
}
bool Key(void* actor,std::wstring& key){
    if(!Walker(actor))return false;
    key=ue_wrap::prop::GetInteractableKeyString(actor);
    if(key.size()>31){
        if(!g_reportedLongKey){UE_LOGI("mannequin sight: native walker key exceeds WireKey limit (%zu); binding refused",key.size());g_reportedLongKey=true;}
        return false;
    }
    if(key.empty()||key==L"None")return false;
    for(wchar_t c:key)if(c<33||c>126)return false;
    return true;
}
// Once per pump, include all tracked props so even a non-walker key collision
// refuses the binding. Cached serial/world and the current native key are still
// checked at every use; a stale entry never authorizes an observation.
void IndexWorld(){
    g_index.clear();
    std::vector<prop_element_tracker::TrackedKeyEntry> entries;
    prop_element_tracker::CollectTrackedKeyEntries(entries);
    for(const auto& e:entries){
        if(e.key.empty()||!R::IsLiveByIndex(e.actor,e.internalIdx)||WI::WorldOf(e.actor)!=g_world)continue;
        auto [it,inserted]=g_index.try_emplace(e.key);
        if(inserted)it->second.actor.Set(e.actor);
        else if(!it->second.actor.Is(e.actor))it->second.duplicate=true;
    }
    g_indexFresh=true;
}
void* Unique(const std::wstring& key){
    if(!g_indexFresh)IndexWorld();
    auto it=g_index.find(key);if(it==g_index.end()||it->second.duplicate)return nullptr;
    void* actor=it->second.actor.Get();std::wstring current;
    return actor&&it->second.actor.StampedWorld()==g_world&&Key(actor,current)&&current==key?actor:nullptr;
}
bool HostBinding(void* actor,uint32_t& eid){
    eid=prop_element_tracker::GetPropElementIdForActor(actor);
    auto* e=element::Registry::Get().Get(eid);
    return element::Registry::IsHostId(eid)&&e&&e->GetType()==element::ElementType::Prop&&!e->IsMirror()&&e->LiveActor()==actor;
}
bool MirrorKey(element::Element* entity,std::wstring& key){
    if(!entity)return false;
    const auto& name=entity->GetName();
    if(name.empty()||name.size()>31||name=="None")return false;
    key.clear();key.reserve(name.size());
    for(unsigned char c:name){if(c<33||c>126)return false;key.push_back(wchar_t(c));}
    return true;
}
bool ClientBinding(uint32_t eid,const std::wstring& key,void* actor){
    auto& registry=element::Registry::Get();auto* entity=registry.Get(eid);std::wstring canonical;
    // Fuzzy adoption deliberately skips native rekey while held/driven. The
    // host's PropSpawn mirror owns the wire key; native local keys may differ.
    // Require its complete 1:1 binding instead of inventing a key-only alias.
    return element::Registry::IsHostId(eid)&&entity&&entity->IsMirror()&&
           entity->GetType()==element::ElementType::Prop&&entity->LiveActor()==actor&&
           registry.EidForActor(actor)==eid&&Walker(actor)&&MirrorKey(entity,canonical)&&canonical==key;
}
bool Valid(const Packet& p){
    if(p.version!=1||p.op<Hello||p.op>Stand||p.value>1||p.reserved||!p.world||!p.challenge)return false;
    if(p.op==Hello||p.op==Ack){
        Packet empty{};empty.version=1;empty.op=p.op;empty.world=p.world;empty.challenge=p.challenge;
        return std::memcmp(&p,&empty,sizeof(p))==0;
    }
    if(!p.token||!p.eid||!element::Registry::IsHostId(p.eid)||!p.key.len||p.key.len>31)return false;
    for(size_t i=0;i<31;++i){const unsigned char c=p.key.data[i];if(i<p.key.len?(c<33||c>126):c!=0)return false;}
    return true;
}
Packet Make(Op op,int slot,const std::wstring& key={},const Row* row=nullptr,bool value=false){
    Packet p{};p.version=1;p.op=op;p.value=value;p.world=Host()?g_epoch:g_remoteEpoch;p.challenge=g_peers[slot].challenge;
    if(row){p.token=row->token;p.eid=row->eid;net::WireKeyFromString(key,p.key);}return p;
}
bool Send(int slot,const Packet& p){return Host()?g_session->SendReliableToSlot(slot,net::ReliableKind::MannequinSight,&p,sizeof(p)):g_session->SendReliable(net::ReliableKind::MannequinSight,&p,sizeof(p));}
void ResetWorld(void* world){g_rows.clear();g_index.clear();g_indexFresh=false;g_peers={};g_reportedLongKey=false;g_remoteEpoch=0;g_world=world;g_worldGeneration=WI::Generation();g_epoch=world?Mint():0;}
bool CurrentPeer(int slot){
    if(!Ready(slot))return false;
    const auto gen=Host()?g_session->peerGenerationForSlot(slot):1u;
    return gen&&g_peers[slot].generation==gen&&g_peers[slot].supported;
}
Row* Existing(void* actor,std::wstring& key){
    if(!Active())return nullptr;
    if(Host()){if(!Key(actor,key))return nullptr;}
    else{
        const auto eid=element::Registry::Get().EidForActor(actor);
        if(!MirrorKey(element::Registry::Get().Get(eid),key)||!ClientBinding(eid,key,actor))return nullptr;
    }
    auto it=g_rows.find(key);
    if(it==g_rows.end()||!it->second.actor.Is(actor))return nullptr;
    if(Host()&&Unique(key)!=actor)return nullptr;
    if(!Host()&&!ClientBinding(it->second.eid,key,actor))return nullptr;
    if(Host()){uint32_t eid=0;if(!HostBinding(actor,eid)||eid!=it->second.eid)return nullptr;}
    return &it->second;
}
} // namespace
void Install(net::Session* session){
    if(g_session!=session||(session&&session->StopCount()!=g_stopCount)){
        OnDisconnect();g_session=session;g_stopCount=session?session->StopCount():0;
    }
}
void Tick(net::Session& session){
    Install(&session);
    void* world=WI::CurrentWorld();if(world!=g_world||WI::Generation()!=g_worldGeneration)ResetWorld(world);
    if(!Active())return;
    g_indexFresh=false;
    const uint64_t now=Now();
    const bool host=Host();
    for(int slot=host?1:0;slot<(host?net::kMaxPeers:1);++slot){
        auto& peer=g_peers[slot];const uint32_t generation=host?session.peerGenerationForSlot(slot):1u;
        if(!Ready(slot)||!generation||generation!=peer.generation){
            peer=Peer{};peer.generation=Ready(slot)?generation:0;
            for(auto& kv:g_rows){kv.second.remote[slot]={};kv.second.sent[slot]=0;kv.second.sentValue[slot]=-1;}
            if(!host){g_rows.clear();g_remoteEpoch=0;}
        }
        if(!host||!Ready(slot)||!generation||peer.supported)continue;
        if(!peer.challenge)peer.challenge=Mint();
        // At most three probes per world/connection: old DLLs otherwise log unknown kind forever.
        if(peer.tries<3&&(!peer.tries||now-peer.lastHello>=2000)){
            if(Send(slot,Make(Hello,slot))){++peer.tries;peer.lastHello=now;}
        }
    }
    for(auto it=g_rows.begin();it!=g_rows.end();){
        auto& row=it->second;
        if(!row.actor.Alive()||row.actor.StampedWorld()!=g_world||
           (host?Unique(it->first)!=row.actor.Raw():!ClientBinding(row.eid,it->first,row.actor.Raw()))) {
            if(host){it=g_rows.erase(it);continue;}
            // Retain the last host token across a local rendering replacement;
            // only a current authoritative mirror can bind its successor.
            row.host={};row.local={};row.sent={};row.sentValue.fill(-1);++it;continue;
        }
        if(host){uint32_t eid=0;if(!HostBinding(row.actor.Get(),eid)||eid!=row.eid){it=g_rows.erase(it);continue;}}
        const Sample sample=host?row.host:row.local;
        if(Fresh(sample,kSampleExpiry))for(int slot=host?1:0;slot<(host?net::kMaxPeers:1);++slot){
            if(!CurrentPeer(slot))continue;
            if(row.sentValue[slot]!=int(sample.value)||now-row.sent[slot]>=kHeartbeat){
                if(Send(slot,Make(host?Stand:Sight,slot,it->first,&row,sample.value))){row.sentValue[slot]=sample.value;row.sent[slot]=now;}
            }
        }
        ++it;
    }
}
void PublishHostStand(void* actor,bool stand){
    if(!Host())return;
    std::wstring key;uint32_t eid=0;
    if(!Key(actor,key)||Unique(key)!=actor||!HostBinding(actor,eid))return;
    auto it=g_rows.find(key);
    if(it==g_rows.end()){if(g_rows.size()>=kMaxWalkers)return;it=g_rows.emplace(key,Row{}).first;}
    auto& row=it->second;
    if(!row.actor.Is(actor)||row.eid!=eid){
        row=Row{};row.actor.Set(actor);row.eid=eid;row.token=Mint();
        UE_LOGI("mannequin sight: host walker bound eid=%u token=%llu",eid,static_cast<unsigned long long>(row.token));
    }
    row.host={true,stand,Now()};
}
void ReportLocalSight(void* actor,bool seen){
    if(!Active()||Host()||!CurrentPeer(0))return;
    std::wstring key;if(auto* row=Existing(actor,key))row->local={true,seen,Now()};
}
bool TryGetRemoteSight(uint8_t slot,void* actor,bool& seen){
    if(!Host()||slot==0||slot>=net::kMaxPeers||!CurrentPeer(slot))return false;
    std::wstring key;auto* row=Existing(actor,key);if(!row||!Fresh(row->remote[slot]))return false;
    seen=row->remote[slot].value;return true;
}
bool TryGetHostStand(void* actor,bool& stand){
    if(!Active()||Host()||!CurrentPeer(0))return false;
    std::wstring key;auto* row=Existing(actor,key);if(!row||!Fresh(row->host))return false;
    stand=row->host.value;return true;
}
void Receive(net::Session& session,const Packet& p,int slot){
    // Inbox draining can precede this subsystem's first Tick, including after a
    // restart of the same Session object. Never borrow the previous run/world.
    Install(&session);
    void* world=WI::CurrentWorld();if(world!=g_world||WI::Generation()!=g_worldGeneration)ResetWorld(world);
    if(!Active()||!Valid(p)||!Ready(slot))return;
    const bool host=Host();
    if((host&&(slot<=0||slot>=net::kMaxPeers))||(!host&&slot!=0))return;
    const auto generation=host?session.peerGenerationForSlot(slot):1u;
    if(!generation)return;
    if(g_peers[slot].generation!=generation){
        if(host||p.op!=Hello)return;
        g_peers[slot]=Peer{};g_peers[slot].generation=generation;g_rows.clear();g_remoteEpoch=0;
    }
    auto& peer=g_peers[slot];
    if(!host&&p.op==Hello){
        if(g_remoteEpoch!=p.world||peer.challenge!=p.challenge){g_rows.clear();g_remoteEpoch=p.world;peer.challenge=p.challenge;peer.supported=false;}
        if(Send(0,Make(Ack,0))){
            if(!peer.supported)UE_LOGI("mannequin sight: client channel active world=%llu",static_cast<unsigned long long>(g_remoteEpoch));
            peer.supported=true;
        }
        return;
    }
    if(p.world!=(host?g_epoch:g_remoteEpoch)||p.challenge!=peer.challenge)return;
    if(host&&p.op==Ack){
        if(!peer.supported)UE_LOGI("mannequin sight: host channel active slot=%d generation=%u",slot,generation);
        peer.supported=true;return;
    }
    if(!peer.supported||(host?p.op!=Sight:p.op!=Stand))return;
    const std::wstring key=net::StringFromWireKey(p.key);
    if(host){
        auto it=g_rows.find(key);if(it==g_rows.end())return;
        auto& row=it->second;uint32_t eid=0;
        if(row.token!=p.token||row.eid!=p.eid||!row.actor.Alive()||Unique(key)!=row.actor.Raw()||!HostBinding(row.actor.Get(),eid)||eid!=p.eid)return;
        if(!row.remote[slot].known)UE_LOGI("mannequin sight: first native sight accepted slot=%d eid=%u seen=%u",slot,p.eid,unsigned(p.value));
        row.remote[slot]={true,p.value!=0,Now()};
    }else{
        auto* entity=element::Registry::Get().Get(p.eid);
        void* actor=entity?entity->LiveActor():nullptr;
        if(!ClientBinding(p.eid,key,actor))return;
        auto it=g_rows.find(key);if(it==g_rows.end()){if(g_rows.size()>=kMaxWalkers)return;it=g_rows.emplace(key,Row{}).first;}
        auto& row=it->second;
        if(row.token&&(p.token<row.token||(p.token==row.token&&row.eid!=p.eid)))return;
        if(row.token!=p.token||row.eid!=p.eid||!row.actor.Is(actor)){row=Row{};row.actor.Set(actor);row.eid=p.eid;row.token=p.token;}
        if(!row.host.known)UE_LOGI("mannequin sight: first host stand accepted eid=%u token=%llu stand=%u",p.eid,static_cast<unsigned long long>(p.token),unsigned(p.value));
        row.host={true,p.value!=0,Now()};
    }
}
void OnDisconnect(){g_session=nullptr;ResetWorld(nullptr);}
} // namespace coop::mannequin_sight_sync
