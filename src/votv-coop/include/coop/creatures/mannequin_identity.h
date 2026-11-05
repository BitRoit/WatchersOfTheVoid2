#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace coop::net {class Session;}
namespace coop::mannequin_identity {
// Exact keyed walking-mannequin twins only. Never adopts by position/class.
void Tick(net::Session&,std::vector<void*>& walkers);
// A currently held exact twin streams the canonical host identity without
// stealing its Registry row. The local rendering retires after release.
bool TryGetHeldIdentity(net::Session&,void* actor,uint32_t& eid,std::wstring& key);
void OnDisconnect();
}
