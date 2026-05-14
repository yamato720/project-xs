#include "base/CycleSimulator.h"
#include "base/Error.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

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
    for (const auto& kernel : kernels_) {
        kernel->set_frequency_hz_from_parent(frequency_hz_);
    }
}

void CycleSimulator::add_kernel(const std::shared_ptr<Kernel>& kernel) {
    if (!kernel) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "CycleSimulator",
                     "cannot add null kernel");
    }
    kernel->on_attached_to_simulator(*this);
    kernel->set_frequency_hz_from_parent(frequency_hz_);
    kernel->set_current_cycle_from_parent(current_cycle_);
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

    if (current_cycle_ == 0) {
        validate_or_throw();
    }

    if (current_cycle_ >= max_cycles_) {
        finished_ = true;
        return true;
    }

    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleStepBegin);
    for (const auto& group : port_groups_) {
        group->sync_inputs();
    }
    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleAfterInputSync);
    run_single(current_cycle_);
    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleAfterRunSingle);
    emit_outputs();
    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleAfterEmitOutputs);

    for (const auto& kernel : kernels_) {
        kernel->run(current_cycle_);
        if (kernel->terminate_requested()) {
            finished_ = true;
        }
    }
    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleAfterKernelRun);

    for (const auto& group : port_groups_) {
        group->end_cycle();
    }
    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleAfterPortGroupEndCycle);
    for (const auto& kernel : kernels_) {
        kernel->end_cycle();
    }
    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleAfterKernelEndCycle);

    ++current_cycle_;
    for (const auto& kernel : kernels_) {
        kernel->set_current_cycle_from_parent(current_cycle_);
    }

    if (current_cycle_ >= max_cycles_) {
        finished_ = true;
    }

    capture_snapshot_if_automatic(SnapshotCaptureStage::CycleStepEnd);
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
    oss << ", state_arrays=" << state_array_registry_.all_arrays_info();
    oss << ", port_groups=" << all_port_groups_info();
    oss << ", kernels=" << all_kernels_info();
    return oss.str();
}

void CycleSimulator::collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const {
    if (port_groups_.empty()) {
        error::append(diagnostics,
                      error::Severity::Error,
                      error::Stage::Validate,
                      error::Kind::ConstraintViolation,
                      "CycleSimulator " + name_,
                      "no PortGroup registered");
    }

    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        for (std::size_t other = index + 1; other < port_groups_.size(); ++other) {
            if (port_groups_[index]->name() == port_groups_[other]->name()) {
                error::append(diagnostics,
                              error::Severity::Error,
                              error::Stage::Validate,
                              error::Kind::DuplicateName,
                              "CycleSimulator " + name_,
                              "duplicate PortGroup name: " + port_groups_[index]->name());
            }
        }
    }

    for (std::size_t index = 0; index < kernels_.size(); ++index) {
        for (std::size_t other = index + 1; other < kernels_.size(); ++other) {
            if (kernels_[index]->name() == kernels_[other]->name()) {
                error::append(diagnostics,
                              error::Severity::Error,
                              error::Stage::Validate,
                              error::Kind::DuplicateName,
                              "CycleSimulator " + name_,
                              "duplicate kernel name: " + kernels_[index]->name());
            }
        }
    }

    for (const auto& group : port_groups_) {
        group->collect_diagnostics(diagnostics);
    }
    for (const auto& kernel : kernels_) {
        kernel->collect_diagnostics(diagnostics);
    }
}

std::vector<error::Diagnostic> CycleSimulator::validate() const {
    std::vector<error::Diagnostic> diagnostics;
    collect_diagnostics(diagnostics);
    return diagnostics;
}

void CycleSimulator::validate_or_throw() const {
    const auto diagnostics = validate();
    error::throw_if_any_error(diagnostics);
}

void CycleSimulator::save_checkpoint(const std::string& path) const {
    project_xs::sim::save_checkpoint(path, snapshot());
}

void CycleSimulator::restore_checkpoint(const std::string& path) {
    restore(load_cycle_simulator_checkpoint(path, snapshot()));
}

void CycleSimulator::set_snapshot_capture_directory(std::string directory) {
    snapshot_capture_root_directory_ = normalize_snapshot_capture_directory(directory);
}

void CycleSimulator::start_snapshot_capture(SnapshotCaptureMode mode,
                                            std::string capture_name) {
    snapshot_capture_contexts_.push_back(SnapshotCaptureContext{
        std::move(capture_name),
        mode,
    });
    if (mode == SnapshotCaptureMode::Automatic) {
        capture_snapshot(SnapshotCaptureStage::AutomaticSegmentBegin);
    }
}

void CycleSimulator::stop_snapshot_capture(std::string capture_name) {
    if (snapshot_capture_contexts_.empty()) {
        if (!capture_name.empty()) {
            error::raise(error::Stage::Runtime,
                         error::Kind::InvalidArgument,
                         "CycleSimulator",
                         "no active snapshot capture named: " + capture_name);
        }
        return;
    }

    auto& context = snapshot_capture_contexts_.back();
    if (context.name != capture_name) {
        error::raise(error::Stage::Runtime,
                     error::Kind::InvalidArgument,
                     "CycleSimulator",
                     "snapshot capture stop name \"" + capture_name +
                         "\" does not match active capture name \"" +
                         context.name + "\"");
    }

    if (context.mode == SnapshotCaptureMode::Automatic) {
        capture_snapshot(SnapshotCaptureStage::AutomaticSegmentEnd);
        finish_snapshot_capture_segment(snapshot_capture_contexts_.back());
    }
    snapshot_capture_contexts_.pop_back();
}

const CycleSimulatorSnapshotRecord& CycleSimulator::capture_snapshot(
    SnapshotCaptureStage stage) {
    CycleSimulatorSnapshotRecord record;
    record.sequence = snapshot_capture_sequence_++;
    record.stage = stage;
    record.snapshot = snapshot();
    snapshot_history_.push_back(std::move(record));
    if (snapshot_capture_active()) {
        store_snapshot_capture_record(snapshot_history_.back());
    }
    return snapshot_history_.back();
}

void CycleSimulator::clear_snapshot_history() {
    snapshot_history_.clear();
    snapshot_capture_sequence_ = 0;
    snapshot_capture_segment_index_ = 0;
    snapshot_capture_contexts_.clear();
}

void CycleSimulator::run_single(std::uint64_t cycle) {
    (void)cycle;
}

void CycleSimulator::reset_extra() {
}

void CycleSimulator::initialize_zero_extra() {
}

void CycleSimulator::emit_outputs() {
    for (const auto& group : port_groups_) {
        group->emit_outputs();
    }
}

void CycleSimulator::capture_snapshot_if_automatic(SnapshotCaptureStage stage) {
    for (const auto& context : snapshot_capture_contexts_) {
        if (context.mode == SnapshotCaptureMode::Automatic) {
            capture_snapshot(stage);
            return;
        }
    }
}

void CycleSimulator::begin_snapshot_capture_segment(SnapshotCaptureContext& context) {
    context.segment_index = snapshot_capture_segment_index_++;
    context.segment_directory = prepare_snapshot_capture_segment_directory(
        snapshot_capture_root_directory_,
        "cycle_simulator",
        name_,
        current_cycle_,
        context.segment_index,
        context.name);
    context.waveform_path = snapshot_capture_waveform_path(context.segment_directory);
    std::ofstream(context.waveform_path, std::ios::out | std::ios::trunc).close();
    context.frame_count = 0;
    context.segment_active = true;
    write_snapshot_capture_manifest(context.segment_directory,
                                    "cycle_simulator",
                                    name_,
                                    context.segment_index,
                                    context.frame_count,
                                    false,
                                    context.name);
}

void CycleSimulator::finish_snapshot_capture_segment(SnapshotCaptureContext& context) {
    if (!context.segment_active) {
        return;
    }
    if (context.frame_count != 0 && !snapshot_history_.empty()) {
        auto& last_record = snapshot_history_[context.last_record_index];
        last_record.checkpoint_path = snapshot_capture_last_checkpoint_path(context.segment_directory);
        last_record.checkpoint_role = "segment_last";
        project_xs::sim::save_checkpoint(last_record.checkpoint_path, last_record.snapshot);
    }
    write_snapshot_capture_manifest(context.segment_directory,
                                    "cycle_simulator",
                                    name_,
                                    context.segment_index,
                                    context.frame_count,
                                    true,
                                    context.name);
    render_snapshot_capture_html(context.segment_directory);
    context.segment_active = false;
}

void CycleSimulator::store_snapshot_capture_record(CycleSimulatorSnapshotRecord& record) {
    const std::size_t record_index = snapshot_history_.size() - 1;
    for (std::size_t index = 0; index < snapshot_capture_contexts_.size(); ++index) {
        auto& context = snapshot_capture_contexts_[index];
        const bool is_top_context = index + 1 == snapshot_capture_contexts_.size();
        if (context.mode != SnapshotCaptureMode::Automatic) {
            continue;
        }
        if (!context.segment_active) {
            begin_snapshot_capture_segment(context);
        }
        append_waveform_jsonl_frame(context.waveform_path, record.sequence, record.stage, *this);
        if (context.frame_count == 0) {
            context.first_record_index = record_index;
            const std::string path = snapshot_capture_first_checkpoint_path(context.segment_directory);
            if (is_top_context) {
                record.checkpoint_path = path;
                record.checkpoint_role = "segment_first";
            }
            project_xs::sim::save_checkpoint(path, record.snapshot);
        }
        context.last_record_index = record_index;
        ++context.frame_count;
    }

    for (std::size_t index = 0; index < snapshot_capture_contexts_.size(); ++index) {
        auto& context = snapshot_capture_contexts_[index];
        const bool is_top_context = index + 1 == snapshot_capture_contexts_.size();
        if (context.mode != SnapshotCaptureMode::Manual) {
            continue;
        }
        const std::string path = prepare_manual_checkpoint_path(
            snapshot_capture_root_directory_,
            "cycle_simulator",
            name_,
            record.sequence,
            context.name);
        if (is_top_context) {
            record.checkpoint_path = path;
            record.checkpoint_role = "manual";
        }
        project_xs::sim::save_checkpoint(path, record.snapshot);
    }
}

}  // namespace project_xs::sim
