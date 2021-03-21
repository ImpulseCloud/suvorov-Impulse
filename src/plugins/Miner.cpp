// The MIT License (MIT)
//
// Copyright (c) 2017-2020 Alexander Kurbatov

#include "Hub.h"
#include "Miner.h"
#include "core/API.h"
#include "core/Helpers.h"
#include "core/Order.h"

#include <sc2api/sc2_typeenums.h>
#include <sc2api/sc2_unit_filters.h>

#include <vector>
#include <unordered_map>

namespace {
const int mule_energy_cost = 50;

// Allocate workers optimally to mineral patches (direct distance, not pathing)
// Intended for first-frame-spread and re-assign after worker-harrass
// not optimized for workers > patches * 2
std::vector<int> assignWorkersToPatches(const sc2::Units & workers,
    const sc2::Units & patches, const sc2::Unit* townhall) {
    if (workers.empty() || patches.empty() || townhall == nullptr)
        return {};

    // this will be returned where index is worker-index and value is patch-index,
    // of the passed-in sc2::Units vectors
    std::vector<int> workerToMineralTarget;
    workerToMineralTarget.resize(workers.size(), -1); // initialized to -1

    std::vector<std::vector<int>> assignedWorkersOnMineral;
    assignedWorkersOnMineral.resize(patches.size());  // worker-indexes per mineral

    std::vector<int> free_workers_index;
    free_workers_index.reserve(workers.size());
    for (size_t i = 0; i < workers.size(); i++)
        free_workers_index.push_back(i);

    std::vector<int> free_patches_index;
    free_patches_index.reserve(patches.size());
    for (size_t i = 0; i < patches.size(); i++)
        free_patches_index.push_back(i);

    // TODO: Memo-ize 2DSquared distances between all workers-to-patches?
    // put distances in the matrix after doing, then use for later lookup

    // sort mineral-patches by closeness-to-townhall-center in reverse-order,
    // so we can use .back() and .pop_back()
    sc2::Point2D th_pos = townhall->pos;
    std::sort(free_patches_index.begin(), free_patches_index.end(),
        [th_pos, patches](int a, int b) {
        return DistanceSquared2D(th_pos, patches[a]->pos)
            > DistanceSquared2D(th_pos, patches[b]->pos); });

    // iterate through all patches, choosing first closest free_worker
    // then, iterate on remaining workers, assign to closest less-saturated-patch
    while (!free_patches_index.empty() && !free_workers_index.empty()) {
        size_t chooser = free_patches_index.back(); // patch chooses its own miner

        auto target_pos = patches[chooser]->pos;
        auto closest = std::min_element(
            free_workers_index.begin(), free_workers_index.end(),
            [target_pos, workers](int a, int b) {
            return DistanceSquared2D(target_pos, workers[a]->pos)
                < DistanceSquared2D(target_pos, workers[b]->pos);
        });

        size_t assignee = *closest;
        free_workers_index.erase(closest);

        assignedWorkersOnMineral[chooser].push_back(assignee);

        workerToMineralTarget[assignee] = chooser;  // insert or assign
        if (assignedWorkersOnMineral[chooser].size() < 2) // toAlloc(mins[chooser]))
            std::rotate(free_patches_index.begin(), free_patches_index.end() - 1,
                free_patches_index.end());
        else
            free_patches_index.pop_back();
    }

    return workerToMineralTarget;
}

void SpreadMiners(const sc2::Unit* townhall) {
    auto th_pos = townhall->pos;

    Units patches = gAPI->observer().GetUnits(
        [th_pos](const sc2::Unit & u) {
        return  sc2::IsMineralPatch()(u.unit_type) && Distance2D(u.pos, th_pos) < 15.0f;
    }, sc2::Unit::Alliance::Neutral);

    Units workers = gAPI->observer().GetUnits(
        [th_pos](const sc2::Unit & u) {
        return  u.unit_type == gHub->GetCurrentWorkerType() &&
            Distance2D(u.pos, th_pos) < 15.0f;
    });

    const std::vector<int> targets =
        assignWorkersToPatches(workers(), patches(), townhall);

    // Send workers to their assigned mineral patches
    for (size_t workers_index = 0; workers_index < targets.size(); workers_index++) {
        if (targets[workers_index] != -1)  // no mineral patch target
            gAPI->action().Cast(*workers()[workers_index], sc2::ABILITY_ID::SMART,
                *patches()[targets[workers_index]]);
    }
}

void SecureMineralsIncome(Builder* builder_) {
    auto town_halls = gAPI->observer().GetUnits(sc2::IsTownHall());

    for (const auto& i : town_halls()) {
        if (!i->orders.empty() || i->build_progress != BUILD_FINISHED)
            continue;

        if (i->assigned_harvesters > i->ideal_harvesters)
            continue;

        if (builder_->CountScheduledOrders(gHub->GetCurrentWorkerType()) > 0)
            continue;

        // FIXME (alkurbatov): We should set an assignee for drones
        // and pick a larva closest to the assignee.
        if (gHub->GetCurrentRace() == sc2::Race::Zerg) {
            builder_->ScheduleOptionalOrder(sc2::UNIT_TYPEID::ZERG_DRONE);
            continue;
        }

        builder_->ScheduleOptionalOrder(gHub->GetCurrentWorkerType(), i);
    }
    
    if (gAPI->observer().GetGameLoop() == 1)
        SpreadMiners(town_halls().back());
}

void SecureVespeneIncome() {
    auto refineries = gAPI->observer().GetUnits(IsRefinery());

    for (const auto& i : refineries()) {
        if (i->assigned_harvesters >= i->ideal_harvesters)
            continue;

        gHub->AssignVespeneHarvester(*i);
    }
}

void CallDownMULE() {
    auto orbitals = gAPI->observer().GetUnits(
        IsUnit(sc2::UNIT_TYPEID::TERRAN_ORBITALCOMMAND));

    if (orbitals.Empty())
        return;

    auto units = gAPI->observer().GetUnits(sc2::IsVisibleMineralPatch(),
        sc2::Unit::Alliance::Neutral);

    for (const auto& i : orbitals()) {
        if (i->energy < mule_energy_cost)
            continue;

        const sc2::Unit* mineral_target = units.GetClosestUnit(i->pos);
        if (!mineral_target)
            continue;

        gAPI->action().Cast(*i, sc2::ABILITY_ID::EFFECT_CALLDOWNMULE, *mineral_target);
    }
}

const Expansion* GetBestMiningExpansionNear(const sc2::Unit* unit_) {
    if (!unit_)
        return nullptr;

    sc2::Point2D worker_loc = unit_->pos;
    float unsaturated_dist = std::numeric_limits<float>::max();
    const Expansion* closest_unsaturated = nullptr;
    float saturated_dist = std::numeric_limits<float>::max();
    const Expansion* closest_saturated = nullptr;

    std::vector<const Expansion*> saturated_expansions;

    // Find closest unsaturated expansion
    for (auto& i : gHub->GetExpansions()) {
        if (i.owner != Owner::SELF)
            continue;

        const sc2::Unit* th = gAPI->observer().GetUnit(i.town_hall_tag);
        if (th->build_progress < BUILD_FINISHED)
            continue;

        float dist = sc2::DistanceSquared2D(worker_loc, th->pos);

        if (th->assigned_harvesters >= th->ideal_harvesters) {
            if (dist < saturated_dist) {
                closest_saturated = &i;
                saturated_dist = dist;
            }

            continue;
        }

        if (dist < unsaturated_dist) {
            closest_unsaturated = &i;
            unsaturated_dist = dist;
        }
    }

    if (closest_unsaturated != nullptr)
        return closest_unsaturated;  // Return nearest unsaturated

    return closest_saturated;  // No unsaturated, send to nearest saturated
}

void DistrubuteMineralWorker(const sc2::Unit* unit_) {
    if (!unit_)
        return;

    sc2::Point2D target_loc = gAPI->observer().StartingLocation();
    const Expansion* expansion = GetBestMiningExpansionNear(unit_);
    if (expansion)
        target_loc = expansion->town_hall_location;

    auto patches = gAPI->observer().GetUnits(
        sc2::IsVisibleMineralPatch(), sc2::Unit::Alliance::Neutral);
    const sc2::Unit* mineral_target = patches.GetClosestUnit(target_loc);

    if (!mineral_target)
        return;

    gAPI->action().Cast(*unit_, sc2::ABILITY_ID::SMART, *mineral_target);
}

}  // namespace

void Miner::OnStep(Builder* builder_) {
    SecureMineralsIncome(builder_);
    SecureVespeneIncome();

    if (gHub->GetCurrentRace() == sc2::Race::Terran)
        CallDownMULE();
}

void Miner::OnUnitCreated(const sc2::Unit* unit_, Builder*) {
    if (unit_->unit_type == gHub->GetCurrentWorkerType())
        DistrubuteMineralWorker(unit_);
}

void Miner::OnUnitIdle(const sc2::Unit* unit_, Builder*) {
    if (unit_->unit_type == gHub->GetCurrentWorkerType())
        DistrubuteMineralWorker(unit_);
}
