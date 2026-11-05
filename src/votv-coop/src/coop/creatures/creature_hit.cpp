// coop/creatures/creature_hit.cpp -- see coop/creatures/creature_hit.h.

#include "coop/creatures/creature_hit.h"

#include "coop/creatures/npc_mirror_brain.h"
#include "coop/element/element.h"
#include "coop/element/registry.h"

#include "ue_wrap/core/reflection.h"

namespace coop::creature_hit {

namespace R  = ue_wrap::reflection;
namespace EL = coop::element;

bool IsCreature(void* actor) {
    if (!actor) return false;
    const EL::Registry& reg = EL::Registry::Get();
    const EL::ElementId eid = reg.EidForActor(actor);
    if (eid == EL::kInvalidId) return false;
    const EL::Element* el = reg.Get(eid);
    return el && el->GetType() == EL::ElementType::Npc;
}

bool IsDelivered(void* actor) {
    if (!actor || !R::IsLive(actor)) return false;
    if (IsCreature(actor) && coop::npc_mirror_brain::IsParkedClass(R::ClassOf(actor)))
        return true;
    // These native projectiles are host-owned; client copies never execute their damage graph.
    // Match exact verified leaves, never arbitrary Actor/Character ancestry.
    const auto& name = R::NameOf(R::ClassOf(actor));
    return R::NameEquals(name, L"hoelUfoAttack_C") ||
           R::NameEquals(name, L"tentacleBallShot_C") || R::NameEquals(name, L"kavotiaShot_C");
}

}  // namespace coop::creature_hit
