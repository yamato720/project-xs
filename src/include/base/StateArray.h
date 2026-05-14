#ifndef PROJECT_XS_BASE_STATE_ARRAY_H
#define PROJECT_XS_BASE_STATE_ARRAY_H

#include "base/State.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace project_xs::sim {

namespace state_array_detail {

// 把多维索引展平成线性索引。
inline std::size_t flatten_index(const std::vector<std::size_t>& shape,
                                 const std::vector<std::size_t>& indices) {
    if (shape.size() != indices.size()) {
        error::raise(error::Stage::Elaboration,
                     error::Kind::ConstraintViolation,
                     "StateArray",
                     "index rank mismatch");
    }

    std::size_t flat_index = 0;
    std::size_t stride = 1;
    for (std::size_t reverse = shape.size(); reverse > 0; --reverse) {
        const std::size_t axis = reverse - 1;
        if (indices[axis] >= shape[axis]) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::NotFound,
                         "StateArray",
                         "index out of range");
        }
        flat_index += indices[axis] * stride;
        stride *= shape[axis];
    }
    return flat_index;
}

// 计算数组总元素数。
inline std::size_t element_count(const std::vector<std::size_t>& shape) {
    if (shape.empty()) {
        return 0;
    }

    std::size_t count = 1;
    for (std::size_t extent : shape) {
        if (extent == 0) {
            return 0;
        }
        count *= extent;
    }
    return count;
}

// 把线性索引还原成多维索引。
inline std::vector<std::size_t> unflatten_index(const std::vector<std::size_t>& shape,
                                                std::size_t flat_index) {
    const std::size_t total = element_count(shape);
    if (flat_index >= total) {
        throw std::runtime_error("StateArray flat index out of range");
    }

    std::vector<std::size_t> indices(shape.size(), 0);
    for (std::size_t axis = shape.size(); axis > 0; --axis) {
        const std::size_t current = axis - 1;
        const std::size_t extent = shape[current];
        indices[current] = flat_index % extent;
        flat_index /= extent;
    }
    return indices;
}

// 把 shape 转成可读字符串。
inline std::string shape_string(const std::vector<std::size_t>& shape) {
    if (shape.empty()) {
        return "[]";
    }

    std::string text = "[";
    for (std::size_t index = 0; index < shape.size(); ++index) {
        if (index != 0) {
            text += " x ";
        }
        text += std::to_string(shape[index]);
    }
    text += "]";
    return text;
}

}  // namespace state_array_detail

// 数组状态的共同抽象底座。
// 用于表达“具名数组/高维数组状态块”。
class StateArrayBase {
  public:
    // 单个数组状态运行时快照。
    using Snapshot = StateArraySnapshot;

    // 构造一个数组状态元信息对象。
    StateArrayBase(std::string name,
                   std::string type_name,
                   std::string description,
                   const std::type_info& type_info,
                   std::size_t data_size,
                   std::size_t width_bits,
                   std::vector<std::size_t> shape)
        : name_(std::move(name)),
          type_name_(std::move(type_name)),
          description_(std::move(description)),
          type_info_(&type_info),
          type_index_(type_info),
          data_size_(data_size),
          width_bits_(width_bits),
          shape_(std::move(shape)) {}

    // 虚析构，保证通过基类指针释放派生数组状态对象时安全。
    virtual ~StateArrayBase() = default;

    // 返回数组状态名称。
    const std::string& name() const { return name_; }

    // 返回数组状态类型名字符串。
    const std::string& type_name() const { return type_name_; }

    // 返回数组状态描述文本。
    const std::string& description() const { return description_; }

    // 返回底层真实类型信息。
    const std::type_info& type_info() const { return *type_info_; }

    // 返回底层真实类型索引。
    const std::type_index& type_index() const { return type_index_; }

    // 返回单个元素占用字节数。
    std::size_t data_size() const { return data_size_; }

    // 返回单个元素位宽。
    std::size_t width_bits() const { return width_bits_; }

    // 返回数组形状。
    const std::vector<std::size_t>& shape() const { return shape_; }

    // 返回数组维数。
    std::size_t rank() const { return shape_.size(); }

    // 返回某一维的长度；越界时报错。
    std::size_t extent(std::size_t dim) const {
        if (dim >= shape_.size()) {
            throw std::runtime_error("StateArray extent dim out of range");
        }
        return shape_[dim];
    }

    // 返回总元素数。
    std::size_t element_count() const { return state_array_detail::element_count(shape_); }

    // 把线性索引还原成多维坐标；越界时报错。
    std::vector<std::size_t> flat_to_indices(std::size_t flat_index) const {
        return state_array_detail::unflatten_index(shape_, flat_index);
    }

    // 返回整个数组状态的摘要信息。
    virtual std::string info(PortValueBase base = PortValueBase::Decimal) const = 0;

    // 返回某个线性索引元素的值字符串；用于通用波形采样。
    virtual std::string element_value_string(std::size_t flat_index,
                                             PortValueBase base = PortValueBase::Decimal) const = 0;

    // 返回某个线性索引元素可用于显示的主名和别名。
    virtual std::vector<std::string> element_name_options(std::size_t flat_index) const = 0;

    // 保存整个数组状态值。
    virtual Snapshot snapshot() const = 0;

    // 恢复整个数组状态值。
    virtual void restore(const Snapshot& snapshot) = 0;

  protected:
    // 把多维索引展平成线性索引。
    std::size_t flatten_index(const std::vector<std::size_t>& indices) const {
        return state_array_detail::flatten_index(shape_, indices);
    }

    // 基于元素地址创建具体端口对象。
    template <typename PortT>
    std::shared_ptr<PortT> make_port(PortDirection direction,
                                     void* element_ptr,
                                     std::string port_name) const {
        auto port = std::shared_ptr<PortT>(new PortT(
            std::move(port_name),
            direction,
            width_bits_,
            *type_info_,
            data_size_,
            element_ptr));
        port->set_bound_state_path(name());
        return port;
    }

  private:
    // 数组状态名称。
    std::string name_;

    // 数组状态类型名字符串。
    std::string type_name_;

    // 数组状态说明文本。
    std::string description_;

    // 指向底层真实类型信息的指针。
    const std::type_info* type_info_ = nullptr;

    // 底层真实类型索引。
    std::type_index type_index_ = typeid(void);

    // 单个元素占用字节数。
    std::size_t data_size_ = 0;

    // 单个元素位宽。
    std::size_t width_bits_ = 0;

    // 数组形状。
    std::vector<std::size_t> shape_;
};

template <typename T>
// 具名数组/高维数组状态块。
class StateArray final : public StateArrayBase {
  public:
    // 构造一个高维数组状态块。
    StateArray(std::string name,
               std::string description,
               std::vector<std::size_t> shape,
               T initial_value = T{},
               std::size_t width_bits = sizeof(T) * 8)
        : StateArrayBase(std::move(name),
                         state_detail::type_name_of<T>(),
                         std::move(description),
                         typeid(T),
                         sizeof(T),
                         width_bits,
                         std::move(shape)),
          values_(element_count(), initial_value) {}

    // 返回全部底层元素值的只读视图。
    // 这里故意不暴露可写 vector 引用，
    // 避免外部通过 push_back / resize 破坏固定 shape 约束。
    const std::vector<T>& values() const { return values_; }

    // 将数组内全部元素重置为同一个值，但不改变 shape / 元素名 / 别名。
    void fill(const T& value) {
        for (auto& entry : values_) {
            entry = value;
        }
    }

    // 给某个线性索引元素分配一个名字。
    void set_element_name(std::size_t index, std::string element_name) {
        if (index >= values_.size()) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::NotFound,
                         "StateArray",
                         "element index out of range on " + name());
        }
        if (element_name.empty()) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::InvalidArgument,
                         "StateArray",
                         "element name must not be empty on " + name());
        }

        const std::size_t existing_index = find_named_index(element_name);
        if (existing_index != kNameNotFound && existing_index != index) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::DuplicateName,
                         "StateArray",
                         "duplicate element name: " + element_name);
        }

        if (element_names_.size() != values_.size()) {
            element_names_.resize(values_.size());
        }
        element_names_[index] = std::move(element_name);
    }

    // 给某个多维索引元素分配一个名字。
    void set_element_name(const std::vector<std::size_t>& indices, std::string element_name) {
        set_element_name(flatten_index(indices), std::move(element_name));
    }

    // 给某个线性索引元素追加一个别名。
    void add_element_alias(std::size_t index, std::string element_alias) {
        if (index >= values_.size()) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::NotFound,
                         "StateArray",
                         "element index out of range on " + name());
        }
        if (element_alias.empty()) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::InvalidArgument,
                         "StateArray",
                         "element alias must not be empty on " + name());
        }
        const std::size_t existing_index = find_named_index(element_alias);
        if (existing_index != kNameNotFound && existing_index != index) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::DuplicateName,
                         "StateArray",
                         "duplicate element alias: " + element_alias);
        }

        if (element_aliases_.size() != values_.size()) {
            element_aliases_.resize(values_.size());
        }
        element_aliases_[index].push_back(std::move(element_alias));
    }

    // 给某个多维索引元素追加一个别名。
    void add_element_alias(const std::vector<std::size_t>& indices, std::string element_alias) {
        add_element_alias(flatten_index(indices), std::move(element_alias));
    }

    // 按名字访问一个元素。
    T& at_name(std::string_view element_name) {
        return values_.at(find_required_named_index(element_name));
    }

    // 按名字只读访问一个元素。
    const T& at_name(std::string_view element_name) const {
        return values_.at(find_required_named_index(element_name));
    }

    // 判断某个元素名字或别名是否存在。
    bool has_name(std::string_view element_name) const {
        return find_named_index(element_name) != kNameNotFound;
    }

    // 返回当前数组所有已登记的主名字和别名。
    std::vector<std::string> all_element_names() const {
        std::vector<std::string> names;
        for (const auto& name : element_names_) {
            if (!name.empty()) {
                names.push_back(name);
            }
        }
        for (const auto& aliases : element_aliases_) {
            for (const auto& alias : aliases) {
                if (!alias.empty()) {
                    names.push_back(alias);
                }
            }
        }
        return names;
    }

    // 返回某个线性索引元素的主名字；未命名时返回空串。
    const std::string& element_name(std::size_t index) const {
        static const std::string kEmpty;
        if (index >= element_names_.size()) {
            return kEmpty;
        }
        return element_names_[index];
    }

    // 返回某个线性索引元素的别名列表；没有别名时返回空列表。
    const std::vector<std::string>& element_aliases(std::size_t index) const {
        static const std::vector<std::string> kEmpty;
        if (index >= element_aliases_.size()) {
            return kEmpty;
        }
        return element_aliases_[index];
    }

    // 返回某个具名元素的可读摘要信息。
    std::string element_info(std::string_view element_name,
                             PortValueBase base = PortValueBase::Decimal) const {
        const std::size_t index = find_required_named_index(element_name);
        const auto indices = flat_to_indices(index);

        std::string coords = "[";
        for (std::size_t axis = 0; axis < indices.size(); ++axis) {
            if (axis != 0) {
                coords += ", ";
            }
            coords += std::to_string(indices[axis]);
        }
        coords += "]";

        std::string value_text;
        if (base == PortValueBase::Decimal ||
            std::is_same_v<T, bool> ||
            std::is_same_v<T, float> ||
            std::is_same_v<T, double>) {
            value_text = state_detail::decimal_string(values_[index]);
        } else {
            value_text = state_detail::bit_pattern_string(values_[index], width_bits(), base);
        }

        return std::string(element_name) + ": index=" + std::to_string(index) +
               ", coords=" + coords + ", value=" + value_text;
    }

    // 通过线性索引访问一个元素。
    // 允许改单个元素值，但不允许改变数组总长度或 shape。
    T& at_flat(std::size_t index) { return values_.at(index); }

    // 通过线性索引只读访问一个元素。
    const T& at_flat(std::size_t index) const { return values_.at(index); }

    // 通过多维索引访问一个元素。
    // 允许改单个元素值，但不允许改变数组总长度或 shape。
    T& at(const std::vector<std::size_t>& indices) { return values_.at(flatten_index(indices)); }

    // 通过多维索引只读访问一个元素。
    const T& at(const std::vector<std::size_t>& indices) const {
        return values_.at(flatten_index(indices));
    }

    // 基于线性索引创建一个 wire 输入端口。
    std::shared_ptr<WirePort> make_wire_input_port(std::size_t index,
                                                   std::string port_name) {
        return make_port<WirePort>(PortDirection::Input, &values_.at(index), std::move(port_name));
    }

    // 基于元素名字创建一个 wire 输入端口。
    std::shared_ptr<WirePort> make_wire_input_port(std::string_view element_name,
                                                   std::string port_name = "") {
        const std::size_t index = find_required_named_index(element_name);
        const std::string final_name =
            port_name.empty() ? std::string(element_name) : std::move(port_name);
        return make_port<WirePort>(PortDirection::Input, &values_.at(index), std::move(final_name));
    }

    // 基于线性索引创建一个 wire 输出端口。
    std::shared_ptr<WirePort> make_wire_output_port(std::size_t index,
                                                    std::string port_name) {
        return make_port<WirePort>(PortDirection::Output, &values_.at(index), std::move(port_name));
    }

    // 基于元素名字创建一个 wire 输出端口。
    std::shared_ptr<WirePort> make_wire_output_port(std::string_view element_name,
                                                    std::string port_name = "") {
        const std::size_t index = find_required_named_index(element_name);
        const std::string final_name =
            port_name.empty() ? std::string(element_name) : std::move(port_name);
        return make_port<WirePort>(
            PortDirection::Output,
            &values_.at(index),
            std::move(final_name));
    }

    // 基于线性索引创建一个 reg 输入端口。
    std::shared_ptr<RegPort> make_reg_input_port(std::size_t index,
                                                 std::string port_name) {
        return make_port<RegPort>(PortDirection::Input, &values_.at(index), std::move(port_name));
    }

    // 基于元素名字创建一个 reg 输入端口。
    std::shared_ptr<RegPort> make_reg_input_port(std::string_view element_name,
                                                 std::string port_name = "") {
        const std::size_t index = find_required_named_index(element_name);
        const std::string final_name =
            port_name.empty() ? std::string(element_name) : std::move(port_name);
        return make_port<RegPort>(PortDirection::Input, &values_.at(index), std::move(final_name));
    }

    // 基于线性索引创建一个 reg 输出端口。
    std::shared_ptr<RegPort> make_reg_output_port(std::size_t index,
                                                  std::string port_name) {
        return make_port<RegPort>(PortDirection::Output, &values_.at(index), std::move(port_name));
    }

    // 基于元素名字创建一个 reg 输出端口。
    std::shared_ptr<RegPort> make_reg_output_port(std::string_view element_name,
                                                  std::string port_name = "") {
        const std::size_t index = find_required_named_index(element_name);
        const std::string final_name =
            port_name.empty() ? std::string(element_name) : std::move(port_name);
        return make_port<RegPort>(
            PortDirection::Output,
            &values_.at(index),
            std::move(final_name));
    }

    // 保存整个数组状态值。
    Snapshot snapshot() const override {
        Snapshot shot;
        shot.name = name();
        shot.type_name = type_name();
        shot.type_index = type_index();
        shot.data_size = data_size();
        shot.width_bits = width_bits();
        shot.shape = shape();
        shot.value_storage.resize(values_.size() * sizeof(T));
        if (!shot.value_storage.empty()) {
            std::memcpy(shot.value_storage.data(), values_.data(), shot.value_storage.size());
        }
        return shot;
    }

    // 恢复整个数组状态值。
    void restore(const Snapshot& snapshot) override {
        if (name() != snapshot.name ||
            type_index() != snapshot.type_index ||
            data_size() != snapshot.data_size ||
            width_bits() != snapshot.width_bits ||
            shape() != snapshot.shape ||
            snapshot.value_storage.size() != values_.size() * sizeof(T)) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::LayoutMismatch,
                         "StateArray",
                         "snapshot layout mismatch on " + name());
        }

        if (!snapshot.value_storage.empty()) {
            std::memcpy(values_.data(), snapshot.value_storage.data(), snapshot.value_storage.size());
        }
    }

    // 返回整个数组状态的摘要信息。
    std::string info(PortValueBase base = PortValueBase::Decimal) const override {
        std::string text = name() + ": shape=" +
                           state_array_detail::shape_string(shape()) +
                           ", type=" + type_name() + ", desc=" + description() + ", values=";
        if (values_.empty()) {
            text += "(empty)";
            return text;
        }

        for (std::size_t index = 0; index < values_.size(); ++index) {
            if (index != 0) {
                text += " | ";
            }
            if (base == PortValueBase::Decimal ||
                std::is_same_v<T, bool> ||
                std::is_same_v<T, float> ||
                std::is_same_v<T, double>) {
                text += state_detail::decimal_string(values_[index]);
            } else {
                text += state_detail::bit_pattern_string(values_[index], width_bits(), base);
            }
        }
        return text;
    }

    // 返回某个线性索引元素的值字符串；用于通用波形采样。
    std::string element_value_string(std::size_t flat_index,
                                     PortValueBase base = PortValueBase::Decimal) const override {
        const T& value = values_.at(flat_index);
        if (base == PortValueBase::Decimal ||
            std::is_same_v<T, bool> ||
            std::is_same_v<T, float> ||
            std::is_same_v<T, double>) {
            return state_detail::decimal_string(value);
        }
        return state_detail::bit_pattern_string(value, width_bits(), base);
    }

    // 返回某个线性索引元素可用于显示的主名和别名。
    std::vector<std::string> element_name_options(std::size_t flat_index) const override {
        if (flat_index >= values_.size()) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::NotFound,
                         "StateArray",
                         "element index out of range on " + name());
        }

        std::vector<std::string> options;
        if (flat_index < element_names_.size() && !element_names_[flat_index].empty()) {
            options.push_back(element_names_[flat_index]);
        }
        if (flat_index < element_aliases_.size()) {
            for (const auto& alias : element_aliases_[flat_index]) {
                if (!alias.empty()) {
                    options.push_back(alias);
                }
            }
        }
        options.push_back(std::to_string(flat_index));
        return options;
    }

  private:
    static constexpr std::size_t kNameNotFound = static_cast<std::size_t>(-1);

    std::size_t find_named_index(std::string_view element_name) const {
        for (std::size_t index = 0; index < element_names_.size(); ++index) {
            if (element_names_[index] == element_name) {
                return index;
            }
        }
        for (std::size_t index = 0; index < element_aliases_.size(); ++index) {
            for (const auto& alias : element_aliases_[index]) {
                if (alias == element_name) {
                    return index;
                }
            }
        }
        return kNameNotFound;
    }

    std::size_t find_required_named_index(std::string_view element_name) const {
        const std::size_t index = find_named_index(element_name);
        if (index == kNameNotFound) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::NotFound,
                         "StateArray",
                         "element name not found on " + name() + ": " +
                             std::string(element_name));
        }
        return index;
    }

    // 扁平存放的数组元素值。
    std::vector<T> values_;

    // 与线性索引一一对应的可选元素名字。
    std::vector<std::string> element_names_;

    // 与线性索引一一对应的可选元素别名列表。
    std::vector<std::vector<std::string>> element_aliases_;
};

// 高维数组状态目录。
// 新代码优先通过 create_array<T>() 让目录统一创建并持有数组状态；
// register_array() 保留给旧代码或外部已经拥有生命周期的数组状态。
class StateArrayRegistry {
  public:
    // 数组状态目录运行时快照。
    using Snapshot = StateArrayRegistrySnapshot;

    // 创建一个由当前目录拥有的数组状态对象，并自动注册。
    template <typename T>
    StateArray<T>& create_array(std::string name,
                                std::string description,
                                std::vector<std::size_t> shape,
                                T initial_value = T{},
                                std::size_t width_bits = sizeof(T) * 8) {
        auto array = std::make_unique<StateArray<T>>(
            std::move(name),
            std::move(description),
            std::move(shape),
            std::move(initial_value),
            width_bits);
        StateArray<T>& ref = *array;
        register_array(ref);
        owned_arrays_.push_back(std::move(array));
        return ref;
    }

    // 注册一个已经存在的数组状态对象。
    template <typename T>
    StateArray<T>& register_array(StateArray<T>& array) {
        if (find(array.name())) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::DuplicateName,
                         "StateArrayRegistry",
                         "duplicate state array name: " + array.name());
        }
        entries_.push_back(&array);
        return array;
    }

    // 按名字查找一个数组状态；找不到返回空指针。
    const StateArrayBase* find(std::string_view name) const {
        for (const auto* entry : entries_) {
            if (entry->name() == name) {
                return entry;
            }
        }
        return nullptr;
    }

    // 按名字和类型查找一个数组状态；找不到或类型不匹配时报错。
    template <typename T>
    const StateArray<T>* find_typed(std::string_view name) const {
        const StateArrayBase* entry = find_required(name);
        ensure_type_match<T>(*entry);
        return static_cast<const StateArray<T>*>(entry);
    }

    // 按名字和类型查找一个可写数组状态；找不到或类型不匹配时报错。
    template <typename T>
    StateArray<T>* find_typed_mutable(std::string_view name) {
        StateArrayBase* entry = find_required_mutable(name);
        ensure_type_match<T>(*entry);
        return static_cast<StateArray<T>*>(entry);
    }

    // 返回全部数组状态条目。
    const std::vector<StateArrayBase*>& entries() const { return entries_; }

    // 返回指定数组状态的信息字符串。
    std::string info(std::string_view name,
                     PortValueBase base = PortValueBase::Decimal) const {
        return find_required(name)->info(base);
    }

    // 返回全部数组状态的信息字符串。
    std::string all_info(PortValueBase base = PortValueBase::Decimal) const {
        if (entries_.empty()) {
            return "(empty)";
        }

        std::string text;
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            if (index != 0) {
                text += " | ";
            }
            text += entries_[index]->info(base);
        }
        return text;
    }

    // 兼容统一命名：返回指定数组状态的信息字符串。
    std::string array_info(std::string_view name,
                           PortValueBase base = PortValueBase::Decimal) const {
        return info(name, base);
    }

    // 兼容统一命名：返回全部数组状态的信息字符串。
    std::string all_arrays_info(PortValueBase base = PortValueBase::Decimal) const {
        return all_info(base);
    }

    // 保存当前目录中全部数组状态值。
    Snapshot snapshot() const;

    // 恢复当前目录中全部数组状态值。
    void restore(const Snapshot& snapshot);

  private:
    // 按名字查找一个数组状态；找不到时报错。
    const StateArrayBase* find_required(std::string_view name) const {
        const StateArrayBase* entry = find(name);
        if (!entry) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::NotFound,
                         "StateArrayRegistry",
                         "state array not found: " + std::string(name));
        }
        return entry;
    }

    // 按名字查找一个可写数组状态；找不到时报错。
    StateArrayBase* find_required_mutable(std::string_view name) {
        for (auto* entry : entries_) {
            if (entry->name() == name) {
                return entry;
            }
        }
        error::raise(error::Stage::Elaboration,
                     error::Kind::NotFound,
                     "StateArrayRegistry",
                     "state array not found: " + std::string(name));
    }

    // 检查某个数组状态项是否与给定模板类型匹配。
    template <typename T>
    static void ensure_type_match(const StateArrayBase& entry) {
        if (entry.type_index() != typeid(T) ||
            entry.data_size() != sizeof(T) ||
            entry.width_bits() > sizeof(T) * 8) {
            error::raise(error::Stage::Elaboration,
                         error::Kind::TypeMismatch,
                         "StateArrayRegistry",
                         "state array type mismatch on " + entry.name());
        }
    }

    // 当前目录内注册的全部数组状态对象。
    // 由当前目录直接拥有的数组状态对象。
    std::vector<std::unique_ptr<StateArrayBase>> owned_arrays_;

    // 当前目录内登记的全部数组状态对象。
    std::vector<StateArrayBase*> entries_;
};

}  // namespace project_xs::sim

#endif
