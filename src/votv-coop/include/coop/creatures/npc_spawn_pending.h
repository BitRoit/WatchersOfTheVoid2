#pragma once
#include "coop/net/protocol.h"
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace coop::npc_spawn_pending {
// Only pak-verified generated classes may initiate a package load. A wire leaf
// never becomes an arbitrary path; loaded subclasses still pass the NPC gate.
inline const wchar_t* ClassPath(std::wstring_view leaf) {
    struct Path { const wchar_t* leaf; const wchar_t* path; };
    static constexpr Path paths[] = {
        {L"npc_zombie_C", L"/Game/objects/npc/npc_zombie.npc_zombie_C"},
        {L"kerfurOmega_C", L"/Game/objects/kerfurOmega.kerfurOmega_C"},
        {L"npc_krampus_C", L"/Game/objects/npc/npc_krampus.npc_krampus_C"},
        {L"npc_funguy_C", L"/Game/objects/npc/npc_funguy.npc_funguy_C"},
        {L"npc_goreSlither_C", L"/Game/objects/npc/npc_goreSlither.npc_goreSlither_C"},
        {L"fossilhound_C", L"/Game/objects/fossilhound.fossilhound_C"},
        {L"antibreather_C", L"/Game/objects/antibreather.antibreather_C"},
        {L"npc_orborb_C", L"/Game/objects/npc/npc_orborb.npc_orborb_C"},
        {L"npc_arirFollower_C", L"/Game/objects/npc/npc_arirFollower.npc_arirFollower_C"},
        {L"npc_ariral_shooter_C", L"/Game/objects/npc/npc_ariral_shooter.npc_ariral_shooter_C"},
        {L"npc_ariral_pigBeater_C", L"/Game/objects/npc/npc_ariral_pigBeater.npc_ariral_pigBeater_C"},
        {L"killerwisp_C", L"/Game/objects/killerwisp.killerwisp_C"},
        {L"ventCrawler_C", L"/Game/objects/ventCrawler.ventCrawler_C"},
        {L"wisp_C", L"/Game/objects/wisp.wisp_C"},
        {L"wisp_b_C", L"/Game/objects/wisp_b.wisp_b_C"},
        {L"wisp_red_C", L"/Game/objects/wisp_red.wisp_red_C"},
        {L"wisp_bl_C", L"/Game/objects/wisp_bl.wisp_bl_C"},
        {L"wisp_w_C", L"/Game/objects/wisp_w.wisp_w_C"},
        {L"wisp_g_C", L"/Game/objects/wisp_g.wisp_g_C"},
        {L"wisp_o_C", L"/Game/objects/wisp_o.wisp_o_C"},
        {L"wisp_p_C", L"/Game/objects/wisp_p.wisp_p_C"},
        {L"wisp_blu_C", L"/Game/objects/wisp_blu.wisp_blu_C"},
        {L"grayTest_C", L"/Game/objects/misc/grayTest.grayTest_C"},
        {L"eg_C", L"/Game/objects/eg.eg_C"},
        {L"tentacleBall_C", L"/Game/objects/tentacleBall.tentacleBall_C"},
        {L"npc_arirGunStealer_C", L"/Game/objects/npc/npc_arirGunStealer.npc_arirGunStealer_C"},
        {L"kavotia_C", L"/Game/objects/kavotia.kavotia_C"},
        {L"rufus_C", L"/Game/objects/rufus.rufus_C"},
        {L"arirSlapper_C", L"/Game/objects/arirSlapper.arirSlapper_C"},
    };
    for (const auto& row : paths) if (leaf == row.leaf) return row.path;
    return nullptr;
}

struct Pending {
    net::EntitySpawnPayload spawn{};
    uint64_t expires = 0;
    net::EntityPoseSnapshot pose{};
    bool hasPose = false;
};
class Queue {
    std::vector<Pending> rows_;
    size_t cursor_ = 0;
public:
    static constexpr size_t kLimit = 64;
    static constexpr uint64_t kLifetimeMs = 15000;
    Pending* Find(uint32_t eid) {
        for (auto& row : rows_) if (row.spawn.elementId == eid) return &row;
        return nullptr;
    }
    bool Put(const net::EntitySpawnPayload& p, uint64_t now) {
        if (Find(p.elementId)) return true; // preserve latest pose and original expiry
        if (rows_.size() >= kLimit) return false;
        rows_.push_back(Pending{p, now + kLifetimeMs});
        return true;
    }
    void Erase(uint32_t eid) {
        rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [eid](const Pending& p) {
            return p.spawn.elementId == eid;
        }), rows_.end());
    }
    void Clear() { rows_.clear(); cursor_ = 0; }
    size_t Size() const { return rows_.size(); }
    void Pose(const net::EntityPoseSnapshot& pose) {
        if (auto* row = Find(pose.elementId)) {
            row->pose = pose; row->hasPose = true;
            row->spawn.locX = pose.x; row->spawn.locY = pose.y; row->spawn.locZ = pose.z;
            row->spawn.rotPitch = pose.pitch; row->spawn.rotYaw = pose.yaw; row->spawn.rotRoll = pose.roll;
        }
    }
    // Copy before dispatch: reflection/spawn callbacks can mutate or clear us.
    std::vector<Pending> Ready(uint64_t now) {
        rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [now](const Pending& p) {
            return now >= p.expires;
        }), rows_.end());
        std::vector<Pending> out;
        const auto n = std::min<size_t>(4, rows_.size());
        for (size_t i = 0; i < n; ++i) out.push_back(rows_[(cursor_ + i) % rows_.size()]);
        cursor_ += n;
        return out;
    }
};
}
