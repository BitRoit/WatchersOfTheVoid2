#pragma once
#include <cstdint>
namespace coop::net { class Session; struct MannequinSightPayload; }
// Game-thread only. A fresh false is distinct from absence/unsupported/stale.
namespace coop::mannequin_sight_sync {
void Install(net::Session*);
void Tick(net::Session&);
void ReportLocalSight(void* walker,bool seen);
bool TryGetRemoteSight(uint8_t slot,void* walker,bool& seen);
void PublishHostStand(void* walker,bool stand);
bool TryGetHostStand(void* walker,bool& stand);
void Receive(net::Session&,const net::MannequinSightPayload&,int senderSlot);
void OnDisconnect();
}
