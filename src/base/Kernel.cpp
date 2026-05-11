#include "base/Kernel.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace project_xs::sim {

Kernel::Kernel(std::string name, std::uint64_t latency)
    : name_(std::move(name)), latency_(latency), ports_(name_ + "_ports") {}

PortGroup& Kernel::create_port_group(std::string name) {
    owned_extra_port_groups_.push_back(std::make_unique<PortGroup>(std::move(name)));
    PortGroup* group = owned_extra_port_groups_.back().get();
    add_port_group(group);
    return *group;
}

void Kernel::add_port_group(PortGroup* group) {
    if (!group) {
        throw std::runtime_error("cannot add null PortGroup to Kernel");
    }
    extra_port_groups_.push_back(group);
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
    for (PortGroup* group : extra_port_groups_) {
        group->clear();
    }
    for (const auto& component : components_) {
        component->reset();
    }
    reset_extra();
}

void Kernel::initialize_zero() {
    ports_.initialize_zero();
    for (PortGroup* group : extra_port_groups_) {
        group->initialize_zero();
    }
    for (const auto& component : components_) {
        component->initialize_zero();
    }
    initialize_zero_extra();
}

void Kernel::run(std::uint64_t cycle) {
    ports_.sync_inputs();
    for (PortGroup* group : extra_port_groups_) {
        group->sync_inputs();
    }
    run_single(cycle);
    emit_outputs();
    after_outputs_emitted(cycle);
    handle_latency_event(cycle);
}

void Kernel::end_cycle() {
    ports_.end_cycle();
    for (PortGroup* group : extra_port_groups_) {
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
    ports_.copy_runtime_from(other.ports_);
    if (extra_port_groups_.size() != other.extra_port_groups_.size()) {
        throw std::runtime_error("Kernel extra PortGroup count mismatch on " + name_);
    }
    for (std::size_t index = 0; index < extra_port_groups_.size(); ++index) {
        extra_port_groups_[index]->copy_runtime_from(*other.extra_port_groups_[index]);
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
    ports_.emit_outputs();
    for (PortGroup* group : extra_port_groups_) {
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
