// The MIT License (MIT)
//
// Copyright (c) 2017-2020 Alexander Kurbatov

#include "Worker.h"
#include "core/API.h"

Worker::Worker(const sc2::Unit& unit_):
    GameObject(unit_), m_job(Job::GATHERING_MINERALS),
        m_resource_carried_last(sc2::BUFF_ID::INVALID) {
}

void Worker::BuildRefinery(Order* order_, const sc2::Unit* geyser_) {
    order_->assignee = Tag();

    gAPI->action().Build(*order_, geyser_);
    m_job = Job::BUILDING_REFINERY;
}

void Worker::Build(Order* order_, const sc2::Point2D& point_) {
    order_->assignee = Tag();

    gAPI->action().Build(*order_, point_);
    m_job = Job::BUILDING;
}

void Worker::GatherVespene(const sc2::Unit& target_) {
    gAPI->action().Cast(ToUnit(), sc2::ABILITY_ID::SMART, target_);
    m_job = Job::GATHERING_VESPENE;
}

uint32_t Worker::CheckMineralsMined() {
    const sc2::Unit* unit = gAPI->observer().GetUnit(Tag());
    if (!unit || (!m_resource_carried_last &&
        (unit->buffs.empty() || !sc2::IsCarryingMinerals(*unit))))
        return 0;  // no resource carried before or now (NONE -> NONE)

    if (std::find(unit->buffs.begin(), unit->buffs.end(), m_resource_carried_last)
       != unit->buffs.end()) {
        return 0;  // last resource still carried (MINERAL -> MINERAL)
    }

    auto is_mineral = [](const sc2::BuffID& buff){
        return buff == sc2::BUFF_ID::CARRYMINERALFIELDMINERALS
            || buff == sc2::BUFF_ID::CARRYHIGHYIELDMINERALFIELDMINERALS;
    };
    auto it = std::find_if(unit->buffs.begin(), unit->buffs.end(), is_mineral);

    if (it == unit->buffs.end() || unit->buffs.empty()) {
        if (m_resource_carried_last == sc2::BUFF_ID::CARRYMINERALFIELDMINERALS) {
            m_resource_carried_last = sc2::BUFF_ID::INVALID;
            return 5;  // normal minerals
        }

        if (m_resource_carried_last == sc2::BUFF_ID::CARRYHIGHYIELDMINERALFIELDMINERALS) {
            m_resource_carried_last = sc2::BUFF_ID::INVALID;
            return 7;  // gold minerals
        }
    }  // dropped off mineral (MINERAL -> NONE)

    if (!m_resource_carried_last && it != unit->buffs.end()) {
        m_resource_carried_last = *it;
        return 0;
    }  // have new mineral (NONE -> MINERAL)

    return 0;  // this should never be reached
}

uint32_t Worker::CheckVespeneMined() {
    const sc2::Unit* unit = gAPI->observer().GetUnit(Tag());
    if (!unit || (!m_resource_carried_last &&
        (unit->buffs.empty() || !sc2::IsCarryingVespene(*unit))))
        return 0;  // no resource carried before or now (NONE -> NONE)

    if (std::find(unit->buffs.begin(), unit->buffs.end(), m_resource_carried_last)
       != unit->buffs.end()) {
        return 0;  // last resource still carried (VESPENE -> VESPENE)
    }

    auto is_vespene = [](const sc2::BuffID& buff){
    return buff == sc2::BUFF_ID::CARRYHARVESTABLEVESPENEGEYSERGAS
        || buff == sc2::BUFF_ID::CARRYHARVESTABLEVESPENEGEYSERGASPROTOSS
        || buff == sc2::BUFF_ID::CARRYHARVESTABLEVESPENEGEYSERGASZERG;
    };  // NOTE (impulsecloud): need to change this to include rich vespene
    auto it = std::find_if(unit->buffs.begin(), unit->buffs.end(), is_vespene);

    if (it == unit->buffs.end() || unit->buffs.empty()) {
        // NOTE (impulsecloud): we only have BUFF_IDs for normal vespene right now
        m_resource_carried_last = sc2::BUFF_ID::INVALID;
        return 4;  // normal minerals

//        if (m_resource_carried_last == sc2::BUFF_ID:: ?RICHVESPENE? ) {
//            m_resource_carried_last = sc2::BUFF_ID::INVALID;
//            return 5;  // NOTE (impulsecloud): ich vespene ?? NOT IN BUFF_ID enum!
//        }
    }  // dropped off mineral (VESPENE -> NONE)

    if (!m_resource_carried_last && it != unit->buffs.end()) {
        m_resource_carried_last = *it;
        return 0;
    }  // have new mineral (NONE -> VESPENE)

    return 0;  // this should never be reached
}

