#pragma once
namespace coop::net { class Session; }
namespace coop::mannequin_target {
void Install(net::Session* session);
void Track(void* walker);
void Tick();
void OnDisconnect();
}
