#include "base/PortGroup.h"
#include "base/Error.h"

#include <stdexcept>
#include <utility>

namespace project_xs::sim {

PortGroup::PortGroup(std::string name) : name_(std::move(name)) {}

void PortGroup::add_input(const std::shared_ptr<Port>& input) {
    if (!input) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "PortGroup",
                     "cannot add null input port");
    }
    if (input->direction() != PortDirection::Input) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::DirectionMismatch,
                     "PortGroup",
                     "input must have Input direction");
    }
    inputs_.push_back(input);
}

void PortGroup::add_output(const std::shared_ptr<Port>& output) {
    if (!output) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "PortGroup",
                     "cannot add null output port");
    }
    if (output->direction() != PortDirection::Output) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::DirectionMismatch,
                     "PortGroup",
                     "output must have Output direction");
    }
    outputs_.push_back(output);
}

const std::shared_ptr<Port>& PortGroup::input_at(std::size_t index) const {
    if (index >= inputs_.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "PortGroup",
                     "input index out of range");
    }
    return inputs_[index];
}

const std::shared_ptr<Port>& PortGroup::output_at(std::size_t index) const {
    if (index >= outputs_.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "PortGroup",
                     "output index out of range");
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

std::shared_ptr<Port> PortGroup::find_port(std::string_view name) const {
    if (auto input = find_input(name)) {
        return input;
    }
    return find_output(name);
}

const std::shared_ptr<Port>& PortGroup::get_input(std::string_view name) const {
    for (const auto& input : inputs_) {
        if (input->name() == name) {
            return input;
        }
    }
    error::raise(error::Stage::Elaboration,
                 error::Kind::NotFound,
                 "PortGroup",
                 "input name not found: " + std::string(name));
}

const std::shared_ptr<Port>& PortGroup::get_output(std::string_view name) const {
    for (const auto& output : outputs_) {
        if (output->name() == name) {
            return output;
        }
    }
    error::raise(error::Stage::Elaboration,
                 error::Kind::NotFound,
                 "PortGroup",
                 "output name not found: " + std::string(name));
}

const std::shared_ptr<Port>& PortGroup::get_port(std::string_view name) const {
    if (auto input = find_input(name)) {
        for (const auto& port : inputs_) {
            if (port == input) {
                return port;
            }
        }
    }
    if (auto output = find_output(name)) {
        for (const auto& port : outputs_) {
            if (port == output) {
                return port;
            }
        }
    }
    error::raise(error::Stage::Elaboration,
                 error::Kind::NotFound,
                 "PortGroup",
                 "port name not found: " + std::string(name));
}

std::string PortGroup::input_info(std::string_view name, PortValueBase base) const {
    return get_input(name)->info(base);
}

std::string PortGroup::output_info(std::string_view name, PortValueBase base) const {
    return get_output(name)->info(base);
}

std::string PortGroup::port_info(std::string_view name, PortValueBase base) const {
    return get_port(name)->info(base);
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

std::string PortGroup::all_inputs_info(PortValueBase base) const {
    return info_inputs(base);
}

std::string PortGroup::all_outputs_info(PortValueBase base) const {
    return info_outputs(base);
}

std::string PortGroup::all_ports_info(PortValueBase base) const {
    return info(base);
}

std::string PortGroup::info(PortValueBase base) const {
    return name_ + " {inputs: " + info_inputs(base) + "; outputs: " + info_outputs(base) + "}";
}

void PortGroup::copy_runtime_from(const PortGroup& other) {
    if (inputs_.size() != other.inputs_.size() || outputs_.size() != other.outputs_.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::LayoutMismatch,
                     "PortGroup",
                     "runtime copy size mismatch: " + name_);
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
