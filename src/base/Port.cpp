#include "base/Port.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace project_xs::sim {

Port::Port(std::string name,
           PortDirection direction,
           std::size_t width_bits,
           const std::type_info& type_info,
           std::size_t data_size,
           void* bound_variable)
    : name_(std::move(name)),
      direction_(direction),
      width_bits_(width_bits),
      data_size_(data_size),
      type_index_(type_info),
      bound_variable_(bound_variable),
      visible_storage_(data_size),
      pending_storage_(data_size) {
    if (bound_variable_ == nullptr) {
        throw std::runtime_error("Port requires a non-null bound variable");
    }
    if (width_bits_ == 0) {
        throw std::runtime_error("Port width_bits must be > 0");
    }
    if (data_size_ == 0) {
        throw std::runtime_error("Port data_size must be > 0");
    }
}

void Port::connect(const std::shared_ptr<Port>& output_port) {
    ensure_direction(PortDirection::Input);

    if (!output_port) {
        throw std::runtime_error("cannot connect input port to null output port");
    }
    output_port->ensure_direction(PortDirection::Output);

    if (width_bits_ != output_port->width_bits_ ||
        data_size_ != output_port->data_size_ ||
        type_index_ != output_port->type_index_) {
        throw std::runtime_error(
            "port connect failed between " + name_ + " and " + output_port->name_ +
            ": type/width mismatch");
    }

    source_output_ = output_port;
}

void Port::clear() {
    valid_ = false;
    pending_valid_ = false;
    std::fill(visible_storage_.begin(), visible_storage_.end(), std::byte{0});
    std::fill(pending_storage_.begin(), pending_storage_.end(), std::byte{0});
    clear_bound_variable();
}

void Port::initialize_zero() {
    clear();
}

void Port::ensure_direction(PortDirection expected) const {
    if (direction_ != expected) {
        throw std::runtime_error("port direction mismatch on " + name_);
    }
}

void Port::ensure_type_match(const std::type_info& expected_type,
                             std::size_t expected_size,
                             std::size_t expected_width_bits) const {
    const std::type_index expected_index(expected_type);
    if (type_index_ != expected_index ||
        data_size_ != expected_size ||
        width_bits_ != expected_width_bits) {
        throw std::runtime_error("port type/width mismatch on " + name_);
    }
}

void Port::copy_bound_to_storage(std::vector<std::byte>& storage) const {
    std::memcpy(storage.data(), bound_variable_, data_size_);
}

void Port::copy_storage_to_bound(const std::vector<std::byte>& storage) const {
    std::memcpy(bound_variable_, storage.data(), data_size_);
}

void Port::clear_bound_variable() const {
    std::memset(bound_variable_, 0, data_size_);
}

std::shared_ptr<Port> Port::source_output() const {
    return source_output_.lock();
}

void WirePort::sync_input() {
    ensure_direction(PortDirection::Input);

    const auto source = source_output();
    if (!source || !source->visible_valid()) {
        valid_ = false;
        clear_bound_variable();
        return;
    }

    visible_storage_ = source->visible_storage();
    valid_ = true;
    copy_storage_to_bound(visible_storage_);
}

void WirePort::emit_bound_value() {
    ensure_direction(PortDirection::Output);
    copy_bound_to_storage(visible_storage_);
    valid_ = true;
}

void WirePort::end_cycle() {
}

void WirePort::initialize_zero() {
    clear();
}

void RegPort::sync_input() {
    ensure_direction(PortDirection::Input);

    if (valid_) {
        copy_storage_to_bound(visible_storage_);
    } else {
        clear_bound_variable();
    }

    const auto source = source_output();
    if (!source || !source->visible_valid()) {
        pending_valid_ = false;
        std::fill(pending_storage_.begin(), pending_storage_.end(), std::byte{0});
        return;
    }

    pending_storage_ = source->visible_storage();
    pending_valid_ = true;
}

void RegPort::emit_bound_value() {
    ensure_direction(PortDirection::Output);
    copy_bound_to_storage(pending_storage_);
    pending_valid_ = true;
}

void RegPort::end_cycle() {
    visible_storage_ = pending_storage_;
    valid_ = pending_valid_;
}

void RegPort::initialize_zero() {
    std::fill(visible_storage_.begin(), visible_storage_.end(), std::byte{0});
    std::fill(pending_storage_.begin(), pending_storage_.end(), std::byte{0});
    valid_ = true;
    pending_valid_ = true;
    clear_bound_variable();
}

}  // namespace project_xs::sim
