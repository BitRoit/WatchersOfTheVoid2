// Actual registration/resolution implementation; only engine names/classes and
// game-thread posting are mocked. Exercise the failed registration seen in logs.
#include "../../src/votv-coop/src/ue_wrap/core/script_gate_registry.cpp"
#include "../../src/votv-coop/src/ue_wrap/core/script_gate_names.cpp"
#include "../../src/votv-coop/src/coop/creatures/deer_sync.cpp"
#include "../../src/votv-coop/src/coop/creatures/centipede_sync.cpp"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fixture {
struct Class { bool declares=false,native=false; alignas(16) std::array<uint8_t,512> function{}; };
std::unordered_map<std::wstring,std::unique_ptr<Class>> classes;
std::unordered_map<std::wstring,int32_t> names;
std::vector<ue_wrap::game_thread::Task> tasks;
uint64_t classVersion=1;int errors=0;bool installed=true;
void Define(const wchar_t* name,bool declares,bool native=false){
 auto c=std::make_unique<Class>();c->declares=declares;c->native=native;
 const uint32_t flags=native?ue_wrap::profile::off::FUNC_Native:0;
 static_assert(ue_wrap::profile::off::UFunction_FunctionFlags+sizeof(flags)<=512);
 std::memcpy(c->function.data()+ue_wrap::profile::off::UFunction_FunctionFlags,&flags,sizeof(flags));
 classes[name]=std::move(c);++classVersion;
}
void Drain(){auto pending=std::move(tasks);tasks.clear();for(auto& fn:pending)fn();}
}
namespace ue_wrap::script_gate::detail {
Entry g_fnTable[kSlots],g_nameTable[kSlots];
std::atomic<int> g_fnWatches{0},g_nameWatches{0},g_namesPending{0},g_unjudged{0};
std::uint64_t g_judgedAtClassSet=0;int g_fnKeyed=0,g_nameKeyed=0;std::mutex g_regMutex;
bool RefusedUninstalled(const char*){return !fixture::installed;}
}
namespace ue_wrap::game_thread {
void Post(Task t){fixture::tasks.push_back(std::move(t));}
bool IsGameThread(){return true;}
}
namespace ue_wrap::fname_utils {
reflection::FName StringToFName(const std::wstring& name){
 auto [it,added]=fixture::names.try_emplace(name,static_cast<int32_t>(fixture::names.size()+1));
 return {it->second,0};
}
}
namespace ue_wrap::object_index {
uint64_t ClassSetVersion(){return fixture::classVersion;}
void* ClassByName(const wchar_t* name){auto it=fixture::classes.find(name);return it==fixture::classes.end()?nullptr:it->second.get();}
}
namespace ue_wrap::reflection {
void* FindFunction(void* cls,const wchar_t*){auto* c=static_cast<fixture::Class*>(cls);return c&&c->declares?c->function.data():nullptr;}
}
namespace ue_wrap::log {
void Write(Level level,const char*,...){if(level==Level::Error)++fixture::errors;}
}
namespace ue_wrap::facts {void Function::NoteUse(Use,bool) const{}}

int main(){
 using namespace ue_wrap::script_gate;
 using namespace ue_wrap::script_gate::detail;
 auto cb=+[](const Call&){return Verdict::Run;};
 fixture::Define(L"deer_C",false);
 CHECK(WatchClassName(L"deer_C",L"K2_DestroyActor",19,cb,nullptr));fixture::Drain();
 CHECK(ClassNameWatchSettled(L"deer_C",L"K2_DestroyActor",19));
 CHECK(!ClassNameWatchLive(L"deer_C",L"K2_DestroyActor",19));
 const int deadKeyed=g_nameKeyed,deadErrors=fixture::errors;
 for(int i=0;i<10000;++i)CHECK(!WatchClassName(L"deer_C",L"K2_DestroyActor",19,cb,nullptr));
 CHECK(g_nameKeyed==deadKeyed&&g_namesPending==0&&g_nameWatches==0&&fixture::tasks.empty());
 CHECK(fixture::errors==deadErrors);
 std::wstring copiedClass=L"deer_C",copiedName=L"K2_DestroyActor";
 CHECK(!WatchClassName(copiedClass.c_str(),copiedName.c_str(),19,cb,nullptr));
 CHECK(ClassNameWatchSettled(copiedClass.c_str(),copiedName.c_str(),19));
 CHECK(!ClassNameWatchLive(copiedClass.c_str(),copiedName.c_str(),19));
 CHECK(g_nameKeyed==deadKeyed);
 // The underlying keyed registrar must preserve a dead identity as well.
 Entry* refused=nullptr;
 for(auto& e:g_nameTable)if(e.dead.load()&&e.tag==19&&e.name&&std::wcscmp(e.name,L"K2_DestroyActor")==0)refused=&e;
 CHECK(refused);
 CHECK(!Register(g_nameTable,g_nameWatches,g_nameKeyed,
   refused->key.load(),19,cb,nullptr,refused->name,refused->className,0,false));
 CHECK(g_nameKeyed==deadKeyed);
 fixture::Define(L"native_C",true,true);
 CHECK(WatchClassName(L"native_C",L"nativeVerb",20,cb,nullptr));fixture::Drain();
 CHECK(ClassNameWatchSettled(L"native_C",L"nativeVerb",20));
 const int nativeKeyed=g_nameKeyed;
 for(int i=0;i<2000;++i)CHECK(!WatchClassName(L"native_C",L"nativeVerb",20,cb,nullptr));
 CHECK(g_nameKeyed==nativeKeyed);
 // A package absent during registration can settle dead when the class loads.
 CHECK(WatchClassName(L"late_C",L"missing",21,cb,nullptr));fixture::Drain();CHECK(g_unjudged==1);
 fixture::Define(L"late_C",false);ResolvePendingNames();CHECK(g_unjudged==0);
 CHECK(!ClassNameWatchLive(L"late_C",L"missing",21)&&ClassNameWatchSettled(L"late_C",L"missing",21));
 const int lateKeyed=g_nameKeyed;
 for(int i=0;i<2000;++i)CHECK(!WatchClassName(L"late_C",L"missing",21,cb,nullptr));
 CHECK(g_nameKeyed==lateKeyed);
 // Live watches still reopen normally, without becoming permanently refused.
 fixture::Define(L"good_C",true);
 CHECK(WatchClassName(L"good_C",L"event",22,cb,nullptr));fixture::Drain();
 CHECK(ClassNameWatchLive(L"good_C",L"event",22));const int liveKeyed=g_nameKeyed;
 CHECK(UnwatchNameScoped(L"good_C",L"event",22,cb,nullptr));
 CHECK(!ClassNameWatchLive(L"good_C",L"event",22));
 CHECK(WatchClassName(L"good_C",L"event",22,cb,nullptr));
 CHECK(ClassNameWatchLive(L"good_C",L"event",22)&&g_nameKeyed==liveKeyed);
 std::wstring goodClass=L"good_C",goodEvent=L"event";
 CHECK(ClassNameWatchLive(goodClass.c_str(),goodEvent.c_str(),22));
 // Real feature entry points must not re-register inherited native methods.
 const int beforeModules=g_nameKeyed;
 for(int i=0;i<3000;++i){coop::deer_sync::Install(nullptr);coop::centipede_sync::Install(nullptr);}
 CHECK(g_nameKeyed==beforeModules&&fixture::tasks.empty());
 // The failed-watch storm cannot starve later unrelated feature registrations.
 std::vector<std::wstring> verbs;verbs.reserve(1100);
 for(int i=0;i<1100;++i)verbs.push_back(L"legitimate-verb-"+std::to_wstring(i));
 for(int i=0;i<1100;++i)CHECK(WatchName(verbs[i].c_str(),100+i,cb,nullptr));
 fixture::Drain();
 for(int i=0;i<1100;++i)CHECK(NameWatchLive(verbs[i].c_str(),100+i));
 CHECK(g_namesPending==0&&g_nameKeyed<kMaxKeyed);
 std::printf("PASS real watch registration/resolution: 14000 refused retries, late/native failures, live reopen, feature installs and 1100 later verbs (%d keyed slots)\n",g_nameKeyed);
}
