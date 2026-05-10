#ifndef PROJECT_XS_BASE_CYCLE_SIMULATOR_H
#define PROJECT_XS_BASE_CYCLE_SIMULATOR_H

#include "base/Kernel.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace project_xs::sim {

// 最小周期模拟器。
// 这个类本身不关心算法细节，只负责：
// - 保存当前周期号
// - 保存最大运行周期
// - 按顺序推动所有已注册 kernel
class CycleSimulator {
  public:
    // 构造模拟器。
    // max_cycles < 0 表示无限运行；
    // max_cycles >= 0 表示最多运行指定拍数。
    explicit CycleSimulator(std::int64_t max_cycles = -1);

    // 设置最大运行周期。
    // 传入 -1 时，内部会转换成 uint64_t 最大值。
    void set_max_cycles(std::int64_t max_cycles);

    // 返回当前配置的最大运行周期上限。
    std::uint64_t max_cycles() const { return max_cycles_; }

    // 注册一个 kernel。
    // 每次 step() 时，会按注册顺序逐个调用它们的 run()。
    void add_kernel(const std::shared_ptr<Kernel>& kernel);

    // 清空所有已注册 kernel，并把模拟器状态恢复到初始值。
    void clear();

    // 复位模拟器以及所有已注册 kernel。
    void reset();

    // 把所有已注册 kernel 的寄存器型端口初始化为 0。
    void initialize_zero();

    // 执行一个周期。
    // 典型顺序是：
    // 1. 推动所有 kernel
    // 2. 当前周期号加一
    // 3. 检查是否达到最大周期数或命中 terminate 请求
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

  private:
    // 当前注册的所有 kernel。
    std::vector<std::shared_ptr<Kernel>> kernels_;

    // 下一次 step() 将要使用的周期号。
    std::uint64_t current_cycle_ = 0;

    // 最大运行周期上限。
    std::uint64_t max_cycles_ = 0;

    // 当前模拟器是否已经结束。
    bool finished_ = false;
};

}  // namespace project_xs::sim

#endif
