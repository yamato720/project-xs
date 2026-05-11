#include "base/KernelComponent.h"

#include <stdexcept>
#include <utility>

namespace project_xs::sim {

KernelComponent::KernelComponent(std::string name,
                                 std::uint64_t latency,
                                 std::uint64_t first_delay_align)
    : name_(std::move(name)),
      latency_(latency),
      first_delay_align_(first_delay_align),
      align_remaining_(first_delay_align),
      ports_(name_ + "_ports") {}

void KernelComponent::add_port_group(PortGroup* group) {
    if (!group) {
        throw std::runtime_error("cannot add null PortGroup to KernelComponent");
    }
    extra_port_groups_.push_back(group);
}

void KernelComponent::reset() {
    align_remaining_ = first_delay_align_;
    phase_ = 0;
    ports_.clear();
    for (PortGroup* group : extra_port_groups_) {
        group->clear();
    }
    reset_extra();
}

void KernelComponent::initialize_zero() {
    ports_.initialize_zero();
    for (PortGroup* group : extra_port_groups_) {
        group->initialize_zero();
    }
    initialize_zero_extra();
}

void KernelComponent::run(std::uint64_t cycle) {
    ports_.sync_inputs();
    for (PortGroup* group : extra_port_groups_) {
        group->sync_inputs();
    }
    run_single(cycle);
    emit_outputs();
    after_outputs_emitted(cycle);
    advance_phase();
}

void KernelComponent::end_cycle() {
    ports_.end_cycle();
    for (PortGroup* group : extra_port_groups_) {
        group->end_cycle();
    }
    end_cycle_extra();
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
    ports_.copy_runtime_from(other.ports_);
    if (extra_port_groups_.size() != other.extra_port_groups_.size()) {
        throw std::runtime_error("KernelComponent extra PortGroup count mismatch on " + name_);
    }
    for (std::size_t index = 0; index < extra_port_groups_.size(); ++index) {
        extra_port_groups_[index]->copy_runtime_from(*other.extra_port_groups_[index]);
    }
}

void KernelComponent::reset_extra() {
}

void KernelComponent::initialize_zero_extra() {
}

void KernelComponent::end_cycle_extra() {
}

void KernelComponent::emit_outputs() {
    ports_.emit_outputs();
    for (PortGroup* group : extra_port_groups_) {
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
