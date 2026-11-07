#pragma once
#include <cstdint>
#include "coop/net/protocol.h"
namespace coop::players {
inline constexpr uint8_t kMaxPeers = coop::net::kMaxPeers;
struct Registry {
    uint8_t localPeerId = 1;
    static Registry& Get() { static Registry r; return r; }
    uint8_t LocalPeerId() const { return localPeerId; }
};
}
