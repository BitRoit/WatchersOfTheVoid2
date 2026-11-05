#pragma once
#include "coop/net/session.h"

namespace coop::mannequin_sync {
void Install(net::Session* session);
void Tick(net::Session& session);
void OnDisconnect();
}
