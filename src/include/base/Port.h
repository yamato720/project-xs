#ifndef PROJECT_XS_BASE_PORT_H
#define PROJECT_XS_BASE_PORT_H

#include "base/RuntimeTrace.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

namespace project_xs::sim {

class StateBase;
class StateArrayBase;

// 端口抽象基类。
// 这层只保留“单向端口 + 固定类型 + 固定宽度 + 绑定变量”的共同语义。
// 真正的时序行为交给两个派生类：
// - WirePort: 当前拍写入，本拍即可见
// - RegPort : 当前拍写入，下一拍才可见
class Port {
  public:
    // 构造一个基础端口对象。
    Port(std::string name,
         PortDirection direction,
         std::size_t width_bits,
         const std::type_info& type_info,
         std::size_t data_size,
         void* bound_variable);

    // 虚析构，保证通过基类指针释放派生端口时安全。
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

    // 返回端口时序类型。
    virtual std::string_view timing_kind() const = 0;

    // 返回端口绑定的 state 路径，用于波形说明。
    const std::string& bound_state_path() const { return bound_state_path_; }

    // 设置端口绑定的 state 路径。端口由 State/StateArray 创建时自动填充。
    void set_bound_state_path(std::string path) { bound_state_path_ = std::move(path); }

    // 当前端口是否有一个可见的有效值。
    bool valid() const { return valid_; }

    // 返回当前端口是否已经连接到上游输出。
    // 对输出端口，这个值始终返回 true。
    bool connected() const {
        return direction_ == PortDirection::Output || !source_output_.expired();
    }

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

    // 返回当前端口是否存在可见有效值。
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

    // 保存当前端口运行态。
    PortSnapshot snapshot() const;

    // 恢复当前端口运行态。
    void restore(const PortSnapshot& snapshot);

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

    // 当前端口的逻辑名称。
    std::string name_;

    // 当前端口方向。
    PortDirection direction_;

    // 当前端口位宽。
    std::size_t width_bits_ = 0;

    // 当前端口底层值占用字节数。
    std::size_t data_size_ = 0;

    // 当前端口绑定的数据类型索引。
    std::type_index type_index_;

    // 当前端口绑定到的底层变量地址。
    void* bound_variable_ = nullptr;

    // 当前端口绑定的 state 路径，仅用于诊断和波形显示。
    std::string bound_state_path_;

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
    std::string_view timing_kind() const override { return "wire"; }

    // 同步一个 wire 输入端口的上游可见值。
    void sync_input() override;

    // 发射一个 wire 输出端口当前绑定变量的值。
    void emit_bound_value() override;

    // wire 端口的拍末提交；通常无额外动作。
    void end_cycle() override;

    // 把 wire 端口初始化成可见 0。
    void initialize_zero() override;

  private:
    // 允许单状态对象直接创建 wire 端口。
    friend class StateBase;

    // 允许数组状态对象按元素创建 wire 端口。
    friend class StateArrayBase;

    // 仅允许通过状态对象内部辅助接口构造 wire 端口。
    WirePort(std::string name,
             PortDirection direction,
             std::size_t width_bits,
             const std::type_info& type_info,
             std::size_t data_size,
             void* bound_variable)
        : Port(std::move(name),
               direction,
               width_bits,
               type_info,
               data_size,
               bound_variable) {}
};

// 寄存器型端口。
// 语义：
// - 输出端：本拍 emit 后不会立刻对外可见，要等 end_cycle() 提交
// - 输入端：本拍 sync_input() 看到的是“上一拍已经提交好的值”，
//   同时会把上游当前值采样成下一拍待见值
// 适合模拟 pipeline 寄存器、stage 边界、寄存一级的信号。
class RegPort final : public Port {
  public:
    std::string_view timing_kind() const override { return "reg"; }

    // 同步一个 reg 输入端口的上游已提交值。
    void sync_input() override;

    // 发射一个 reg 输出端口当前绑定变量的值到 pending。
    void emit_bound_value() override;

    // 提交 reg 端口 pending 值，使其在下一拍可见。
    void end_cycle() override;

    // 把 reg 端口初始化成可见 0。
    void initialize_zero() override;

  private:
    // 允许单状态对象直接创建 reg 端口。
    friend class StateBase;

    // 允许数组状态对象按元素创建 reg 端口。
    friend class StateArrayBase;

    // 仅允许通过状态对象内部辅助接口构造 reg 端口。
    RegPort(std::string name,
            PortDirection direction,
            std::size_t width_bits,
            const std::type_info& type_info,
            std::size_t data_size,
            void* bound_variable)
        : Port(std::move(name),
               direction,
               width_bits,
               type_info,
               data_size,
               bound_variable) {}
};

}  // namespace project_xs::sim

#endif
