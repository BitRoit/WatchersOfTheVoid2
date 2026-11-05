#pragma once
namespace coop::net {class Session;}
namespace coop::mannequin_interaction {
// Client native hover/grab admission for the host-driven walking mannequin.
// Physics stays parked until an actual local handle/constraint takes the body.
void Tick(net::Session&);
void OnDisconnect();
}
