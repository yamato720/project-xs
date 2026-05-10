#ifndef PROJECT_XS_BASE_KERNEL_H
#define PROJECT_XS_BASE_KERNEL_H

#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/PortGroup.h"

#include <cstdint>
#include <memory>
#include <string>
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

    // 绑定一个输入端口。
    // 当前约束是“最多一个输入端口”。
    void bind_input(const std::shared_ptr<Port>& input);

    // 绑定一个输出端口。
    // 当前约束是“最多一个输出端口”。
    void bind_output(const std::shared_ptr<Port>& output);

    // 返回当前绑定的输入/输出端口。
    const std::shared_ptr<Port>& input_port() const { return input_; }
    const std::shared_ptr<Port>& output_port() const { return output_; }

    // 返回 kernel 自带的默认普通端口组。
    // 这个端口组只承载无协议端口，适合作为统一的多输入/多输出插槽。
    PortGroup& ports() { return ports_; }
    const PortGroup& ports() const { return ports_; }

    // 向当前 kernel 内注册一个子组件。
    // 默认 run_single() 会按注册顺序依次推动这些组件。
    void add_component(const std::shared_ptr<KernelComponent>& component);

    // 复位 kernel 自身和所有内部组件。
    virtual void reset();

    // 将 kernel 自身及其内部组件的端口初始化为 0。
    virtual void initialize_zero();

    // 推进 kernel 一个周期。
    // 执行顺序固定为：
    // 1. 如果有输入端口，先同步输入
    // 2. 执行 run_single()
    // 3. 处理 kernel 自身的周期事件
    void run(std::uint64_t cycle);

    // 拍末提交 kernel 自身以及其内部所有组件的端口状态。
    virtual void end_cycle();

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

    // 从 kernel 内部请求终止整个模拟器。
    void terminate() { terminate_requested_ = true; }

    // 返回 kernel 自身已经累计推进的拍数。
    std::uint64_t elapsed_cycles() const { return elapsed_cycles_; }

    // 把当前绑定变量经输出端口发射出去。
    void emit_output();

  private:
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

    // 当前绑定的输入/输出端口。
    std::shared_ptr<Port> input_;
    std::shared_ptr<Port> output_;

    // 默认普通端口组。
    PortGroup ports_;
};

}  // namespace project_xs::sim

#endif
