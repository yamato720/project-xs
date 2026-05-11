#include "base/PortGroup.h"

#include <stdexcept>
#include <utility>

namespace project_xs::sim {

PortGroup::PortGroup(std::string name) : name_(std::move(name)) {}

void PortGroup::add_input(const std::shared_ptr<Port>& input) {
    if (!input) {
        throw std::runtime_error("cannot add null input port to PortGroup");
    }
    if (input->direction() != PortDirection::Input) {
        throw std::runtime_error("PortGroup input must have Input direction");
    }
    inputs_.push_back(input);
}

void PortGroup::add_output(const std::shared_ptr<Port>& output) {
    if (!output) {
        throw std::runtime_error("cannot add null output port to PortGroup");
    }
    if (output->direction() != PortDirection::Output) {
        throw std::runtime_error("PortGroup output must have Output direction");
    }
    outputs_.push_back(output);
}

const std::shared_ptr<Port>& PortGroup::input_at(std::size_t index) const {
    if (index >= inputs_.size()) {
        throw std::runtime_error("PortGroup input index out of range");
    }
    return inputs_[index];
}

const std::shared_ptr<Port>& PortGroup::output_at(std::size_t index) const {
    if (index >= outputs_.size()) {
        throw std::runtime_error("PortGroup output index out of range");
    }
    return outputs_[index];
}

std::shared_ptr<Port> PortGroup::find_input(std::string_view name) const {
    for (const auto& input : inputs_) {
        if (input->name() == name) {
            return input;
        }
    }
    return nullptr;
}

std::shared_ptr<Port> PortGroup::find_output(std::string_view name) const {
    for (const auto& output : outputs_) {
        if (output->name() == name) {
            return output;
        }
    }
    return nullptr;
}

const std::shared_ptr<Port>& PortGroup::get_input(std::string_view name) const {
    for (const auto& input : inputs_) {
        if (input->name() == name) {
            return input;
        }
    }
    throw std::runtime_error("PortGroup input name not found: " + std::string(name));
}

const std::shared_ptr<Port>& PortGroup::get_output(std::string_view name) const {
    for (const auto& output : outputs_) {
        if (output->name() == name) {
            return output;
        }
    }
    throw std::runtime_error("PortGroup output name not found: " + std::string(name));
}

void PortGroup::sync_inputs() {
    for (const auto& input : inputs_) {
        input->sync_input();
    }
}

void PortGroup::emit_outputs() {
    for (const auto& output : outputs_) {
        output->emit_bound_value();
    }
}

void PortGroup::end_cycle() {
    for (const auto& input : inputs_) {
        input->end_cycle();
    }
    for (const auto& output : outputs_) {
        output->end_cycle();
    }
}

void PortGroup::clear() {
    for (const auto& input : inputs_) {
        input->clear();
    }
    for (const auto& output : outputs_) {
        output->clear();
    }
}

void PortGroup::initialize_zero() {
    for (const auto& input : inputs_) {
        input->initialize_zero();
    }
    for (const auto& output : outputs_) {
        output->initialize_zero();
    }
}

std::string PortGroup::info_inputs(PortValueBase base) const {
    return info_ports(inputs_, base);
}

std::string PortGroup::info_outputs(PortValueBase base) const {
    return info_ports(outputs_, base);
}

void PortGroup::copy_runtime_from(const PortGroup& other) {
    if (inputs_.size() != other.inputs_.size() || outputs_.size() != other.outputs_.size()) {
        throw std::runtime_error("PortGroup runtime copy size mismatch: " + name_);
    }

    for (std::size_t index = 0; index < inputs_.size(); ++index) {
        inputs_[index]->copy_runtime_from(*other.inputs_[index]);
    }
    for (std::size_t index = 0; index < outputs_.size(); ++index) {
        outputs_[index]->copy_runtime_from(*other.outputs_[index]);
    }
}

std::string PortGroup::info_ports(const std::vector<std::shared_ptr<Port>>& ports,
                                  PortValueBase base) {
    if (ports.empty()) {
        return "(empty)";
    }

    std::string text;
    for (std::size_t index = 0; index < ports.size(); ++index) {
        if (index != 0) {
            text += " | ";
        }
        text += ports[index]->info(base);
    }
    return text;
}

}  // namespace project_xs::sim
