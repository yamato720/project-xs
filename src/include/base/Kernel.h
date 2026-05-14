#ifndef PROJECT_XS_BASE_KERNEL_H
#define PROJECT_XS_BASE_KERNEL_H

#include "base/KernelComponent.h"
#include "base/PortGroup.h"
#include "base/State.h"
#include "base/StateArray.h"
#include "base/Error.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace project_xs::sim {

class CycleSimulator;
class StateArrayBase;

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
    // Kernel 运行时快照。
    using Snapshot = KernelSnapshot;

    // 构造一个 kernel。
    // - name: kernel 名称
    // - latency: kernel 自身的周期窗口长度
    explicit Kernel(std::string name, std::uint64_t latency = 0);

    // 虚析构，保证通过基类指针释放派生 kernel 时安全。
    virtual ~Kernel() = default;

    // 返回 kernel 名称。
    const std::string& name() const { return name_; }

    // 返回 kernel 说明。波形查看器悬停时会优先展示它。
    const std::string& description() const { return description_; }

    // 设置 kernel 说明。
    void set_description(std::string description) { description_ = std::move(description); }

    // 追加一个 kernel 显示别名，用于波形查看器切换显示名。
    void add_name_alias(std::string alias) { name_aliases_.push_back(std::move(alias)); }

    // 返回 kernel 显示别名。
    const std::vector<std::string>& name_aliases() const { return name_aliases_; }

    // 返回 kernel 自身的周期窗口长度。
    std::uint64_t latency() const { return latency_; }

    // 返回 kernel 当前记录的父级 simulator 周期号。
    // 该值由 run() 自动更新，用户不需要手写状态保存它。
    std::uint64_t current_cycle() const { return current_cycle_; }

    // 返回 kernel 当前运行频率，单位 Hz。
    // 当 kernel 加入 simulator 时会自动递归赋给 kernel 及其组件。
    long double frequency_hz() const { return frequency_hz_; }

    // 返回 kernel 自带的默认普通端口组。
    // 这个端口组只承载无协议端口，适合作为统一的多输入/多输出插槽。
    PortGroup& ports() { return *port_groups_.front(); }

    // 返回 kernel 自带的只读默认普通端口组。
    const PortGroup& ports() const { return *port_groups_.front(); }

    // 返回 kernel 自带的只读单状态表。
    const StateSet& state_set() const { return state_set_; }

    // 返回 kernel 自带的只读数组状态表。
    const StateArrayRegistry& state_array_registry() const { return state_array_registry_; }

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

    // 保存当前 kernel 运行态。
    Snapshot snapshot() const;

    // 恢复当前 kernel 运行态；目标 kernel 必须已经按同构结构构造完成。
    void restore(const Snapshot& snapshot);

    // 把当前 kernel checkpoint 保存到本地文件。
    void save_checkpoint(const std::string& path) const;

    // 从本地 checkpoint 恢复当前 kernel 运行态。
    void restore_checkpoint(const std::string& path);

    // 设置快照/波形采集根目录。
    void set_snapshot_capture_directory(std::string directory);

    // 返回当前快照/波形采集根目录。
    const std::string& snapshot_capture_directory() const {
        return snapshot_capture_root_directory_;
    }

    // 开始记录 kernel 快照。capture_name 非空时会替代自动段目录里时间戳前的默认前缀。
    // Manual 只响应显式 capture_snapshot()；
    // Automatic 会在当前 kernel run/end_cycle 的关键阶段自动采样。
    void start_snapshot_capture(SnapshotCaptureMode mode = SnapshotCaptureMode::Automatic,
                                std::string capture_name = {});

    // 便捷形式：默认以 Automatic 开启命名采集。
    void start_snapshot_capture(std::string capture_name) {
        start_snapshot_capture(SnapshotCaptureMode::Automatic, std::move(capture_name));
    }

    // 停止记录 kernel 快照。capture_name 必须严格匹配最后开启的采集名。
    void stop_snapshot_capture(std::string capture_name = {});

    // 返回当前 kernel 快照采集是否处于开启状态。
    bool snapshot_capture_active() const { return !snapshot_capture_contexts_.empty(); }

    // 返回最后开启的 kernel 快照采集模式。
    SnapshotCaptureMode snapshot_capture_mode() const {
        return snapshot_capture_contexts_.empty()
            ? SnapshotCaptureMode::Manual
            : snapshot_capture_contexts_.back().mode;
    }

    // 手动采集一条 kernel 快照。可在派生类任意阶段调用。
    const KernelSnapshotRecord& capture_snapshot(
        SnapshotCaptureStage stage = SnapshotCaptureStage::Manual);

    // 返回当前 kernel 已采集的快照历史。
    const std::vector<KernelSnapshotRecord>& snapshot_history() const {
        return snapshot_history_;
    }

    // 清空当前 kernel 快照历史。
    void clear_snapshot_history();

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

    // 收集当前 kernel 的结构化诊断。
    virtual void collect_diagnostics(std::vector<error::Diagnostic>& diagnostics) const;

    // 校验当前 kernel 合法性，并返回诊断列表。
    std::vector<error::Diagnostic> validate() const;

    // 校验当前 kernel 合法性；若存在 Error 直接抛出第一条。
    void validate_or_throw() const;

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

    // 通过 kernel 基类统一创建并持有一个单状态。
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

    // 通过 kernel 基类统一创建并持有一个数组/高维状态。
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

    // 按名字获取一个由 kernel 持有的可写单状态。
    template <typename T>
    State<T>& state(std::string_view name) {
        return *state_set_.find_typed_mutable<T>(name);
    }

    // 按名字获取一个由 kernel 持有的只读单状态。
    template <typename T>
    const State<T>& state(std::string_view name) const {
        return *state_set_.find_typed<T>(name);
    }

    // 按名字获取一个由 kernel 持有的可写数组/高维状态。
    template <typename T>
    StateArray<T>& state_array(std::string_view name) {
        return *state_array_registry_.find_typed_mutable<T>(name);
    }

    // 按名字获取一个由 kernel 持有的只读数组/高维状态。
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

    // 声明某个子组件必须存在。
    void require_component(std::string name);

    // 声明某个端口组必须存在。
    void require_port_group(std::string name);

    // 声明某个数组状态的 shape 必须匹配预期。
    void require_state_array_shape(const StateArrayBase& array,
                                   std::vector<std::size_t> expected_shape,
                                   std::string label = "");

  private:
    friend class CycleSimulator;

    struct RequiredArrayShape {
        const StateArrayBase* array = nullptr;
        std::vector<std::size_t> expected_shape;
        std::string label;
    };

    // 由父级 simulator 递归下发运行频率。
    void set_frequency_hz_from_parent(long double frequency_hz);

    // 由 run()/restore() 自动维护 kernel 当前周期。
    void set_current_cycle_from_parent(std::uint64_t cycle);

    // 把一段文本安全输出到指定流；自动补换行。
    void write_text(std::ostream& os, const std::string& text) const;

    // 输出当前拍的调试文本。
    void write_debug(std::ostream& os, std::uint64_t cycle) const;

    // 发射当前 kernel 全部端口组中的输出端口。
    void emit_outputs();

    // 推进 kernel 自身的周期事件。
    void handle_latency_event(std::uint64_t cycle);

    // 自动模式下按阶段采集 kernel 快照。
    void capture_snapshot_if_automatic(SnapshotCaptureStage stage);

    // 开始一个自动采集 segment。
    void begin_snapshot_capture_segment(SnapshotCaptureContext& context);

    // 结束指定自动采集 segment，并写入末尾 checkpoint / manifest。
    void finish_snapshot_capture_segment(SnapshotCaptureContext& context);

    // 自动/手动采集开启时，把记录写入本地文件。
    void store_snapshot_capture_record(KernelSnapshotRecord& record);

    // 当前 kernel 内部挂接的所有子组件。
    std::vector<std::shared_ptr<KernelComponent>> components_;

    // kernel 名称。
    std::string name_;

    // kernel 说明。
    std::string description_;

    // kernel 显示别名列表。
    std::vector<std::string> name_aliases_;

    // kernel 自身周期窗口长度。
    std::uint64_t latency_ = 0;

    // kernel 自身已经推进了多少拍。
    std::uint64_t elapsed_cycles_ = 0;

    // 当前 kernel 所处的父级 simulator 周期号。
    std::uint64_t current_cycle_ = 0;

    // 当前 kernel 运行频率，单位 Hz。
    long double frequency_hz_ = 1.0L;

    // 是否请求终止整个模拟器。
    bool terminate_requested_ = false;

    // kernel 自身的状态收束表。
    StateSet state_set_;

    // kernel 自身的数组状态收束表。
    StateArrayRegistry state_array_registry_;

    // port_groups_[0] 永远是默认普通端口组，其余元素按创建顺序追加。
    std::vector<std::unique_ptr<PortGroup>> port_groups_;

    // 当前 kernel 声明要求必须存在的子组件名。
    std::vector<std::string> required_component_names_;

    // 当前 kernel 声明要求必须存在的端口组名。
    std::vector<std::string> required_port_group_names_;

    // 当前 kernel 声明要求必须满足 shape 的数组状态。
    std::vector<RequiredArrayShape> required_array_shapes_;

    // 当前 kernel 快照采集序号。
    std::uint64_t snapshot_capture_sequence_ = 0;

    // 当前 kernel 快照采集历史。
    std::vector<KernelSnapshotRecord> snapshot_history_;

    // 快照/波形采集根目录。
    std::string snapshot_capture_root_directory_ = default_snapshot_capture_directory();

    // 下一个自动采集 segment 序号。
    std::uint64_t snapshot_capture_segment_index_ = 0;

    // 当前正在进行的快照/波形采集上下文栈。
    std::vector<SnapshotCaptureContext> snapshot_capture_contexts_;

};

}  // namespace project_xs::sim

#endif
