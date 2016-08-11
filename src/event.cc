/*
 * Copyright (C) 2014-2016 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// @file event.cc
/// Implementation of Event Class and its derived classes.

#include "event.h"

#include <boost/range/algorithm.hpp>

#include "ccf_group.h"

namespace scram {
namespace mef {

Event::Event(std::string name, std::string base_path, RoleSpecifier role)
    : Element(std::move(name)),
      Role(role, std::move(base_path)),
      Id(*this, *this),
      orphan_(true) {}

Event::~Event() = default;
PrimaryEvent::~PrimaryEvent() = default;

CcfEvent::CcfEvent(std::string name, const CcfGroup* ccf_group)
    : BasicEvent(std::move(name), ccf_group->base_path(), ccf_group->role()),
      ccf_group_(*ccf_group) {}

void Gate::Validate() const {
  // Detect inhibit flavor.
  if (formula_->type() != "and" || !Element::HasAttribute("flavor") ||
      Element::GetAttribute("flavor").value != "inhibit") {
    return;
  }
  if (formula_->num_args() != 2) {
    throw ValidationError(Element::name() +
                          "INHIBIT gate must have only 2 children");
  }
  int num_conditional = boost::count_if(
      formula_->basic_event_args(),
      [](const BasicEventPtr& event) {
        return event->HasAttribute("flavor") &&
               event->GetAttribute("flavor").value == "conditional";
      });
  if (num_conditional != 1)
    throw ValidationError(Element::name() + " : INHIBIT gate must have" +
                          " exactly one conditional event.");
}

const std::set<std::string> Formula::kTwoOrMore_ = {{"and"}, {"or"}, {"nand"},
                                                    {"nor"}};

const std::set<std::string> Formula::kSingle_ = {{"not"}, {"null"}};

Formula::Formula(const std::string& type)
    : type_(type),
      vote_number_(0) {}

int Formula::vote_number() const {
  if (!vote_number_) throw LogicError("Vote number is not set.");
  return vote_number_;
}

void Formula::vote_number(int number) {
  if (type_ != "atleast") {
    throw LogicError("Vote number can only be defined for 'atleast' formulas. "
                     "The operator of this formula is '" + type_ + "'.");
  }
  if (number < 2) throw InvalidArgument("Vote number cannot be less than 2.");
  if (vote_number_) throw LogicError("Trying to re-assign a vote number");

  vote_number_ = number;
}

void Formula::Validate() const {
  assert(kTwoOrMore_.count(type_) || kSingle_.count(type_) ||
         type_ == "atleast" || type_ == "xor");

  int size = Formula::num_args();
  std::string msg;
  if (kTwoOrMore_.count(type_) && size < 2) {
    msg += "\"" + type_ + "\" formula must have 2 or more arguments.";

  } else if (kSingle_.count(type_) && size != 1) {
    msg += "\"" + type_ + "\" formula must have only one argument.";

  } else if (type_ == "xor" && size != 2) {
    msg += "\"" + type_ + "\" formula must have exactly 2 arguments.";

  } else if (type_ == "atleast" && size <= vote_number_) {
    msg += "\"" + type_ + "\" formula must have more arguments "
           "than its vote number " + std::to_string(vote_number_) + ".";
  }
  if (!msg.empty()) throw ValidationError(msg);
}

}  // namespace mef
}  // namespace scram
