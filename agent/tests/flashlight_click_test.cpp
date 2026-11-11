#ifndef FLASHLIGHT_CLICK_SOURCE
#define FLASHLIGHT_CLICK_SOURCE "../../src/votv-coop/src/coop/player/flashlight_click_sound.cpp"
#endif
#include FLASHLIGHT_CLICK_SOURCE
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace test {
enum Id {Actor,Local,Sound,Attenuation,ActorClass,OtherClass,OtherActor,OtherSound,OtherAttenuation,AtvActor,
 AudioClass,EngineStart,EngineLoop,EngineTurbo,DriveStart,DriveBrake,EngineStop,Horn,Count};
struct Obj {
 int id=0,serial=1;
 bool live=true,actor=false,lights=false;
 uint8_t audioFlags=0;
 void* cls=nullptr;
 void* audioRange=nullptr;
 void* engine[6]{};
} objects[Count];
void* At(Id id){return &objects[id];}
uint32_t world=1,serial=1;
bool assets=true,attenuationReady=true,locationReady=true;
int finds=0,spawns=0,locations=0,plays=0;Id sound=Sound,attenuation=Attenuation;
bool engineRange=false;
bool visualReady=true;
int visualCalls=0;
int visualFunction;
ue_wrap::FVector position{12,34,56}, eye{12,34,56};
std::function<void()> findCallback,spawnCallback,locationCallback,playCallback;
std::array<coop::roster_ledger::Row,coop::players::kMaxPeers> people;
using Clear=std::pair<coop::roster_ledger::PerSlotClearFn,void*>;
std::vector<Clear>& Subscribers(){static std::vector<Clear> rows;return rows;}
void ClearSlot(int slot){for(auto [fn,self]:Subscribers())fn(self,slot);}
void Callback(std::function<void()>& callback){auto fn=std::move(callback);callback={};if(fn)fn();}
}
namespace coop::net::session_serial {uint32_t Current(){return test::serial;}}
namespace coop::roster_ledger {
const Row& Get(int slot){CHECK(slot>=0 && slot<players::kMaxPeers);return test::people.at(slot);}
void RegisterPerSlotClear(PerSlotClearFn fn,void* self){test::Subscribers().push_back({fn,self});}
}
namespace ue_wrap::reflection {
bool IsLive(void* p){return p && static_cast<test::Obj*>(p)->live;}
int32_t InternalIndexOf(void* p){return p?static_cast<test::Obj*>(p)->id:-1;}
int32_t AllocateSlotSerial(int32_t id){return test::objects[id].serial;}
int32_t SlotSerial(int32_t id){return test::objects[id].serial;}
bool IsLiveByIndex(void* p,int32_t id){return id>=0 && id<test::Count && p==&test::objects[id] && IsLive(p);}
void* ClassOf(void* p){return p?static_cast<test::Obj*>(p)->cls:nullptr;}
void* FindObject(const wchar_t*,const wchar_t*){++test::finds;test::Callback(test::findCallback);return test::assets?test::At(test::sound):nullptr;}
int32_t FindPropertyOffset(void* cls,const wchar_t* name){
 if(cls==test::At(test::AudioClass) && std::wstring_view(name)==L"AttenuationSettings")
  return offsetof(test::Obj,audioRange);
 if(cls!=test::At(test::ActorClass))return -1;
 const std::wstring_view names[]={L"audio_car_start_Cue",L"audio_car_drive_loop",L"audio_car_turbo",
  L"audio_car_drive_start",L"audio_car_drive_brake",L"audio_car_stop"};
 for(int i=0;i<6;++i)if(names[i]==name)return int32_t(offsetof(test::Obj,engine)+i*sizeof(void*));
 return -1;
}
bool FindBoolProperty(void* cls,const wchar_t* name,int32_t& offset,uint8_t& mask){
 if(cls!=test::At(test::AudioClass))return false;
 const std::wstring_view property(name);
 if(property!=L"bAllowSpatialization" && property!=L"bOverrideAttenuation")return false;
 offset=offsetof(test::Obj,audioFlags);
 mask=property==L"bAllowSpatialization"?1:2;
 return true;
}
bool IsDescendantOfAny(void* cls,void* const* roots,int count){
 return count==1 && cls==roots[0];
}
void* FindDispatchFunctionCached(void* cls,const wchar_t* name){
 CHECK(cls==test::At(test::ActorClass));
 CHECK(std::wstring_view(name)==L"Upd Lights");
 return test::visualReady?&test::visualFunction:nullptr;
}
}
namespace ue_wrap::object_index {
void* ClassByName(const wchar_t* name){return std::wstring_view(name)==L"AudioComponent"?test::At(test::AudioClass):nullptr;}
}
namespace ue_wrap::world_identity {
uint32_t Generation(){return test::world;}
void* CurrentWorld(){return reinterpret_cast<void*>(uintptr_t(test::world));}
void* WorldOf(void* p){return p && static_cast<test::Obj*>(p)->actor?CurrentWorld():nullptr;}
}
namespace ue_wrap::engine {
void* SpawnSoundAttenuation(const SoundAttenuationConfig& cfg){
 CHECK(cfg.extents[0]==(test::engineRange?150.f:100.f));
 CHECK(cfg.falloffDistance==(test::engineRange?1350.f:900.f));
 CHECK(cfg.spatialize && cfg.attenuate && cfg.shape==0 && cfg.distanceAlgorithm==0);
 ++test::spawns;test::Callback(test::spawnCallback);return test::attenuationReady?test::At(test::attenuation):nullptr;
}
bool TryGetActorLocation(void*,FVector& out){++test::locations;out=test::position;test::Callback(test::locationCallback);return test::locationReady;}
bool ReadMainPlayerCameraLocation(void*, FVector& out){out=test::eye;return true;}
void PlaySoundAtLocation(void* actor,void* sound,const FVector& pos,void* attenuation,float volume,float pitch){
 if(actor==test::At(test::Actor)){
  CHECK(coop::players::Registry::Get().PeerIdOfActor(actor)<coop::players::kMaxPeers);
 }
 CHECK(sound==test::At(test::sound) && attenuation==test::At(test::attenuation) && attenuation);
 if(actor==test::At(test::AtvActor)) CHECK(volume==.5f && (pitch==.5f || pitch==1.f));
 else CHECK(volume==1 && pitch==1);
 CHECK(pos.X==12 && pos.Y==34 && pos.Z==56);
 ++test::plays;test::Callback(test::playCallback);
}
}
namespace ue_wrap::atv {
namespace R=reflection;
bool IsAtv(void* actor){return actor==test::At(test::AtvActor);}
void* ComponentAt(void* actor,int32_t offset){
 if(offset<0)return nullptr;
 void* ptr=nullptr;
 std::memcpy(&ptr,static_cast<uint8_t*>(actor)+offset,sizeof(ptr));
 return R::IsLive(ptr)?ptr:nullptr;
}
void WriteFlag(void* object,int32_t offset,uint8_t mask,bool on){
 auto& value=*(static_cast<uint8_t*>(object)+offset);
 if(on)value|=mask;else value&=static_cast<uint8_t>(~mask);
}
#include "atv_engine_audio.inc"
bool GetLights(void* actor){return static_cast<test::Obj*>(actor)->lights;}
const int32_t g_lightsByte=offsetof(test::Obj,lights);
const uint8_t g_lightsMask=1;
void CallApply(void*,void* function){CHECK(function==&test::visualFunction);++test::visualCalls;}
#include "atv_set_lights.inc"
}
namespace coop::atv_sync {
namespace A=ue_wrap::atv;
struct AtvEntry {
 void* actor=nullptr;
 ue_wrap::CachedObjRef headlightAudioActor;
 uint8_t headlightAudioSyncer=0xFF, syncerSlot=0;
 bool headlightAudioState=false;
};
#include "atv_lights.inc"
}
int main(int argc,char** argv){
 using namespace test;namespace S=coop::flashlight_click_sound;
 const std::string selected=argc>1?argv[1]:"";const auto run=[&](const char* name){return selected.empty() || selected==name;};
 const auto reset=[] {
  ++world;++serial;for(int i=0;i<Count;++i){objects[i]={};objects[i].id=i;objects[i].cls=&objects[i];}
  objects[Actor].actor=objects[Local].actor=objects[OtherActor].actor=true;
  objects[Actor].cls=objects[OtherActor].cls=At(ActorClass);
  auto& registry=coop::players::Registry::Get();registry.puppets={};registry.local=At(Local);registry.puppets[1]=At(Actor);
  for(int slot=0;slot<coop::players::kMaxPeers;++slot){people[slot].playerNo=uint16_t(slot+10);ClearSlot(slot);}
  S::g_sound={};S::g_attenuation={};S::g_atvSwitchSound={};S::g_constructingAttenuation=false;
  assets=attenuationReady=locationReady=true;finds=spawns=locations=plays=0;sound=Sound;attenuation=Attenuation;
  engineRange=false;
  visualReady=true;visualCalls=0;
  position=eye={12,34,56};findCallback=spawnCallback=locationCallback=playCallback={};
 };
 const auto click=[](bool enabled=true){S::PlayIfStateChanged(At(Actor),1,enabled);};
 if(run("slot")){
  reset();coop::players::Registry::Get().puppets={};S::PlayIfStateChanged(At(Actor),255,true);CHECK(!finds && !spawns && !locations && !plays);
  auto& registry=coop::players::Registry::Get();registry.puppets[1]=nullptr;registry.puppets[31]=At(Actor);
  S::PlayIfStateChanged(At(Actor),31,true);CHECK(plays==1);puts("PASS flashlight click slot");
 }
 if(run("puppet")){
  reset();S::PlayIfStateChanged(At(Actor),2,true);CHECK(!finds && !plays);S::PlayIfStateChanged(At(Local),1,true);
  S::PlayIfStateChanged(At(OtherActor),1,true);CHECK(!finds && !plays);
  locationCallback=[]{coop::players::Registry::Get().puppets[1]=At(OtherActor);};click();CHECK(!plays);
  puts("PASS flashlight click puppet");
 }
 if(run("lifetime")){
  for(int cause=0;cause<4;++cause){reset();locationCallback=[cause]{if(cause==0)++objects[Actor].serial;if(cause==1)objects[Actor].live=false;
   if(cause==2)objects[Actor].cls=At(OtherClass);if(cause==3)++world;};click();CHECK(!plays && S::g_lastAppliedStateByPeer[1].state==-1);}
  puts("PASS flashlight click lifetime");
 }
 if(run("scope")){
  for(int cause=0;cause<4;++cause){reset();click();CHECK(plays==1);
   if(cause==0)++serial;if(cause==1)++world;if(cause==2)++objects[Actor].serial;if(cause==3)++people[1].playerNo;
   click();CHECK(plays==2);click();CHECK(plays==2);
  }
  puts("PASS flashlight click scope");
 }
 if(run("readiness")){
  for(bool asset:{false,true}){reset();assets=asset;locationReady=!asset;click();CHECK(!plays && S::g_lastAppliedStateByPeer[1].state==-1);
   assets=locationReady=true;click();CHECK(plays==1);click();CHECK(plays==1);
  }
  puts("PASS flashlight click readiness");
 }
 if(run("sound")){
  reset();click();++objects[Sound].serial;click(false);CHECK(finds==2 && plays==2);
  reset();click();objects[Sound].cls=At(OtherClass);sound=OtherSound;click(false);CHECK(finds==2 && plays==2);
  reset();locationCallback=[]{++objects[Sound].serial;};click();CHECK(!plays);click();CHECK(plays==1 && finds==2);
  puts("PASS flashlight click sound");
 }
 if(run("attenuation")){
  reset();click();++objects[Attenuation].serial;click(false);CHECK(spawns==2 && plays==2);
  reset();click();objects[Attenuation].cls=At(OtherClass);attenuation=OtherAttenuation;click(false);CHECK(spawns==2 && plays==2);
  reset();locationCallback=[]{++objects[Attenuation].serial;};click();CHECK(!plays);click();CHECK(plays==1 && spawns==2);
  puts("PASS flashlight click attenuation");
 }
 if(run("reentry")){
  reset();locationCallback=[&]{click();};click();CHECK(plays==1);
  reset();locationCallback=[&]{click(false);};click();CHECK(plays==1 && S::g_lastAppliedStateByPeer[1].state==0);click(false);CHECK(plays==1);
  reset();click(false);locationCallback=[&]{click(false);};click();CHECK(plays==1 && S::g_lastAppliedStateByPeer[1].state==0);
  reset();playCallback=[&]{click();};click();CHECK(plays==1);
  reset();locationCallback=[&]{++people[1].playerNo;ClearSlot(1);click(false);};click();CHECK(plays==1 && S::g_lastAppliedStateByPeer[1].state==0);
  reset();spawnCallback=[&]{click(false);};click();CHECK(!plays && spawns==1);click(false);CHECK(plays==1 && spawns==1);
  puts("PASS flashlight click reentry");
 }
 if(run("position")){
  for(int cause=0;cause<3;++cause){reset();if(cause==0)position.X=NAN;if(cause==1)position.Y=INFINITY;if(cause==2)position.Z=-INFINITY;
   click();CHECK(!plays && S::g_lastAppliedStateByPeer[1].state==-1);position={12,34,56};click();CHECK(plays==1);}
  puts("PASS flashlight click position");
 }
 if(run("no-fallback")){
  reset();attenuationReady=false;click();CHECK(!plays && !locations && S::g_lastAppliedStateByPeer[1].state==-1);
  attenuationReady=true;click();CHECK(plays==1 && spawns==2);
  puts("PASS flashlight click no-fallback");
 }
 if(run("atv")){
  reset();
  objects[AtvActor].actor=true;objects[AtvActor].cls=At(ActorClass);
  eye.X += 1000.f;
  S::PlayAtvHeadlightClick(At(AtvActor),true);CHECK(plays==0 && !spawns);
  eye.X -= 1.f;
  S::PlayAtvHeadlightClick(At(AtvActor),true);CHECK(plays==2);
  S::PlayAtvHeadlightClick(At(AtvActor),false);CHECK(plays==3);
  eye.X=NAN;
  S::PlayAtvHeadlightClick(At(AtvActor),false);CHECK(plays==3);
  eye=position;locationCallback=[]{++world;};
  S::PlayAtvHeadlightClick(At(AtvActor),false);CHECK(plays==3);
  puts("PASS flashlight click atv");
 }
 if(run("atv-receive")){
  reset();
  objects[AtvActor].actor=true;objects[AtvActor].cls=At(ActorClass);
  coop::atv_sync::AtvEntry entry{};
  entry.actor=At(AtvActor);
  using coop::atv_sync::ApplyRemoteLights;
  ApplyRemoteLights(entry,true,true);CHECK(plays==0 && objects[AtvActor].lights);
  ApplyRemoteLights(entry,true,false);CHECK(plays==0);
  ApplyRemoteLights(entry,false,false);CHECK(plays==1 && !objects[AtvActor].lights);
  ApplyRemoteLights(entry,true,false);CHECK(plays==3 && objects[AtvActor].lights);
  entry.syncerSlot=2;ApplyRemoteLights(entry,false,false);CHECK(plays==3 && !objects[AtvActor].lights);
  ++objects[AtvActor].serial;ApplyRemoteLights(entry,true,false);CHECK(plays==3 && objects[AtvActor].lights);
  eye.X+=1000;ApplyRemoteLights(entry,false,false);CHECK(plays==3 && !objects[AtvActor].lights);
  eye=position;ApplyRemoteLights(entry,false,false);CHECK(plays==3 && visualCalls==6);
  visualReady=false;ApplyRemoteLights(entry,true,false);
  CHECK(plays==3 && visualCalls==6 && !objects[AtvActor].lights);
  visualReady=true;ApplyRemoteLights(entry,true,false);
  CHECK(plays==3 && visualCalls==7 && objects[AtvActor].lights);
  puts("PASS flashlight click atv-receive");
 }
 if(run("atv-engine")){
  reset();
  namespace A=ue_wrap::atv;
  engineRange=true;
  auto& atv=objects[AtvActor];
  atv.actor=true;atv.cls=At(ActorClass);
  for(int i=0;i<6;++i){
   auto& emitter=objects[EngineStart+i];
   emitter.cls=At(AudioClass);emitter.audioFlags=0x82;
   atv.engine[i]=&emitter;
  }
  A::PrepareEngineAudio(0);A::PrepareEngineAudio(100);
  CHECK(spawns==1 && A::ConfigureEngineAudio(&atv));
  for(int i=0;i<6;++i)CHECK(objects[EngineStart+i].audioRange==At(Attenuation) &&
                           objects[EngineStart+i].audioFlags==0x81);
  CHECK(!objects[Horn].audioRange && objects[Horn].audioFlags==0);
  objects[EngineLoop].live=false;
  CHECK(!A::ConfigureEngineAudio(&atv));
  puts("PASS ATV engine emitter attenuation binding");
 }
 return 0;
}
