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

void KernelComponent::bind_input(const std::shared_ptr<Port>& input) {
    if (input_ && input != input_) {
        throw std::runtime_error("KernelComponent can only bind one input port");
    }
    input_ = input;
}

void KernelComponent::bind_output(const std::shared_ptr<Port>& output) {
    if (output_ && output != output_) {
        throw std::runtime_error("KernelComponent can only bind one output port");
    }
    output_ = output;
}

void KernelComponent::reset() {
    align_remaining_ = first_delay_align_;
    phase_ = 0;
    ports_.clear();
    if (input_) {
        input_->clear();
    }
    if (output_) {
        output_->clear();
    }
}

void KernelComponent::initialize_zero() {
    ports_.initialize_zero();
    if (input_) {
        input_->initialize_zero();
    }
    if (output_) {
        output_->initialize_zero();
    }
}

void KernelComponent::run(std::uint64_t cycle) {
    ports_.sync_inputs();
    if (input_) {
        input_->sync_input();
    }
    run_single(cycle);
    advance_phase();
}

void KernelComponent::end_cycle() {
    ports_.end_cycle();
    if (input_) {
        input_->end_cycle();
    }
    if (output_) {
        output_->end_cycle();
    }
}

void KernelComponent::run_single(std::uint64_t cycle) {
    (void)cycle;
}

void KernelComponent::emit_output() {
    if (output_) {
        output_->emit_bound_value();
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
