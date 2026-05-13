#include "base/Port.h"
#include "base/Error.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace project_xs::sim {

namespace {

constexpr char kDigits[] = "0123456789abcdef";

template <typename T>
T read_storage_as(const std::vector<std::byte>& storage) {
    T value{};
    std::memcpy(static_cast<void*>(&value), storage.data(), sizeof(T));
    return value;
}

std::string format_hex_bytes(const std::vector<std::byte>& storage) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0');
    for (auto it = storage.rbegin(); it != storage.rend(); ++it) {
        oss << std::setw(2) << std::to_integer<unsigned int>(*it);
    }
    return oss.str();
}

template <typename T>
std::string format_scalar_value(T value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

template <>
std::string format_scalar_value<bool>(bool value) {
    return value ? "1" : "0";
}

template <>
std::string format_scalar_value<std::int8_t>(std::int8_t value) {
    return std::to_string(static_cast<int>(value));
}

template <>
std::string format_scalar_value<std::uint8_t>(std::uint8_t value) {
    return std::to_string(static_cast<unsigned int>(value));
}

std::string demangle_type_name(const std::type_index& type_index) {
    if (type_index == typeid(bool)) {
        return "bool";
    }
    if (type_index == typeid(std::int8_t)) {
        return "int8_t";
    }
    if (type_index == typeid(std::uint8_t)) {
        return "uint8_t";
    }
    if (type_index == typeid(std::int16_t)) {
        return "int16_t";
    }
    if (type_index == typeid(std::uint16_t)) {
        return "uint16_t";
    }
    if (type_index == typeid(std::int32_t)) {
        return "int32_t";
    }
    if (type_index == typeid(std::uint32_t)) {
        return "uint32_t";
    }
    if (type_index == typeid(std::int64_t)) {
        return "int64_t";
    }
    if (type_index == typeid(std::uint64_t)) {
        return "uint64_t";
    }
    if (type_index == typeid(float)) {
        return "float";
    }
    if (type_index == typeid(double)) {
        return "double";
    }
    return type_index.name();
}

std::string trim_leading_zero_digits(const std::string& digits) {
    const std::size_t first_non_zero = digits.find_first_not_of('0');
    if (first_non_zero == std::string::npos) {
        return "0";
    }
    return digits.substr(first_non_zero);
}

std::string storage_bits_string(const std::vector<std::byte>& storage, std::size_t width_bits) {
    std::string bits;
    bits.reserve(width_bits);

    for (std::size_t bit_index = width_bits; bit_index > 0; --bit_index) {
        const std::size_t raw_bit_index = bit_index - 1;
        const std::size_t byte_index = raw_bit_index / 8;
        const std::size_t bit_in_byte = raw_bit_index % 8;

        unsigned int byte_value = 0;
        if (byte_index < storage.size()) {
            byte_value = std::to_integer<unsigned int>(storage[byte_index]);
        }
        const bool bit = ((byte_value >> bit_in_byte) & 0x1U) != 0U;
        bits.push_back(bit ? '1' : '0');
    }

    return bits;
}

std::string format_bits_in_base(const std::vector<std::byte>& storage,
                                std::size_t width_bits,
                                std::size_t digit_bits,
                                const char* prefix) {
    const std::string bits = storage_bits_string(storage, width_bits);
    const std::size_t remainder = bits.size() % digit_bits;
    const std::string padded_bits =
        (remainder == 0 ? std::string() : std::string(digit_bits - remainder, '0')) + bits;

    std::string digits;
    digits.reserve(padded_bits.size() / digit_bits);
    for (std::size_t offset = 0; offset < padded_bits.size(); offset += digit_bits) {
        unsigned int digit_value = 0;
        for (std::size_t bit = 0; bit < digit_bits; ++bit) {
            digit_value <<= 1U;
            digit_value |= static_cast<unsigned int>(padded_bits[offset + bit] - '0');
        }
        digits.push_back(kDigits[digit_value]);
    }

    return std::string(prefix) + trim_leading_zero_digits(digits);
}

}  // namespace

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
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "Port",
                     "requires a non-null bound variable");
    }
    if (width_bits_ == 0) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "Port",
                     "width_bits must be > 0");
    }
    if (data_size_ == 0) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::InvalidArgument,
                     "Port",
                     "data_size must be > 0");
    }
}

void Port::connect(const std::shared_ptr<Port>& output_port) {
    ensure_direction(PortDirection::Input);

    if (!output_port) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::ConnectionMismatch,
                     "Port connect",
                     "cannot connect input port to null output port");
    }
    output_port->ensure_direction(PortDirection::Output);

    if (width_bits_ != output_port->width_bits_ ||
        data_size_ != output_port->data_size_ ||
        type_index_ != output_port->type_index_) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::TypeMismatch,
                     "Port connect",
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

std::string Port::value_string(PortValueBase base) const {
    if (!valid_) {
        return "z";
    }

    if (base == PortValueBase::Binary) {
        return format_bits_in_base(visible_storage_, width_bits_, 1, "0b");
    }
    if (base == PortValueBase::Octal) {
        return format_bits_in_base(visible_storage_, width_bits_, 3, "0o");
    }
    if (base == PortValueBase::Hexadecimal) {
        return format_bits_in_base(visible_storage_, width_bits_, 4, "0x");
    }

    if (type_index_ == typeid(bool)) {
        return format_scalar_value(read_storage_as<bool>(visible_storage_));
    }
    if (type_index_ == typeid(std::int8_t)) {
        return format_scalar_value(read_storage_as<std::int8_t>(visible_storage_));
    }
    if (type_index_ == typeid(std::uint8_t)) {
        return format_scalar_value(read_storage_as<std::uint8_t>(visible_storage_));
    }
    if (type_index_ == typeid(std::int16_t)) {
        return format_scalar_value(read_storage_as<std::int16_t>(visible_storage_));
    }
    if (type_index_ == typeid(std::uint16_t)) {
        return format_scalar_value(read_storage_as<std::uint16_t>(visible_storage_));
    }
    if (type_index_ == typeid(std::int32_t)) {
        return format_scalar_value(read_storage_as<std::int32_t>(visible_storage_));
    }
    if (type_index_ == typeid(std::uint32_t)) {
        return format_scalar_value(read_storage_as<std::uint32_t>(visible_storage_));
    }
    if (type_index_ == typeid(std::int64_t)) {
        return format_scalar_value(read_storage_as<std::int64_t>(visible_storage_));
    }
    if (type_index_ == typeid(std::uint64_t)) {
        return format_scalar_value(read_storage_as<std::uint64_t>(visible_storage_));
    }
    if (type_index_ == typeid(float)) {
        return format_scalar_value(read_storage_as<float>(visible_storage_));
    }
    if (type_index_ == typeid(double)) {
        return format_scalar_value(read_storage_as<double>(visible_storage_));
    }

    return format_hex_bytes(visible_storage_);
}

std::string Port::type_string() const {
    return demangle_type_name(type_index_);
}

std::string Port::info(PortValueBase base) const {
    return name_ + ": value=" + value_string(base) + ", type=" + type_string();
}

void Port::ensure_direction(PortDirection expected) const {
    if (direction_ != expected) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::DirectionMismatch,
                     "Port",
                     "direction mismatch on " + name_);
    }
}

void Port::ensure_type_match(const std::type_info& expected_type,
                             std::size_t expected_size,
                             std::size_t expected_width_bits) const {
    const std::type_index expected_index(expected_type);
    if (type_index_ != expected_index ||
        data_size_ != expected_size ||
        width_bits_ != expected_width_bits) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::TypeMismatch,
                     "Port",
                     "type/width mismatch on " + name_);
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
