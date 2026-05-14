#ifndef PROJECT_XS_BASE_SIMULATION_SESSION_H
#define PROJECT_XS_BASE_SIMULATION_SESSION_H

#include "base/CycleSimulator.h"
#include "base/Error.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
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
    // session 运行时快照。
    using Snapshot = SimulationSessionSnapshot;

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

    // 保存当前 session 运行态。
    // 只保存 session 调度状态和各 simulator 运行态，不保存 simulator 构造方式。
    Snapshot snapshot() const;

    // 恢复当前 session 运行态。
    // 目标 session 必须已经注册了同构、同名、同顺序的 simulator 对象图。
    void restore(const Snapshot& snapshot);

    // 把当前 session checkpoint 保存到本地文件。
    void save_checkpoint(const std::string& path) const;

    // 从本地 checkpoint 恢复当前 session 运行态。
    void restore_checkpoint(const std::string& path);

    // 设置快照/波形采集根目录。
    void set_snapshot_capture_directory(std::string directory);

    // 返回当前快照/波形采集根目录。
    const std::string& snapshot_capture_directory() const {
        return snapshot_capture_root_directory_;
    }

    // 开始记录 session 快照。capture_name 非空时会替代自动段目录里时间戳前的默认前缀。
    // Manual 只响应显式 capture_snapshot()；
    // Automatic 会在当前 session step 的关键阶段自动采样。
    void start_snapshot_capture(SnapshotCaptureMode mode = SnapshotCaptureMode::Automatic,
                                std::string capture_name = {});

    // 便捷形式：默认以 Automatic 开启命名采集。
    void start_snapshot_capture(std::string capture_name) {
        start_snapshot_capture(SnapshotCaptureMode::Automatic, std::move(capture_name));
    }

    // 停止记录 session 快照。capture_name 必须严格匹配最后开启的采集名。
    void stop_snapshot_capture(std::string capture_name = {});

    // 返回当前 session 快照采集是否处于开启状态。
    bool snapshot_capture_active() const { return !snapshot_capture_contexts_.empty(); }

    // 返回最后开启的 session 快照采集模式。
    SnapshotCaptureMode snapshot_capture_mode() const {
        return snapshot_capture_contexts_.empty()
            ? SnapshotCaptureMode::Manual
            : snapshot_capture_contexts_.back().mode;
    }

    // 手动采集一条 session 快照。
    const SimulationSessionSnapshotRecord& capture_snapshot(
        SnapshotCaptureStage stage = SnapshotCaptureStage::Manual);

    // 返回当前 session 已采集的快照历史。
    const std::vector<SimulationSessionSnapshotRecord>& snapshot_history() const {
        return snapshot_history_;
    }

    // 清空当前 session 快照历史。
    void clear_snapshot_history();

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

    // 收集当前 session 的结构化诊断。
    virtual void collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const;

    // 校验当前 session 合法性，并返回诊断列表。
    std::vector<error::Diagnostic> validate() const;

    // 校验当前 session 合法性；若存在 Error 直接抛出第一条。
    void validate_or_throw() const;

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

    // 自动模式下按阶段采集 session 快照。
    void capture_snapshot_if_automatic(SnapshotCaptureStage stage);

    // 开始一个自动采集 segment。
    void begin_snapshot_capture_segment(SnapshotCaptureContext& context);

    // 结束指定自动采集 segment，并写入末尾 checkpoint / manifest。
    void finish_snapshot_capture_segment(SnapshotCaptureContext& context);

    // 自动/手动采集开启时，把记录写入本地文件。
    void store_snapshot_capture_record(SimulationSessionSnapshotRecord& record);

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

    // 当前 session 快照采集序号。
    std::uint64_t snapshot_capture_sequence_ = 0;

    // 当前 session 快照采集历史。
    std::vector<SimulationSessionSnapshotRecord> snapshot_history_;

    // 快照/波形采集根目录。
    std::string snapshot_capture_root_directory_ = default_snapshot_capture_directory();

    // 下一个自动采集 segment 序号。
    std::uint64_t snapshot_capture_segment_index_ = 0;

    // 当前正在进行的快照/波形采集上下文栈。
    std::vector<SnapshotCaptureContext> snapshot_capture_contexts_;
};

}  // namespace project_xs::sim

#endif
