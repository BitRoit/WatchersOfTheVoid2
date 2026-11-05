#include "coop/creatures/centipede_sync.h"

namespace coop::centipede_sync {
void Install(net::Session*) {
    // Compatibility entry point. Registered actors use the canonical lifecycle
    // lanes. K2_DestroyActor is inherited native Actor code, so class-local
    // script watches on the head/segments can never run. Do not spend a new
    // name-watch slot for them every pump or blanket-cancel wire teardowns.
}
}
