#include "base/CycleSimulator.h"
#include "base/Error.h"

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

CycleSimulator::CycleSimulator(std::string name,
                               std::int64_t max_cycles,
                               long double frequency_hz)
    : name_(std::move(name)) {
    port_groups_.push_back(std::make_unique<PortGroup>("cycle_simulator_ports"));
    set_max_cycles(max_cycles);
    set_frequency_hz(frequency_hz);
}

CycleSimulator::CycleSimulator(std::int64_t max_cycles, long double frequency_hz)
    : CycleSimulator("cycle_simulator", max_cycles, frequency_hz) {}

PortGroup& CycleSimulator::create_port_group(std::string name) {
    port_groups_.push_back(std::make_unique<PortGroup>(std::move(name)));
    return *port_groups_.back();
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
    state_set_.copy_values_from(other.state_set_);
    if (port_groups_.size() != other.port_groups_.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "CycleSimulator",
                     "PortGroup count mismatch");
    }
    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        port_groups_[index]->copy_runtime_from(*other.port_groups_[index]);
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
    state_set_.copy_values_from(other.state_set_);
    if (port_groups_.size() != 1) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::ConstraintViolation,
                     "CycleSimulator fullcopy",
                     "target simulator must only contain the default PortGroup");
    }
    if (other.port_groups_.size() != 1) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "CycleSimulator fullcopy",
                     "source simulator PortGroup layout mismatch");
    }
    port_groups_[0]->copy_runtime_from(*other.port_groups_[0]);
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
        error::raise(error::Stage::Runtime,
                     error::Kind::InvalidArgument,
                     "CycleSimulator",
                     "frequency_hz must be > 0");
    }
    frequency_hz_ = frequency_hz;
}

void CycleSimulator::add_kernel(const std::shared_ptr<Kernel>& kernel) {
    if (!kernel) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "CycleSimulator",
                     "cannot add null kernel");
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

const std::shared_ptr<Kernel>& CycleSimulator::get_kernel(std::string_view name) const {
    for (const auto& kernel : kernels_) {
        if (kernel->name() == name) {
            return kernel;
        }
    }
    error::raise(error::Stage::Elaboration,
                 error::Kind::NotFound,
                 "CycleSimulator",
                 "kernel not found: " + std::string(name));
}

PortGroup* CycleSimulator::find_port_group(std::string_view name) {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

const PortGroup* CycleSimulator::find_port_group(std::string_view name) const {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

PortGroup& CycleSimulator::get_port_group(std::string_view name) {
    PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "CycleSimulator",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

const PortGroup& CycleSimulator::get_port_group(std::string_view name) const {
    const PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "CycleSimulator",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

std::string CycleSimulator::kernel_info(std::string_view name) const {
    return get_kernel(name)->info();
}

std::string CycleSimulator::all_kernels_info() const {
    if (kernels_.empty()) {
        return "(empty)";
    }

    std::string text;
    for (std::size_t index = 0; index < kernels_.size(); ++index) {
        if (index != 0) {
            text += " | ";
        }
        text += kernels_[index]->info();
    }
    return text;
}

std::string CycleSimulator::port_group_info(std::string_view name, PortValueBase base) const {
    return get_port_group(name).info(base);
}

std::string CycleSimulator::all_port_groups_info(PortValueBase base) const {
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
    for (const auto& group : port_groups_) {
        group->clear();
    }
    current_cycle_ = 0;
    finished_ = false;
}

void CycleSimulator::reset() {
    current_cycle_ = 0;
    finished_ = false;
    for (const auto& group : port_groups_) {
        group->clear();
    }
    for (const auto& kernel : kernels_) {
        kernel->reset();
    }
    reset_extra();
}

void CycleSimulator::initialize_zero() {
    for (const auto& group : port_groups_) {
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

    for (const auto& group : port_groups_) {
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

    for (const auto& group : port_groups_) {
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
    oss << "name=" << name_;
    oss << ", frequency=" << static_cast<double>(frequency_hz_) << "Hz";
    oss << ", max_cycles=";
    if (max_cycles_ == std::numeric_limits<std::uint64_t>::max()) {
        oss << "inf";
    } else {
        oss << max_cycles_;
    }
    oss << ", current_cycle=" << current_cycle_;
    oss << ", finished=" << (finished_ ? "yes" : "no");
    oss << ", states=" << state_set_.all_states_info();
    oss << ", port_groups=" << all_port_groups_info();
    oss << ", kernels=" << all_kernels_info();
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
    for (const auto& group : port_groups_) {
        group->emit_outputs();
    }
}

}  // namespace project_xs::sim
