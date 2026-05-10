#include "base/CycleSimulator.h"

#include <limits>
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

CycleSimulator::CycleSimulator(std::int64_t max_cycles) {
    set_max_cycles(max_cycles);
}

void CycleSimulator::set_max_cycles(std::int64_t max_cycles) {
    max_cycles_ = normalize_max_cycles(max_cycles);
}

void CycleSimulator::add_kernel(const std::shared_ptr<Kernel>& kernel) {
    if (!kernel) {
        throw std::runtime_error("cannot add null kernel to CycleSimulator");
    }
    kernels_.push_back(kernel);
}

void CycleSimulator::clear() {
    kernels_.clear();
    current_cycle_ = 0;
    finished_ = false;
}

void CycleSimulator::reset() {
    current_cycle_ = 0;
    finished_ = false;
    for (const auto& kernel : kernels_) {
        kernel->reset();
    }
}

void CycleSimulator::initialize_zero() {
    for (const auto& kernel : kernels_) {
        kernel->initialize_zero();
    }
}

bool CycleSimulator::step() {
    if (finished_) {
        return true;
    }

    if (current_cycle_ >= max_cycles_) {
        finished_ = true;
        return true;
    }

    for (const auto& kernel : kernels_) {
        kernel->run(current_cycle_);
        if (kernel->terminate_requested()) {
            finished_ = true;
        }
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

}  // namespace project_xs::sim
