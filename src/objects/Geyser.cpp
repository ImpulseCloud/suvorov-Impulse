// The MIT License (MIT)
//
// Copyright (c) 2017-2018 Alexander Kurbatov

#include "Geyser.h"

Geyser::Geyser(const sc2::Unit& unit_): GameObject(unit_), pos(unit_.pos) {
}

Geyser::Geyser(const sc2::UnitOrder& order_): GameObject(order_.target_unit_tag),
    pos(order_.target_pos) {
}

bool Geyser::operator==(const Geyser& geyser_) const {
    return this->tag() == geyser_.tag() ||
        (this->pos.x == geyser_.pos.x && this->pos.y == geyser_.pos.y);
}
