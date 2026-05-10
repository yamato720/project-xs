#include "base/Kernel.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace project_xs::sim {

Kernel::Kernel(std::string name, std::uint64_t latency)
    : name_(std::move(name)), latency_(latency), ports_(name_ + "_ports") {}

void Kernel::bind_input(const std::shared_ptr<Port>& input) {
    if (input_ && input != input_) {
        throw std::runtime_error("Kernel can only bind one input port");
    }
    input_ = input;
}

void Kernel::bind_output(const std::shared_ptr<Port>& output) {
    if (output_ && output != output_) {
        throw std::runtime_error("Kernel can only bind one output port");
    }
    output_ = output;
}

void Kernel::add_component(const std::shared_ptr<KernelComponent>& component) {
    if (!component) {
        throw std::runtime_error("cannot add null component to Kernel");
    }
    components_.push_back(component);
}

void Kernel::reset() {
    terminate_requested_ = false;
    elapsed_cycles_ = 0;
    ports_.clear();
    if (input_) {
        input_->clear();
    }
    if (output_) {
        output_->clear();
    }
    for (const auto& component : components_) {
        component->reset();
    }
}

void Kernel::initialize_zero() {
    ports_.initialize_zero();
    if (input_) {
        input_->initialize_zero();
    }
    if (output_) {
        output_->initialize_zero();
    }
    for (const auto& component : components_) {
        component->initialize_zero();
    }
}

void Kernel::run(std::uint64_t cycle) {
    ports_.sync_inputs();
    if (input_) {
        input_->sync_input();
    }
    run_single(cycle);
    handle_latency_event(cycle);
}

void Kernel::end_cycle() {
    ports_.end_cycle();
    if (input_) {
        input_->end_cycle();
    }
    if (output_) {
        output_->end_cycle();
    }
    for (const auto& component : components_) {
        component->end_cycle();
    }
}

void Kernel::run_single(std::uint64_t cycle) {
    if (components_.empty()) {
        std::cout << "[cycle " << cycle << "] hello from " << name_ << "\n";
        return;
    }

    for (const auto& component : components_) {
        component->run(cycle);
    }
}

void Kernel::on_latency_reached(std::uint64_t cycle) {
    std::cout << "[cycle " << cycle << "] " << name_ << " kernel延时结束\n";
}

void Kernel::emit_output() {
    if (output_) {
        output_->emit_bound_value();
    }
}

void Kernel::handle_latency_event(std::uint64_t cycle) {
    if (latency_ == 0) {
        return;
    }

    ++elapsed_cycles_;
    if (elapsed_cycles_ >= latency_) {
        on_latency_reached(cycle);
        elapsed_cycles_ = 0;
    }
}

}  // namespace project_xs::sim
