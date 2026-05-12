#ifndef PROJECT_XS_BASE_SIMULATION_SESSION_H
#define PROJECT_XS_BASE_SIMULATION_SESSION_H

#include "base/CycleSimulator.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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
    // 构造一个时间驱动会话。
    SimulationSession(long double frequency_hz, long double run_time_seconds);

    // 虚析构，保证派生会话类型通过基类指针释放时安全。
    virtual ~SimulationSession() = default;

    // 设置/读取会话频率，单位 Hz。
    void set_frequency_hz(long double frequency_hz);

    // 返回当前会话频率，单位 Hz。
    long double frequency_hz() const { return frequency_hz_; }

    // 设置/读取目标运行时间，单位是秒。
    void set_run_time_seconds(long double run_time_seconds);

    // 返回目标运行时间，单位是秒。
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

    // 返回建议给外部直接打印的结束信息。
    virtual std::string finish_info() const;

    // 注册一个被当前 session 调度的 CycleSimulator。
    void add_simulator(const std::shared_ptr<CycleSimulator>& simulator);

    // 按名字查找/获取 session 内的 simulator。
    // find 找不到时返回空指针；get 找不到时抛异常。
    // 这组接口是 session 层面对 simulator_views_ 的统一命名访问口。
    std::shared_ptr<CycleSimulator> find_simulator(std::string_view name) const;

    // 按名字获取一个 simulator；找不到时报错。
    const std::shared_ptr<CycleSimulator>& get_simulator(std::string_view name) const;

    // 返回指定 simulator 的摘要信息。
    std::string simulator_info(std::string_view name) const;

    // 返回所有已注册的 simulator。
    const std::vector<std::shared_ptr<CycleSimulator>>& simulators() const;

    // 返回当前 session 内所有 simulator 的可读摘要。
    // 这里会按 vector 顺序把每个 simulator 的 info() 拼起来。
    std::string simulators_info() const;

    // 兼容统一命名：返回全部 simulator 的摘要信息。
    std::string all_simulators_info() const;

    // 清空当前 session 持有的全部 simulator，并把 session 自己的运行状态回到初始值。
    // clear() 之后：
    // - 不再保留任何已注册 simulator
    // - current_tick 回到 0
    // - finished 回到 false
    // 这一步不对 simulator 做 reset，因为它们已经从 session 中移除了。
    void clear();

    // 保留当前已注册的 simulator，只把“本轮仿真进度”回到起点。
    // reset() 之后：
    // - session 的 current_tick 回到 0
    // - session 的 finished 回到 false
    // - 每个 simulator 自己也会执行 reset()
    // - 调度累计器 accumulated_hz 清零
    // 但不会删除任何已注册 simulator。
    void reset();

    // 在当前已注册 simulator 上执行“可见 0 初始化”。
    // 这一步通常接在 reset() 之后调用，用来让寄存器型端口在仿真起始拍就呈现确定的 0，
    // 而不是保持无效 / 未定义可见态。
    // initialize_zero() 不会改动 session 的 current_tick / finished，也不会删除 simulator。
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
    // session 内部使用的调度单元。
    struct ScheduledSimulator {
        // 被调度的 simulator 实例。
        std::shared_ptr<CycleSimulator> simulator;

        // 当前尚未消耗完的频率累计值。
        long double accumulated_hz = 0.0L;
    };

    // 判断当前 session 内是否所有 simulator 都已经结束。
    bool all_simulators_finished() const;

    // session 调度层真正持有的 simulator 与其累计器。
    std::vector<ScheduledSimulator> scheduled_simulators_;

    // 供外部只读查看的 simulator 视图。
    std::vector<std::shared_ptr<CycleSimulator>> simulator_views_;

    // session 自身的时间步频率。
    long double frequency_hz_ = 1.0L;

    // session 目标总运行时间。
    long double run_time_seconds_ = 0.0L;

    // 当前已经执行到的 session tick 编号。
    std::uint64_t current_tick_ = 0;

    // 当前 session 是否已经结束。
    bool finished_ = false;
};

}  // namespace project_xs::sim

#endif
