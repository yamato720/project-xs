#ifndef PROJECT_XS_BASE_STATE_REGISTRY_H
#define PROJECT_XS_BASE_STATE_REGISTRY_H

#include "base/Port.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace project_xs::sim {

struct StateEntry {
    std::string name;
    std::string type_name;
    std::string description;
    const std::type_info* type_info = nullptr;
    std::type_index type_index = typeid(void);
    std::size_t data_size = 0;
    std::size_t width_bits = 0;
    void* value_ptr = nullptr;
};

class StateRegistry {
  public:
    template <typename T>
    void add_state(std::string name,
                   std::string description,
                   T* value_ptr,
                   std::size_t width_bits = sizeof(T) * 8) {
        if (!value_ptr) {
            throw std::runtime_error("cannot register null state pointer: " + name);
        }
        if (find(name)) {
            throw std::runtime_error("duplicate state name: " + name);
        }

        StateEntry entry;
        entry.name = std::move(name);
        entry.type_name = type_name_of<T>();
        entry.description = std::move(description);
        entry.type_info = &typeid(T);
        entry.type_index = typeid(T);
        entry.data_size = sizeof(T);
        entry.width_bits = width_bits;
        entry.value_ptr = static_cast<void*>(value_ptr);
        entries_.push_back(std::move(entry));
    }

    const StateEntry* find(std::string_view name) const {
        for (const auto& entry : entries_) {
            if (entry.name == name) {
                return &entry;
            }
        }
        return nullptr;
    }

    template <typename T>
    T value(std::string_view name) const {
        const StateEntry* entry = find_required(name);
        ensure_type_match<T>(*entry);
        T out{};
        std::memcpy(static_cast<void*>(&out), entry->value_ptr, sizeof(T));
        return out;
    }

    const std::vector<StateEntry>& entries() const { return entries_; }

    std::string info(std::string_view name,
                     PortValueBase base = PortValueBase::Decimal) const {
        const StateEntry* entry = find_required(name);
        return entry_info(*entry, base);
    }

    std::string all_info(PortValueBase base = PortValueBase::Decimal) const {
        if (entries_.empty()) {
            return "(empty)";
        }

        std::string text;
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            if (index != 0) {
                text += " | ";
            }
            text += entry_info(entries_[index], base);
        }
        return text;
    }

    std::shared_ptr<WirePort> make_wire_input_port(std::string_view state_name,
                                                   std::string port_name = "") const {
        return make_port<WirePort>(PortDirection::Input, state_name, std::move(port_name));
    }

    std::shared_ptr<WirePort> make_wire_output_port(std::string_view state_name,
                                                    std::string port_name = "") const {
        return make_port<WirePort>(PortDirection::Output, state_name, std::move(port_name));
    }

    std::shared_ptr<RegPort> make_reg_input_port(std::string_view state_name,
                                                 std::string port_name = "") const {
        return make_port<RegPort>(PortDirection::Input, state_name, std::move(port_name));
    }

    std::shared_ptr<RegPort> make_reg_output_port(std::string_view state_name,
                                                  std::string port_name = "") const {
        return make_port<RegPort>(PortDirection::Output, state_name, std::move(port_name));
    }

  private:
    template <typename PortT>
    std::shared_ptr<PortT> make_port(PortDirection direction,
                                     std::string_view state_name,
                                     std::string port_name) const {
        const StateEntry* entry = find_required(state_name);
        const std::string final_name = port_name.empty() ? std::string(state_name) : port_name;
        return std::make_shared<PortT>(
            final_name,
            direction,
            entry->width_bits,
            *entry->type_info,
            entry->data_size,
            entry->value_ptr);
    }

    const StateEntry* find_required(std::string_view name) const {
        const StateEntry* entry = find(name);
        if (!entry) {
            throw std::runtime_error("state not found: " + std::string(name));
        }
        return entry;
    }

    template <typename T>
    static void ensure_type_match(const StateEntry& entry) {
        if (entry.type_index != typeid(T) ||
            entry.data_size != sizeof(T) ||
            entry.width_bits > sizeof(T) * 8) {
            throw std::runtime_error("state type mismatch on " + entry.name);
        }
    }

    static std::string type_name_of(const std::type_index& type_index) {
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

    template <typename T>
    static std::string type_name_of() {
        return type_name_of(typeid(T));
    }

    static std::string value_string(const StateEntry& entry, PortValueBase base) {
        if (entry.type_index == typeid(bool)) {
            return format_value<bool>(entry, base);
        }
        if (entry.type_index == typeid(std::int8_t)) {
            return format_value<std::int8_t>(entry, base);
        }
        if (entry.type_index == typeid(std::uint8_t)) {
            return format_value<std::uint8_t>(entry, base);
        }
        if (entry.type_index == typeid(std::int16_t)) {
            return format_value<std::int16_t>(entry, base);
        }
        if (entry.type_index == typeid(std::uint16_t)) {
            return format_value<std::uint16_t>(entry, base);
        }
        if (entry.type_index == typeid(std::int32_t)) {
            return format_value<std::int32_t>(entry, base);
        }
        if (entry.type_index == typeid(std::uint32_t)) {
            return format_value<std::uint32_t>(entry, base);
        }
        if (entry.type_index == typeid(std::int64_t)) {
            return format_value<std::int64_t>(entry, base);
        }
        if (entry.type_index == typeid(std::uint64_t)) {
            return format_value<std::uint64_t>(entry, base);
        }
        if (entry.type_index == typeid(float)) {
            return format_value<float>(entry, base);
        }
        if (entry.type_index == typeid(double)) {
            return format_value<double>(entry, base);
        }
        return "<unsupported>";
    }

    template <typename T>
    static std::string format_value(const StateEntry& entry, PortValueBase base) {
        T value{};
        std::memcpy(static_cast<void*>(&value), entry.value_ptr, sizeof(T));

        if (base == PortValueBase::Decimal ||
            typeid(T) == typeid(float) ||
            typeid(T) == typeid(double) ||
            typeid(T) == typeid(bool)) {
            return decimal_string(value);
        }

        return bit_pattern_string(value, entry.width_bits, base);
    }

    template <typename T>
    static std::string decimal_string(T value) {
        std::ostringstream oss;
        if constexpr (std::is_same_v<T, bool>) {
            oss << (value ? "1" : "0");
        } else if constexpr (std::is_same_v<T, std::int8_t> ||
                             std::is_same_v<T, std::uint8_t>) {
            oss << static_cast<int>(value);
        } else {
            oss << value;
        }
        return oss.str();
    }

    template <typename T>
    static std::string bit_pattern_string(T value,
                                          std::size_t width_bits,
                                          PortValueBase base) {
        std::uint64_t bits = 0;
        std::memcpy(static_cast<void*>(&bits), static_cast<const void*>(&value), sizeof(T));
        if (width_bits < 64) {
            bits &= ((1ULL << width_bits) - 1ULL);
        }

        std::ostringstream oss;
        switch (base) {
            case PortValueBase::Binary:
                oss << "0b";
                for (std::size_t bit = width_bits; bit > 0; --bit) {
                    oss << (((bits >> (bit - 1)) & 1ULL) ? '1' : '0');
                }
                return oss.str();
            case PortValueBase::Octal:
                oss << "0o" << std::oct << bits;
                return oss.str();
            case PortValueBase::Hexadecimal:
                oss << "0x" << std::hex << bits;
                return oss.str();
            case PortValueBase::Decimal:
            default:
                return decimal_string(value);
        }
    }

    static std::string entry_info(const StateEntry& entry, PortValueBase base) {
        return entry.name + ": value=" + value_string(entry, base) + ", type=" +
               entry.type_name + ", desc=" + entry.description;
    }

    std::vector<StateEntry> entries_;
};

}  // namespace project_xs::sim

#endif
