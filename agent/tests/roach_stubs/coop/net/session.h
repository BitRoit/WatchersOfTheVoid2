#pragma once
#include "coop/net/protocol.h"
#include <cstring>
#include <vector>
namespace coop::net {
enum class Role { Host, Client };
struct Session {
    Role currentRole = Role::Client; bool online = true; bool refuse = false;
    std::vector<RoachStatePayload> states; std::vector<RoachConsumedPayload> intents;
    bool connected() const { return online; }
    Role role() const { return currentRole; }
    bool SendReliable(ReliableKind kind, const void* data, size_t) {
        if (refuse) return false;
        if (kind == ReliableKind::RoachState) states.push_back(*static_cast<const RoachStatePayload*>(data));
        else intents.push_back(*static_cast<const RoachConsumedPayload*>(data));
        return true;
    }
    bool SendReliableToSlot(int, ReliableKind kind, const void* data, size_t size) {
        return SendReliable(kind, data, size);
    }
};
}
