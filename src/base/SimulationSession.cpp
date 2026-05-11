#include "base/SimulationSession.h"

#include <sstream>
#include <stdexcept>

namespace project_xs::sim {

namespace {

constexpr long double kSchedulerEpsilon = 1.0e-18L;

}  // namespace

SimulationSession::SimulationSession(long double frequency_hz, long double run_time_seconds) {
    set_frequency_hz(frequency_hz);
    set_run_time_seconds(run_time_seconds);
}

void SimulationSession::set_frequency_hz(long double frequency_hz) {
    if (frequency_hz <= 0.0L) {
        throw std::runtime_error("SimulationSession frequency_hz must be > 0");
    }
    frequency_hz_ = frequency_hz;
}

void SimulationSession::set_run_time_seconds(long double run_time_seconds) {
    if (run_time_seconds < 0.0L) {
        throw std::runtime_error("SimulationSession run_time_seconds must be >= 0");
    }
    run_time_seconds_ = run_time_seconds;
}

long double SimulationSession::current_time_seconds() const {
    return static_cast<long double>(current_tick_) / frequency_hz_;
}

bool SimulationSession::is_finished() const {
    if (finished_ || current_time_seconds() >= run_time_seconds_) {
        return true;
    }

    return all_simulators_finished();
}

void SimulationSession::add_simulator(const std::shared_ptr<CycleSimulator>& simulator) {
    if (!simulator) {
        throw std::runtime_error("cannot add null simulator to SimulationSession");
    }

    scheduled_simulators_.push_back(ScheduledSimulator{simulator, 0.0L});
    simulator_views_.push_back(simulator);
}

const std::vector<std::shared_ptr<CycleSimulator>>& SimulationSession::simulators() const {
    return simulator_views_;
}

std::string SimulationSession::start_info() const {
    std::ostringstream oss;
    oss << "start cycle simulation, session=" << static_cast<double>(frequency_hz_)
        << "Hz, run_time=" << static_cast<double>(run_time_seconds_) << "s";

    for (std::size_t index = 0; index < simulator_views_.size(); ++index) {
        oss << ", simulator[" << index << "]={" << simulator_views_[index]->info() << "}";
    }
    return oss.str();
}

std::string SimulationSession::finish_info() const {
    std::ostringstream oss;
    oss << "simulation finished at time=" << static_cast<double>(current_time_seconds()) << "s";

    for (std::size_t index = 0; index < simulator_views_.size(); ++index) {
        oss << ", simulator[" << index << "]={" << simulator_views_[index]->info() << "}";
    }
    return oss.str();
}

void SimulationSession::clear() {
    scheduled_simulators_.clear();
    simulator_views_.clear();
    current_tick_ = 0;
    finished_ = false;
}

void SimulationSession::reset() {
    current_tick_ = 0;
    finished_ = false;
    for (auto& scheduled : scheduled_simulators_) {
        scheduled.accumulated_hz = 0.0L;
        scheduled.simulator->reset();
    }
}

void SimulationSession::initialize_zero() {
    for (const auto& scheduled : scheduled_simulators_) {
        scheduled.simulator->initialize_zero();
    }
}

bool SimulationSession::step() {
    if (is_finished()) {
        finished_ = true;
        return true;
    }

    for (auto& scheduled : scheduled_simulators_) {
        if (scheduled.simulator->is_finished()) {
            continue;
        }

        scheduled.accumulated_hz += scheduled.simulator->frequency_hz();
        while (scheduled.accumulated_hz + kSchedulerEpsilon >= frequency_hz_) {
            scheduled.accumulated_hz -= frequency_hz_;
            scheduled.simulator->step();
            if (scheduled.simulator->is_finished()) {
                break;
            }
        }
    }

    ++current_tick_;

    if (current_time_seconds() >= run_time_seconds_ || all_simulators_finished()) {
        finished_ = true;
    }

    return finished_;
}

void SimulationSession::run() {
    while (!step()) {
    }
}

bool SimulationSession::all_simulators_finished() const {
    if (scheduled_simulators_.empty()) {
        return false;
    }

    for (const auto& scheduled : scheduled_simulators_) {
        if (!scheduled.simulator->is_finished()) {
            return false;
        }
    }
    return true;
}

}  // namespace project_xs::sim
