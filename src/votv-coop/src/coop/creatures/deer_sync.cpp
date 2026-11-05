#include "coop/creatures/deer_sync.h"

namespace coop::deer_sync {
void Install(net::Session*) {
    // Compatibility entry point, not a completed deer behavior adapter.
    // K2_DestroyActor is inherited native Actor code and cannot be watched as
    // deer_C script. Registered entities use their existing lifecycle lane;
    // unregistered native deer retain their own teardown.
}
}
