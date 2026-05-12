#include "base/KernelComponent.h"
#include "base/Error.h"

#include <stdexcept>
#include <utility>

namespace project_xs::sim {

KernelComponent::KernelComponent(std::string name,
                                 std::uint64_t latency,
                                 std::uint64_t first_delay_align)
    : name_(std::move(name)),
      latency_(latency),
      first_delay_align_(first_delay_align),
      align_remaining_(first_delay_align) {
    port_groups_.push_back(std::make_unique<PortGroup>(name_ + "_ports"));
}

PortGroup& KernelComponent::create_port_group(std::string name) {
    port_groups_.push_back(std::make_unique<PortGroup>(std::move(name)));
    return *port_groups_.back();
}

PortGroup* KernelComponent::find_port_group(std::string_view name) {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

const PortGroup* KernelComponent::find_port_group(std::string_view name) const {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

PortGroup& KernelComponent::get_port_group(std::string_view name) {
    PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "KernelComponent",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

const PortGroup& KernelComponent::get_port_group(std::string_view name) const {
    const PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "KernelComponent",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

std::string KernelComponent::port_group_info(std::string_view name, PortValueBase base) const {
    return get_port_group(name).info(base);
}

std::string KernelComponent::all_port_groups_info(PortValueBase base) const {
    if (port_groups_.empty()) {
        return "(empty)";
    }

    std::string text;
    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        if (index != 0) {
            text += " | ";
        }
        text += port_groups_[index]->info(base);
    }
    return text;
}

void KernelComponent::reset() {
    align_remaining_ = first_delay_align_;
    phase_ = 0;
    for (const auto& group : port_groups_) {
        group->clear();
    }
    reset_extra();
}

void KernelComponent::initialize_zero() {
    for (const auto& group : port_groups_) {
        group->initialize_zero();
    }
    initialize_zero_extra();
}

void KernelComponent::run(std::uint64_t cycle) {
    for (const auto& group : port_groups_) {
        group->sync_inputs();
    }
    run_single(cycle);
    emit_outputs();
    after_outputs_emitted(cycle);
    advance_phase();
}

void KernelComponent::end_cycle() {
    for (const auto& group : port_groups_) {
        group->end_cycle();
    }
    end_cycle_extra();
}

std::string KernelComponent::info() const {
    std::string text = name_ + " {latency=" + std::to_string(latency_) +
                       ", first_delay_align=" + std::to_string(first_delay_align_) +
                       ", phase=" + std::to_string(phase_) +
                       ", phase_valid=" + std::string(phase_valid() ? "yes" : "no") +
                       ", states=" + state_set_.all_states_info() +
                       ", port_groups=" + all_port_groups_info() + "}";
    return text;
}

void KernelComponent::run_single(std::uint64_t cycle) {
    (void)cycle;
}

void KernelComponent::after_outputs_emitted(std::uint64_t cycle) {
    (void)cycle;
}

void KernelComponent::copy_component_runtime_from(const KernelComponent& other) {
    align_remaining_ = other.align_remaining_;
    phase_ = other.phase_;
    state_set_.copy_values_from(other.state_set_);
    if (port_groups_.size() != other.port_groups_.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "KernelComponent",
                     "PortGroup count mismatch on " + name_);
    }
    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        port_groups_[index]->copy_runtime_from(*other.port_groups_[index]);
    }
}

void KernelComponent::reset_extra() {
}

void KernelComponent::initialize_zero_extra() {
}

void KernelComponent::end_cycle_extra() {
}

void KernelComponent::emit_outputs() {
    for (const auto& group : port_groups_) {
        group->emit_outputs();
    }
}

void KernelComponent::advance_phase() {
    if (latency_ == 0) {
        return;
    }

    if (align_remaining_ > 0) {
        --align_remaining_;
        return;
    }

    phase_ = (phase_ + 1) % latency_;
}

}  // namespace project_xs::sim
