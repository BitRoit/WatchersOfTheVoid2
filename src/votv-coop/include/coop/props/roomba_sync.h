#pragma once
namespace coop::net { class Session; }
namespace coop::roomba_sync {
// Game-thread session installation is retried until both seams register.
void Install(net::Session* session);
void OnDisconnect();
}
