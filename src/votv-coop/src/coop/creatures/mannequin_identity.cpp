#include "coop/creatures/mannequin_identity.h"
#include "coop/net/session.h"
#include "coop/element/registry.h"
#include "coop/player/hand_item.h"
#include "coop/player/local_prop_hold.h"
#include "coop/props/prop_drive_host.h"
#include "coop/props/prop_element_tracker.h"
#include "coop/props/remote_prop.h"
#include "coop/session/net_pump.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/world_identity.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <windows.h>

namespace coop::mannequin_identity {
namespace {
namespace R=ue_wrap::reflection;
namespace WI=ue_wrap::world_identity;
namespace P=ue_wrap::prop;
struct Canonical {ue_wrap::CachedObjRef actor;uint32_t eid=0;bool ambiguous=false;};
struct Hidden {ue_wrap::CachedObjRef actor;bool hidden=false,collision=true;};
struct Retiring {ue_wrap::CachedObjRef actor;Canonical canonical;std::wstring key;int index=-1,serial=0;uint64_t retryAt=0;};
net::Session* g_session=nullptr;
void* g_world=nullptr;
uint32_t g_generation=0;
uint64_t g_stops=0;
bool g_hideRefusalSaid=false;
std::unordered_map<std::wstring,Canonical> g_canonical;
std::unordered_map<void*,Hidden> g_hidden;
std::unordered_map<void*,Retiring> g_retiring;
bool Active(){return ue_wrap::game_thread::IsGameThread()&&g_world&&g_session&&g_session->running()&&g_session->connected()&&
    g_session->role()==net::Role::Client&&g_session->StopCount()==g_stops&&WI::CurrentWorld()==g_world&&
    WI::Generation()==g_generation&&net_pump::IsWorldSettled();}
bool Walker(void* actor){
    if(!actor||!R::IsLive(actor)||WI::WorldOf(actor)!=g_world||hand_item::IsHandAxisActor(actor))return false;
    for(void* cls=R::ClassOf(actor);cls;cls=R::SuperStructOf(cls))
        if(R::NameEquals(R::NameOf(cls),L"prop_wMannequin_C"))return true;
    return false;
}
bool Key(element::Element* e,std::wstring& key){
    if(!e)return false;const auto& name=e->GetName();if(name.empty()||name=="None"||name.size()>31)return false;
    key.clear();for(unsigned char c:name){if(c<33||c>126)return false;key.push_back(wchar_t(c));}return true;
}
bool Bound(const Canonical& row,const std::wstring& key){
    if(row.ambiguous||!row.actor.Alive()||!Walker(row.actor.Raw()))return false;
    auto& registry=element::Registry::Get();auto* e=registry.Get(row.eid);std::wstring current;
    return element::Registry::IsAllowedHostAllocatedEid(row.eid)&&e&&e->IsMirror()&&e->GetType()==element::ElementType::Prop&&
        e->LiveActor()==row.actor.Raw()&&registry.EidForActor(row.actor.Raw())==row.eid&&Key(e,current)&&current==key;
}
bool Held(void* actor){return local_prop_hold::IsHeld(actor)||prop_drive_host::InAHand(actor)||hand_item::IsHandAxisActor(actor);}
bool Twin(void* actor,const Canonical& row,const std::wstring& key){
    if(!Bound(row,key)||!Walker(actor)||actor==row.actor.Raw()||R::ClassOf(actor)!=R::ClassOf(row.actor.Raw())||
       P::IsFrozen(actor)||P::IsStatic(actor)||P::GetInteractableKeyString(actor)!=key)return false;
    const auto eid=element::Registry::Get().EidForActor(actor);auto* e=element::Registry::Get().Get(eid);
    // A distinct wire mirror is a different host identity, never ours to steal.
    return !e||(element::Registry::IsLocalId(eid)&&!e->IsMirror()&&e->GetType()==element::ElementType::Prop&&e->LiveActor()==actor);
}
bool ReadFlag(void* actor,const wchar_t* name,bool& value){
    int offset=-1;uint8_t mask=0;if(!R::FindBoolProperty(R::ClassOf(actor),name,offset,mask)||offset<0||!mask)return false;
    value=(*reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(actor)+offset)&mask)!=0;return true;
}
void Restore(Hidden& state){
    if(void* actor=state.actor.Get()){
        ue_wrap::engine::SetActorHiddenInGame(actor,state.hidden);
        ue_wrap::engine::SetActorEnableCollision(actor,state.collision);
    }
}
bool HideCanonical(void* actor){
    if(g_hidden.contains(actor))return g_hidden.at(actor).actor.Get()==actor;
    Hidden state;state.actor.Set(actor);
    if(!ReadFlag(actor,L"bHidden",state.hidden)||!ReadFlag(actor,L"bActorEnableCollision",state.collision)){
        if(!g_hideRefusalSaid){g_hideRefusalSaid=true;UE_LOGI("mannequin identity: presentation flags unavailable; held twin preserved, canonical wire identity retained");}
        return false;
    }
    if(!ue_wrap::engine::SetActorHiddenInGame(actor,true)||!ue_wrap::engine::SetActorEnableCollision(actor,false)){
        Restore(state);return false;
    }
    g_hidden.emplace(actor,state);
    UE_LOGI("mannequin identity: canonical rendering %p hidden while exact-key twin is held",actor);
    return true;
}
void Context(net::Session& session){
    void* world=WI::CurrentWorld();
    if(g_session!=&session||g_world!=world||g_generation!=WI::Generation()||g_stops!=session.StopCount())OnDisconnect();
    g_session=&session;g_world=world;g_generation=WI::Generation();g_stops=session.StopCount();
}
void AddCanonical(void* actor,uint32_t eid){
    if(!Walker(actor))return;
    auto& registry=element::Registry::Get();auto* e=registry.Get(eid);std::wstring key;
    if(!element::Registry::IsAllowedHostAllocatedEid(eid)||!e||!e->IsMirror()||e->GetType()!=element::ElementType::Prop||
       e->LiveActor()!=actor||registry.EidForActor(actor)!=eid||!Key(e,key))return;
    auto [it,inserted]=g_canonical.try_emplace(key);
    if(inserted){it->second.actor.Set(actor);it->second.eid=eid;}else if(!it->second.actor.Is(actor))it->second.ambiguous=true;
}
void RefreshCanonical(){
    g_canonical.clear();std::vector<element::Registry::ActorIdPair> entries;
    element::Registry::Get().SnapshotActorsByType(element::ElementType::Prop,entries);
    for(const auto& e:entries)if(e.mirror&&element::Registry::IsAllowedHostAllocatedEid(e.id)&&R::IsLiveByIndex(e.actor,e.internalIdx))AddCanonical(e.actor,e.id);
    for(auto it=g_hidden.begin();it!=g_hidden.end();){
        bool valid=false;for(const auto& [key,row]:g_canonical)if(row.actor.Is(it->first)&&Bound(row,key)){valid=true;break;}
        if(!valid){Restore(it->second);it=g_hidden.erase(it);}else ++it;
    }
}
bool HeldIdentity(void* actor,uint32_t& eid,std::wstring& key){
    for(const auto& [canonicalKey,row]:g_canonical){
        if(!Bound(row,canonicalKey))continue;
        if(row.actor.Is(actor)) {eid=row.eid;key=canonicalKey;return true;}
        if(Twin(actor,row,canonicalKey)&&!Held(row.actor.Raw())){
            // Identity does not depend on rendering metadata. A missing flag
            // read defers hiding, never authorizes a second peer PropSpawn.
            HideCanonical(row.actor.Get());
            if(!Active()||!Bound(row,canonicalKey)||!Twin(actor,row,canonicalKey)||!local_prop_hold::IsHeld(actor))return false;
            eid=row.eid;key=canonicalKey;return true;
        }
    }
    return false;
}
void CleanupRetired(const Retiring& pending){
    void* actor=pending.actor.Raw();
    // Never unmark a new object that recycled the retired object's address.
    if(R::ObjectAt(pending.index)==actor&&R::SlotSerial(pending.index)==pending.serial){
        prop_element_tracker::UnmarkKnownKeyedProp(actor);
        prop_element_tracker::UnmarkProcessedInit(actor);
    }
    if(Bound(pending.canonical,pending.key))prop_element_tracker::TrackActorKey(pending.canonical.actor.Get(),pending.key);
}
} // namespace
void Tick(net::Session& session,std::vector<void*>& walkers){
    Context(session);
    if(!Active()){OnDisconnect();return;}
    std::unordered_set<void*> retired;
    for(auto it=g_retiring.begin();it!=g_retiring.end();){
        if(it->second.actor.Alive()){++it;continue;}
        if(R::ObjectAt(it->second.index)==it->first&&R::SlotSerial(it->second.index)==it->second.serial)retired.insert(it->first);
        CleanupRetired(it->second);it=g_retiring.erase(it);
    }
    walkers.erase(std::remove_if(walkers.begin(),walkers.end(),[&](void* actor){return retired.contains(actor);}),walkers.end());
    g_canonical.clear();
    for(void* actor:walkers)if(Walker(actor))AddCanonical(actor,element::Registry::Get().EidForActor(actor));
    std::unordered_map<void*,bool> keepHidden;
    for(void* actor:walkers){
        if(!Walker(actor))continue;
        const std::wstring key=P::GetInteractableKeyString(actor);auto it=g_canonical.find(key);
        if(it==g_canonical.end()||!Twin(actor,it->second,key))continue;
        void* canonical=it->second.actor.Get();
        if(Held(actor)){
            // Already held when the authoritative mirror arrived: keep the
            // actual handle/body intact, show that one rendering until release.
            if(!Held(canonical)&&HideCanonical(canonical))keepHidden[canonical]=true;
            continue;
        }
        auto pending=g_retiring.find(actor);
        if(pending!=g_retiring.end()&&GetTickCount64()<pending->second.retryAt)continue;
        Retiring attempt;attempt.actor.Set(actor);attempt.canonical=it->second;attempt.key=key;
        attempt.index=attempt.actor.Idx();attempt.serial=R::SlotSerial(attempt.index);attempt.retryAt=GetTickCount64()+1000;
        remote_prop::ConsumeLocalActor(actor); // exact object, echo-suppressed
        if(!Active()){walkers.clear();OnDisconnect();return;}
        if(!attempt.actor.Alive()){
            if(R::ObjectAt(attempt.index)==actor&&R::SlotSerial(attempt.index)==attempt.serial)retired.insert(actor);
            CleanupRetired(attempt);g_retiring.erase(actor);
            UE_LOGI("mannequin identity: retired exact-key client twin actor=%p canonical=%p eid=%u",actor,canonical,it->second.eid);
        }else g_retiring[actor]=attempt; // delayed/failed native destroy: keep identity and retry boundedly
    }
    for(auto it=g_hidden.begin();it!=g_hidden.end();){
        if(!keepHidden.contains(it->first)){Restore(it->second);it=g_hidden.erase(it);}else ++it;
    }
    walkers.erase(std::remove_if(walkers.begin(),walkers.end(),[&](void* actor){return retired.contains(actor);}),walkers.end());
}
bool TryGetHeldIdentity(net::Session& session,void* actor,uint32_t& eid,std::wstring& key){
    Context(session);
    if(!Active()||!Walker(actor)||!local_prop_hold::IsHeld(actor))return false;
    auto& registry=element::Registry::Get();Canonical own;own.actor.Set(actor);own.eid=registry.EidForActor(actor);
    std::wstring ownKey;
    if(Key(registry.Get(own.eid),ownKey)&&Bound(own,ownKey)){eid=own.eid;key=ownKey;return true;}
    // The first held capture may precede the controller Tick or mirror adoption
    // may have landed since it. Read the current Registry, not yesterday's map.
    // Alias uniqueness must be current even if another mirror arrived since the
    // last Tick. Ordinary canonical carries above remain an O(1) lookup.
    RefreshCanonical();return HeldIdentity(actor,eid,key);
}
void OnDisconnect(){
    for(auto& [_,state]:g_hidden)Restore(state);
    g_hidden.clear();g_canonical.clear();g_retiring.clear();g_hideRefusalSaid=false;g_session=nullptr;g_world=nullptr;g_generation=0;g_stops=0;
}
}
