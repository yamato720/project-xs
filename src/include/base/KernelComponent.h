#ifndef PROJECT_XS_BASE_KERNEL_COMPONENT_H
#define PROJECT_XS_BASE_KERNEL_COMPONENT_H

#include "base/PortGroup.h"

#include <cstdint>
#include <memory>
#include <string>

namespace project_xs::sim {

class Kernel;

// Kernel 内部的周期小组件基类。
// 这个类适合描述：
// - kernel 内部某个独立 stage
// - 某个周期性子单元
// - 某个带相位/延时的小状态块
//
// 它的典型执行顺序是：
// 1. 先同步当前组件自己的所有输入端口
// 2. 再执行当前组件自己的 run_single()
// 3. 再发射当前组件自己的所有输出端口
// 4. 再执行输出后的钩子
// 5. 最后推进当前组件自己的 phase
class KernelComponent {
  public:
    // 构造一个组件。
    // - name: 组件逻辑名
    // - latency: 相位窗口长度
    // - first_delay_align: 首次进入正常相位前需要额外等待的拍数
    explicit KernelComponent(std::string name,
                             std::uint64_t latency = 0,
                             std::uint64_t first_delay_align = 0);
    virtual ~KernelComponent() = default;

    // 返回组件名称。
    const std::string& name() const { return name_; }

    // 返回组件的周期窗口长度。
    std::uint64_t latency() const { return latency_; }

    // 返回首次对齐延时。
    std::uint64_t first_delay_align() const { return first_delay_align_; }

    // 返回组件自带的默认普通端口组。
    // 这个端口组只承载无协议端口，用来给组件内部自由组合多输入/多输出。
    PortGroup& ports() { return *port_groups_.front(); }
    const PortGroup& ports() const { return *port_groups_.front(); }

    // 创建并持有一个附加端口组。
    // 默认组永远是 port_groups_[0]；新创建的组会追加到后面。
    PortGroup& create_port_group(std::string name);

    // 复位组件内部状态。
    // 默认会清空相位和端口状态。
    void reset();

    // 将组件相关端口初始化为 0。
    void initialize_zero();

    // 推进组件一个周期。
    // 这是“单个组件自己的完整更新函数”。
    // 如果上层容器按 vector 顺序逐个调用 run()，
    // 就会形成一个明确的先后执行关系。
    void run(std::uint64_t cycle);

    // 拍末提交组件相关端口。
    // 主要用于 reg 语义端口的提交。
    void end_cycle();

    // 深复制当前组件。
    virtual std::shared_ptr<KernelComponent> clone() const = 0;

  protected:
    // 组件单拍业务逻辑入口。
    // 派生类一般在这里写“本拍该干什么”。
    virtual void run_single(std::uint64_t cycle);

    // 所有输出发射完之后的钩子。
    virtual void after_outputs_emitted(std::uint64_t cycle);

    // 当前组件是否已经完成首对齐，可以进入正常相位流转。
    bool phase_valid() const { return align_remaining_ == 0; }

    // 当前所在的相位编号。
    // 只有 phase_valid() 为 true 时，这个值才具有实际意义。
    std::uint64_t phase() const { return phase_; }

    // 复制基类层的运行时状态，供派生类 clone() 使用。
    void copy_component_runtime_from(const KernelComponent& other);

    // 非端口组类的额外状态钩子。
    virtual void reset_extra();
    virtual void initialize_zero_extra();
    virtual void end_cycle_extra();

  private:
    void emit_outputs();
    // 推进组件内部相位状态。
    // 先消耗首对齐拍数，之后在 [0, latency) 内循环。
    void advance_phase();

    // 组件名称。
    std::string name_;

    // 周期窗口长度。
    std::uint64_t latency_ = 0;

    // 首次进入正常相位前的额外等待拍数。
    std::uint64_t first_delay_align_ = 0;

    // 当前还剩多少拍首对齐尚未完成。
    std::uint64_t align_remaining_ = 0;

    // 当前处于哪个相位。
    std::uint64_t phase_ = 0;

    // port_groups_[0] 永远是默认普通端口组，其余元素按创建顺序追加。
    std::vector<std::unique_ptr<PortGroup>> port_groups_;
};

}  // namespace project_xs::sim

#endif
