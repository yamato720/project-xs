#ifndef PROJECT_XS_BASE_STATE_H
#define PROJECT_XS_BASE_STATE_H

#include "base/Error.h"
#include "base/Port.h"
#include "base/RuntimeTrace.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace project_xs::sim {

class StateSet;

namespace state_detail {

// 把 type_index 转成尽量可读的类型名字符串。
inline std::string type_name_of(const std::type_index& type_index) {
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
// 把模板类型转成可读类型名字符串。
inline std::string type_name_of() {
    return type_name_of(typeid(T));
}

template <typename T>
// 把标量值转成十进制字符串。
inline std::string decimal_string(T value) {
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
// 按指定进制和位宽把标量值转成位模式字符串。
inline std::string bit_pattern_string(T value,
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

}  // namespace state_detail

// 单个状态对象的共同抽象底座。
// 这层既支持 StateSet 统一持有，也保留外部对象注册的兼容能力。
class StateBase {
  public:
    // 构造一个单状态元信息对象。
    StateBase(std::string name,
              std::string type_name,
              std::string description,
              const std::type_info& type_info,
              std::size_t data_size,
              std::size_t width_bits)
        : name_(std::move(name)),
          type_name_(std::move(type_name)),
          description_(std::move(description)),
          type_info_(&type_info),
          type_index_(type_info),
          data_size_(data_size),
          width_bits_(width_bits) {}

    // 虚析构，保证通过基类指针释放派生状态对象时安全。
    virtual ~StateBase() = default;

    // 返回状态名称。
    const std::string& name() const { return name_; }

    // 返回状态类型名字符串。
    const std::string& type_name() const { return type_name_; }

    // 返回状态描述文本。
    const std::string& description() const { return description_; }

    // 返回底层真实类型信息。
    const std::type_info& type_info() const { return *type_info_; }

    // 返回底层真实类型索引。
    const std::type_index& type_index() const { return type_index_; }

    // 返回底层值占用字节数。
    std::size_t data_size() const { return data_size_; }

    // 返回状态位宽。
    std::size_t width_bits() const { return width_bits_; }

    // 返回底层值的可写地址。
    virtual void* value_ptr() = 0;

    // 返回底层值的只读地址。
    virtual const void* value_ptr() const = 0;

    // 按指定进制返回值字符串。
    virtual std::string value_string(PortValueBase base) const = 0;

    // 直接基于当前单状态创建一个 wire 输入端口。
    std::shared_ptr<WirePort> make_wire_input_port(std::string port_name = "") const {
        return make_port<WirePort>(PortDirection::Input, std::move(port_name));
    }

    // 直接基于当前单状态创建一个 wire 输出端口。
    std::shared_ptr<WirePort> make_wire_output_port(std::string port_name = "") const {
        return make_port<WirePort>(PortDirection::Output, std::move(port_name));
    }

    // 直接基于当前单状态创建一个 reg 输入端口。
    std::shared_ptr<RegPort> make_reg_input_port(std::string port_name = "") const {
        return make_port<RegPort>(PortDirection::Input, std::move(port_name));
    }

    // 直接基于当前单状态创建一个 reg 输出端口。
    std::shared_ptr<RegPort> make_reg_output_port(std::string port_name = "") const {
        return make_port<RegPort>(PortDirection::Output, std::move(port_name));
    }

    // 返回单状态摘要信息。
    std::string info(PortValueBase base = PortValueBase::Decimal) const {
        return name_ + ": value=" + value_string(base) + ", type=" + type_name_ +
               ", desc=" + description_;
    }

  protected:
    // 基于当前单状态创建一个具体端口对象。
    template <typename PortT>
    std::shared_ptr<PortT> make_port(PortDirection direction, std::string port_name) const {
        const std::string final_name = port_name.empty() ? name_ : port_name;
        return std::shared_ptr<PortT>(new PortT(
            final_name,
            direction,
            width_bits_,
            *type_info_,
            data_size_,
            const_cast<void*>(value_ptr())));
    }

  private:
    // 状态名称。
    std::string name_;

    // 状态类型名字符串。
    std::string type_name_;

    // 状态说明文本。
    std::string description_;

    // 指向底层真实类型信息的指针。
    const std::type_info* type_info_ = nullptr;

    // 底层真实类型索引。
    std::type_index type_index_ = typeid(void);

    // 底层值占用字节数。
    std::size_t data_size_ = 0;

    // 当前状态位宽。
    std::size_t width_bits_ = 0;
};

template <typename T>
// 单个状态对象。
// 可以由 StateSet 统一创建并持有，也可以作为兼容路径直接挂在类成员里。
class State final : public StateBase {
  public:
    // 构造一个真正持有值的单状态对象。
    State(std::string name,
          std::string description,
          T initial_value,
          std::size_t width_bits = sizeof(T) * 8)
        : StateBase(std::move(name),
                    state_detail::type_name_of<T>(),
                    std::move(description),
                    typeid(T),
                    sizeof(T),
                    width_bits),
          value_(std::move(initial_value)) {}

    // 构造一个单状态对象，并在构造完成时自动注册到给定 StateSet。
    State(StateSet& state_set,
          std::string name,
          std::string description,
          T initial_value,
          std::size_t width_bits = sizeof(T) * 8);

    // 返回底层值的可写引用。
    T& value() { return value_; }

    // 返回底层值的只读引用。
    const T& value() const { return value_; }

    // 允许 State<T> 在表达式中自动退化成底层值引用。
    operator T&() { return value_; }

    // 允许 const State<T> 在表达式中自动退化成底层只读值引用。
    operator const T&() const { return value_; }

    // 直接用一个普通值给当前状态赋值。
    State& operator=(const T& rhs) {
        value_ = rhs;
        return *this;
    }

    // 直接用一个右值给当前状态赋值。
    State& operator=(T&& rhs) {
        value_ = std::move(rhs);
        return *this;
    }

    // 用另一个同类型状态对象给当前状态赋值。
    State& operator=(const State& rhs) {
        if (this != &rhs) {
            value_ = rhs.value_;
        }
        return *this;
    }

    // 用另一个同类型状态对象右值给当前状态赋值。
    State& operator=(State&& rhs) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (this != &rhs) {
            value_ = std::move(rhs.value_);
        }
        return *this;
    }

    // 原地加法。
    template <typename U>
    State& operator+=(U&& rhs) {
        value_ += std::forward<U>(rhs);
        return *this;
    }

    // 原地减法。
    template <typename U>
    State& operator-=(U&& rhs) {
        value_ -= std::forward<U>(rhs);
        return *this;
    }

    // 原地乘法。
    template <typename U>
    State& operator*=(U&& rhs) {
        value_ *= std::forward<U>(rhs);
        return *this;
    }

    // 原地除法。
    template <typename U>
    State& operator/=(U&& rhs) {
        value_ /= std::forward<U>(rhs);
        return *this;
    }

    // 原地取模。
    template <typename U>
    State& operator%=(U&& rhs) {
        value_ %= std::forward<U>(rhs);
        return *this;
    }

    // 前置自增。
    State& operator++() {
        ++value_;
        return *this;
    }

    // 后置自增。
    T operator++(int) {
        T old = value_;
        value_++;
        return old;
    }

    // 前置自减。
    State& operator--() {
        --value_;
        return *this;
    }

    // 后置自减。
    T operator--(int) {
        T old = value_;
        value_--;
        return old;
    }

    // 返回底层值的可写地址。
    void* value_ptr() override { return static_cast<void*>(&value_); }

    // 返回底层值的只读地址。
    const void* value_ptr() const override { return static_cast<const void*>(&value_); }

    // 按指定进制返回当前状态值字符串。
    std::string value_string(PortValueBase base) const override {
        if (base == PortValueBase::Decimal ||
            std::is_same_v<T, float> ||
            std::is_same_v<T, double> ||
            std::is_same_v<T, bool>) {
            return state_detail::decimal_string(value_);
        }
        return state_detail::bit_pattern_string(value_, width_bits(), base);
    }

  private:
    // 当前单状态真正持有的值本体。
    T value_;
};

// 单状态目录项抽象。
// 不拥有值本体，只把已经挂在 private 里的状态对象组织成可查询目录。
class StateHandleBase {
  public:
    // 构造一个单状态目录项。
    explicit StateHandleBase(StateBase& state) : state_(&state) {}

    // 虚析构，保证通过基类指针释放派生目录项时安全。
    virtual ~StateHandleBase() = default;

    // 返回被引用状态的基类引用。
    StateBase& state() { return *state_; }

    // 返回被引用状态的只读基类引用。
    const StateBase& state() const { return *state_; }

  private:
    // 被目录项引用的单状态对象。
    StateBase* state_ = nullptr;
};

template <typename T>
// 强类型单状态目录项。
class StateHandle final : public StateHandleBase {
  public:
    // 构造一个强类型单状态目录项。
    explicit StateHandle(State<T>& state) : StateHandleBase(state) {}
};

// 单状态目录。
// 提供按名字查询、摘要输出和运行时复制。
// 新代码优先通过 create_state<T>() 让目录统一创建并持有状态值本体；
// register_state() 保留给旧代码或外部已经拥有生命周期的状态对象。
class StateSet {
  public:
    // 单状态目录运行时快照。
    using Snapshot = StateSetSnapshot;

    // 创建一个由当前目录拥有的单状态对象，并自动注册。
    template <typename T>
    State<T>& create_state(std::string name,
                           std::string description,
                           T initial_value = T{},
                           std::size_t width_bits = sizeof(T) * 8) {
        auto state = std::make_unique<State<T>>(
            std::move(name),
            std::move(description),
            std::move(initial_value),
            width_bits);
        State<T>& ref = *state;
        register_state(ref);
        owned_states_.push_back(std::move(state));
        return ref;
    }

    // 注册一个已经存在的单状态对象。
    template <typename T>
    State<T>& register_state(State<T>& state) {
        if (find(state.name())) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::DuplicateName,
                         "StateSet",
                         "duplicate state name: " + state.name());
        }
        entries_.push_back(std::make_unique<StateHandle<T>>(state));
        return state;
    }

    // 按名字查找一个状态；找不到返回空指针。
    const StateBase* find(std::string_view name) const {
        for (const auto& entry : entries_) {
            if (entry->state().name() == name) {
                return &entry->state();
            }
        }
        return nullptr;
    }

    // 按名字和类型查找一个状态；找不到或类型不匹配时报错。
    template <typename T>
    const State<T>* find_typed(std::string_view name) const {
        const StateBase* entry = find_required(name);
        ensure_type_match<T>(*entry);
        return static_cast<const State<T>*>(entry);
    }

    // 按名字和类型查找一个可写状态；找不到或类型不匹配时报错。
    template <typename T>
    State<T>* find_typed_mutable(std::string_view name) {
        StateBase* entry = find_required_mutable(name);
        ensure_type_match<T>(*entry);
        return static_cast<State<T>*>(entry);
    }

    // 按名字读取一个状态值副本。
    template <typename T>
    T value(std::string_view name) const {
        return find_typed<T>(name)->value();
    }

    // 按名字读取一个可写状态值引用。
    template <typename T>
    T& mutable_value(std::string_view name) {
        return find_typed_mutable<T>(name)->value();
    }

    // 返回全部目录项。
    const std::vector<std::unique_ptr<StateHandleBase>>& entries() const { return entries_; }

    // 返回指定状态项的信息字符串。
    std::string info(std::string_view name,
                     PortValueBase base = PortValueBase::Decimal) const {
        return find_required(name)->info(base);
    }

    // 返回全部状态项的信息字符串。
    std::string all_info(PortValueBase base = PortValueBase::Decimal) const {
        if (entries_.empty()) {
            return "(empty)";
        }

        std::string text;
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            if (index != 0) {
                text += " | ";
            }
            text += entries_[index]->state().info(base);
        }
        return text;
    }

    // 兼容统一命名：返回指定状态项的信息字符串。
    std::string state_info(std::string_view name,
                           PortValueBase base = PortValueBase::Decimal) const {
        return info(name, base);
    }

    // 兼容统一命名：返回全部状态项的信息字符串。
    std::string all_states_info(PortValueBase base = PortValueBase::Decimal) const {
        return all_info(base);
    }

    // 保存当前目录中全部单状态值。
    Snapshot snapshot() const;

    // 恢复当前目录中全部单状态值。
    void restore(const Snapshot& snapshot);

  private:
    // 按名字查找一个状态；找不到时报错。
    const StateBase* find_required(std::string_view name) const {
        const StateBase* entry = find(name);
        if (!entry) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::NotFound,
                         "StateSet",
                         "state not found: " + std::string(name));
        }
        return entry;
    }

    // 按名字查找一个可写状态；找不到时报错。
    StateBase* find_required_mutable(std::string_view name) {
        for (const auto& entry : entries_) {
            if (entry->state().name() == name) {
                return &entry->state();
            }
        }
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "StateSet",
                     "state not found: " + std::string(name));
    }

    // 检查某个状态项是否与给定模板类型匹配。
    template <typename T>
    static void ensure_type_match(const StateBase& entry) {
        if (entry.type_index() != typeid(T) ||
            entry.data_size() != sizeof(T) ||
            entry.width_bits() > sizeof(T) * 8) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::TypeMismatch,
                         "StateSet",
                         "state type mismatch on " + entry.name());
        }
    }

    // 由当前目录直接拥有的状态对象。
    std::vector<std::unique_ptr<StateBase>> owned_states_;

    // 当前目录内持有的全部单状态句柄。
    std::vector<std::unique_ptr<StateHandleBase>> entries_;
};

template <typename T>
inline State<T>::State(StateSet& state_set,
                       std::string name,
                       std::string description,
                       T initial_value,
                       std::size_t width_bits)
    : State(std::move(name), std::move(description), std::move(initial_value), width_bits) {
    state_set.register_state(*this);
}

}  // namespace project_xs::sim

#endif
