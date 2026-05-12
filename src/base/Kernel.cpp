#include "base/Kernel.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace project_xs::sim {

Kernel::Kernel(std::string name, std::uint64_t latency)
    : name_(std::move(name)), latency_(latency) {
    port_groups_.push_back(std::make_unique<PortGroup>(name_ + "_ports"));
}

PortGroup& Kernel::create_port_group(std::string name) {
    port_groups_.push_back(std::make_unique<PortGroup>(std::move(name)));
    return *port_groups_.back();
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
    for (const auto& group : port_groups_) {
        group->clear();
    }
    for (const auto& component : components_) {
        component->reset();
    }
    reset_extra();
}

void Kernel::initialize_zero() {
    for (const auto& group : port_groups_) {
        group->initialize_zero();
    }
    for (const auto& component : components_) {
        component->initialize_zero();
    }
    initialize_zero_extra();
}

void Kernel::run(std::uint64_t cycle) {
    for (const auto& group : port_groups_) {
        group->sync_inputs();
    }
    run_single(cycle);
    emit_outputs();
    after_outputs_emitted(cycle);
    handle_latency_event(cycle);
}

void Kernel::end_cycle() {
    for (const auto& group : port_groups_) {
        group->end_cycle();
    }
    for (const auto& component : components_) {
        component->end_cycle();
    }
    end_cycle_extra();
}

bool Kernel::remove_component(std::string_view name) {
    for (auto it = components_.begin(); it != components_.end(); ++it) {
        if ((*it)->name() == name) {
            components_.erase(it);
            return true;
        }
    }
    return false;
}

void Kernel::run_single(std::uint64_t cycle) {
    for (const auto& component : components_) {
        component->run(cycle);
    }
}

void Kernel::on_latency_reached(std::uint64_t cycle) {
    write_text(std::cout, latency_info(cycle));
}

void Kernel::after_outputs_emitted(std::uint64_t cycle) {
    write_debug(std::cout, cycle);
}

std::string Kernel::debug_info(std::uint64_t cycle) const {
    if (components_.empty()) {
        return "[cycle " + std::to_string(cycle) + "] hello from " + name_;
    }
    return {};
}

std::string Kernel::latency_info(std::uint64_t cycle) const {
    return "[cycle " + std::to_string(cycle) + "] " + name_ + " kernel延时结束";
}

void Kernel::on_attached_to_simulator(CycleSimulator& simulator) {
    (void)simulator;
}

void Kernel::copy_kernel_runtime_from(const Kernel& other) {
    elapsed_cycles_ = other.elapsed_cycles_;
    terminate_requested_ = other.terminate_requested_;
    if (port_groups_.size() != other.port_groups_.size()) {
        throw std::runtime_error("Kernel PortGroup count mismatch on " + name_);
    }
    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        port_groups_[index]->copy_runtime_from(*other.port_groups_[index]);
    }
}

void Kernel::reset_extra() {
}

void Kernel::initialize_zero_extra() {
}

void Kernel::end_cycle_extra() {
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

void Kernel::emit_outputs() {
    for (const auto& group : port_groups_) {
        group->emit_outputs();
    }
}

void Kernel::write_debug(std::ostream& os, std::uint64_t cycle) const {
    write_text(os, debug_info(cycle));
}

void Kernel::write_text(std::ostream& os, const std::string& text) const {
    if (text.empty()) {
        return;
    }

    os << text;
    if (text.back() != '\n') {
        os << "\n";
    }
}

}  // namespace project_xs::sim
