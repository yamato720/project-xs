#include "base/KernelComponent.h"
#include "base/Error.h"
#include "base/StateArray.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace project_xs::sim {

KernelComponent::KernelComponent(std::string name,
                                 std::uint64_t latency,
                                 std::uint64_t first_delay_align)
    : name_(std::move(name)),
      latency_(latency),
      first_delay_align_(first_delay_align),
      align_remaining_(first_delay_align) {
    port_groups_.push_back(std::make_unique<PortGroup>(name_ + "_ports"));
}

PortGroup& KernelComponent::create_port_group(std::string name) {
    port_groups_.push_back(std::make_unique<PortGroup>(std::move(name)));
    return *port_groups_.back();
}

PortGroup* KernelComponent::find_port_group(std::string_view name) {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

const PortGroup* KernelComponent::find_port_group(std::string_view name) const {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

PortGroup& KernelComponent::get_port_group(std::string_view name) {
    PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "KernelComponent",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

const PortGroup& KernelComponent::get_port_group(std::string_view name) const {
    const PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "KernelComponent",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

std::string KernelComponent::port_group_info(std::string_view name, PortValueBase base) const {
    return get_port_group(name).info(base);
}

std::string KernelComponent::all_port_groups_info(PortValueBase base) const {
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

void KernelComponent::reset() {
    align_remaining_ = first_delay_align_;
    phase_ = 0;
    current_cycle_ = 0;
    for (const auto& group : port_groups_) {
        group->clear();
    }
    reset_extra();
}

void KernelComponent::initialize_zero() {
    for (const auto& group : port_groups_) {
        group->initialize_zero();
    }
    initialize_zero_extra();
}

void KernelComponent::run(std::uint64_t cycle) {
    current_cycle_ = cycle;
    capture_snapshot_if_automatic(SnapshotCaptureStage::ComponentRunBegin);
    for (const auto& group : port_groups_) {
        group->sync_inputs();
    }
    capture_snapshot_if_automatic(SnapshotCaptureStage::ComponentAfterInputSync);
    run_single(cycle);
    capture_snapshot_if_automatic(SnapshotCaptureStage::ComponentAfterRunSingle);
    emit_outputs();
    capture_snapshot_if_automatic(SnapshotCaptureStage::ComponentAfterEmitOutputs);
    write_debug(std::cout, cycle);
    after_outputs_emitted(cycle);
    advance_phase();
    capture_snapshot_if_automatic(SnapshotCaptureStage::ComponentRunEnd);
}

void KernelComponent::end_cycle() {
    for (const auto& group : port_groups_) {
        group->end_cycle();
    }
    end_cycle_extra();
    capture_snapshot_if_automatic(SnapshotCaptureStage::ComponentEndCycleEnd);
}

std::string KernelComponent::info() const {
    std::string text = name_ + " {latency=" + std::to_string(latency_) +
                       ", first_delay_align=" + std::to_string(first_delay_align_) +
                       ", phase=" + std::to_string(phase_) +
                       ", phase_valid=" + std::string(phase_valid() ? "yes" : "no") +
                       ", states=" + state_set_.all_states_info() +
                       ", state_arrays=" + state_array_registry_.all_arrays_info() +
                       ", port_groups=" + all_port_groups_info() + "}";
    return text;
}

void KernelComponent::collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const {
    if (port_groups_.empty()) {
        error::append(diagnostics,
                      error::Severity::Error,
                      error::Stage::Validate,
                      error::Kind::ConstraintViolation,
                      "KernelComponent " + name_,
                      "no PortGroup registered");
    }

    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        for (std::size_t other = index + 1; other < port_groups_.size(); ++other) {
            if (port_groups_[index]->name() == port_groups_[other]->name()) {
                error::append(diagnostics,
                              error::Severity::Error,
                              error::Stage::Validate,
                              error::Kind::DuplicateName,
                              "KernelComponent " + name_,
                              "duplicate PortGroup name: " + port_groups_[index]->name());
            }
        }
    }

    for (const auto& requirement : required_array_shapes_) {
        if (!requirement.array) {
            error::append(diagnostics,
                          error::Severity::Error,
                          error::Stage::Validate,
                          error::Kind::ConstraintViolation,
                          "KernelComponent " + name_,
                          "required array shape rule points to null array");
            continue;
        }
        if (requirement.array->shape() != requirement.expected_shape) {
            error::append(diagnostics,
                          error::Severity::Error,
                          error::Stage::Validate,
                          error::Kind::ConstraintViolation,
                          "KernelComponent " + name_,
                          "array shape mismatch on " + requirement.label);
        }
    }

    for (const auto& group : port_groups_) {
        group->collect_diagnostics(diagnostics);
    }
}

std::vector<error::Diagnostic> KernelComponent::validate() const {
    std::vector<error::Diagnostic> diagnostics;
    collect_diagnostics(diagnostics);
    return diagnostics;
}

void KernelComponent::validate_or_throw() const {
    const auto diagnostics = validate();
    error::throw_if_any_error(diagnostics);
}

void KernelComponent::save_checkpoint(const std::string& path) const {
    project_xs::sim::save_checkpoint(path, snapshot());
}

void KernelComponent::restore_checkpoint(const std::string& path) {
    restore(load_kernel_component_checkpoint(path, snapshot()));
}

void KernelComponent::set_snapshot_capture_directory(std::string directory) {
    snapshot_capture_root_directory_ = std::move(directory);
}

void KernelComponent::start_snapshot_capture(SnapshotCaptureMode mode) {
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

void KernelComponent::stop_snapshot_capture() {
    if (snapshot_capture_active_ &&
        snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        capture_snapshot(SnapshotCaptureStage::AutomaticSegmentEnd);
        finish_snapshot_capture_segment();
    }
    snapshot_capture_active_ = false;
}

const KernelComponentSnapshotRecord& KernelComponent::capture_snapshot(
    SnapshotCaptureStage stage) {
    KernelComponentSnapshotRecord record;
    record.sequence = snapshot_capture_sequence_++;
    record.stage = stage;
    record.snapshot = snapshot();
    snapshot_history_.push_back(std::move(record));
    if (snapshot_capture_active_) {
        store_snapshot_capture_record(snapshot_history_.back());
    }
    return snapshot_history_.back();
}

void KernelComponent::clear_snapshot_history() {
    snapshot_history_.clear();
    snapshot_capture_sequence_ = 0;
    snapshot_capture_segment_index_ = 0;
}

void KernelComponent::run_single(std::uint64_t cycle) {
    (void)cycle;
}

std::string KernelComponent::debug_info(std::uint64_t cycle) const {
    (void)cycle;
    return {};
}

void KernelComponent::after_outputs_emitted(std::uint64_t cycle) {
    (void)cycle;
}

void KernelComponent::reset_extra() {
}

void KernelComponent::initialize_zero_extra() {
}

void KernelComponent::end_cycle_extra() {
}

void KernelComponent::set_frequency_hz_from_parent(long double frequency_hz) {
    frequency_hz_ = frequency_hz;
}

void KernelComponent::set_current_cycle_from_parent(std::uint64_t cycle) {
    current_cycle_ = cycle;
}

void KernelComponent::require_state_array_shape(const StateArrayBase& array,
                                                std::vector<std::size_t> expected_shape,
                                                std::string label) {
    required_array_shapes_.push_back(RequiredArrayShape{
        &array,
        std::move(expected_shape),
        label.empty() ? array.name() : std::move(label),
    });
}

void KernelComponent::write_text(std::ostream& os, const std::string& text) const {
    if (text.empty()) {
        return;
    }

    os << text;
    if (text.back() != '\n') {
        os << "\n";
    }
}

void KernelComponent::write_debug(std::ostream& os, std::uint64_t cycle) const {
    write_text(os, debug_info(cycle));
}

void KernelComponent::emit_outputs() {
    for (const auto& group : port_groups_) {
        group->emit_outputs();
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

void KernelComponent::capture_snapshot_if_automatic(SnapshotCaptureStage stage) {
    if (snapshot_capture_active_ &&
        snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        capture_snapshot(stage);
    }
}

void KernelComponent::begin_snapshot_capture_segment() {
    snapshot_capture_segment_directory_ = prepare_snapshot_capture_segment_directory(
        snapshot_capture_root_directory_,
        "kernel_component",
        name_,
        snapshot_capture_sequence_,
        snapshot_capture_segment_index_++);
    snapshot_capture_waveform_path_ =
        snapshot_capture_waveform_path(snapshot_capture_segment_directory_);
    std::ofstream(snapshot_capture_waveform_path_, std::ios::out | std::ios::trunc).close();
    snapshot_capture_segment_frame_count_ = 0;
    snapshot_capture_segment_active_ = true;
    write_snapshot_capture_manifest(snapshot_capture_segment_directory_,
                                    "kernel_component",
                                    name_,
                                    snapshot_capture_segment_index_ - 1,
                                    snapshot_capture_segment_frame_count_,
                                    false);
}

void KernelComponent::finish_snapshot_capture_segment() {
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
                                    "kernel_component",
                                    name_,
                                    snapshot_capture_segment_index_ - 1,
                                    snapshot_capture_segment_frame_count_,
                                    true);
    render_snapshot_capture_html(snapshot_capture_segment_directory_);
    snapshot_capture_segment_active_ = false;
}

void KernelComponent::store_snapshot_capture_record(KernelComponentSnapshotRecord& record) {
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
        "kernel_component",
        name_,
        record.sequence);
    record.checkpoint_path = path;
    record.checkpoint_role = "manual";
    project_xs::sim::save_checkpoint(path, record.snapshot);
}

}  // namespace project_xs::sim
