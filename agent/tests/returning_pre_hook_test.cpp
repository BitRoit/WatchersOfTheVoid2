// Exercises the production thunk and signature reader, without an Unreal runtime.
#include "../../src/votv-coop/src/ue_wrap/core/ufunction_hook.cpp"
#include "facts_test_support.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace { std::vector<ue_wrap::reflection::ParamInfo> metadata; }
namespace ue_wrap::game_thread { bool IsGameThread() { return true; } }
namespace ue_wrap::log { void Write(Level,const char*,...) {} }
namespace ue_wrap::reflection { std::vector<ParamInfo> FunctionParams(void*) { return metadata; } }
namespace ue_wrap::script_gate { const ExecHandler* ExecHandlers() { return nullptr; } }
using namespace ue_wrap::ufunction_hook;
namespace {
int callbacks=0, originals=0, stepped=0; bool recurse=false, fault=false, run=false;
void* replacement=reinterpret_cast<void*>(0x1234);
void __fastcall Original(void*,void* stack,void* result) {
    ++originals;
    auto*& code=*reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(stack)+P::off::FFrame_Code);
    if(code) { CHECK(*code==0x42); code+=sizeof(void*)+1; CHECK(*code==kExEndFunctionParms); ++code; }
    if(result) std::memcpy(result,&replacement,sizeof(replacement));
}
Verdict Before(void*,void*,const uint8_t* params) {
    ++callbacks; void* target=nullptr; std::memcpy(&target,params,sizeof(target)); CHECK(target==replacement); CHECK(CurrentResult()); CHECK(CurrentNativeFunction()==g_slots[0].ufunction);
    void* zero=replacement; std::memcpy(&zero,CurrentResult(),sizeof(zero)); CHECK(!zero);
    if(fault) { RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr); }
    if(recurse) {
        alignas(16) uint8_t nested[512]{};
        void* locals=const_cast<uint8_t*>(params);
        std::memcpy(nested+P::off::FFrame_Locals,&locals,sizeof(locals));
        PreThunk<0>(nullptr,nested,CurrentResult()); // global guard must forward exactly once
    } else std::memcpy(CurrentResult(),&replacement,sizeof(replacement));
    return run ? Verdict::Run : Verdict::Cancel;
}
void __fastcall Step(void*,void* stack,void* out) {
    ++stepped; auto*& code=*reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(stack)+P::off::FFrame_Code);
    std::memcpy(out,code,sizeof(void*)); code+=sizeof(void*);
}
void Clean() { CHECK(!CurrentResult() && !CurrentNativeFunction() && !CurrentCallerFrame().function && !t_inCb); }
}
int main() {
    using namespace ue_wrap::profile;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const uint64_t pod=cpf::Parm|cpf::NoDestructor;
    metadata={{L"Target",0,8,pod},{L"ReturnValue",8,8,pod|cpf::ReturnParm|cpf::OutParm}};
    Slot candidate;
    CHECK(!ReadPreSignature(nullptr,candidate)); // old API still refuses returns
    CHECK(ReadPreSignature(nullptr,candidate,8) && candidate.paramCount==1 && candidate.paramOffsets[0]==0 && candidate.resultBytes==8);
    CHECK(!ReadPreSignature(nullptr,candidate,4)); CHECK(!ReadPreSignature(nullptr,candidate,9));
    auto valid=metadata;
    metadata.push_back(metadata.back()); CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.pop_back(); CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.back().flags &= ~cpf::NoDestructor; CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.back().flags |= cpf::ReferenceParm; CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.back().offset=INT32_MAX; CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.back().size=1; CHECK(ReadPreSignature(nullptr,candidate,1));
    metadata=valid; metadata.front().flags &= ~cpf::NoDestructor; CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.front().flags |= cpf::OutParm|cpf::ReferenceParm|cpf::ConstParm;
    CHECK(ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.front().flags |= cpf::OutParm; CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.front().offset=INT32_MAX; CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; metadata.front().size=0; CHECK(!ReadPreSignature(nullptr,candidate,8));
    metadata=valid; CHECK(ReadPreSignature(nullptr,g_slots[0],8));
    auto& slot=g_slots[0]; slot.original=&Original; slot.pre=&Before; slot.armed.store(true); slot.ufunction=&candidate;
    alignas(16) uint8_t stack[512]{}; void* locals=&replacement;
    std::memcpy(stack+off::FFrame_Locals,&locals,sizeof(locals));
    void* result=replacement;
    PreThunk<0>(nullptr,stack,&result); CHECK(result==replacement && callbacks==1 && originals==0); Clean();
    recurse=true; result=nullptr; PreThunk<0>(nullptr,stack,&result);
    CHECK(result==replacement && callbacks==2 && originals==1); Clean(); recurse=false;
    fault=true; PreThunk<0>(nullptr,stack,&result); CHECK(callbacks==3 && originals==2 && result==replacement); Clean(); fault=false;
    PreThunk<0>(nullptr,stack,nullptr); CHECK(callbacks==3 && originals==3); Clean();
    ue_wrap::script_gate::ExecHandler exec[256]{}; exec[0x42]=&Step; g_exec=exec;
    uint8_t script[10]={0x42}; std::memcpy(script+1,&replacement,sizeof(replacement)); script[9]=kExEndFunctionParms;
    uint8_t* code=script; std::memcpy(stack+off::FFrame_Code,&code,sizeof(code));
    PreThunk<0>(nullptr,stack,&result); CHECK(stepped==1 && originals==3 && callbacks==4);
    CHECK(*reinterpret_cast<uint8_t**>(stack+off::FFrame_Code)==script+10); Clean();
    run=true; code=script; std::memcpy(stack+off::FFrame_Code,&code,sizeof(code));
    PreThunk<0>(nullptr,stack,&result); CHECK(stepped==2 && originals==4 && callbacks==5); Clean();
    CHECK(*reinterpret_cast<uint8_t**>(stack+off::FFrame_Code)==script+10);
    std::puts("PASS returning pre hook: exact POD signature, old-return refusal, PE/bytecode cancellation and P_FINISH, nested forwarding once, null-storage fail-open, SEH/TLS cleanup");
}
