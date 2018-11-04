// The MIT License (MIT)
//
// Copyright (c) 2017-2018 Alexander Kurbatov

#include "Unit.h"
#include "core/API.h"
#include "core/Helpers.h"

Unit::Unit(sc2::UNIT_TYPEID who_builds_): m_who_builds(who_builds_) {
}

bool Unit::Build(Order* order_) {
    if (!order_->assignee) {
        auto producers = gAPI->observer().GetUnits(IsIdleUnit(m_who_builds));
        if (producers().empty())
            return false;

        order_->assignee = producers().front();
    }

    gAPI->action().Build(*order_);

    return true;
}
