#pragma once
#include <cstdint>
namespace coop::net {
inline constexpr int kRoachEntriesPerPage = 12, kRoachSnapshotCap = 128;
enum class ReliableKind { RoachState, RoachConsumed };
struct RoachStatePayload {
    uint32_t seq; uint8_t page, pageCount, totalCount, entryCount;
    struct Entry { float x, y, z, scale; } entries[12];
};
struct RoachConsumedPayload { float x, y, z; };
}
