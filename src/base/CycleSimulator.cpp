#include "base/CycleSimulator.h"

#include <limits>
#include <sstream>
#include <stdexcept>

namespace project_xs::sim {

namespace {

std::uint64_t normalize_max_cycles(std::int64_t max_cycles) {
    if (max_cycles < 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(max_cycles);
}

}  // namespace

CycleSimulator::CycleSimulator(std::int64_t max_cycles, long double frequency_hz) {
    set_max_cycles(max_cycles);
    set_frequency_hz(frequency_hz);
}

void CycleSimulator::add_port_group(PortGroup* group) {
    if (!group) {
        throw std::runtime_error("cannot add null PortGroup to CycleSimulator");
    }
    extra_port_groups_.push_back(group);
}

CycleSimulator& CycleSimulator::operator()(std::int64_t max_cycles, long double frequency_hz) {
    set_max_cycles(max_cycles);
    set_frequency_hz(frequency_hz);
    return *this;
}

void CycleSimulator::copy(const CycleSimulator& other) {
    if (this == &other) {
        return;
    }

    current_cycle_ = other.current_cycle_;
    finished_ = other.finished_;
    ports_.copy_runtime_from(other.ports_);
    if (extra_port_groups_.size() != other.extra_port_groups_.size()) {
        throw std::runtime_error("CycleSimulator extra PortGroup count mismatch");
    }
    for (std::size_t index = 0; index < extra_port_groups_.size(); ++index) {
        extra_port_groups_[index]->copy_runtime_from(*other.extra_port_groups_[index]);
    }
    copy_runtime_extra_from(other);
}

void CycleSimulator::fullcopy(const CycleSimulator& other) {
    if (this == &other) {
        return;
    }

    clear();
    set_max_cycles(other.max_cycles_ == std::numeric_limits<std::uint64_t>::max()
                       ? -1
                       : static_cast<std::int64_t>(other.max_cycles_));
    set_frequency_hz(other.frequency_hz_);
    current_cycle_ = other.current_cycle_;
    finished_ = other.finished_;
    ports_.copy_runtime_from(other.ports_);
    if (!extra_port_groups_.empty()) {
        throw std::runtime_error("fullcopy target simulator must not pre-register extra PortGroups");
    }
    copy_runtime_extra_from(other);

    for (const auto& kernel : other.kernels_) {
        auto cloned = kernel->clone();
        cloned->on_attached_to_simulator(*this);
        kernels_.push_back(cloned);
    }
}

void CycleSimulator::set_max_cycles(std::int64_t max_cycles) {
    max_cycles_ = normalize_max_cycles(max_cycles);
}

void CycleSimulator::set_frequency_hz(long double frequency_hz) {
    if (frequency_hz <= 0.0L) {
        throw std::runtime_error("CycleSimulator frequency_hz must be > 0");
    }
    frequency_hz_ = frequency_hz;
}

void CycleSimulator::add_kernel(const std::shared_ptr<Kernel>& kernel) {
    if (!kernel) {
        throw std::runtime_error("cannot add null kernel to CycleSimulator");
    }
    kernel->on_attached_to_simulator(*this);
    kernels_.push_back(kernel);
}

std::shared_ptr<Kernel> CycleSimulator::find_kernel(std::string_view name) const {
    for (const auto& kernel : kernels_) {
        if (kernel->name() == name) {
            return kernel;
        }
    }
    return nullptr;
}

bool CycleSimulator::remove_kernel(std::string_view name) {
    for (auto it = kernels_.begin(); it != kernels_.end(); ++it) {
        if ((*it)->name() == name) {
            kernels_.erase(it);
            return true;
        }
    }
    return false;
}

void CycleSimulator::clear() {
    kernels_.clear();
    ports_.clear();
    for (PortGroup* group : extra_port_groups_) {
        group->clear();
    }
    current_cycle_ = 0;
    finished_ = false;
}

void CycleSimulator::reset() {
    current_cycle_ = 0;
    finished_ = false;
    ports_.clear();
    for (PortGroup* group : extra_port_groups_) {
        group->clear();
    }
    for (const auto& kernel : kernels_) {
        kernel->reset();
    }
    reset_extra();
}

void CycleSimulator::initialize_zero() {
    ports_.initialize_zero();
    for (PortGroup* group : extra_port_groups_) {
        group->initialize_zero();
    }
    for (const auto& kernel : kernels_) {
        kernel->initialize_zero();
    }
    initialize_zero_extra();
}

bool CycleSimulator::step() {
    if (finished_) {
        return true;
    }

    if (current_cycle_ >= max_cycles_) {
        finished_ = true;
        return true;
    }

    ports_.sync_inputs();
    for (PortGroup* group : extra_port_groups_) {
        group->sync_inputs();
    }
    run_single(current_cycle_);
    emit_outputs();

    for (const auto& kernel : kernels_) {
        kernel->run(current_cycle_);
        if (kernel->terminate_requested()) {
            finished_ = true;
        }
    }

    ports_.end_cycle();
    for (PortGroup* group : extra_port_groups_) {
        group->end_cycle();
    }
    for (const auto& kernel : kernels_) {
        kernel->end_cycle();
    }

    ++current_cycle_;

    if (current_cycle_ >= max_cycles_) {
        finished_ = true;
    }

    return finished_;
}

void CycleSimulator::run() {
    while (!step()) {
    }
}

void CycleSimulator::terminate() {
    finished_ = true;
}

std::string CycleSimulator::info() const {
    std::ostringstream oss;
    oss << "frequency=" << static_cast<double>(frequency_hz_) << "Hz";
    oss << ", max_cycles=";
    if (max_cycles_ == std::numeric_limits<std::uint64_t>::max()) {
        oss << "inf";
    } else {
        oss << max_cycles_;
    }
    oss << ", current_cycle=" << current_cycle_;
    oss << ", finished=" << (finished_ ? "yes" : "no");
    return oss.str();
}

void CycleSimulator::run_single(std::uint64_t cycle) {
    (void)cycle;
}

void CycleSimulator::reset_extra() {
}

void CycleSimulator::initialize_zero_extra() {
}

void CycleSimulator::copy_runtime_extra_from(const CycleSimulator& other) {
    (void)other;
}

void CycleSimulator::emit_outputs() {
    ports_.emit_outputs();
    for (PortGroup* group : extra_port_groups_) {
        group->emit_outputs();
    }
}

}  // namespace project_xs::sim
