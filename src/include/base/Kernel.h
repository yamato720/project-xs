#ifndef PROJECT_XS_BASE_KERNEL_H
#define PROJECT_XS_BASE_KERNEL_H

#include "base/KernelComponent.h"
#include "base/PortGroup.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace project_xs::sim {

class CycleSimulator;

// Kernel 基类。
// 这是“可被周期模拟器逐拍推动”的上层模块抽象。
//
// 一个 Kernel 可以：
// - 自己直接实现 run_single()
// - 或者内部挂多个 KernelComponent，由这些小组件协同工作
//
// 因此它更像一个“容器级模块”。
class Kernel {
  public:
    // 构造一个 kernel。
    // - name: kernel 名称
    // - latency: kernel 自身的周期窗口长度
    explicit Kernel(std::string name, std::uint64_t latency = 0);
    virtual ~Kernel() = default;

    // 返回 kernel 名称。
    const std::string& name() const { return name_; }

    // 返回 kernel 自身的周期窗口长度。
    std::uint64_t latency() const { return latency_; }

    // 返回 kernel 自带的默认普通端口组。
    // 这个端口组只承载无协议端口，适合作为统一的多输入/多输出插槽。
    PortGroup& ports() { return ports_; }
    const PortGroup& ports() const { return ports_; }

    // 创建并持有一个附加端口组，同时自动注册到统一调度列表。
    PortGroup& create_port_group(std::string name);

    // 注册一个附加端口组。
    // 默认 portgroup 永远具有最高优先级；附加组会按注册顺序在其后处理。
    void add_port_group(PortGroup* group);

    // 向当前 kernel 内注册一个子组件。
    // 在默认实现里，这些组件会按 vector 注册顺序逐个调用它们自己的 run()。
    void add_component(const std::shared_ptr<KernelComponent>& component);

    // 复位 kernel 自身和所有内部组件。
    void reset();

    // 将 kernel 自身及其内部组件的端口初始化为 0。
    void initialize_zero();

    // 推进 kernel 一个周期。
    // 执行顺序固定为：
    // 1. 同步默认端口组里的所有输入
    // 2. 执行 run_single()
    //    默认实现会按 vector 顺序逐个推动内部组件
    // 3. 自动发射 kernel 自身全部输出端口
    // 4. 执行输出发射后的钩子
    // 5. 处理 kernel 自身的周期事件
    void run(std::uint64_t cycle);

    // 拍末提交 kernel 自身以及其内部所有组件的端口状态。
    void end_cycle();

    // 深复制当前 kernel。
    virtual std::shared_ptr<Kernel> clone() const = 0;

    // 删除一个子组件。
    // 删除成功返回 true，否则返回 false。
    bool remove_component(std::string_view name);

    // 当 kernel 被挂到某个 CycleSimulator 上时调用。
    // 派生类可在这里把自己的输入端口连接到 simulator 的默认 portgroup。
    virtual void on_attached_to_simulator(CycleSimulator& simulator);

    // 返回该 kernel 是否请求终止整个周期模拟器。
    bool terminate_requested() const { return terminate_requested_; }

  protected:
    // kernel 的单拍业务逻辑入口。
    // 默认行为：
    // - 如果没有内部组件，打印 hello
    // - 如果已经挂了组件，则依次推动这些组件
    virtual void run_single(std::uint64_t cycle);

    // kernel 自身到达 latency 时的默认钩子。
    virtual void on_latency_reached(std::uint64_t cycle);

    // 所有输出发射完之后的钩子。
    virtual void after_outputs_emitted(std::uint64_t cycle);

    // 返回当前拍的调试输出文本；默认返回空串。
    // 派生类可覆写来自定义逐拍输出。
    virtual std::string debug_info(std::uint64_t cycle) const;

    // 返回 latency 事件文本；默认返回一条简单提示。
    virtual std::string latency_info(std::uint64_t cycle) const;

    // 从 kernel 内部请求终止整个模拟器。
    void terminate() { terminate_requested_ = true; }

    // 返回 kernel 自身已经累计推进的拍数。
    std::uint64_t elapsed_cycles() const { return elapsed_cycles_; }

    // 复制基类层的运行时状态，供派生类 clone() 使用。
    void copy_kernel_runtime_from(const Kernel& other);

    // 非端口组类的额外状态钩子。
    virtual void reset_extra();
    virtual void initialize_zero_extra();
    virtual void end_cycle_extra();

  private:
    void write_text(std::ostream& os, const std::string& text) const;
    void write_debug(std::ostream& os, std::uint64_t cycle) const;
    void emit_outputs();

    // 推进 kernel 自身的周期事件。
    void handle_latency_event(std::uint64_t cycle);

    // 当前 kernel 内部挂接的所有子组件。
    std::vector<std::shared_ptr<KernelComponent>> components_;

    // kernel 名称。
    std::string name_;

    // kernel 自身周期窗口长度。
    std::uint64_t latency_ = 0;

    // kernel 自身已经推进了多少拍。
    std::uint64_t elapsed_cycles_ = 0;

    // 是否请求终止整个模拟器。
    bool terminate_requested_ = false;

    // 默认普通端口组。
    PortGroup ports_;

    // 由 kernel 自身拥有的附加端口组。
    std::vector<std::unique_ptr<PortGroup>> owned_extra_port_groups_;

    // 默认组之外的附加端口组。
    std::vector<PortGroup*> extra_port_groups_;
};

}  // namespace project_xs::sim

#endif
