#ifndef PROJECT_XS_BASE_CYCLE_SIMULATOR_H
#define PROJECT_XS_BASE_CYCLE_SIMULATOR_H

#include "base/Kernel.h"
#include "base/PortGroup.h"
#include "base/State.h"
#include "base/StateArray.h"
#include "base/Error.h"

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
    // 周期模拟器运行时快照。
    using Snapshot = CycleSimulatorSnapshot;

    // 允许显式给 simulator 命名。
    // 这样 session 持有多个 simulator 时，就可以按名字查询和输出信息。
    explicit CycleSimulator(std::string name,
                            std::int64_t max_cycles = -1,
                            long double frequency_hz = 1.0L);

    // 构造模拟器。
    // max_cycles < 0 表示无限运行；
    // max_cycles >= 0 表示最多运行指定拍数。
    // frequency_hz 必须 > 0。
    explicit CycleSimulator(std::int64_t max_cycles = -1,
                            long double frequency_hz = 1.0L);

    // 虚析构，保证通过基类指针释放派生 simulator 时安全。
    virtual ~CycleSimulator() = default;

    // 返回模拟器名称。
    const std::string& name() const { return name_; }

    // 允许单独改名，便于 session 级按名字管理 simulator。
    void set_name(std::string name) { name_ = std::move(name); }

    // 返回 simulator 说明。波形查看器悬停时会优先展示它。
    const std::string& description() const { return description_; }

    // 设置 simulator 说明。
    void set_description(std::string description) { description_ = std::move(description); }

    // 追加一个 simulator 显示别名，用于波形查看器切换显示名。
    void add_name_alias(std::string alias) { name_aliases_.push_back(std::move(alias)); }

    // 返回 simulator 显示别名。
    const std::vector<std::string>& name_aliases() const { return name_aliases_; }

    // 保存当前 simulator 运行态。
    // 只保存状态，不保存对象构造方式；restore 目标必须已经构造出同构对象图。
    Snapshot snapshot() const;

    // 恢复当前 simulator 运行态。
    void restore(const Snapshot& snapshot);

    // 把当前 simulator checkpoint 保存到本地文件。
    void save_checkpoint(const std::string& path) const;

    // 从本地 checkpoint 恢复当前 simulator 运行态。
    void restore_checkpoint(const std::string& path);

    // 设置快照/波形采集根目录。
    void set_snapshot_capture_directory(std::string directory);

    // 返回当前快照/波形采集根目录。
    const std::string& snapshot_capture_directory() const {
        return snapshot_capture_root_directory_;
    }

    // 开始记录 simulator 快照。Manual 只响应显式 capture_snapshot()；
    // Automatic 会在当前 simulator step 的关键阶段自动采样。
    void start_snapshot_capture(SnapshotCaptureMode mode = SnapshotCaptureMode::Automatic);

    // 停止记录 simulator 快照。
    void stop_snapshot_capture();

    // 返回当前 simulator 快照采集是否处于开启状态。
    bool snapshot_capture_active() const { return snapshot_capture_active_; }

    // 返回当前 simulator 快照采集模式。
    SnapshotCaptureMode snapshot_capture_mode() const { return snapshot_capture_mode_; }

    // 手动采集一条 simulator 快照。可在派生类任意阶段调用。
    const CycleSimulatorSnapshotRecord& capture_snapshot(
        SnapshotCaptureStage stage = SnapshotCaptureStage::Manual);

    // 返回当前 simulator 已采集的快照历史。
    const std::vector<CycleSimulatorSnapshotRecord>& snapshot_history() const {
        return snapshot_history_;
    }

    // 清空当前 simulator 快照历史。
    void clear_snapshot_history();

    // 设置最大运行周期。
    // 传入 -1 时，内部会转换成 uint64_t 最大值。
    void set_max_cycles(std::int64_t max_cycles);

    // 设置/读取周期频率，单位 Hz。
    void set_frequency_hz(long double frequency_hz);

    // 返回当前周期频率，单位 Hz。
    long double frequency_hz() const { return frequency_hz_; }

    // 返回当前配置的最大运行周期上限。
    std::uint64_t max_cycles() const { return max_cycles_; }

    // 返回 simulator 自带的默认普通端口组。
    // 返回可写默认端口组。
    PortGroup& ports() { return *port_groups_.front(); }

    // 返回只读默认端口组。
    const PortGroup& ports() const { return *port_groups_.front(); }

    // 返回 simulator 自带的只读单状态表。
    const StateSet& state_set() const { return state_set_; }

    // 返回 simulator 自带的只读数组状态表。
    const StateArrayRegistry& state_array_registry() const { return state_array_registry_; }

    // 创建并持有一个附加端口组。
    // 默认组永远是 port_groups_[0]；新创建的组会追加到后面。
    PortGroup& create_port_group(std::string name);

    // 返回当前 simulator 持有的全部 kernel / portgroup。
    // 这两个 vector 保留顺序语义，同时补了按名字查询接口。
    const std::vector<std::shared_ptr<Kernel>>& kernels() const { return kernels_; }

    // 返回当前 simulator 持有的全部端口组。
    const std::vector<std::unique_ptr<PortGroup>>& port_groups() const { return port_groups_; }

    // 注册一个 kernel。
    // 每次 step() 时，会按注册顺序逐个调用它们自己的完整 run()。
    void add_kernel(const std::shared_ptr<Kernel>& kernel);

    // 按名字查找一个 kernel；找不到返回空指针。
    // 这组接口是 simulator 层面对 kernels_ / port_groups_ 的统一命名访问口。
    std::shared_ptr<Kernel> find_kernel(std::string_view name) const;

    // 按名字查找一个附属端口组。
    PortGroup* find_port_group(std::string_view name);

    // 只读按名字查找一个附属端口组。
    const PortGroup* find_port_group(std::string_view name) const;

    // 按名字获取一个附属端口组；找不到时报错。
    PortGroup& get_port_group(std::string_view name);

    // 只读按名字获取一个附属端口组；找不到时报错。
    const PortGroup& get_port_group(std::string_view name) const;

    // 按名字获取一个 kernel；找不到时报错。
    const std::shared_ptr<Kernel>& get_kernel(std::string_view name) const;

    // 返回指定 kernel 的摘要信息。
    std::string kernel_info(std::string_view name) const;

    // 返回全部 kernel 的摘要信息。
    std::string all_kernels_info() const;

    // 返回指定附属端口组的摘要信息。
    std::string port_group_info(std::string_view name,
                                PortValueBase base = PortValueBase::Decimal) const;

    // 返回全部附属端口组的摘要信息。
    std::string all_port_groups_info(PortValueBase base = PortValueBase::Decimal) const;

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
    // 这里会把：
    // - 自身配置
    // - 自身状态表
    // - 全部 portgroup
    // - 全部 kernel 摘要
    // 一并串起来输出，便于直接调试查看。
    std::string info() const;

    // 收集当前 simulator 的结构化诊断。
    virtual void collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const;

    // 校验当前 simulator 合法性，并返回诊断列表。
    std::vector<error::Diagnostic> validate() const;

    // 校验当前 simulator 合法性；若存在 Error 直接抛出第一条。
    void validate_or_throw() const;

  protected:
    // simulator 自身的单拍逻辑入口。
    // 派生类可在这里驱动默认 portgroup 的输入输出变量。
    virtual void run_single(std::uint64_t cycle);

    // 非端口组类的额外状态钩子。
    virtual void reset_extra();

    // 非端口组类的附加“可见 0 初始化”钩子。
    virtual void initialize_zero_extra();

    // 通过 simulator 基类统一创建并持有一个单状态。
    template <typename T>
    State<T>& create_state(std::string name,
                           std::string description,
                           T initial_value = T{},
                           std::size_t width_bits = sizeof(T) * 8) {
        return state_set_.create_state<T>(
            std::move(name),
            std::move(description),
            std::move(initial_value),
            width_bits);
    }

    // 通过 simulator 基类统一创建并持有一个数组/高维状态。
    template <typename T>
    StateArray<T>& create_state_array(std::string name,
                                      std::string description,
                                      std::vector<std::size_t> shape,
                                      T initial_value = T{},
                                      std::size_t width_bits = sizeof(T) * 8) {
        return state_array_registry_.create_array<T>(
            std::move(name),
            std::move(description),
            std::move(shape),
            std::move(initial_value),
            width_bits);
    }

    // 按名字获取一个由 simulator 持有的可写单状态。
    template <typename T>
    State<T>& state(std::string_view name) {
        return *state_set_.find_typed_mutable<T>(name);
    }

    // 按名字获取一个由 simulator 持有的只读单状态。
    template <typename T>
    const State<T>& state(std::string_view name) const {
        return *state_set_.find_typed<T>(name);
    }

    // 按名字获取一个由 simulator 持有的可写数组/高维状态。
    template <typename T>
    StateArray<T>& state_array(std::string_view name) {
        return *state_array_registry_.find_typed_mutable<T>(name);
    }

    // 按名字获取一个由 simulator 持有的只读数组/高维状态。
    template <typename T>
    const StateArray<T>& state_array(std::string_view name) const {
        return *state_array_registry_.find_typed<T>(name);
    }

  private:
    // 发射当前 simulator 全部端口组中的输出端口。
    void emit_outputs();

    // 自动模式下按阶段采集 simulator 快照。
    void capture_snapshot_if_automatic(SnapshotCaptureStage stage);

    // 开始一个自动采集 segment。
    void begin_snapshot_capture_segment();

    // 结束当前自动采集 segment，并写入末尾 checkpoint / manifest。
    void finish_snapshot_capture_segment();

    // 自动/手动采集开启时，把记录写入本地文件。
    void store_snapshot_capture_record(CycleSimulatorSnapshotRecord& record);

    // 当前 simulator 的逻辑名称。
    std::string name_ = "cycle_simulator";

    // 当前 simulator 的说明。
    std::string description_;

    // 当前 simulator 的显示别名列表。
    std::vector<std::string> name_aliases_;

    // 当前注册的所有 kernel。
    std::vector<std::shared_ptr<Kernel>> kernels_;

    // simulator 自身的状态收束表。
    StateSet state_set_;

    // simulator 自身的数组状态收束表。
    StateArrayRegistry state_array_registry_;

    // port_groups_[0] 永远是默认普通端口组，其余元素按创建顺序追加。
    std::vector<std::unique_ptr<PortGroup>> port_groups_;

    // 下一次 step() 将要使用的周期号。
    std::uint64_t current_cycle_ = 0;

    // 最大运行周期上限。
    std::uint64_t max_cycles_ = 0;

    // 周期频率，单位 Hz。
    long double frequency_hz_ = 1.0L;

    // 当前模拟器是否已经结束。
    bool finished_ = false;

    // simulator 快照采集模式。
    SnapshotCaptureMode snapshot_capture_mode_ = SnapshotCaptureMode::Manual;

    // simulator 快照采集是否开启。
    bool snapshot_capture_active_ = false;

    // 当前 simulator 快照采集序号。
    std::uint64_t snapshot_capture_sequence_ = 0;

    // 当前 simulator 快照采集历史。
    std::vector<CycleSimulatorSnapshotRecord> snapshot_history_;

    // 快照/波形采集根目录。
    std::string snapshot_capture_root_directory_ = default_snapshot_capture_directory();

    // 当前自动采集 segment 目录。
    std::string snapshot_capture_segment_directory_;

    // 当前自动采集 segment 的 waveform.jsonl 路径。
    std::string snapshot_capture_waveform_path_;

    // 下一个自动采集 segment 序号。
    std::uint64_t snapshot_capture_segment_index_ = 0;

    // 当前自动采集 segment 已写入帧数。
    std::uint64_t snapshot_capture_segment_frame_count_ = 0;

    // 当前自动采集 segment 是否已开启。
    bool snapshot_capture_segment_active_ = false;

    // 当前自动采集 segment 的首尾记录位置。
    std::size_t snapshot_capture_segment_first_record_index_ = 0;
    std::size_t snapshot_capture_segment_last_record_index_ = 0;
};

}  // namespace project_xs::sim

#endif
