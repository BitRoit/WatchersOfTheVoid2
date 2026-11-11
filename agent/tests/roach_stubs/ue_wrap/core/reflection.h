#pragma once
#include <cstdint>
namespace ue_wrap::reflection {
void* FindClass(const wchar_t*); void* FindFunction(void*, const wchar_t*);
int32_t FindPropertyOffset(void*, const wchar_t*);
bool IsLive(void*); bool IsLiveByIndex(void*, int32_t);
int32_t InternalIndexOf(void*); int32_t AllocateSlotSerial(int32_t); int32_t SlotSerial(int32_t);
}
