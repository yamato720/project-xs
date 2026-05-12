#ifndef PROJECT_XS_BASE_KERNEL_H
#define PROJECT_XS_BASE_KERNEL_H

#include "base/KernelComponent.h"
#include "base/PortGroup.h"
#include "base/State.h"

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

    // 虚析构，保证通过基类指针释放派生 kernel 时安全。
    virtual ~Kernel() = default;

    // 返回 kernel 名称。
    const std::string& name() const { return name_; }

    // 返回 kernel 自身的周期窗口长度。
    std::uint64_t latency() const { return latency_; }

    // 返回 kernel 自带的默认普通端口组。
    // 这个端口组只承载无协议端口，适合作为统一的多输入/多输出插槽。
    PortGroup& ports() { return *port_groups_.front(); }

    // 返回 kernel 自带的只读默认普通端口组。
    const PortGroup& ports() const { return *port_groups_.front(); }

    // 返回 kernel 自带的只读状态表。
    const StateSet& state_set() const { return state_set_; }

    // 创建并持有一个附加端口组。
    // 默认组永远是 port_groups_[0]；新创建的组会追加到后面。
    PortGroup& create_port_group(std::string name);

    // 返回当前 kernel 持有的全部端口组。
    // vector 顺序仍然保留执行/组织语义，同时支持按名字查找。
    const std::vector<std::unique_ptr<PortGroup>>& port_groups() const { return port_groups_; }

    // 向当前 kernel 内注册一个子组件。
    // 在默认实现里，这些组件会按 vector 注册顺序逐个调用它们自己的 run()。
    void add_component(const std::shared_ptr<KernelComponent>& component);

    // 返回当前 kernel 内已注册的全部组件。
    // 这个 vector 既决定执行顺序，也可以被按名字查询。
    const std::vector<std::shared_ptr<KernelComponent>>& components() const { return components_; }

    // 按名字查找/获取子组件或附属端口组。
    // find 找不到时返回空指针；get 找不到时抛异常。
    // 这组接口是 kernel 层面对 components_ / port_groups_ 的统一命名访问口。
    std::shared_ptr<KernelComponent> find_component(std::string_view name) const;

    // 按名字获取一个子组件；找不到时报错。
    const std::shared_ptr<KernelComponent>& get_component(std::string_view name) const;

    // 按名字查找一个附属端口组。
    PortGroup* find_port_group(std::string_view name);

    // 只读按名字查找一个附属端口组。
    const PortGroup* find_port_group(std::string_view name) const;

    // 按名字获取一个附属端口组；找不到时报错。
    PortGroup& get_port_group(std::string_view name);

    // 只读按名字获取一个附属端口组；找不到时报错。
    const PortGroup& get_port_group(std::string_view name) const;

    // 返回指定子组件的摘要信息。
    std::string component_info(std::string_view name) const;

    // 返回全部子组件的摘要信息。
    std::string all_components_info() const;

    // 返回指定附属端口组的摘要信息。
    std::string port_group_info(std::string_view name,
                                PortValueBase base = PortValueBase::Decimal) const;

    // 返回全部附属端口组的摘要信息。
    std::string all_port_groups_info(PortValueBase base = PortValueBase::Decimal) const;

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

    // 返回当前 kernel 的可读状态摘要。
    // 输出会聚合 kernel 自身状态、全部 portgroup 和全部 component 的摘要。
    virtual std::string info() const;

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

    // 返回 kernel 自带的可写状态表。
    // 主要用于构造阶段注册状态项。
    StateSet& mutable_state_set() { return state_set_; }

    // 非端口组类的额外状态钩子。
    virtual void reset_extra();

    // 非端口组类的附加“可见 0 初始化”钩子。
    virtual void initialize_zero_extra();

    // 非端口组类的拍末附加钩子。
    virtual void end_cycle_extra();

  private:
    // 把一段文本安全输出到指定流；自动补换行。
    void write_text(std::ostream& os, const std::string& text) const;

    // 输出当前拍的调试文本。
    void write_debug(std::ostream& os, std::uint64_t cycle) const;

    // 发射当前 kernel 全部端口组中的输出端口。
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

    // kernel 自身的状态收束表。
    StateSet state_set_;

    // port_groups_[0] 永远是默认普通端口组，其余元素按创建顺序追加。
    std::vector<std::unique_ptr<PortGroup>> port_groups_;
};

}  // namespace project_xs::sim

#endif
