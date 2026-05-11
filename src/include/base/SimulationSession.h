#ifndef PROJECT_XS_BASE_SIMULATION_SESSION_H
#define PROJECT_XS_BASE_SIMULATION_SESSION_H

#include "base/CycleSimulator.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace project_xs::sim {

// 时间驱动的上层仿真入口。
// 这个类本身不持有 kernel，而是持有多个 CycleSimulator。
// 使用方式是：
// 1. 先创建 SimulationSession，给出会话频率和总运行时间
// 2. 再在外部手动创建各个 CycleSimulator，并设置它们各自的频率
// 3. 把这些 CycleSimulator 注册进 session
// 4. session 按自己的时间步推动所有 simulator，并根据频率比决定谁该前进一步
class SimulationSession {
  public:
    SimulationSession(long double frequency_hz, long double run_time_seconds);

    // 设置/读取会话频率，单位 Hz。
    void set_frequency_hz(long double frequency_hz);
    long double frequency_hz() const { return frequency_hz_; }

    // 设置/读取目标运行时间，单位是秒。
    void set_run_time_seconds(long double run_time_seconds);
    long double run_time_seconds() const { return run_time_seconds_; }

    // 返回当前已经执行到的会话时间，单位是秒。
    long double current_time_seconds() const;

    // 返回当前会话是否结束。
    // 条件包括：
    // - 到达目标运行时间
    // - 所有已注册 CycleSimulator 都已经 finished
    bool is_finished() const;

    // 返回建议给外部直接打印的开始/结束信息。
    // 如需定制格式，可在派生类中覆写。
    virtual std::string start_info() const;
    virtual std::string finish_info() const;

    // 注册一个被当前 session 调度的 CycleSimulator。
    void add_simulator(const std::shared_ptr<CycleSimulator>& simulator);

    // 返回所有已注册的 simulator。
    const std::vector<std::shared_ptr<CycleSimulator>>& simulators() const;

    void clear();
    void reset();
    void initialize_zero();

    // 执行一个时间驱动步进。
    // session 自己先前进一个时间步，再根据每个 simulator 的频率决定是否推动它。
    // 如果任意一个 simulator 在这个过程中 finished，整个 session 也结束。
    bool step();

    // 连续执行直到时间或底层 simulator 结束。
    void run();

    // 返回当前已经执行了多少个 session 自己的时间步。
    std::uint64_t current_tick() const { return current_tick_; }

  private:
    struct ScheduledSimulator {
        std::shared_ptr<CycleSimulator> simulator;
        long double accumulated_hz = 0.0L;
    };

    bool all_simulators_finished() const;

    std::vector<ScheduledSimulator> scheduled_simulators_;
    std::vector<std::shared_ptr<CycleSimulator>> simulator_views_;
    long double frequency_hz_ = 1.0L;
    long double run_time_seconds_ = 0.0L;
    std::uint64_t current_tick_ = 0;
    bool finished_ = false;
};

}  // namespace project_xs::sim

#endif
