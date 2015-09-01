/*
 * Copyright (C) 2014-2015 Olzhas Rakhimov
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

/// @file preprocessor.cc
/// Implementation of preprocessing algorithms.
/// The main goal of preprocessing algorithms is
/// to make Boolean graphs simpler, modular, easier for analysis.
///
/// If a preprocessing algorithm has
/// its limitations, side-effects, and assumptions,
/// the documentation in the header file
/// must contain all the relevant information within
/// its description, notes, or warnings.
/// The default assumption for all algorithms is
/// that the Boolean graph is valid and well-formed.
///
/// Some suggested Notes/Warnings: (Contract for preprocessing algorithms)
///
///   * Works with coherent graphs only
///   * Works with positive gates or nodes only
///   * Depends on node visit information, gate marks, or other node flags.
///   * May introduce NULL or UNITY state gates or constants
///   * May introduce NULL/NOT type gates
///   * Operates on certain gate types only
///   * Works with normalized gates or structure only
///   * Cannot accept a graph with gates of certain types
///   * May destroy modules
///   * Can accept graphs with constants or constant gates
///   * Depends on other preprocessing functions or algorithms
///   * Swaps the root gate of the graph with another (arg) gate
///   * Removes gates or other kind of nodes
///   * May introduce new gate clones or subgraphs,
///     making the graph more complex.
///   * Works on particular cases or setups only
///   * Has tradeoffs
///   * Runs better/More effective before/after some preprocessing step(s)
///   * Coupled with another preprocessing algorithms
///
/// Assuming that the Boolean graph is provided
/// in the state as described in the contract,
/// the algorithms should never throw an exception.
/// The algorithms must guarantee that,
/// given a valid and well-formed Boolean graph,
/// the resulting Boolean graph will at least be
/// valid, well-formed,
/// and semantically equivalent (isomorphic) to the input Boolean graph.
///
/// If the contract is not respected,
/// the result or behavior of the algorithm can be undefined.
/// There is no requirement
/// to check for the broken contract
/// and to exit gracefully.

#include "preprocessor.h"

#include <algorithm>
#include <list>
#include <queue>

#include "logger.h"

namespace scram {

Preprocessor::Preprocessor(BooleanGraph* graph) noexcept
    : graph_(graph),
      root_sign_(1) {}

void Preprocessor::ProcessFaultTree() noexcept {
  assert(graph_->root());
  assert(graph_->root()->parents().empty());
  assert(!graph_->root()->mark());

  CLOCK(time_1);
  LOG(DEBUG2) << "Preprocessing Phase I...";
  Preprocessor::PhaseOne();
  LOG(DEBUG2) << "Finished Preprocessing Phase I in " << DUR(time_1);
  if (Preprocessor::CheckRootGate()) return;

  CLOCK(time_2);
  LOG(DEBUG2) << "Preprocessing Phase II...";
  Preprocessor::PhaseTwo();
  LOG(DEBUG2) << "Finished Preprocessing Phase II in " << DUR(time_2);
  if (Preprocessor::CheckRootGate()) return;

  if (!graph_->normal_) {
    CLOCK(time_3);
    LOG(DEBUG2) << "Preprocessing Phase III...";
    Preprocessor::PhaseThree();
    LOG(DEBUG2) << "Finished Preprocessing Phase III in " << DUR(time_3);
    graph_->normal_ = true;
    if (Preprocessor::CheckRootGate()) return;
  }

  if (!graph_->coherent()) {
    CLOCK(time_4);
    LOG(DEBUG2) << "Preprocessing Phase IV...";
    Preprocessor::PhaseFour();
    LOG(DEBUG2) << "Finished Preprocessing Phase IV in " << DUR(time_4);
    if (Preprocessor::CheckRootGate()) return;
  }

  CLOCK(time_5);
  LOG(DEBUG2) << "Preprocessing Phase V...";
  Preprocessor::PhaseFive();
  LOG(DEBUG2) << "Finished Preprocessing Phase V in " << DUR(time_5);

  Preprocessor::CheckRootGate();  // To cleanup.

  assert(const_gates_.empty());
  assert(null_gates_.empty());
  assert(graph_->normal_);
}

void Preprocessor::PhaseOne() noexcept {
  if (!graph_->constants_.empty()) {
    LOG(DEBUG3) << "Removing constants...";
    Preprocessor::RemoveConstants();
    LOG(DEBUG3) << "Constant are removed!";
  }
  if (!graph_->coherent_) {
    LOG(DEBUG3) << "Partial normalization of gates...";
    Preprocessor::NormalizeGates(false);
    LOG(DEBUG3) << "Finished the partial normalization of gates!";
  }
  if (!graph_->null_gates_.empty()) {
    LOG(DEBUG3) << "Removing NULL gates...";
    Preprocessor::RemoveNullGates();
    LOG(DEBUG3) << "Finished cleaning NULL gates!";
  }
}

void Preprocessor::PhaseTwo() noexcept {
  CLOCK(mult_time);
  LOG(DEBUG3) << "Detecting multiple definitions...";
  bool graph_changed = true;
  while (graph_changed) {
    graph_changed = Preprocessor::ProcessMultipleDefinitions();
  }
  LOG(DEBUG3) << "Finished multi-definition detection in " << DUR(mult_time);

  if (Preprocessor::CheckRootGate()) return;

  LOG(DEBUG3) << "Detecting modules...";
  Preprocessor::DetectModules();
  LOG(DEBUG3) << "Finished module detection!";

  CLOCK(merge_time);
  LOG(DEBUG3) << "Merging common arguments...";
  Preprocessor::MergeCommonArgs();
  LOG(DEBUG3) << "Finished merging common args in " << DUR(merge_time);

  if (graph_->coherent()) {
    CLOCK(optim_time);
    LOG(DEBUG3) << "Boolean optimization...";
    Preprocessor::BooleanOptimization();
    LOG(DEBUG3) << "Finished Boolean optimization in " << DUR(optim_time);
  }

  if (Preprocessor::CheckRootGate()) return;

  CLOCK(decom_time);
  LOG(DEBUG3) << "Decomposition of common nodes...";
  Preprocessor::DecomposeCommonNodes();
  LOG(DEBUG3) << "Finished the Decomposition in " << DUR(decom_time);

  if (Preprocessor::CheckRootGate()) return;

  LOG(DEBUG3) << "Processing Distributivity...";
  graph_->ClearGateMarks();
  Preprocessor::DetectDistributivity(graph_->root());
  Preprocessor::ClearConstGates();
  Preprocessor::ClearNullGates();
  LOG(DEBUG3) << "Distributivity detection is done!";

  LOG(DEBUG3) << "Coalescing gates...";
  graph_changed = true;
  while (graph_changed) {
    assert(const_gates_.empty());
    assert(null_gates_.empty());

    graph_changed = false;
    graph_->ClearGateMarks();
    if (graph_->root()->state() == kNormalState)
      Preprocessor::JoinGates(graph_->root(), false);  // Registers const gates.

    if (!const_gates_.empty()) {
      Preprocessor::ClearConstGates();
      graph_changed = true;
    }
  }
  LOG(DEBUG3) << "Gate coalescense is done!";

  if (Preprocessor::CheckRootGate()) return;

  LOG(DEBUG3) << "Detecting modules...";
  Preprocessor::DetectModules();
  LOG(DEBUG3) << "Finished module detection!";
}

void Preprocessor::PhaseThree() noexcept {
  assert(!graph_->normal_);
  LOG(DEBUG3) << "Full normalization of gates...";
  assert(root_sign_ == 1);
  Preprocessor::NormalizeGates(true);
  LOG(DEBUG3) << "Finished the full normalization gates!";

  if (Preprocessor::CheckRootGate()) return;
  Preprocessor::PhaseTwo();
}

void Preprocessor::PhaseFour() noexcept {
  assert(!graph_->coherent());
  LOG(DEBUG3) << "Propagating complements...";
  if (root_sign_ < 0) {
    IGatePtr root = graph_->root();
    assert(root->type() == kOrGate || root->type() == kAndGate ||
           root->type() == kNullGate);
    if (root->type() == kOrGate || root->type() == kAndGate)
      root->type(root->type() == kOrGate ? kAndGate : kOrGate);
    root->InvertArgs();
    root_sign_ = 1;
  }
  std::map<int, IGatePtr> complements;
  graph_->ClearGateMarks();
  Preprocessor::PropagateComplements(graph_->root(), &complements);
  complements.clear();
  LOG(DEBUG3) << "Complement propagation is done!";

  if (Preprocessor::CheckRootGate()) return;
  Preprocessor::PhaseTwo();
}

void Preprocessor::PhaseFive() noexcept {
  LOG(DEBUG3) << "Coalescing gates...";
  bool graph_changed = true;
  while (graph_changed) {
    assert(const_gates_.empty());
    assert(null_gates_.empty());

    graph_changed = false;
    graph_->ClearGateMarks();
    if (graph_->root()->state() == kNormalState)
      Preprocessor::JoinGates(graph_->root(), true);  // Make layered.

    if (!const_gates_.empty()) {
      Preprocessor::ClearConstGates();
      graph_changed = true;
    }
  }
  LOG(DEBUG3) << "Gate coalescense is done!";

  if (Preprocessor::CheckRootGate()) return;
  Preprocessor::PhaseTwo();
  if (Preprocessor::CheckRootGate()) return;

  LOG(DEBUG3) << "Coalescing gates...";
  graph_changed = true;
  while (graph_changed) {
    assert(const_gates_.empty());
    assert(null_gates_.empty());

    graph_changed = false;
    graph_->ClearGateMarks();
    if (graph_->root()->state() == kNormalState)
      Preprocessor::JoinGates(graph_->root(), true);  // Make layered.

    if (!const_gates_.empty()) {
      Preprocessor::ClearConstGates();
      graph_changed = true;
    }
  }
  LOG(DEBUG3) << "Gate coalescense is done!";
}

bool Preprocessor::CheckRootGate() noexcept {
  IGatePtr root = graph_->root();
  if (root->state() != kNormalState) {  // The root gate has become constant.
    if (root_sign_ < 0) {
      State orig_state = root->state();
      root = IGatePtr(new IGate(kNullGate));
      graph_->root(root);
      if (orig_state == kNullState) {
        root->MakeUnity();
      } else {
        assert(orig_state == kUnityState);
        root->Nullify();
      }
      root_sign_ = 1;
    }
    return true;  // No more processing is needed.
  }
  if (root->type() == kNullGate) {  // Special case of preprocessing.
    assert(root->args().size() == 1);
    if (!root->gate_args().empty()) {
      int signed_index = root->gate_args().begin()->first;
      root = root->gate_args().begin()->second;
      graph_->root(root);  // Destroy the previous root.
      assert(root->parents().empty());
      root_sign_ *= signed_index > 0 ? 1 : -1;
    } else {
      assert(root->variable_args().size() == 1);
      if (root_sign_ < 0) root->InvertArgs();
      root_sign_ = 1;
      return true;  // Only one variable argument.
    }
  }
  return false;
}

void Preprocessor::RemoveNullGates() noexcept {
  assert(null_gates_.empty());
  assert(!graph_->null_gates_.empty());
  null_gates_ = graph_->null_gates_;  // Transferring for internal uses.
  graph_->null_gates_.clear();

  IGatePtr root = graph_->root();
  if (null_gates_.size() == 1 && null_gates_.front().lock() == root) {
    null_gates_.clear();  // Special case of only one NULL gate as the root.
    return;
  }

  Preprocessor::ClearNullGates();
  assert(null_gates_.empty());
}

void Preprocessor::RemoveConstants() noexcept {
  assert(const_gates_.empty());
  assert(!graph_->constants_.empty());
  std::vector<std::weak_ptr<Constant> >::iterator it;
  for (it = graph_->constants_.begin(); it != graph_->constants_.end(); ++it) {
    if (it->expired()) continue;
    Preprocessor::PropagateConstant(it->lock());
    assert(it->expired());
  }
  assert(const_gates_.empty());
  graph_->constants_.clear();
}

void Preprocessor::PropagateConstant(const ConstantPtr& constant) noexcept {
  while (!constant->parents().empty()) {
    IGatePtr parent = constant->parents().begin()->second.lock();

    int sign = parent->args().count(constant->index()) ? 1 : -1;
    Preprocessor::ProcessConstantArg(parent, sign * constant->index(),
                                     constant->state());

    if (parent->state() != kNormalState) {
      Preprocessor::PropagateConstGate(parent);
    } else if (parent->type() == kNullGate) {
      Preprocessor::PropagateNullGate(parent);
    }
  }
}

void Preprocessor::ProcessConstantArg(const IGatePtr& gate, int arg,
                                      bool state) noexcept {
  if (arg < 0) state = !state;

  if (state) {  // Unity state or True arg.
    Preprocessor::ProcessTrueArg(gate, arg);
  } else {  // Null state or False arg.
    Preprocessor::ProcessFalseArg(gate, arg);
  }
}

void Preprocessor::ProcessTrueArg(const IGatePtr& gate, int arg) noexcept {
  switch (gate->type()) {
    case kNullGate:
    case kOrGate:
      gate->MakeUnity();
      break;
    case kNandGate:
    case kAndGate:
      Preprocessor::RemoveConstantArg(gate, arg);
      break;
    case kNorGate:
    case kNotGate:
      gate->Nullify();
      break;
    case kXorGate:  // Special handling due to its internal negation.
      assert(gate->args().size() == 2);
      gate->EraseArg(arg);
      assert(gate->args().size() == 1);
      gate->type(kNotGate);
      break;
    case kAtleastGate:  // (K - 1) / (N - 1).
      assert(gate->args().size() > 2);
      gate->EraseArg(arg);
      int k = gate->vote_number();
      --k;
      gate->vote_number(k);
      if (k == 1) gate->type(kOrGate);
      break;
  }
}

void Preprocessor::ProcessFalseArg(const IGatePtr& gate, int arg) noexcept {
  switch (gate->type()) {
    case kNorGate:
    case kXorGate:
    case kOrGate:
      Preprocessor::RemoveConstantArg(gate, arg);
      break;
    case kNullGate:
    case kAndGate:
      gate->Nullify();
      break;
    case kNandGate:
    case kNotGate:
      gate->MakeUnity();
      break;
    case kAtleastGate:  // K / (N - 1).
      assert(gate->args().size() > 2);
      gate->EraseArg(arg);
      int k = gate->vote_number();
      int n = gate->args().size();
      if (k == n) gate->type(kAndGate);
      break;
  }
}

void Preprocessor::RemoveConstantArg(const IGatePtr& gate, int arg) noexcept {
  assert(gate->args().size() > 1);  // One-arg gates must have become constant.
  gate->EraseArg(arg);
  if (gate->args().size() == 1) {
    switch (gate->type()) {
      case kXorGate:
      case kOrGate:
      case kAndGate:
        gate->type(kNullGate);
        break;
      case kNorGate:
      case kNandGate:
        gate->type(kNotGate);
        break;
      default:
        assert(false);  // Other one-arg gates must not happen.
    }
  }  // More complex cases with K/N gates are handled by the caller functions.
}

void Preprocessor::PropagateConstGate(const IGatePtr& gate) noexcept {
  assert(gate->state() != kNormalState);

  while (!gate->parents().empty()) {
    IGatePtr parent = gate->parents().begin()->second.lock();

    int sign = parent->args().count(gate->index()) ? 1 : -1;
    bool state = gate->state() == kNullState ? false : true;
    Preprocessor::ProcessConstantArg(parent, sign * gate->index(), state);

    if (parent->state() != kNormalState) {
      Preprocessor::PropagateConstGate(parent);
    } else if (parent->type() == kNullGate) {
      Preprocessor::PropagateNullGate(parent);
    }
  }
}

void Preprocessor::PropagateNullGate(const IGatePtr& gate) noexcept {
  assert(gate->type() == kNullGate);

  while (!gate->parents().empty()) {
    IGatePtr parent = gate->parents().begin()->second.lock();
    int sign = parent->args().count(gate->index()) ? 1 : -1;
    parent->JoinNullGate(sign * gate->index());

    if (parent->state() != kNormalState) {
      Preprocessor::PropagateConstGate(parent);
    } else if (parent->type() == kNullGate) {
      Preprocessor::PropagateNullGate(parent);
    }
  }
}

void Preprocessor::ClearConstGates() noexcept {
  graph_->ClearGateMarks();  // New gates may get created without marks!
  std::vector<IGateWeakPtr>::iterator it;
  for (it = const_gates_.begin(); it != const_gates_.end(); ++it) {
    if (it->expired()) continue;
    Preprocessor::PropagateConstGate(it->lock());
  }
  const_gates_.clear();
}

void Preprocessor::ClearNullGates() noexcept {
  graph_->ClearGateMarks();  // New gates may get created without marks!
  std::vector<IGateWeakPtr>::iterator it;
  for (it = null_gates_.begin(); it != null_gates_.end(); ++it) {
    if (it->expired()) continue;
    Preprocessor::PropagateNullGate(it->lock());
  }
  null_gates_.clear();
}

void Preprocessor::NormalizeGates(bool full) noexcept {
  assert(const_gates_.empty());
  assert(null_gates_.empty());
  // Handle special case for the root gate.
  IGatePtr root_gate = graph_->root();
  Operator type = root_gate->type();
  switch (type) {
    case kNorGate:
    case kNandGate:
    case kNotGate:
      root_sign_ *= -1;
  }
  // Process negative gates.
  // Note that root's negative gate is processed in the above lines.
  graph_->ClearGateMarks();
  Preprocessor::NotifyParentsOfNegativeGates(root_gate);

  graph_->ClearGateMarks();
  Preprocessor::NormalizeGate(root_gate, full);  // Registers null gates only.

  assert(const_gates_.empty());
  Preprocessor::ClearNullGates();
}

void Preprocessor::NotifyParentsOfNegativeGates(
    const IGatePtr& gate) noexcept {
  if (gate->mark()) return;
  gate->mark(true);
  std::vector<int> to_negate;  // Args to get the negation.
  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
    IGatePtr arg = it->second;
    Preprocessor::NotifyParentsOfNegativeGates(arg);
    switch (arg->type()) {
      case kNorGate:
      case kNandGate:
      case kNotGate:
        to_negate.push_back(it->first);
    }
  }
  std::vector<int>::iterator it_neg;
  for (it_neg = to_negate.begin(); it_neg != to_negate.end(); ++it_neg) {
    gate->InvertArg(*it_neg);  // Does not produce constants or duplicates.
  }
}

void Preprocessor::NormalizeGate(const IGatePtr& gate, bool full) noexcept {
  if (gate->mark()) return;
  gate->mark(true);
  assert(gate->state() == kNormalState);
  assert(!gate->args().empty());
  // Depth-first traversal before the arguments may get changed.
  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
    Preprocessor::NormalizeGate(it->second, full);
  }

  switch (gate->type()) {  // Negation is already processed.
    case kNotGate:
      assert(gate->args().size() == 1);
      gate->type(kNullGate);
      null_gates_.push_back(gate);  // Register for removal.
      break;
    case kNorGate:
    case kOrGate:
      assert(gate->args().size() > 1);
      gate->type(kOrGate);
      break;
    case kNandGate:
    case kAndGate:
      assert(gate->args().size() > 1);
      gate->type(kAndGate);
      break;
    case kXorGate:
      assert(gate->args().size() == 2);
      if (!full) break;
      Preprocessor::NormalizeXorGate(gate);
      break;
    case kAtleastGate:
      assert(gate->args().size() > 2);
      assert(gate->vote_number() > 1);
      if (!full) break;
      Preprocessor::NormalizeAtleastGate(gate);
      break;
    case kNullGate:
      null_gates_.push_back(gate);  // Register for removal.
      break;
  }
}

void Preprocessor::NormalizeXorGate(const IGatePtr& gate) noexcept {
  assert(gate->args().size() == 2);
  IGatePtr gate_one(new IGate(kAndGate));
  IGatePtr gate_two(new IGate(kAndGate));
  gate_one->mark(true);
  gate_two->mark(true);

  gate->type(kOrGate);
  std::set<int>::const_iterator it = gate->args().begin();
  gate->ShareArg(*it, gate_one);
  gate->ShareArg(*it, gate_two);
  gate_two->InvertArg(*it);

  ++it;  // Handling the second argument.
  gate->ShareArg(*it, gate_one);
  gate_one->InvertArg(*it);
  gate->ShareArg(*it, gate_two);

  gate->EraseAllArgs();
  gate->AddArg(gate_one->index(), gate_one);
  gate->AddArg(gate_two->index(), gate_two);
}

void Preprocessor::NormalizeAtleastGate(const IGatePtr& gate) noexcept {
  assert(gate->type() == kAtleastGate);
  int vote_number = gate->vote_number();

  assert(vote_number > 0);  // Vote number can be 1 for special OR gates.
  assert(gate->args().size() > 1);
  if (gate->args().size() == vote_number) {
    gate->type(kAndGate);
    return;
  } else if (vote_number == 1) {
    gate->type(kOrGate);
    return;
  }

  const std::set<int>& args = gate->args();
  std::set<int>::const_iterator it = args.begin();

  IGatePtr first_arg(new IGate(kAndGate));
  gate->ShareArg(*it, first_arg);

  IGatePtr grand_arg(new IGate(kAtleastGate));
  first_arg->AddArg(grand_arg->index(), grand_arg);
  grand_arg->vote_number(vote_number - 1);

  IGatePtr second_arg(new IGate(kAtleastGate));
  second_arg->vote_number(vote_number);

  for (++it; it != args.end(); ++it) {
    gate->ShareArg(*it, grand_arg);
    gate->ShareArg(*it, second_arg);
  }

  first_arg->mark(true);
  second_arg->mark(true);
  grand_arg->mark(true);

  gate->type(kOrGate);
  gate->EraseAllArgs();
  gate->AddArg(first_arg->index(), first_arg);
  gate->AddArg(second_arg->index(), second_arg);

  Preprocessor::NormalizeAtleastGate(grand_arg);
  Preprocessor::NormalizeAtleastGate(second_arg);
}

void Preprocessor::PropagateComplements(
    const IGatePtr& gate,
    std::map<int, IGatePtr>* gate_complements) noexcept {
  if (gate->mark()) return;
  gate->mark(true);
  // If the argument gate is complement,
  // then create a new gate
  // that propagates its sign to its arguments
  // and itself becomes non-complement.
  // Keep track of complement gates
  // for optimization of repeated complements.
  std::vector<int> to_swap;  // Args with negation to get swapped.
  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
    IGatePtr arg_gate = it->second;
    if (it->first < 0) {
      to_swap.push_back(it->first);
      if (gate_complements->count(arg_gate->index())) continue;
      Operator type = arg_gate->type();
      assert(type == kAndGate || type == kOrGate);
      Operator complement_type = type == kOrGate ? kAndGate : kOrGate;
      IGatePtr complement_gate;
      if (arg_gate->parents().size() == 1) {  // Optimization. Reuse.
        arg_gate->type(complement_type);
        arg_gate->InvertArgs();
        complement_gate = arg_gate;
      } else {
        complement_gate = arg_gate->Clone();
        complement_gate->type(complement_type);
        complement_gate->InvertArgs();
      }
      gate_complements->insert(std::make_pair(arg_gate->index(),
                                              complement_gate));
      arg_gate = complement_gate;  // Needed for further propagation.
    }
    Preprocessor::PropagateComplements(arg_gate, gate_complements);
  }

  std::vector<int>::iterator it_ch;
  for (it_ch = to_swap.begin(); it_ch != to_swap.end(); ++it_ch) {
    assert(*it_ch < 0);
    gate->EraseArg(*it_ch);
    IGatePtr complement = gate_complements->find(-*it_ch)->second;
    gate->AddArg(complement->index(), complement);
    assert(gate->state() == kNormalState);  // No duplicates.
  }
}

bool Preprocessor::JoinGates(const IGatePtr& gate, bool common) noexcept {
  if (gate->mark()) return false;
  gate->mark(true);
  bool possible = true;  // If joining is possible at all.
  Operator target_type;  // What kind of arg gate are we searching for?
  switch (gate->type()) {
    case kNandGate:
    case kAndGate:
      assert(gate->args().size() > 1);
      target_type = kAndGate;
      break;
    case kNorGate:
    case kOrGate:
      assert(gate->args().size() > 1);
      target_type = kOrGate;
      break;
    default:
      possible = false;
  }
  assert(!gate->args().empty());
  std::vector<IGatePtr> to_join;  // Gate arguments of the same logic.
  bool changed = false;  // Indication if the graph is changed.
  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
    IGatePtr arg_gate = it->second;
    if (Preprocessor::JoinGates(arg_gate, common)) changed = true;

    if (!possible) continue;  // Joining with the parent is impossible.

    if (it->first < 0) continue;  // Cannot join a negative arg gate.
    if (arg_gate->IsModule()) continue;  // Preserve modules.
    if (!common && arg_gate->parents().size() > 1) continue;  // Check common.

    if (arg_gate->type() == target_type) to_join.push_back(arg_gate);
  }

  std::vector<IGatePtr>::iterator it_ch;
  for (it_ch = to_join.begin(); it_ch != to_join.end(); ++it_ch) {
    gate->JoinGate(*it_ch);
    changed = true;
    if (gate->state() != kNormalState) {
      const_gates_.push_back(gate);  // Register for future processing.
      break;  // The parent is constant. No need to join other arguments.
    }
    assert(gate->args().size() > 1);  // Does not produce NULL type gates.
  }
  return changed;
}

bool Preprocessor::ProcessMultipleDefinitions() noexcept {
  assert(null_gates_.empty());
  assert(const_gates_.empty());

  graph_->ClearGateMarks();
  // The original gate and its multiple definitions.
  std::unordered_map<IGatePtr, std::vector<IGateWeakPtr> > multi_def;
  GateSet* unique_gates = new GateSet();
  Preprocessor::DetectMultipleDefinitions(graph_->root(), &multi_def,
                                          unique_gates);
  delete unique_gates;  // To remove extra reference counts.
  graph_->ClearGateMarks();

  if (multi_def.empty()) return false;
  LOG(DEBUG4) << multi_def.size() << " gates are multiply defined.";
  for (const auto& def : multi_def) {
    LOG(DEBUG5) << "Gate " << def.first->index() << ": "
        << def.second.size() << " times.";
    for (const IGateWeakPtr& dup : def.second) {
      if (dup.expired()) continue;
      Preprocessor::ReplaceGate(dup.lock(), def.first);
    }
  }
  Preprocessor::ClearConstGates();
  Preprocessor::ClearNullGates();
  return true;
}

void Preprocessor::DetectMultipleDefinitions(
    const IGatePtr& gate,
    std::unordered_map<IGatePtr, std::vector<IGateWeakPtr> >* multi_def,
    GateSet* unique_gates) noexcept {
  if (gate->mark()) return;
  gate->mark(true);
  assert(gate->state() == kNormalState);

  if (!gate->IsModule()) {  // Modules are unique by definition.
    std::pair<IGatePtr, bool> ret = unique_gates->insert(gate);
    assert(ret.first->mark());
    if (!ret.second) {  // The gate is duplicate.
      (*multi_def)[ret.first].push_back(gate);
      return;
    }
  }
  // No redefinition is found for this gate.
  for (const std::pair<int, IGatePtr>& arg : gate->gate_args()) {
    Preprocessor::DetectMultipleDefinitions(arg.second, multi_def,
                                            unique_gates);
  }
}

void Preprocessor::DetectModules() noexcept {
  assert(const_gates_.empty());
  assert(null_gates_.empty());
  // First stage, traverse the graph depth-first for gates
  // and indicate visit time for each node.
  graph_->ClearNodeVisits();

  LOG(DEBUG4) << "Assigning timings to nodes...";
  IGatePtr root_gate = graph_->root();
  int time = 0;
  Preprocessor::AssignTiming(time, root_gate);
  LOG(DEBUG4) << "Timings are assigned to nodes.";

  graph_->ClearGateMarks();
  Preprocessor::FindModules(root_gate);

  assert(!root_gate->Revisited());
  assert(root_gate->min_time() == 1);
  assert(root_gate->max_time() == root_gate->ExitTime());
}

int Preprocessor::AssignTiming(int time, const IGatePtr& gate) noexcept {
  if (gate->Visit(++time)) return time;  // Revisited gate.
  assert(gate->constant_args().empty());

  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
    time = Preprocessor::AssignTiming(time, it->second);
  }

  std::unordered_map<int, VariablePtr>::const_iterator it_b;
  for (it_b = gate->variable_args().begin();
       it_b != gate->variable_args().end(); ++it_b) {
    it_b->second->Visit(++time);  // Enter the leaf.
    it_b->second->Visit(time);  // Exit at the same time.
  }
  bool re_visited = gate->Visit(++time);  // Exiting the gate in second visit.
  assert(!re_visited);  // No cyclic visiting.
  return time;
}

void Preprocessor::FindModules(const IGatePtr& gate) noexcept {
  if (gate->mark()) return;
  gate->mark(true);
  int enter_time = gate->EnterTime();
  int exit_time = gate->ExitTime();
  int min_time = enter_time;
  int max_time = exit_time;

  std::vector<std::pair<int, NodePtr> > non_shared_args;
  std::vector<std::pair<int, NodePtr> > modular_args;
  std::vector<std::pair<int, NodePtr> > non_modular_args;

  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
    IGatePtr arg_gate = it->second;
    Preprocessor::FindModules(arg_gate);
    if (arg_gate->IsModule() && !arg_gate->Revisited()) {
      assert(arg_gate->parents().size() == 1);
      assert(arg_gate->parents().count(gate->index()));

      non_shared_args.push_back(*it);
      continue;  // Sub-graph's visit times are within the Enter and Exit time.
    }
    int min = arg_gate->min_time();
    int max = arg_gate->max_time();
    assert(min > 0);
    assert(max > 0);
    assert(max > min);
    if (min > enter_time && max < exit_time) {
      modular_args.push_back(*it);
    } else {
      non_modular_args.push_back(*it);
    }
    min_time = std::min(min_time, min);
    max_time = std::max(max_time, max);
  }

  std::unordered_map<int, VariablePtr>::const_iterator it_b;
  for (it_b = gate->variable_args().begin();
       it_b != gate->variable_args().end(); ++it_b) {
    VariablePtr arg = it_b->second;
    int min = arg->EnterTime();
    int max = arg->LastVisit();
    assert(min > 0);
    assert(max > 0);
    if (min == max) {
      assert(min > enter_time && max < exit_time);
      assert(arg->parents().size() == 1);
      assert(arg->parents().count(gate->index()));

      non_shared_args.push_back(*it_b);
      continue;  // The single parent argument.
    }
    assert(max > min);
    if (min > enter_time && max < exit_time) {
      modular_args.push_back(*it_b);
    } else {
      non_modular_args.push_back(*it_b);
    }
    min_time = std::min(min_time, min);
    max_time = std::max(max_time, max);
  }

  // Determine if this gate is module itself.
  if (!gate->IsModule() && min_time == enter_time && max_time == exit_time) {
    LOG(DEBUG4) << "Found original module: " << gate->index();
    assert(non_modular_args.empty());
    gate->TurnModule();
  }

  max_time = std::max(max_time, gate->LastVisit());
  gate->min_time(min_time);
  gate->max_time(max_time);

  Preprocessor::ProcessModularArgs(gate, non_shared_args, &modular_args,
                                   &non_modular_args);
}

void Preprocessor::ProcessModularArgs(
    const IGatePtr& gate,
    const std::vector<std::pair<int, NodePtr> >& non_shared_args,
    std::vector<std::pair<int, NodePtr> >* modular_args,
    std::vector<std::pair<int, NodePtr> >* non_modular_args) noexcept {
  assert(gate->args().size() ==
         (non_shared_args.size() + modular_args->size() +
          non_modular_args->size()));
  // Attempting to create new modules for specific gate types.
  switch (gate->type()) {
    case kNorGate:
    case kOrGate:
    case kNandGate:
    case kAndGate:
      Preprocessor::CreateNewModule(gate, non_shared_args);

      Preprocessor::FilterModularArgs(modular_args, non_modular_args);
      assert(modular_args->size() != 1);  // One modular arg is non-shared.
      std::vector<std::vector<std::pair<int, NodePtr> > > groups;
      Preprocessor::GroupModularArgs(*modular_args, &groups);
      Preprocessor::CreateNewModules(gate, *modular_args, groups);
  }
}

std::shared_ptr<IGate> Preprocessor::CreateNewModule(
    const IGatePtr& gate,
    const std::vector<std::pair<int, NodePtr> >& args) noexcept {
  IGatePtr module;  // Empty pointer as an indication of a failure.
  if (args.empty()) return module;
  if (args.size() == 1) return module;
  if (args.size() == gate->args().size()) {
    assert(gate->IsModule());
    return module;
  }
  assert(args.size() < gate->args().size());
  switch (gate->type()) {
    case kNandGate:
    case kAndGate:
      module = IGatePtr(new IGate(kAndGate));
      break;
    case kNorGate:
    case kOrGate:
      module = IGatePtr(new IGate(kOrGate));
      break;
    default:
      return module;  // Cannot create sub-modules for other types.
  }
  module->TurnModule();
  module->mark(true);
  std::vector<std::pair<int, NodePtr> >::const_iterator it;
  for (it = args.begin(); it != args.end(); ++it) {
    gate->TransferArg(it->first, module);
  }
  gate->AddArg(module->index(), module);
  assert(gate->args().size() > 1);
  LOG(DEBUG4) << "Created a module for Gate " << gate->index() << ": Gate "
      << module->index() << " with "  << args.size() << " arguments.";
  return module;
}

namespace {

/// Detects overlap in ranges.
///
/// @param[in] a_min The lower boundary of the first range.
/// @param[in] a_max The upper boundary of the first range.
/// @param[in] b_min The lower boundary of the second range.
/// @param[in] b_max The upper boundary of the second range.
///
/// @returns true if there's overlap in the ranges.
bool DetectOverlap(int a_min, int a_max, int b_min, int b_max) noexcept {
  assert(a_min < a_max);
  assert(b_min < b_max);
  return std::max(a_min, b_min) <= std::min(a_max, b_max);
}

}  // namespace

void Preprocessor::FilterModularArgs(
    std::vector<std::pair<int, NodePtr> >* modular_args,
    std::vector<std::pair<int, NodePtr> >* non_modular_args) noexcept {
  if (modular_args->empty() || non_modular_args->empty()) return;
  std::vector<std::pair<int, NodePtr> > new_non_modular;
  std::vector<std::pair<int, NodePtr> > still_modular;
  std::vector<std::pair<int, NodePtr> >::iterator it;
  for (it = modular_args->begin(); it != modular_args->end(); ++it) {
    int min = it->second->min_time();
    int max = it->second->max_time();
    bool non_module = false;
    std::vector<std::pair<int, NodePtr> >::iterator it_n;
    for (it_n = non_modular_args->begin(); it_n != non_modular_args->end();
         ++it_n) {
      bool overlap = DetectOverlap(min, max, it_n->second->min_time(),
                                   it_n->second->max_time());
      if (overlap) {
        non_module = true;
        break;
      }
    }
    if (non_module) {
      new_non_modular.push_back(*it);
    } else {
      still_modular.push_back(*it);
    }
  }
  Preprocessor::FilterModularArgs(&still_modular, &new_non_modular);
  *modular_args = still_modular;
  non_modular_args->insert(non_modular_args->end(), new_non_modular.begin(),
                           new_non_modular.end());
}

void Preprocessor::GroupModularArgs(
    const std::vector<std::pair<int, NodePtr> >& modular_args,
    std::vector<std::vector<std::pair<int, NodePtr> > >* groups) noexcept {
  if (modular_args.empty()) return;
  assert(modular_args.size() > 1);
  assert(groups->empty());
  std::list<std::pair<int, NodePtr> > member_list(modular_args.begin(),
                                                  modular_args.end());
  while (!member_list.empty()) {
    std::vector<std::pair<int, NodePtr> > group;
    NodePtr first_member = member_list.front().second;
    group.push_back(member_list.front());
    member_list.pop_front();
    int low = first_member->min_time();
    int high = first_member->max_time();

    int prev_size = 0;  // To track the addition of a new member into the group.
    while (prev_size < group.size()) {
      prev_size = group.size();
      std::list<std::pair<int, NodePtr> >::iterator it;
      for (it = member_list.begin(); it != member_list.end();) {
        int min = it->second->min_time();
        int max = it->second->max_time();
        if (DetectOverlap(min, max, low, high)) {
          group.push_back(*it);
          low = std::min(min, low);
          high = std::max(max, high);
          std::list<std::pair<int, NodePtr> >::iterator it_erase = it;
          ++it;
          member_list.erase(it_erase);
        } else {
          ++it;
        }
      }
    }
    assert(group.size() > 1);
    groups->push_back(group);
  }
  LOG(DEBUG4) << "Grouped modular args in " << groups->size() << " group(s).";
  assert(!groups->empty());
}

void Preprocessor::CreateNewModules(
    const IGatePtr& gate,
    const std::vector<std::pair<int, NodePtr>>& modular_args,
    const std::vector<std::vector<std::pair<int, NodePtr>>>& groups) noexcept {
  if (modular_args.empty()) return;
  assert(modular_args.size() > 1);
  assert(!groups.empty());
  if (modular_args.size() == gate->args().size() && groups.size() == 1) {
    assert(gate->IsModule());
    return;
  }
  IGatePtr main_arg;

  if (modular_args.size() == gate->args().size()) {
    assert(groups.size() > 1);
    assert(gate->IsModule());
    main_arg = gate;
  } else {
    main_arg = Preprocessor::CreateNewModule(gate, modular_args);
    assert(main_arg);
  }
  std::vector<std::vector<std::pair<int, NodePtr> > >::const_iterator it;
  for (it = groups.begin(); it != groups.end(); ++it) {
    Preprocessor::CreateNewModule(main_arg, *it);
  }
}

bool Preprocessor::MergeCommonArgs() noexcept {
  assert(null_gates_.empty());
  assert(const_gates_.empty());
  bool changed = false;

  LOG(DEBUG4) << "Merging common arguments for AND gates...";
  bool ret = Preprocessor::MergeCommonArgs(kAndGate);
  if (ret) changed = true;
  LOG(DEBUG4) << "Finished merging for AND gates!";

  LOG(DEBUG4) << "Merging common arguments for OR gates...";
  ret = Preprocessor::MergeCommonArgs(kOrGate);
  if (ret) changed = true;
  LOG(DEBUG4) << "Finished merging for OR gates!";

  assert(null_gates_.empty());
  assert(const_gates_.empty());
  return changed;
}

bool Preprocessor::MergeCommonArgs(const Operator& op) noexcept {
  assert(op == kAndGate || op == kOrGate);
  graph_->ClearNodeCounts();
  graph_->ClearGateMarks();
  // Gather and group gates
  // by their operator types and common arguments.
  Preprocessor::MarkCommonArgs(graph_->root(), op);
  graph_->ClearGateMarks();
  std::vector<std::pair<IGatePtr, std::vector<int> > > group;
  Preprocessor::GatherCommonArgs(graph_->root(), op, &group);
  // Finding common parents for the common arguments.
  MergeTable::Collection parents;
  Preprocessor::GroupCommonParents(2, group, &parents);
  if (parents.empty()) return false;  // No candidates for merging.

  LOG(DEBUG4) << "Merging " << parents.size() << " groups...";
  // After common arguments and parents are grouped,
  // the merging technique must find the most optimal strategy
  // to create new gates
  // that will represent the common arguments.
  // The strategy may favor modularity, size, or other parameters
  // of the new structure of the final graph.
  // The common elements within
  // the groups of common parents and common arguments
  // create the biggest challenge for finding the optimal solution.
  // For example,
  // {
  // (a, b) : (p1, p2),
  // (b, c) : (p2, p3)
  // }
  // The strategy has to make
  // the most optimal choice
  // between two mutually exclusive options.
  graph_->ClearOptiValues();
  /// @todo Must group by size to detect supersets.
  ///       If supersets are processed before the subsets,
  ///       the optimization of the supersets is impossible.
  /// @todo Must find a way to efficiently transfer data
  ///       from the map to the table.
  MergeTable::MergeGroup table(parents.begin(), parents.end());
  // Sorting in descending order for more efficient pop.
  std::sort(table.begin(), table.end(),
            [](const MergeTable::Option& lhs, const MergeTable::Option& rhs) {
              return lhs.first.size() > rhs.first.size();
            });
  assert(table.front().first.size() >= table.back().first.size());
  while (!table.empty()) {
    MergeTable::CommonParents& common_parents = table.back().second;
    MergeTable::CommonArgs& common_args = table.back().first;
    std::vector<IGatePtr> useful_parents;  // With full set of args.

    for (const IGatePtr& common_parent : common_parents) {
      if (common_parent->opti_value()) {  // Modified parent.
        assert(common_parent->opti_value() == 1);
        const std::set<int>& args = common_parent->args();
        bool have_args = std::includes(args.begin(), args.end(),
                                       common_args.begin(), common_args.end());
        // Erased and optimized common args.
        if (!have_args) continue;
      }
      useful_parents.push_back(common_parent);
    }

    if (useful_parents.size() < 2) {  // No point of merging arguments.
      table.pop_back();
      continue;  /// @todo Investigate better options.
    }
    LOG(DEBUG5) << "Merging " << common_args.size() << " args into a new gate";
    IGatePtr parent = useful_parents.front();  // To get the arguments.
    IGatePtr merge_gate(new IGate(parent->type()));
    for (int index : common_args) {
      parent->ShareArg(index, merge_gate);
      for (const IGatePtr& common_parent : useful_parents) {
        common_parent->EraseArg(index);
      }
    }
    for (const IGatePtr& common_parent : useful_parents) {
      common_parent->AddArg(merge_gate->index(), merge_gate);
      common_parent->opti_value(1);  // Mark as processed.
      if (common_parent->args().size() == 1) {
        common_parent->type(kNullGate);
        null_gates_.push_back(common_parent);
      }
      assert(common_parent->state() == kNormalState);
    }
    for (int i = 0; i < table.size() - 1; ++i) {
      std::vector<int>& set_args = table[i].first;
      if (set_args.size() <= common_args.size()) continue;
      bool superset = std::includes(set_args.begin(), set_args.end(),
                                    common_args.begin(), common_args.end());
      if (!superset) continue;
      std::vector<int> diff;
      std::set_difference(set_args.begin(), set_args.end(),
                          common_args.begin(), common_args.end(),
                          std::back_inserter(diff));
      assert(merge_gate->index() > diff.back());
      diff.push_back(merge_gate->index());  // Assumes sequential indexing.
      set_args = diff;
      assert(table[i].first.size() == diff.size());
    }
    table.pop_back();  // Erasing the group from operations.
  }
  Preprocessor::ClearNullGates();
  return true;
}

void Preprocessor::MarkCommonArgs(const IGatePtr& gate,
                                  const Operator& op) noexcept {
  if (gate->mark()) return;
  gate->mark(true);

  bool in_group = gate->type() == op ? true : false;

  for (const std::pair<int, IGatePtr>& arg : gate->gate_args()) {
    IGatePtr arg_gate = arg.second;
    assert(arg_gate->state() == kNormalState);
    Preprocessor::MarkCommonArgs(arg_gate, op);
    if (in_group) arg_gate->AddCount(arg.first > 0);
  }

  if (!in_group) return;  // No need to visit leaf variables.

  for (const std::pair<int, VariablePtr>& arg : gate->variable_args()) {
    arg.second->AddCount(arg.first > 0);
  }
  assert(gate->constant_args().empty());
}

void Preprocessor::GatherCommonArgs(
    const IGatePtr& gate,
    const Operator& op,
    std::vector<std::pair<IGatePtr, std::vector<int> > >* group) noexcept {
  if (gate->mark()) return;
  gate->mark(true);

  bool in_group = gate->type() == op ? true : false;

  std::vector<int> common_args;
  for (const std::pair<int, IGatePtr>& arg : gate->gate_args()) {
    IGatePtr arg_gate = arg.second;
    assert(arg_gate->state() == kNormalState);
    Preprocessor::GatherCommonArgs(arg_gate, op, group);
    if (!in_group) continue;
    int count = arg.first > 0 ? arg_gate->pos_count() : arg_gate->neg_count();
    if (count > 1) common_args.push_back(arg.first);
  }

  if (!in_group) return;  // No need to check variables.

  for (const std::pair<int, VariablePtr>& arg : gate->variable_args()) {
    VariablePtr var = arg.second;
    int count = arg.first > 0 ? var->pos_count() : var->neg_count();
    if (count > 1) common_args.push_back(arg.first);
  }
  assert(gate->constant_args().empty());

  if (common_args.size() < 2) return;  // Can't be merged anyway.

  std::sort(common_args.begin(), common_args.end());
  group->emplace_back(gate, common_args);
}

void Preprocessor::GroupCommonParents(
    int num_common_args,
    const std::vector<std::pair<IGatePtr, std::vector<int> > >& group,
    MergeTable::Collection* parents) noexcept {
  if (group.empty()) return;
  for (int i = 0; i < group.size() - 1; ++i) {
    const std::vector<int>& args_gate = group[i].second;
    assert(args_gate.size() > 1);
    int j = i;
    for (++j; j < group.size(); ++j) {
      const std::vector<int>& args_comp = group[j].second;
      assert(args_comp.size() > 1);

      std::vector<int> common;
      std::set_intersection(args_gate.begin(), args_gate.end(),
                            args_comp.begin(), args_comp.end(),
                            std::back_inserter(common));
      if (common.size() < num_common_args) continue;  // Doesn't satisfy.
      MergeTable::CommonParents& common_parents = (*parents)[common];
      common_parents.insert(group[i].first);
      common_parents.insert(group[j].first);
    }
  }
}

bool Preprocessor::DetectDistributivity(const IGatePtr& gate) noexcept {
  if (gate->mark()) return false;
  gate->mark(true);
  assert(gate->state() == kNormalState);
  bool changed = false;
  bool possible = true;  // Whether or not distributivity possible.
  Operator distr_type;
  switch (gate->type()) {
    case kAndGate:
    case kNandGate:
      distr_type = kOrGate;
      break;
    case kOrGate:
    case kNorGate:
      distr_type = kAndGate;
      break;
    default:
      possible = false;
  }
  std::vector<IGatePtr> candidates;
  // Collect child gates of distributivity type.
  for (const std::pair<int, IGatePtr>& arg : gate->gate_args()) {
    IGatePtr child_gate = arg.second;
    bool ret = Preprocessor::DetectDistributivity(child_gate);
    if (ret) changed = true;
    if (!possible) continue;  // Distributivity is not possible.
    if (arg.first < 0) continue;  // Does not work on negation.
    if (child_gate->state() != kNormalState) continue;  // No arguments.
    if (child_gate->IsModule()) continue;  // Can't have common arguments.
    if (child_gate->type() == distr_type) candidates.push_back(child_gate);
  }
  if (Preprocessor::HandleDistributiveArgs(gate, distr_type, candidates))
    changed = true;
  return changed;
}

bool Preprocessor::HandleDistributiveArgs(
    const IGatePtr& gate,
    const Operator& distr_type,
    const std::vector<IGatePtr>& candidates) noexcept {
  if (candidates.size() < 2) return false;
  // Detecting a combination
  // that gives the most optimization is combinatorial.
  // The problem is similar to merging common arguments of gates.
  std::vector<std::pair<IGatePtr, std::vector<int>>> group;
  for (const IGatePtr& candidate : candidates) {
    group.emplace_back(candidate, std::vector<int>(candidate->args().begin(),
                                                   candidate->args().end()));
  }
  LOG(DEBUG5) << "Considering " << group.size() << " candidates...";
  MergeTable::Collection options;
  Preprocessor::GroupCommonParents(1, group, &options);
  if (options.empty()) return false;
  LOG(DEBUG4) << "Got " << options.size() << " distributive option(s).";

  MergeTable table;
  Preprocessor::GroupDistributiveArgs(options, &table);
  assert(!table.groups.empty());
  LOG(DEBUG4) << "Found " << table.groups.size() << " distributive group(s).";
  // Sanitize the table with single parent gates only.
  for (MergeTable::MergeGroup& group : table.groups) {
    MergeTable::Option& base_option = group.front();
    std::vector<std::pair<IGatePtr, IGatePtr>> to_swap;
    for (const IGatePtr& member : base_option.second) {
      assert(!member->parents().empty());
      if (member->parents().size() > 1) {
        IGatePtr clone = member->Clone();
        clone->mark(true);
        to_swap.emplace_back(member, clone);
      }
    }
    for (const auto& pair : to_swap) {
      gate->EraseArg(pair.first->index());
      gate->AddArg(pair.second->index(), pair.second);
      for (MergeTable::Option& option : group) {
        if (option.second.count(pair.first)) {
          option.second.erase(pair.first);
          option.second.insert(pair.second);
        }
      }
    }
  }

  for (MergeTable::MergeGroup& group : table.groups) {
    TransformDistributiveArgs(gate, distr_type, &group);
  }
  assert(!gate->args().empty());
  return true;
}

void Preprocessor::GroupDistributiveArgs(const MergeTable::Collection& options,
                                         MergeTable* table) noexcept {
  assert(!options.empty());
  MergeTable::MergeGroup all_options(options.begin(), options.end());
  // Sorting in descending size of common arguments.
  std::sort(all_options.begin(), all_options.end(),
            [](const MergeTable::Option& lhs, const MergeTable::Option& rhs) {
            return lhs.first.size() < rhs.first.size();
            });

  // Isolated options in subset to superset relationship.
  typedef std::vector<MergeTable::Option*> OptionGroup;

  /// @todo The current logic misses opportunities
  ///       that may branch with the same base option.
  while (!all_options.empty()) {
    OptionGroup best_group;
    for (int i = 0; i < all_options.size(); ++i) {
      OptionGroup group = {&all_options[i]};
      int j = i;
      for (++j; j < all_options.size(); ++j) {
        MergeTable::Option* candidate = &all_options[j];
        bool superset = std::includes(candidate->first.begin(),
                                      candidate->first.end(),
                                      group.back()->first.begin(),
                                      group.back()->first.end());
        if (!superset) continue;  // Does not include all the arguments.
        bool parents = std::includes(group.back()->second.begin(),
                                     group.back()->second.end(),
                                     candidate->second.begin(),
                                     candidate->second.end());
        if (!parents) continue;  // Parents do not match.
        group.push_back(candidate);
      }
      if (group.size() > best_group.size()) {  // The more members, the merrier.
        best_group = group;
      } else if (group.size() == best_group.size()) {  // Optimistic choice.
        if (group.front()->second.size() < best_group.front()->second.size())
          best_group = group;  // The fewer parents, the more room for others.
      }
    }
    MergeTable::MergeGroup merge_group;  // The group to go into the table.
    for (const auto& member : best_group) {
      merge_group.push_back(*member);
      member->second.clear();  // To remove the best group from the all options.
    }
    table->groups.push_back(merge_group);

    const MergeTable::CommonParents& gates = merge_group.front().second;
    for (MergeTable::Option& option : all_options) {
      MergeTable::CommonParents& parents = option.second;
      for (const IGatePtr& gate : gates) parents.erase(gate);
    }
    all_options.erase(std::remove_if(all_options.begin(), all_options.end(),
                                     [](const MergeTable::Option& option) {
                                     return option.second.size() < 2;
                                     }),
                      all_options.end());
  }
}

void Preprocessor::TransformDistributiveArgs(
    const IGatePtr& gate,
    const Operator& distr_type,
    MergeTable::MergeGroup* group) noexcept {
  if (group->empty()) return;
  const MergeTable::Option& base_option = group->front();
  const MergeTable::CommonArgs& args = base_option.first;
  const MergeTable::CommonParents& gates = base_option.second;

  IGatePtr new_parent;
  if (gate->args().size() == gates.size()) {
    new_parent = gate;  // Reuse the gate to avoid extra merging operations.
    switch (gate->type()) {
      case kAndGate:
      case kOrGate:
        gate->type(distr_type);
        break;
      case kNandGate:
        gate->type(kNorGate);
        break;
      case kNorGate:
        gate->type(kNandGate);
        break;
    }
  } else {
    new_parent = IGatePtr(new IGate(distr_type));
    new_parent->mark(true);
    gate->AddArg(new_parent->index(), new_parent);
  }

  IGatePtr sub_parent(new IGate(distr_type == kAndGate ? kOrGate : kAndGate));
  sub_parent->mark(true);
  new_parent->AddArg(sub_parent->index(), sub_parent);

  IGatePtr rep = *gates.begin();  // Representative of common parents.
  // Getting the common part of the distributive equation.
  for (int index : args) {  // May be negative.
    if (rep->gate_args().count(index)) {
      IGatePtr common = rep->gate_args().find(index)->second;
      new_parent->AddArg(index, common);
    } else {
      VariablePtr common = rep->variable_args().find(index)->second;
      new_parent->AddArg(index, common);
    }
  }

  // Removing the common part from the sub-equations.
  for (const IGatePtr& member : gates) {
    assert(member->parents().size() == 1);
    gate->EraseArg(member->index());

    sub_parent->AddArg(member->index(), member);
    for (int index : args) {
      member->EraseArg(index);
    }
    if (member->args().size() == 1) {
      member->type(kNullGate);
      null_gates_.push_back(member);
    } else if (member->args().empty()) {
      if (member->type() == kAndGate) {
        member->MakeUnity();
      } else {
        assert(member->type() == kOrGate);
        member->Nullify();
      }
      const_gates_.push_back(member);
    }
  }
  // Cleaning the arguments from the group.
  MergeTable::MergeGroup::iterator it = group->begin();
  for (++it; it != group->end(); ++it) {
    MergeTable::Option& super = *it;
    MergeTable::CommonArgs& super_args = super.first;
    for (int index : args) {
      super_args.erase(std::lower_bound(super_args.begin(), super_args.end(),
                                        index));
    }
  }
  group->erase(group->begin());
  Preprocessor::TransformDistributiveArgs(sub_parent, distr_type, group);
}

void Preprocessor::BooleanOptimization() noexcept {
  assert(const_gates_.empty());
  assert(null_gates_.empty());
  graph_->ClearNodeVisits();
  graph_->ClearGateMarks();

  std::vector<IGateWeakPtr> common_gates;
  std::vector<std::weak_ptr<Variable> > common_variables;
  Preprocessor::GatherCommonNodes(&common_gates, &common_variables);

  std::vector<IGateWeakPtr>::iterator it;
  for (it = common_gates.begin(); it != common_gates.end(); ++it) {
    Preprocessor::ProcessCommonNode(*it);
  }

  std::vector<std::weak_ptr<Variable> >::iterator it_b;
  for (it_b = common_variables.begin(); it_b != common_variables.end();
       ++it_b) {
    Preprocessor::ProcessCommonNode(*it_b);
  }
}

void Preprocessor::GatherCommonNodes(
      std::vector<IGateWeakPtr>* common_gates,
      std::vector<std::weak_ptr<Variable> >* common_variables) noexcept {
  std::queue<IGatePtr> gates_queue;
  gates_queue.push(graph_->root());
  while (!gates_queue.empty()) {
    IGatePtr gate = gates_queue.front();
    gates_queue.pop();
    std::unordered_map<int, IGatePtr>::const_iterator it;
    for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
      IGatePtr arg_gate = it->second;
      assert(arg_gate->state() == kNormalState);
      if (arg_gate->Visited()) continue;
      arg_gate->Visit(1);
      gates_queue.push(arg_gate);
      if (arg_gate->parents().size() > 1) common_gates->push_back(arg_gate);
    }

    std::unordered_map<int, VariablePtr>::const_iterator it_b;
    for (it_b = gate->variable_args().begin();
         it_b != gate->variable_args().end(); ++it_b) {
      VariablePtr arg = it_b->second;
      if (arg->Visited()) continue;
      arg->Visit(1);
      if (arg->parents().size() > 1) common_variables->push_back(arg);
    }
  }
}

template<class N>
void Preprocessor::ProcessCommonNode(
    const std::weak_ptr<N>& common_node) noexcept {
  assert(const_gates_.empty());
  assert(null_gates_.empty());
  if (common_node.expired()) return;  // The node has been deleted.

  std::shared_ptr<N> node = common_node.lock();

  if (node->parents().size() == 1) return;  // The parent is deleted.

  IGatePtr root = graph_->root();
  graph_->ClearOptiValues();

  assert(node->opti_value() == 0);
  node->opti_value(1);
  int mult_tot = node->parents().size();  // Total multiplicity.
  assert(mult_tot > 1);
  mult_tot += Preprocessor::PropagateFailure(node);
  // The results of the failure propagation.
  std::map<int, IGateWeakPtr> destinations;
  int num_dest = 0;  // This is not the same as the size of destinations.
  if (root->opti_value() == 1) {  // The root gate failed.
    destinations.insert(std::make_pair(root->index(), root));
    num_dest = 1;
  } else {
    assert(root->opti_value() == 0);
    num_dest = Preprocessor::CollectFailureDestinations(root, node->index(),
                                                        &destinations);
  }

  if (num_dest == 0) return;  // No failure destination detected.
  assert(!destinations.empty());
  if (num_dest < mult_tot) {  // Redundancy detection.
    Preprocessor::ProcessRedundantParents(node, &destinations);
    Preprocessor::ProcessFailureDestinations(node, destinations);
    Preprocessor::ClearConstGates();
    Preprocessor::ClearNullGates();
  }
}

int Preprocessor::PropagateFailure(const NodePtr& node) noexcept {
  assert(node->opti_value() == 1);
  int mult_tot = 0;
  std::unordered_map<int, IGateWeakPtr>::const_iterator it;
  for (it = node->parents().begin(); it != node->parents().end(); ++it) {
    assert(!it->second.expired());
    IGatePtr parent = it->second.lock();
    if (parent->opti_value() == 1) continue;
    parent->ArgFailed();  // Send a notification.
    if (parent->opti_value() == 1) {  // The parent failed.
      int mult = parent->parents().size();  // Multiplicity of the parent.
      if (mult > 1) mult_tot += mult;  // Total multiplicity.
      mult_tot += Preprocessor::PropagateFailure(parent);
    }
  }
  return mult_tot;
}

int Preprocessor::CollectFailureDestinations(
    const IGatePtr& gate,
    int index,
    std::map<int, IGateWeakPtr>* destinations) noexcept {
  assert(gate->opti_value() == 0);
  if (gate->args().count(index)) {  // Argument may be non-gate.
    gate->opti_value(3);
  } else {
    gate->opti_value(2);
  }
  int num_dest = 0;
  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = gate->gate_args().begin(); it != gate->gate_args().end(); ++it) {
    IGatePtr arg = it->second;
    if (arg->opti_value() == 0) {
      num_dest +=
          Preprocessor::CollectFailureDestinations(arg, index, destinations);
    } else if (arg->opti_value() == 1 && arg->index() != index) {
      ++num_dest;
      destinations->insert(std::make_pair(arg->index(), arg));
    }  // Ignore gates with optimization values of 2 or 3.
  }
  return num_dest;
}

void Preprocessor::ProcessRedundantParents(
    const NodePtr& node,
    std::map<int, IGateWeakPtr>* destinations) noexcept {
  std::vector<IGateWeakPtr> redundant_parents;
  for (const std::pair<int, IGateWeakPtr>& member : node->parents()) {
    assert(!member.second.expired());
    IGatePtr parent = member.second.lock();
    if (parent->opti_value() < 3) {
      // Special cases for the redundant parent and the destination parent.
      switch (parent->type()) {
        case kOrGate:
          if (destinations->count(parent->index())) {
            destinations->erase(parent->index());
            continue;  // No need to add into the redundancy list.
          }
      }
      redundant_parents.push_back(parent);
    }
  }
  // The node behaves like a constant False for redundant parents.
  for (const IGateWeakPtr& ptr : redundant_parents) {
    if (ptr.expired()) continue;
    IGatePtr parent = ptr.lock();
    Preprocessor::ProcessConstantArg(parent, node->index(), false);
    if (parent->state() != kNormalState) {
      const_gates_.push_back(parent);
    } else if (parent->type() == kNullGate) {
      null_gates_.push_back(parent);
    }
  }
}

template<class N>
void Preprocessor::ProcessFailureDestinations(
    const std::shared_ptr<N>& node,
    const std::map<int, IGateWeakPtr>& destinations) noexcept {
  std::map<int, IGateWeakPtr>::const_iterator it_d;
  for (it_d = destinations.begin(); it_d != destinations.end(); ++it_d) {
    if (it_d->second.expired()) continue;
    IGatePtr target = it_d->second.lock();
    assert(target->type() != kNullGate);
    switch (target->type()) {
      case kOrGate:
        target->AddArg(node->index(), node);
        break;
      case kAndGate:
      case kAtleastGate:
        IGatePtr new_gate(new IGate(kOrGate));
        if (target == graph_->root()) {
          graph_->root(new_gate);
        } else {
          Preprocessor::ReplaceGate(target, new_gate);
        }
        new_gate->AddArg(target->index(), target);
        new_gate->AddArg(node->index(), node);
        break;
    }
  }
}

bool Preprocessor::DecomposeCommonNodes() noexcept {
  assert(const_gates_.empty());
  assert(null_gates_.empty());

  graph_->ClearNodeVisits();
  std::vector<IGateWeakPtr> common_gates;
  std::vector<std::weak_ptr<Variable> > common_variables;
  Preprocessor::GatherCommonNodes(&common_gates, &common_variables);
  graph_->ClearNodeVisits();

  bool changed = false;
  // The processing is done deepest-layer-first.
  // The deepest-first processing avoids generating extra parents
  // for the nodes that are deep in the graph.
  std::vector<IGateWeakPtr>::reverse_iterator it;
  for (it = common_gates.rbegin(); it != common_gates.rend(); ++it) {
    bool ret = Preprocessor::ProcessDecompositionCommonNode(*it);
    if (ret) changed = true;
  }

  // Variables are processed after gates
  // because, if parent gates are removed,
  // there may be no need to process these variables.
  std::vector<std::weak_ptr<Variable> >::reverse_iterator it_b;
  for (it_b = common_variables.rbegin(); it_b != common_variables.rend();
       ++it_b) {
    bool ret = Preprocessor::ProcessDecompositionCommonNode(*it_b);
    if (ret) changed = true;
  }
  return changed;
}

bool Preprocessor::ProcessDecompositionCommonNode(
    const std::weak_ptr<Node>& common_node) noexcept {
  assert(const_gates_.empty());
  assert(null_gates_.empty());
  if (common_node.expired()) return false;  // The node has been deleted.

  NodePtr node = common_node.lock();

  if (node->parents().size() < 2) return false;

  bool possible = false;  // Possibility in particular setups for decomposition.

  // Determine if the decomposition setups are possible.
  std::unordered_map<int, IGateWeakPtr>::const_iterator it;
  for (it = node->parents().begin(); it != node->parents().end(); ++it) {
    assert(!it->second.expired());
    IGatePtr parent = it->second.lock();
    assert(parent->LastVisit() != node->index());
    switch (parent->type()) {
      case kAndGate:
      case kNandGate:
      case kOrGate:
      case kNorGate:
        possible = true;
    }
    if (possible) break;
  }

  if (!possible) return false;

  // Mark parents and ancestors.
  for (it = node->parents().begin(); it != node->parents().end(); ++it) {
    assert(!it->second.expired());
    IGatePtr parent = it->second.lock();
    Preprocessor::MarkDecompositionDestinations(parent, node->index());
  }
  // Find destinations with particular setups.
  // If a parent gets marked upon destination search,
  // the parent is the destination.
  std::vector<IGateWeakPtr> dest;
  for (it = node->parents().begin(); it != node->parents().end(); ++it) {
    assert(!it->second.expired());
    IGatePtr parent = it->second.lock();
    if (parent->LastVisit() == node->index()) {
      switch (parent->type()) {
        case kAndGate:
        case kNandGate:
        case kOrGate:
        case kNorGate:
          dest.push_back(parent);
      }
    } else {
      parent->Visit(node->index());  // Mark for processing by the destination.
    }
  }
  if (dest.empty()) return false;  // No setups are found.

  LOG(DEBUG4) << "Processing decomposition for node " << node->index();
  Preprocessor::ProcessDecompositionDestinations(node, dest);
  LOG(DEBUG4) << "Finished the decomposition for node " << node->index();
  return true;
}

void Preprocessor::MarkDecompositionDestinations(const IGatePtr& parent,
                                                 int index) noexcept {
  std::unordered_map<int, IGateWeakPtr>::const_iterator it;
  for (it = parent->parents().begin(); it != parent->parents().end(); ++it) {
    assert(!it->second.expired());
    IGatePtr ancestor = it->second.lock();
    if (ancestor->LastVisit() == index) continue;
    ancestor->Visit(index);
    if (ancestor->IsModule()) continue;  // Limited with the sub-graph.
    Preprocessor::MarkDecompositionDestinations(ancestor, index);
  }
}

void Preprocessor::ProcessDecompositionDestinations(
    const NodePtr& node,
    const std::vector<IGateWeakPtr>& dest) noexcept {
  std::unordered_map<int, IGatePtr> clones_true;  // True state propagation.
  std::unordered_map<int, IGatePtr> clones_false;  // False state propagation.
  std::vector<IGateWeakPtr>::const_iterator it;
  for (it = dest.begin(); it != dest.end(); ++it) {
    if (it->expired()) continue;  // Removed by constant propagation.
    IGatePtr parent = it->lock();

    // The destination may already be processed
    // in the link of ancestors.
    if (!node->parents().count(parent->index())) continue;

    bool state = false;  // State for the constant propagation.
    switch (parent->type()) {
      case kAndGate:
      case kNandGate:
        state = true;
        break;
      case kOrGate:
      case kNorGate:
        state = false;
        break;
      default:
        assert(false);
    }
    int sign = parent->args().count(node->index()) ? 1 : -1;
    if (sign < 0) state = !state;
    std::unordered_map<int, IGatePtr>& clones =
        state ? clones_true : clones_false;
    LOG(DEBUG5) << "Processing decomposition ancestor G" << parent->index();
    Preprocessor::ProcessDecompositionAncestors(parent, node, state, true,
                                                &clones);
    LOG(DEBUG5) << "Finished Processing ancestor G" << parent->index();
  }
  Preprocessor::ClearConstGates();  // Actual propagation of the constant.
  Preprocessor::ClearNullGates();
}

void Preprocessor::ProcessDecompositionAncestors(
    const IGatePtr& ancestor,
    const NodePtr& node,
    bool state,
    bool destination,
    std::unordered_map<int, IGatePtr>* clones) noexcept {
  if (!destination && node->parents().count(ancestor->index())) {
    LOG(DEBUG5) << "Reached decomposition sub-parent G" << ancestor->index();
    int sign = ancestor->args().count(node->index()) ? 1 : -1;
    Preprocessor::ProcessConstantArg(ancestor, sign * node->index(), state);

    if (ancestor->state() != kNormalState) {
      const_gates_.push_back(ancestor);
      return;
    } else if (ancestor->type() == kNullGate) {
      null_gates_.push_back(ancestor);
    }
  }
  std::vector<std::pair<int, IGatePtr> > to_swap;  // For common gates.
  std::vector<IGatePtr> ancestors;  // For ancestors to work on.
  std::unordered_map<int, IGatePtr>::const_iterator it;
  for (it = ancestor->gate_args().begin(); it != ancestor->gate_args().end();
       ++it) {
    IGatePtr gate = it->second;
    if (gate->LastVisit() != node->index()) continue;
    if (clones->count(gate->index())) {  // Already processed gate.
      IGatePtr copy = clones->find(gate->index())->second;
      to_swap.push_back(std::make_pair(it->first, copy));
    } else if (gate->parents().size() == 1) {
      gate->ClearVisits();  // To avoid revisiting in destination linking.
      ancestors.push_back(gate);  // Unprocessed gate.
    } else {
      assert(gate->parents().size() > 1);
      IGatePtr copy = gate->Clone();
      clones->insert(std::make_pair(gate->index(), copy));
      to_swap.push_back(std::make_pair(it->first, copy));
      ancestors.push_back(copy);  // Process only new clones.
    }
  }
  // Swaping is first
  // because it reduces the number of common nodes
  // for the sub-graph.
  std::vector<std::pair<int, IGatePtr> >::iterator it_s;
  for (it_s = to_swap.begin(); it_s != to_swap.end(); ++it_s) {
    ancestor->EraseArg(it_s->first);
    int sign = it_s->first > 0 ? 1 : -1;
    ancestor->AddArg(sign * it_s->second->index(), it_s->second);
  }
  std::vector<IGatePtr>::iterator it_an;
  for (it_an = ancestors.begin(); it_an != ancestors.end(); ++it_an) {
    Preprocessor::ProcessDecompositionAncestors(*it_an, node, state, false,
                                                clones);
  }
}

void Preprocessor::ReplaceGate(const IGatePtr& gate,
                               const IGatePtr& replacement) noexcept {
  assert(!gate->parents().empty());
  while (!gate->parents().empty()) {
    IGatePtr parent = gate->parents().begin()->second.lock();
    int index = gate->index();
    int sign = 1;  // Guessing the sign.
    if (parent->args().count(-index)) sign = -1;
    parent->EraseArg(sign * index);
    parent->AddArg(sign * replacement->index(), replacement);

    if (parent->state() != kNormalState) {
      const_gates_.push_back(parent);
    } else if (parent->type() == kNullGate) {
      null_gates_.push_back(parent);
    }
  }
}

}  // namespace scram
