#include "base/Kernel.h"
#include "base/Error.h"
#include "base/StateArray.h"

#include <fstream>
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

std::shared_ptr<KernelComponent> Kernel::find_component(std::string_view name) const {
    for (const auto& component : components_) {
        if (component->name() == name) {
            return component;
        }
    }
    return nullptr;
}

const std::shared_ptr<KernelComponent>& Kernel::get_component(std::string_view name) const {
    for (const auto& component : components_) {
        if (component->name() == name) {
            return component;
        }
    }
    error::raise(error::Stage::Elaboration,
                 error::Kind::NotFound,
                 "Kernel",
                 "component not found: " + std::string(name));
}

PortGroup* Kernel::find_port_group(std::string_view name) {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

const PortGroup* Kernel::find_port_group(std::string_view name) const {
    for (const auto& group : port_groups_) {
        if (group->name() == name) {
            return group.get();
        }
    }
    return nullptr;
}

PortGroup& Kernel::get_port_group(std::string_view name) {
    PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "Kernel",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

const PortGroup& Kernel::get_port_group(std::string_view name) const {
    const PortGroup* group = find_port_group(name);
    if (!group) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "Kernel",
                     "PortGroup not found: " + std::string(name));
    }
    return *group;
}

std::string Kernel::component_info(std::string_view name) const {
    return get_component(name)->info();
}

std::string Kernel::all_components_info() const {
    if (components_.empty()) {
        return "(empty)";
    }

    std::string text;
    for (std::size_t index = 0; index < components_.size(); ++index) {
        if (index != 0) {
            text += " | ";
        }
        text += components_[index]->info();
    }
    return text;
}

std::string Kernel::port_group_info(std::string_view name, PortValueBase base) const {
    return get_port_group(name).info(base);
}

std::string Kernel::all_port_groups_info(PortValueBase base) const {
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

void Kernel::add_component(const std::shared_ptr<KernelComponent>& component) {
    if (!component) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "Kernel",
                     "cannot add null component");
    }
    component->set_frequency_hz_from_parent(frequency_hz_);
    component->set_current_cycle_from_parent(current_cycle_);
    components_.push_back(component);
}

void Kernel::reset() {
    terminate_requested_ = false;
    elapsed_cycles_ = 0;
    current_cycle_ = 0;
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
    current_cycle_ = cycle;
    capture_snapshot_if_automatic(SnapshotCaptureStage::KernelRunBegin);
    for (const auto& group : port_groups_) {
        group->sync_inputs();
    }
    capture_snapshot_if_automatic(SnapshotCaptureStage::KernelAfterInputSync);
    run_single(cycle);
    capture_snapshot_if_automatic(SnapshotCaptureStage::KernelAfterRunSingle);
    emit_outputs();
    capture_snapshot_if_automatic(SnapshotCaptureStage::KernelAfterEmitOutputs);
    after_outputs_emitted(cycle);
    handle_latency_event(cycle);
    capture_snapshot_if_automatic(SnapshotCaptureStage::KernelRunEnd);
}

void Kernel::end_cycle() {
    for (const auto& group : port_groups_) {
        group->end_cycle();
    }
    for (const auto& component : components_) {
        component->end_cycle();
    }
    end_cycle_extra();
    capture_snapshot_if_automatic(SnapshotCaptureStage::KernelEndCycleEnd);
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

std::string Kernel::info() const {
    std::string text = name_ + " {latency=" + std::to_string(latency_) +
                       ", elapsed_cycles=" + std::to_string(elapsed_cycles_) +
                       ", terminate_requested=" +
                       std::string(terminate_requested_ ? "yes" : "no") +
                       ", states=" + state_set_.all_states_info() +
                       ", state_arrays=" + state_array_registry_.all_arrays_info() +
                       ", port_groups=" + all_port_groups_info() +
                       ", components=" + all_components_info() + "}";
    return text;
}

void Kernel::collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const {
    if (port_groups_.empty()) {
        error::append(diagnostics,
                      error::Severity::Error,
                      error::Stage::Validate,
                      error::Kind::ConstraintViolation,
                      "Kernel " + name_,
                      "no PortGroup registered");
    }

    for (std::size_t index = 0; index < port_groups_.size(); ++index) {
        for (std::size_t other = index + 1; other < port_groups_.size(); ++other) {
            if (port_groups_[index]->name() == port_groups_[other]->name()) {
                error::append(diagnostics,
                              error::Severity::Error,
                              error::Stage::Validate,
                              error::Kind::DuplicateName,
                              "Kernel " + name_,
                              "duplicate PortGroup name: " + port_groups_[index]->name());
            }
        }
    }

    for (std::size_t index = 0; index < components_.size(); ++index) {
        for (std::size_t other = index + 1; other < components_.size(); ++other) {
            if (components_[index]->name() == components_[other]->name()) {
                error::append(diagnostics,
                              error::Severity::Error,
                              error::Stage::Validate,
                              error::Kind::DuplicateName,
                              "Kernel " + name_,
                              "duplicate component name: " + components_[index]->name());
            }
        }
    }

    for (const auto& component_name : required_component_names_) {
        if (!find_component(component_name)) {
            error::append(diagnostics,
                          error::Severity::Error,
                          error::Stage::Validate,
                          error::Kind::NotFound,
                          "Kernel " + name_,
                          "required component not found: " + component_name);
        }
    }

    for (const auto& group_name : required_port_group_names_) {
        if (!find_port_group(group_name)) {
            error::append(diagnostics,
                          error::Severity::Error,
                          error::Stage::Validate,
                          error::Kind::NotFound,
                          "Kernel " + name_,
                          "required PortGroup not found: " + group_name);
        }
    }

    for (const auto& requirement : required_array_shapes_) {
        if (!requirement.array) {
            error::append(diagnostics,
                          error::Severity::Error,
                          error::Stage::Validate,
                          error::Kind::ConstraintViolation,
                          "Kernel " + name_,
                          "required array shape rule points to null array");
            continue;
        }
        if (requirement.array->shape() != requirement.expected_shape) {
            error::append(diagnostics,
                          error::Severity::Error,
                          error::Stage::Validate,
                          error::Kind::ConstraintViolation,
                          "Kernel " + name_,
                          "array shape mismatch on " + requirement.label);
        }
    }

    for (const auto& group : port_groups_) {
        group->collect_diagnostics(diagnostics);
    }
    for (const auto& component : components_) {
        component->collect_diagnostics(diagnostics);
    }
}

std::vector<error::Diagnostic> Kernel::validate() const {
    std::vector<error::Diagnostic> diagnostics;
    collect_diagnostics(diagnostics);
    return diagnostics;
}

void Kernel::validate_or_throw() const {
    const auto diagnostics = validate();
    error::throw_if_any_error(diagnostics);
}

void Kernel::save_checkpoint(const std::string& path) const {
    project_xs::sim::save_checkpoint(path, snapshot());
}

void Kernel::restore_checkpoint(const std::string& path) {
    restore(load_kernel_checkpoint(path, snapshot()));
}

void Kernel::set_snapshot_capture_directory(std::string directory) {
    snapshot_capture_root_directory_ = normalize_snapshot_capture_directory(directory);
}

void Kernel::start_snapshot_capture(SnapshotCaptureMode mode) {
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

void Kernel::stop_snapshot_capture() {
    if (snapshot_capture_active_ &&
        snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        capture_snapshot(SnapshotCaptureStage::AutomaticSegmentEnd);
        finish_snapshot_capture_segment();
    }
    snapshot_capture_active_ = false;
}

const KernelSnapshotRecord& Kernel::capture_snapshot(SnapshotCaptureStage stage) {
    KernelSnapshotRecord record;
    record.sequence = snapshot_capture_sequence_++;
    record.stage = stage;
    record.snapshot = snapshot();
    snapshot_history_.push_back(std::move(record));
    if (snapshot_capture_active_) {
        store_snapshot_capture_record(snapshot_history_.back());
    }
    return snapshot_history_.back();
}

void Kernel::clear_snapshot_history() {
    snapshot_history_.clear();
    snapshot_capture_sequence_ = 0;
    snapshot_capture_segment_index_ = 0;
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

void Kernel::set_frequency_hz_from_parent(long double frequency_hz) {
    frequency_hz_ = frequency_hz;
    for (const auto& component : components_) {
        component->set_frequency_hz_from_parent(frequency_hz_);
    }
}

void Kernel::set_current_cycle_from_parent(std::uint64_t cycle) {
    current_cycle_ = cycle;
    for (const auto& component : components_) {
        component->set_current_cycle_from_parent(cycle);
    }
}

void Kernel::reset_extra() {
}

void Kernel::initialize_zero_extra() {
}

void Kernel::end_cycle_extra() {
}

void Kernel::require_component(std::string name) {
    required_component_names_.push_back(std::move(name));
}

void Kernel::require_port_group(std::string name) {
    required_port_group_names_.push_back(std::move(name));
}

void Kernel::require_state_array_shape(const StateArrayBase& array,
                                       std::vector<std::size_t> expected_shape,
                                       std::string label) {
    required_array_shapes_.push_back(RequiredArrayShape{
        &array,
        std::move(expected_shape),
        label.empty() ? array.name() : std::move(label),
    });
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

void Kernel::capture_snapshot_if_automatic(SnapshotCaptureStage stage) {
    if (snapshot_capture_active_ &&
        snapshot_capture_mode_ == SnapshotCaptureMode::Automatic) {
        capture_snapshot(stage);
    }
}

void Kernel::begin_snapshot_capture_segment() {
    snapshot_capture_segment_directory_ = prepare_snapshot_capture_segment_directory(
        snapshot_capture_root_directory_,
        "kernel",
        name_,
        snapshot_capture_sequence_,
        snapshot_capture_segment_index_++);
    snapshot_capture_waveform_path_ =
        snapshot_capture_waveform_path(snapshot_capture_segment_directory_);
    std::ofstream(snapshot_capture_waveform_path_, std::ios::out | std::ios::trunc).close();
    snapshot_capture_segment_frame_count_ = 0;
    snapshot_capture_segment_active_ = true;
    write_snapshot_capture_manifest(snapshot_capture_segment_directory_,
                                    "kernel",
                                    name_,
                                    snapshot_capture_segment_index_ - 1,
                                    snapshot_capture_segment_frame_count_,
                                    false);
}

void Kernel::finish_snapshot_capture_segment() {
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
                                    "kernel",
                                    name_,
                                    snapshot_capture_segment_index_ - 1,
                                    snapshot_capture_segment_frame_count_,
                                    true);
    render_snapshot_capture_html(snapshot_capture_segment_directory_);
    snapshot_capture_segment_active_ = false;
}

void Kernel::store_snapshot_capture_record(KernelSnapshotRecord& record) {
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
        "kernel",
        name_,
        record.sequence);
    record.checkpoint_path = path;
    record.checkpoint_role = "manual";
    project_xs::sim::save_checkpoint(path, record.snapshot);
}

}  // namespace project_xs::sim
