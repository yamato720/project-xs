#ifndef PROJECT_XS_BASE_PORT_H
#define PROJECT_XS_BASE_PORT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace project_xs::sim {

// 端口方向。
// 一个端口实例在构造完成后方向固定。
enum class PortDirection {
    Input,
    Output,
};

// 端口值输出格式。
// - Decimal: 按数值十进制输出
// - Binary / Octal / Hexadecimal: 按端口位宽输出位模式
enum class PortValueBase {
    Decimal,
    Binary,
    Octal,
    Hexadecimal,
};

// 端口抽象基类。
// 这层只保留“单向端口 + 固定类型 + 固定宽度 + 绑定变量”的共同语义。
// 真正的时序行为交给两个派生类：
// - WirePort: 当前拍写入，本拍即可见
// - RegPort : 当前拍写入，下一拍才可见
class Port {
  public:
    Port(std::string name,
         PortDirection direction,
         std::size_t width_bits,
         const std::type_info& type_info,
         std::size_t data_size,
         void* bound_variable);
    virtual ~Port() = default;

    // 返回端口名称。
    const std::string& name() const { return name_; }

    // 返回端口方向。
    PortDirection direction() const { return direction_; }

    // 返回端口位宽。
    std::size_t width_bits() const { return width_bits_; }

    // 返回底层数据字节数。
    std::size_t data_size() const { return data_size_; }

    // 返回端口绑定的数据类型。
    const std::type_index& type_index() const { return type_index_; }

    // 当前端口是否有一个可见的有效值。
    bool valid() const { return valid_; }

    // 把当前输入端口连接到某个输出端口。
    // 连接时会严格检查：
    // - 方向是否匹配
    // - 类型是否一致
    // - 字节大小是否一致
    // - 位宽是否一致
    void connect(const std::shared_ptr<Port>& output_port);

    // 输入端口在单拍逻辑开始前调用。
    // 由派生类决定“本拍如何看到上游值”。
    virtual void sync_input() = 0;

    // 输出端口在本拍逻辑中调用。
    // 它会把当前绑定变量的值采样进端口内部存储。
    virtual void emit_bound_value() = 0;

    // 拍末统一提交。
    // WirePort 通常不需要做事；
    // RegPort 会把 pending 值升级成下一拍可见值。
    virtual void end_cycle() = 0;

    // 清空端口可见值和待提交值，并把绑定变量清零。
    virtual void clear();

    // 把端口初始化成“当前可见的 0 值”。
    // 对 wire 型端口，通常等价于 clear()；
    // 对 reg 型端口，表示在仿真起始拍就让它呈现一个确定的 0。
    virtual void initialize_zero();

    // 以严格类型读取端口当前可见值。
    // 如果类型/位宽/字节大小不匹配，则会抛异常。
    template <typename T>
    bool read(T& out) const {
        ensure_type_match(typeid(T), sizeof(T), sizeof(T) * 8);
        if (!valid_) {
            return false;
        }

        std::memcpy(static_cast<void*>(&out), visible_storage_.data(), data_size_);
        return true;
    }

    // 只读访问当前端口的可见值与有效位。
    // 主要供输入端口在同步上游值时使用。
    const std::vector<std::byte>& visible_storage() const { return visible_storage_; }
    bool visible_valid() const { return valid_; }

    // 返回当前端口可见值的字符串表示。
    // 无有效值时返回 "z"。
    // Decimal 下尽量按标量类型输出；
    // 其他进制按端口位宽输出位模式。
    std::string value_string(PortValueBase base = PortValueBase::Decimal) const;

    // 返回当前端口绑定类型的可读字符串。
    std::string type_string() const;

    // 返回端口信息字符串，包含名字、内容、类型。
    std::string info(PortValueBase base = PortValueBase::Decimal) const;

    // 复制另一个端口的运行时状态。
    // 要求双方方向、类型、位宽和字节数一致。
    void copy_runtime_from(const Port& other);

  protected:
    // 校验方向。
    void ensure_direction(PortDirection expected) const;

    // 校验类型、位宽、字节大小是否匹配。
    void ensure_type_match(const std::type_info& expected_type,
                           std::size_t expected_size,
                           std::size_t expected_width_bits) const;

    // 把当前绑定变量采样到某个端口内部存储区。
    void copy_bound_to_storage(std::vector<std::byte>& storage) const;

    // 把某个端口内部存储区写回绑定变量。
    void copy_storage_to_bound(const std::vector<std::byte>& storage) const;

    // 把绑定变量清零。
    void clear_bound_variable() const;

    // 获取当前输入端口连接到的输出端口。
    std::shared_ptr<Port> source_output() const;

    std::string name_;
    PortDirection direction_;
    std::size_t width_bits_ = 0;
    std::size_t data_size_ = 0;
    std::type_index type_index_;
    void* bound_variable_ = nullptr;

    // 当前拍对外可见的值。
    std::vector<std::byte> visible_storage_;

    // 用于寄存器型端口在拍内暂存“下一拍才可见”的值。
    std::vector<std::byte> pending_storage_;

    // 当前拍可见值是否有效。
    bool valid_ = false;

    // 待提交值是否有效。
    bool pending_valid_ = false;

    // 输入端口连接到的输出端口。
    std::weak_ptr<Port> source_output_;
};

// 线网型端口。
// 语义：
// - 输出端：emit 后本拍立即可见
// - 输入端：sync_input 后本拍立即读到上游当前可见值
// 适合模拟组合直通、同拍传播的简化信号。
class WirePort final : public Port {
  public:
    using Port::Port;

    // 构造一个 wire 型输入端口，并绑定到某个变量。
    template <typename T>
    static std::shared_ptr<WirePort> make_input(std::string name,
                                                T* bound_variable,
                                                std::size_t width_bits = sizeof(T) * 8) {
        return std::make_shared<WirePort>(
            std::move(name),
            PortDirection::Input,
            width_bits,
            typeid(T),
            sizeof(T),
            static_cast<void*>(bound_variable));
    }

    // 构造一个 wire 型输出端口，并绑定到某个变量。
    template <typename T>
    static std::shared_ptr<WirePort> make_output(std::string name,
                                                 T* bound_variable,
                                                 std::size_t width_bits = sizeof(T) * 8) {
        return std::make_shared<WirePort>(
            std::move(name),
            PortDirection::Output,
            width_bits,
            typeid(T),
            sizeof(T),
            static_cast<void*>(bound_variable));
    }

    void sync_input() override;
    void emit_bound_value() override;
    void end_cycle() override;
    void initialize_zero() override;
};

// 寄存器型端口。
// 语义：
// - 输出端：本拍 emit 后不会立刻对外可见，要等 end_cycle() 提交
// - 输入端：本拍 sync_input() 看到的是“上一拍已经提交好的值”，
//   同时会把上游当前值采样成下一拍待见值
// 适合模拟 pipeline 寄存器、stage 边界、寄存一级的信号。
class RegPort final : public Port {
  public:
    using Port::Port;

    // 构造一个 reg 型输入端口，并绑定到某个变量。
    template <typename T>
    static std::shared_ptr<RegPort> make_input(std::string name,
                                               T* bound_variable,
                                               std::size_t width_bits = sizeof(T) * 8) {
        return std::make_shared<RegPort>(
            std::move(name),
            PortDirection::Input,
            width_bits,
            typeid(T),
            sizeof(T),
            static_cast<void*>(bound_variable));
    }

    // 构造一个 reg 型输出端口，并绑定到某个变量。
    template <typename T>
    static std::shared_ptr<RegPort> make_output(std::string name,
                                                T* bound_variable,
                                                std::size_t width_bits = sizeof(T) * 8) {
        return std::make_shared<RegPort>(
            std::move(name),
            PortDirection::Output,
            width_bits,
            typeid(T),
            sizeof(T),
            static_cast<void*>(bound_variable));
    }

    void sync_input() override;
    void emit_bound_value() override;
    void end_cycle() override;
    void initialize_zero() override;
};

}  // namespace project_xs::sim

#endif
