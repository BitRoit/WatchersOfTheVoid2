#pragma once
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include <cmath>

namespace ue_wrap::atv::detail {
struct RigPartPose { FVector pos; FRotator rot; };
struct RigQuat { float x,y,z,w; };
inline RigQuat RigRotation(const FRotator& r) {
    RigQuat q; engine::RotatorToQuat(r.Pitch,r.Yaw,r.Roll,q.x,q.y,q.z,q.w); return q;
}
inline RigQuat RigMultiply(const RigQuat& a,const RigQuat& b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline FVector RigRotate(const RigQuat& q,const FVector& p) {
    const RigQuat v=RigMultiply(RigMultiply(q,{p.X,p.Y,p.Z,0}),{-q.x,-q.y,-q.z,q.w});
    return {v.x,v.y,v.z};
}
inline bool RigFinite(const RigPartPose& p) {
    return std::isfinite(p.pos.X)&&std::isfinite(p.pos.Y)&&std::isfinite(p.pos.Z)&&
        std::isfinite(p.rot.Pitch)&&std::isfinite(p.rot.Yaw)&&std::isfinite(p.rot.Roll);
}
// One rigid delta for the four native physics roots. Never rebuild wheel offsets
// from construction markers: those describe a different suspension compression.
inline bool RebaseRigPose(RigPartPose (&parts)[4],const RigPartPose& target) {
    if(!RigFinite(target))return false;
    for(const auto& p:parts)if(!RigFinite(p))return false;
    const RigPartPose original=parts[0];
    const auto old=RigRotation(original.rot);
    const auto delta=RigMultiply(RigRotation(target.rot),{-old.x,-old.y,-old.z,old.w});
    RigPartPose result[4];
    result[0]=target;
    for(int i=1;i<4;++i){
        const FVector relative{parts[i].pos.X-original.pos.X,parts[i].pos.Y-original.pos.Y,parts[i].pos.Z-original.pos.Z};
        const auto moved=RigRotate(delta,relative);
        const auto rotation=RigMultiply(delta,RigRotation(parts[i].rot));
        result[i]={{target.pos.X+moved.X,target.pos.Y+moved.Y,target.pos.Z+moved.Z},
            engine::QuatToRotator(rotation.x,rotation.y,rotation.z,rotation.w)};
        if(!RigFinite(result[i]))return false;
    }
    for(int i=0;i<4;++i)parts[i]=result[i];
    return true;
}
} // namespace ue_wrap::atv::detail
