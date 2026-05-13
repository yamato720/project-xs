#ifndef PROJECT_XS_BASE_KERNEL_COMPONENT_H
#define PROJECT_XS_BASE_KERNEL_COMPONENT_H

#include "base/PortGroup.h"
#include "base/State.h"
#include "base/StateArray.h"
#include "base/Error.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace project_xs::sim {

class Kernel;
class StateArrayBase;

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
    // 组件运行时快照。
    using Snapshot = KernelComponentSnapshot;

    // 构造一个组件。
    // - name: 组件逻辑名
    // - latency: 相位窗口长度
    // - first_delay_align: 首次进入正常相位前需要额外等待的拍数
    explicit KernelComponent(std::string name,
                             std::uint64_t latency = 0,
                             std::uint64_t first_delay_align = 0);
    // 虚析构，保证通过基类指针释放派生组件时安全。
    virtual ~KernelComponent() = default;

    // 返回组件名称。
    const std::string& name() const { return name_; }

    // 返回组件说明。波形查看器悬停时会优先展示它。
    const std::string& description() const { return description_; }

    // 设置组件说明。
    void set_description(std::string description) { description_ = std::move(description); }

    // 追加一个组件显示别名，用于波形查看器切换显示名。
    void add_name_alias(std::string alias) { name_aliases_.push_back(std::move(alias)); }

    // 返回组件显示别名。
    const std::vector<std::string>& name_aliases() const { return name_aliases_; }

    // 返回组件的周期窗口长度。
    std::uint64_t latency() const { return latency_; }

    // 返回组件当前记录的父级周期号。
    // 该值由 run() 自动更新，用户不需要手写状态保存它。
    std::uint64_t current_cycle() const { return current_cycle_; }

    // 返回组件当前运行频率，单位 Hz。
    // 当组件加入 kernel、kernel 加入 simulator 时会自动从父级递归赋值。
    long double frequency_hz() const { return frequency_hz_; }

    // 返回首次对齐延时。
    std::uint64_t first_delay_align() const { return first_delay_align_; }

    // 返回组件自带的默认普通端口组。
    // 这个端口组只承载无协议端口，用来给组件内部自由组合多输入/多输出。
    PortGroup& ports() { return *port_groups_.front(); }

    // 返回组件自带的只读默认普通端口组。
    const PortGroup& ports() const { return *port_groups_.front(); }

    // 返回组件自带的只读单状态表。
    const StateSet& state_set() const { return state_set_; }

    // 返回组件自带的只读数组状态表。
    const StateArrayRegistry& state_array_registry() const { return state_array_registry_; }

    // 创建并持有一个附加端口组。
    // 默认组永远是 port_groups_[0]；新创建的组会追加到后面。
    PortGroup& create_port_group(std::string name);

    // 返回当前组件持有的全部端口组。
    // 这个 vector 保留顺序语义，同时支持按名字查找。
    const std::vector<std::unique_ptr<PortGroup>>& port_groups() const { return port_groups_; }

    // 按名字查找/获取附属端口组。
    // find 找不到时返回空指针；get 找不到时抛异常。
    // 这组接口是组件层面对 vector<PortGroup> 的统一命名访问口。
    PortGroup* find_port_group(std::string_view name);

    // 只读按名字查找一个附属端口组。
    const PortGroup* find_port_group(std::string_view name) const;

    // 按名字获取一个附属端口组；找不到时报错。
    PortGroup& get_port_group(std::string_view name);

    // 只读按名字获取一个附属端口组；找不到时报错。
    const PortGroup& get_port_group(std::string_view name) const;

    // 返回指定附属端口组的摘要信息。
    std::string port_group_info(std::string_view name,
                                PortValueBase base = PortValueBase::Decimal) const;

    // 返回当前组件全部附属端口组的摘要信息。
    std::string all_port_groups_info(PortValueBase base = PortValueBase::Decimal) const;

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

    // 保存当前组件运行态。
    Snapshot snapshot() const;

    // 恢复当前组件运行态；目标组件必须已经按同构结构构造完成。
    void restore(const Snapshot& snapshot);

    // 把当前组件 checkpoint 保存到本地文件。
    void save_checkpoint(const std::string& path) const;

    // 从本地 checkpoint 恢复当前组件运行态。
    void restore_checkpoint(const std::string& path);

    // 设置快照/波形采集根目录。
    // 自动挡会在该目录下创建 segment，手动挡会在该目录下写 checkpoint。
    void set_snapshot_capture_directory(std::string directory);

    // 返回当前快照/波形采集根目录。
    const std::string& snapshot_capture_directory() const {
        return snapshot_capture_root_directory_;
    }

    // 开始记录组件快照。Manual 只响应显式 capture_snapshot()；
    // Automatic 会在当前组件 run/end_cycle 的关键阶段自动采样。
    void start_snapshot_capture(SnapshotCaptureMode mode = SnapshotCaptureMode::Automatic);

    // 停止记录组件快照。
    void stop_snapshot_capture();

    // 返回当前组件快照采集是否处于开启状态。
    bool snapshot_capture_active() const { return snapshot_capture_active_; }

    // 返回当前组件快照采集模式。
    SnapshotCaptureMode snapshot_capture_mode() const { return snapshot_capture_mode_; }

    // 手动采集一条组件快照。可在派生类任意阶段调用。
    const KernelComponentSnapshotRecord& capture_snapshot(
        SnapshotCaptureStage stage = SnapshotCaptureStage::Manual);

    // 返回当前组件已采集的快照历史。
    const std::vector<KernelComponentSnapshotRecord>& snapshot_history() const {
        return snapshot_history_;
    }

    // 清空当前组件快照历史。
    void clear_snapshot_history();

    // 返回当前组件的可读状态摘要。
    // 输出会聚合组件相位信息、自身状态表和全部 portgroup。
    virtual std::string info() const;

    // 收集当前组件的结构化诊断。
    virtual void collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const;

    // 校验当前组件是否合法，并返回诊断列表。
    std::vector<error::Diagnostic> validate() const;

    // 校验当前组件是否合法；若存在 Error 直接抛出第一条。
    void validate_or_throw() const;

  protected:
    // 组件单拍业务逻辑入口。
    // 派生类一般在这里写“本拍该干什么”。
    virtual void run_single(std::uint64_t cycle);

    // 返回当前组件该拍的调试输出文本；默认返回空串。
    virtual std::string debug_info(std::uint64_t cycle) const;

    // 所有输出发射完之后的钩子。
    virtual void after_outputs_emitted(std::uint64_t cycle);

    // 当前组件是否已经完成首对齐，可以进入正常相位流转。
    bool phase_valid() const { return align_remaining_ == 0; }

    // 当前所在的相位编号。
    // 只有 phase_valid() 为 true 时，这个值才具有实际意义。
    std::uint64_t phase() const { return phase_; }

    // 通过组件基类统一创建并持有一个单状态。
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

    // 通过组件基类统一创建并持有一个数组/高维状态。
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

    // 按名字获取一个由组件持有的可写单状态。
    template <typename T>
    State<T>& state(std::string_view name) {
        return *state_set_.find_typed_mutable<T>(name);
    }

    // 按名字获取一个由组件持有的只读单状态。
    template <typename T>
    const State<T>& state(std::string_view name) const {
        return *state_set_.find_typed<T>(name);
    }

    // 按名字获取一个由组件持有的可写数组/高维状态。
    template <typename T>
    StateArray<T>& state_array(std::string_view name) {
        return *state_array_registry_.find_typed_mutable<T>(name);
    }

    // 按名字获取一个由组件持有的只读数组/高维状态。
    template <typename T>
    const StateArray<T>& state_array(std::string_view name) const {
        return *state_array_registry_.find_typed<T>(name);
    }

    // 非端口组类的额外状态钩子。
    virtual void reset_extra();

    // 非端口组类的附加“可见 0 初始化”钩子。
    virtual void initialize_zero_extra();

    // 非端口组类的拍末附加钩子。
    virtual void end_cycle_extra();

    // 声明某个数组状态的 shape 必须匹配预期。
    // 组件内部状态应由组件自己声明约束，避免顶层 kernel 通过 friend 访问内部对象。
    void require_state_array_shape(const StateArrayBase& array,
                                   std::vector<std::size_t> expected_shape,
                                   std::string label = "");

  private:
    friend class Kernel;

    struct RequiredArrayShape {
        const StateArrayBase* array = nullptr;
        std::vector<std::size_t> expected_shape;
        std::string label;
    };

    // 由父级 kernel 递归下发运行频率。
    void set_frequency_hz_from_parent(long double frequency_hz);

    // 由 run()/restore() 自动维护组件当前周期。
    void set_current_cycle_from_parent(std::uint64_t cycle);

    // 把一段文本安全输出到指定流；自动补换行。
    void write_text(std::ostream& os, const std::string& text) const;

    // 输出当前拍的组件调试文本。
    void write_debug(std::ostream& os, std::uint64_t cycle) const;

    // 发射当前组件全部端口组中的输出端口。
    void emit_outputs();

    // 推进组件内部相位状态。
    // 先消耗首对齐拍数，之后在 [0, latency) 内循环。
    void advance_phase();

    // 自动模式下按阶段采集组件快照。
    void capture_snapshot_if_automatic(SnapshotCaptureStage stage);

    // 开始一个自动采集 segment。
    void begin_snapshot_capture_segment();

    // 结束当前自动采集 segment，并写入末尾 checkpoint / manifest。
    void finish_snapshot_capture_segment();

    // 自动/手动采集开启时，把记录写入本地文件。
    void store_snapshot_capture_record(KernelComponentSnapshotRecord& record);

    // 组件名称。
    std::string name_;

    // 组件说明。
    std::string description_;

    // 组件显示别名列表。
    std::vector<std::string> name_aliases_;

    // 周期窗口长度。
    std::uint64_t latency_ = 0;

    // 首次进入正常相位前的额外等待拍数。
    std::uint64_t first_delay_align_ = 0;

    // 当前还剩多少拍首对齐尚未完成。
    std::uint64_t align_remaining_ = 0;

    // 当前处于哪个相位。
    std::uint64_t phase_ = 0;

    // 当前组件所处的父级周期号。
    std::uint64_t current_cycle_ = 0;

    // 当前组件运行频率，单位 Hz。
    long double frequency_hz_ = 1.0L;

    // 组件自身的状态收束表。
    StateSet state_set_;

    // 组件自身的数组状态收束表。
    StateArrayRegistry state_array_registry_;

    // port_groups_[0] 永远是默认普通端口组，其余元素按创建顺序追加。
    std::vector<std::unique_ptr<PortGroup>> port_groups_;

    // 当前组件声明要求必须满足 shape 的数组状态。
    std::vector<RequiredArrayShape> required_array_shapes_;

    // 组件快照采集模式。
    SnapshotCaptureMode snapshot_capture_mode_ = SnapshotCaptureMode::Manual;

    // 组件快照采集是否开启。
    bool snapshot_capture_active_ = false;

    // 当前组件快照采集序号。
    std::uint64_t snapshot_capture_sequence_ = 0;

    // 当前组件快照采集历史。
    std::vector<KernelComponentSnapshotRecord> snapshot_history_;

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
