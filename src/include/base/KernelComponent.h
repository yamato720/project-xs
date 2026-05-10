#ifndef PROJECT_XS_BASE_KERNEL_COMPONENT_H
#define PROJECT_XS_BASE_KERNEL_COMPONENT_H

#include "base/Port.h"
#include "base/PortGroup.h"

#include <cstdint>
#include <memory>
#include <string>

namespace project_xs::sim {

// Kernel 内部的周期小组件基类。
// 这个类适合描述：
// - kernel 内部某个独立 stage
// - 某个周期性子单元
// - 某个带相位/延时的小状态块
//
// 它的典型执行顺序是：
// 1. 如果绑定了输入端口，先同步输入
// 2. 执行本拍的 run_single()
// 3. 推进内部 phase
// 4. 在拍末由 end_cycle() 提交端口状态
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

    // 绑定输入端口。
    // 当前约束是“最多一个输入端口”。
    void bind_input(const std::shared_ptr<Port>& input);

    // 绑定输出端口。
    // 当前约束是“最多一个输出端口”。
    void bind_output(const std::shared_ptr<Port>& output);

    // 返回当前绑定的输入/输出端口。
    const std::shared_ptr<Port>& input_port() const { return input_; }
    const std::shared_ptr<Port>& output_port() const { return output_; }

    // 返回组件自带的默认普通端口组。
    // 这个端口组只承载无协议端口，用来给组件内部自由组合多输入/多输出。
    PortGroup& ports() { return ports_; }
    const PortGroup& ports() const { return ports_; }

    // 复位组件内部状态。
    // 默认会清空相位和端口状态。
    virtual void reset();

    // 将组件相关端口初始化为 0。
    virtual void initialize_zero();

    // 推进组件一个周期。
    // 执行顺序固定为：
    // 1. sync_input()
    // 2. run_single()
    // 3. advance_phase()
    void run(std::uint64_t cycle);

    // 拍末提交组件相关端口。
    // 主要用于 reg 语义端口的提交。
    void end_cycle();

  protected:
    // 组件单拍业务逻辑入口。
    // 派生类一般在这里写“本拍该干什么”。
    virtual void run_single(std::uint64_t cycle);

    // 当前组件是否已经完成首对齐，可以进入正常相位流转。
    bool phase_valid() const { return align_remaining_ == 0; }

    // 当前所在的相位编号。
    // 只有 phase_valid() 为 true 时，这个值才具有实际意义。
    std::uint64_t phase() const { return phase_; }

    // 把当前绑定变量经输出端口发射出去。
    // 具体是本拍立即可见还是下一拍可见，取决于绑定的是 WirePort 还是 RegPort。
    void emit_output();

  private:
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

    // 当前绑定的输入/输出端口。
    std::shared_ptr<Port> input_;
    std::shared_ptr<Port> output_;

    // 默认普通端口组。
    PortGroup ports_;
};

}  // namespace project_xs::sim

#endif
