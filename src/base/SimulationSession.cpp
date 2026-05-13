#include "base/SimulationSession.h"
#include "base/Error.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

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
        error::raise(error::Stage::Runtime,
                     error::Kind::InvalidArgument,
                     "SimulationSession",
                     "frequency_hz must be > 0");
    }
    frequency_hz_ = frequency_hz;
}

void SimulationSession::set_run_time_seconds(long double run_time_seconds) {
    if (run_time_seconds < 0.0L) {
        error::raise(error::Stage::Runtime,
                     error::Kind::InvalidArgument,
                     "SimulationSession",
                     "run_time_seconds must be >= 0");
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
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "SimulationSession",
                     "cannot add null simulator");
    }

    scheduled_simulators_.push_back(ScheduledSimulator{simulator, 0.0L});
    simulator_views_.push_back(simulator);
}

SimulationSession::Snapshot SimulationSession::snapshot() const {
    Snapshot shot;
    shot.frequency_hz = frequency_hz_;
    shot.run_time_seconds = run_time_seconds_;
    shot.current_tick = current_tick_;
    shot.finished = finished_;
    shot.simulators.reserve(scheduled_simulators_.size());

    for (const auto& scheduled : scheduled_simulators_) {
        if (!scheduled.simulator) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::InvalidArgument,
                         "SimulationSession",
                         "cannot snapshot null simulator");
        }

        ScheduledSimulatorSnapshot scheduled_shot;
        scheduled_shot.name = scheduled.simulator->name();
        scheduled_shot.accumulated_hz = scheduled.accumulated_hz;
        scheduled_shot.simulator = scheduled.simulator->snapshot();
        shot.simulators.push_back(std::move(scheduled_shot));
    }

    return shot;
}

void SimulationSession::restore(const Snapshot& snapshot) {
    if (snapshot.frequency_hz <= 0.0L || snapshot.run_time_seconds < 0.0L) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "SimulationSession",
                     "invalid session snapshot timing fields");
    }

    if (scheduled_simulators_.size() != snapshot.simulators.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "SimulationSession",
                     "snapshot simulator count mismatch");
    }

    frequency_hz_ = snapshot.frequency_hz;
    run_time_seconds_ = snapshot.run_time_seconds;
    current_tick_ = snapshot.current_tick;
    finished_ = snapshot.finished;

    for (std::size_t index = 0; index < scheduled_simulators_.size(); ++index) {
        auto& scheduled = scheduled_simulators_[index];
        const auto& scheduled_shot = snapshot.simulators[index];
        if (!scheduled.simulator ||
            scheduled.simulator->name() != scheduled_shot.name ||
            scheduled_shot.simulator.name != scheduled_shot.name) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::LayoutMismatch,
                         "SimulationSession",
                         "snapshot simulator layout mismatch at index " +
                             std::to_string(index));
        }

        scheduled.accumulated_hz = scheduled_shot.accumulated_hz;
        scheduled.simulator->restore(scheduled_shot.simulator);
    }
}

std::shared_ptr<CycleSimulator> SimulationSession::find_simulator(std::string_view name) const {
    for (const auto& simulator : simulator_views_) {
        if (simulator->name() == name) {
            return simulator;
        }
    }
    return nullptr;
}

const std::shared_ptr<CycleSimulator>& SimulationSession::get_simulator(
    std::string_view name) const {
    for (const auto& simulator : simulator_views_) {
        if (simulator->name() == name) {
            return simulator;
        }
    }
    error::raise(error::Stage::Elaboration,
                 error::Kind::NotFound,
                 "SimulationSession",
                 "simulator not found: " + std::string(name));
}

std::string SimulationSession::simulator_info(std::string_view name) const {
    return get_simulator(name)->info();
}

const std::vector<std::shared_ptr<CycleSimulator>>& SimulationSession::simulators() const {
    return simulator_views_;
}

std::string SimulationSession::simulators_info() const {
    if (simulator_views_.empty()) {
        return "(empty)";
    }

    std::string text;
    for (std::size_t index = 0; index < simulator_views_.size(); ++index) {
        if (index != 0) {
            text += " | ";
        }
        text += simulator_views_[index]->info();
    }
    return text;
}

std::string SimulationSession::all_simulators_info() const {
    return simulators_info();
}

void SimulationSession::collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const {
    if (simulator_views_.empty()) {
        error::append(diagnostics,
                      error::Severity::Warning,
                      error::Stage::Validate,
                      error::Kind::ConstraintViolation,
                      "SimulationSession",
                      "no simulator registered");
    }

    for (std::size_t index = 0; index < simulator_views_.size(); ++index) {
        for (std::size_t other = index + 1; other < simulator_views_.size(); ++other) {
            if (simulator_views_[index]->name() == simulator_views_[other]->name()) {
                error::append(diagnostics,
                              error::Severity::Error,
                              error::Stage::Validate,
                              error::Kind::DuplicateName,
                              "SimulationSession",
                              "duplicate simulator name: " + simulator_views_[index]->name());
            }
        }
    }

    for (const auto& simulator : simulator_views_) {
        simulator->collect_diagnostics(diagnostics);
    }
}

std::vector<error::Diagnostic> SimulationSession::validate() const {
    std::vector<error::Diagnostic> diagnostics;
    collect_diagnostics(diagnostics);
    return diagnostics;
}

void SimulationSession::validate_or_throw() const {
    const auto diagnostics = validate();
    error::throw_if_any_error(diagnostics);
}

std::string SimulationSession::start_info() const {
    std::ostringstream oss;
    oss << "start cycle simulation, session=" << static_cast<double>(frequency_hz_)
        << "Hz, run_time=" << static_cast<double>(run_time_seconds_) << "s";

    for (std::size_t index = 0; index < simulator_views_.size(); ++index) {
        oss << ", simulator[" << index << "]={name=" << simulator_views_[index]->name()
            << ", frequency=" << static_cast<double>(simulator_views_[index]->frequency_hz())
            << "Hz, current_cycle=" << simulator_views_[index]->current_cycle()
            << ", finished=" << (simulator_views_[index]->is_finished() ? "yes" : "no") << "}";
    }
    return oss.str();
}

std::string SimulationSession::finish_info() const {
    std::ostringstream oss;
    oss << "simulation finished at time=" << static_cast<double>(current_time_seconds()) << "s";

    for (std::size_t index = 0; index < simulator_views_.size(); ++index) {
        oss << ", simulator[" << index << "]={name=" << simulator_views_[index]->name()
            << ", frequency=" << static_cast<double>(simulator_views_[index]->frequency_hz())
            << "Hz, current_cycle=" << simulator_views_[index]->current_cycle()
            << ", finished=" << (simulator_views_[index]->is_finished() ? "yes" : "no") << "}";
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

    if (current_tick_ == 0) {
        validate_or_throw();
    }

    capture_snapshot_if_automatic(SnapshotCaptureStage::SessionStepBegin);
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
    capture_snapshot_if_automatic(SnapshotCaptureStage::SessionAfterDispatch);

    ++current_tick_;

    if (current_time_seconds() >= run_time_seconds_ || all_simulators_finished()) {
        finished_ = true;
    }

    capture_snapshot_if_automatic(SnapshotCaptureStage::SessionStepEnd);
    return finished_;
}

void SimulationSession::run() {
    while (!step()) {
    }
}

void SimulationSession::save_checkpoint(const std::string& path) const {
    project_xs::sim::save_checkpoint(path, snapshot());
}

void SimulationSession::restore_checkpoint(const std::string& path) {
    restore(load_simulation_session_checkpoint(path, snapshot()));
}

void SimulationSession::set_snapshot_capture_directory(std::string directory) {
    snapshot_capture_root_directory_ = std::move(directory);
}

void SimulationSession::start_snapshot_capture(SnapshotCaptureMode mode) {
    if (snapshot_capture_segment_active_) {
        finish_snapshot_capture_segment();
    }
    snapshot_capture_mode_ = mode;
    snapshot_capture_active_ = true;
    if (snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        begin_snapshot_capture_segment();
        capture_snapshot(SnapshotCaptureStage::AutomaticSegmentBegin);
    }
}

void SimulationSession::stop_snapshot_capture() {
    if (snapshot_capture_active_ &&
        snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        capture_snapshot(SnapshotCaptureStage::AutomaticSegmentEnd);
        finish_snapshot_capture_segment();
    }
    snapshot_capture_active_ = false;
}

const SimulationSessionSnapshotRecord& SimulationSession::capture_snapshot(
    SnapshotCaptureStage stage) {
    SimulationSessionSnapshotRecord record;
    record.sequence = snapshot_capture_sequence_++;
    record.stage = stage;
    record.snapshot = snapshot();
    snapshot_history_.push_back(std::move(record));
    if (snapshot_capture_active_) {
        store_snapshot_capture_record(snapshot_history_.back());
    }
    return snapshot_history_.back();
}

void SimulationSession::clear_snapshot_history() {
    snapshot_history_.clear();
    snapshot_capture_sequence_ = 0;
    snapshot_capture_segment_index_ = 0;
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

void SimulationSession::capture_snapshot_if_automatic(SnapshotCaptureStage stage) {
    if (snapshot_capture_active_ &&
        snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        capture_snapshot(stage);
    }
}

void SimulationSession::begin_snapshot_capture_segment() {
    snapshot_capture_segment_directory_ = prepare_snapshot_capture_segment_directory(
        snapshot_capture_root_directory_,
        "simulation_session",
        "session",
        current_tick_,
        snapshot_capture_segment_index_++);
    snapshot_capture_waveform_path_ =
        snapshot_capture_waveform_path(snapshot_capture_segment_directory_);
    std::ofstream(snapshot_capture_waveform_path_, std::ios::out | std::ios::trunc).close();
    snapshot_capture_segment_frame_count_ = 0;
    snapshot_capture_segment_active_ = true;
    write_snapshot_capture_manifest(snapshot_capture_segment_directory_,
                                    "simulation_session",
                                    "session",
                                    snapshot_capture_segment_index_ - 1,
                                    snapshot_capture_segment_frame_count_,
                                    false);
}

void SimulationSession::finish_snapshot_capture_segment() {
    if (!snapshot_capture_segment_active_) {
        return;
    }
    if (snapshot_capture_segment_frame_count_ != 0 && !snapshot_history_.empty()) {
        auto& last_record = snapshot_history_[snapshot_capture_segment_last_record_index_];
        last_record.checkpoint_path =
            snapshot_capture_last_checkpoint_path(snapshot_capture_segment_directory_);
        last_record.checkpoint_role = "segment_last";
        project_xs::sim::save_checkpoint(last_record.checkpoint_path, last_record.snapshot);
    }
    write_snapshot_capture_manifest(snapshot_capture_segment_directory_,
                                    "simulation_session",
                                    "session",
                                    snapshot_capture_segment_index_ - 1,
                                    snapshot_capture_segment_frame_count_,
                                    true);
    render_snapshot_capture_html(snapshot_capture_segment_directory_);
    snapshot_capture_segment_active_ = false;
}

void SimulationSession::store_snapshot_capture_record(
    SimulationSessionSnapshotRecord& record) {
    if (snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        if (!snapshot_capture_segment_active_) {
            begin_snapshot_capture_segment();
        }
        const std::size_t record_index = snapshot_history_.size() - 1;
        append_waveform_jsonl_frame(snapshot_capture_waveform_path_, record.sequence, record.stage, *this);
        if (snapshot_capture_segment_frame_count_ == 0) {
            snapshot_capture_segment_first_record_index_ = record_index;
            record.checkpoint_path =
                snapshot_capture_first_checkpoint_path(snapshot_capture_segment_directory_);
            record.checkpoint_role = "segment_first";
            project_xs::sim::save_checkpoint(record.checkpoint_path, record.snapshot);
        }
        snapshot_capture_segment_last_record_index_ = record_index;
        ++snapshot_capture_segment_frame_count_;
        return;
    }

    const std::string path = prepare_manual_checkpoint_path(
        snapshot_capture_root_directory_,
        "simulation_session",
        "session",
        record.sequence);
    record.checkpoint_path = path;
    record.checkpoint_role = "manual";
    project_xs::sim::save_checkpoint(path, record.snapshot);
}

}  // namespace project_xs::sim
