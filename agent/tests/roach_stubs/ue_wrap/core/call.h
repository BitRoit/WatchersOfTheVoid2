#pragma once
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace ue_wrap {
inline bool mockFailNewRotation = false;
struct ParamFrame {
    void* fn; std::map<std::wstring, std::vector<unsigned char>> fields;
    explicit ParamFrame(void* f) : fn(f) {}
    bool valid() const { return fn != nullptr; }
    bool SetRaw(const wchar_t* n, const void* p, int size) {
        if (mockFailNewRotation && std::wstring(n) == L"NewRotation") return false;
        const auto* data = static_cast<const unsigned char*>(p);
        fields[n] = std::vector<unsigned char>(data, data + size); return true;
    }
    bool GetRaw(const wchar_t* n, void* p, int size) const {
        auto i = fields.find(n); if (i == fields.end() || i->second.size() != static_cast<size_t>(size)) return false;
        std::memcpy(p, i->second.data(), size); return true;
    }
    template<class T> bool Set(const wchar_t* n, const T& v) { return SetRaw(n, &v, sizeof(v)); }
    template<class T> T Get(const wchar_t* n) const { T v{}; GetRaw(n, &v, sizeof(v)); return v; }
};
bool Call(void*, ParamFrame&);
}
