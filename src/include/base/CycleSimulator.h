#ifndef PROJECT_XS_BASE_CYCLE_SIMULATOR_H
#define PROJECT_XS_BASE_CYCLE_SIMULATOR_H

#include "base/Kernel.h"
#include "base/PortGroup.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace project_xs::sim {

// 最小周期模拟器。
// 这个类本身不关心算法细节，只负责：
// - 保存当前周期号
// - 保存最大运行周期
// - 保存周期频率
// - 提供一个默认普通端口组，作为 simulator 级跨周期输入输出接口
// - 按顺序推动所有已注册 kernel
class CycleSimulator {
  public:
    // 构造模拟器。
    // max_cycles < 0 表示无限运行；
    // max_cycles >= 0 表示最多运行指定拍数。
    // frequency_hz 必须 > 0。
    explicit CycleSimulator(std::int64_t max_cycles = -1,
                            long double frequency_hz = 1.0L);

    // 允许用 A(max_cycles, frequency_hz) 这种方式就地改写调度配置。
    // 返回自身，便于继续链式调用。
    CycleSimulator& operator()(std::int64_t max_cycles, long double frequency_hz);

    // 复制另一个 simulator 的运行时状态。
    // 当前只复制：
    // - current_cycle
    // - finished
    // 不复制：
    // - 已注册 kernel
    // - max_cycles / frequency_hz
    void copy(const CycleSimulator& other);

    // 完整复制另一个 simulator。
    // 包括：
    // - max_cycles / frequency_hz
    // - current_cycle / finished
    // - 所有已注册 kernel（通过 clone() 深复制）
    void fullcopy(const CycleSimulator& other);

    // 设置最大运行周期。
    // 传入 -1 时，内部会转换成 uint64_t 最大值。
    void set_max_cycles(std::int64_t max_cycles);

    // 设置/读取周期频率，单位 Hz。
    void set_frequency_hz(long double frequency_hz);
    long double frequency_hz() const { return frequency_hz_; }

    // 返回当前配置的最大运行周期上限。
    std::uint64_t max_cycles() const { return max_cycles_; }

    // 返回 simulator 自带的默认普通端口组。
    PortGroup& ports() { return ports_; }
    const PortGroup& ports() const { return ports_; }

    // 注册一个附加端口组。
    // 默认 portgroup 永远具有最高优先级；附加组会按注册顺序在其后处理。
    void add_port_group(PortGroup* group);

    // 注册一个 kernel。
    // 每次 step() 时，会按注册顺序逐个调用它们自己的完整 run()。
    void add_kernel(const std::shared_ptr<Kernel>& kernel);

    // 按名字查找一个 kernel；找不到返回空指针。
    std::shared_ptr<Kernel> find_kernel(std::string_view name) const;

    // 删除一个 kernel。
    // 删除成功返回 true，否则返回 false。
    bool remove_kernel(std::string_view name);

    // 清空所有已注册 kernel，并把模拟器状态恢复到初始值。
    void clear();

    // 复位模拟器以及所有已注册 kernel。
    void reset();

    // 把所有已注册 kernel 的寄存器型端口初始化为 0。
    void initialize_zero();

    // 执行一个周期。
    // 典型顺序是：
    // 1. simulator 自身完成默认/附加端口组的输入同步与输出发射
    // 2. 按 vector 顺序逐个执行 kernel->run()
    // 3. 再按相同顺序逐个执行 kernel->end_cycle()
    // 4. 当前周期号加一
    // 5. 检查是否达到最大周期数或命中 terminate 请求
    bool step();

    // 连续执行，直到 step() 返回 true。
    void run();

    // 主动终止整个模拟器。
    void terminate();

    // 返回当前周期号。
    // 注意它表示“下一次 step() 将要执行的周期号”。
    std::uint64_t current_cycle() const { return current_cycle_; }

    // 返回模拟器是否已经结束。
    bool is_finished() const { return finished_; }

    // 返回当前模拟器的可读状态信息。
    std::string info() const;

  protected:
    // simulator 自身的单拍逻辑入口。
    // 派生类可在这里驱动默认 portgroup 的输入输出变量。
    virtual void run_single(std::uint64_t cycle);

    // 非端口组类的额外状态钩子。
    virtual void reset_extra();
    virtual void initialize_zero_extra();

    // 复制派生类自己的额外运行时状态；默认空实现。
    virtual void copy_runtime_extra_from(const CycleSimulator& other);

  private:
    void emit_outputs();

    // 当前注册的所有 kernel。
    std::vector<std::shared_ptr<Kernel>> kernels_;

    // 默认普通端口组。
    PortGroup ports_{"cycle_simulator_ports"};

    // 默认组之外的附加端口组。
    std::vector<PortGroup*> extra_port_groups_;

    // 下一次 step() 将要使用的周期号。
    std::uint64_t current_cycle_ = 0;

    // 最大运行周期上限。
    std::uint64_t max_cycles_ = 0;

    // 周期频率，单位 Hz。
    long double frequency_hz_ = 1.0L;

    // 当前模拟器是否已经结束。
    bool finished_ = false;
};

}  // namespace project_xs::sim

#endif
