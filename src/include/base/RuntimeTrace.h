#ifndef PROJECT_XS_BASE_RUNTIME_TRACE_H
#define PROJECT_XS_BASE_RUNTIME_TRACE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace project_xs::sim {

class CycleSimulator;
class Kernel;
class KernelComponent;
class SimulationSession;

// 运行态记录公共定义。
//
// 这里把两类容易混在一起的能力集中描述：
// - Snapshot：某一拍的完整运行态，用于 restore 回放；它必须和同构对象图配套使用。
// - Waveform：连续多拍的轻量采样，用于后续导出 CSV/JSON/VCD 或画波形；它不负责 restore。
//
// 也就是说：
// - snapshot/restore 面向“回到某一拍继续仿真”，可从 component 局部一路覆盖到 session 调度层
// - waveform trace 面向“记录每拍信号变化供观察”

// 端口方向。
// 一个端口实例在构造完成后方向固定。
enum class PortDirection {
    Input,
    Output,
};

// 端口值输出格式。
// - Decimal: 按数值十进制输出
// - Binary / Octal / Hexadecimal: 按端口位宽输出位模式
enum class PortValueBase {
    Decimal,
    Binary,
    Octal,
    Hexadecimal,
};

// 快照采集模式。
// - Manual: 只在调用 capture_snapshot(...) 时记录
// - Automatic: 在对象自己的关键执行阶段自动记录
enum class SnapshotCaptureMode {
    Manual,
    Automatic,
};

// 单个正在进行的快照/波形采集上下文。
// 同一对象允许多个上下文按栈嵌套开启；stop 时必须匹配栈顶 name。
struct SnapshotCaptureContext {
    std::string name;
    SnapshotCaptureMode mode = SnapshotCaptureMode::Manual;
    std::string segment_directory;
    std::string waveform_path;
    std::uint64_t segment_index = 0;
    std::uint64_t frame_count = 0;
    bool segment_active = false;
    std::size_t first_record_index = 0;
    std::size_t last_record_index = 0;
};

// 快照采集阶段。
// Manual 可由调用者自由标记；其他阶段由对应层级在 step/run/end_cycle 中自动打点。
enum class SnapshotCaptureStage {
    Manual,
    AutomaticSegmentBegin,
    AutomaticSegmentEnd,
    SessionStepBegin,
    SessionAfterDispatch,
    SessionStepEnd,
    CycleStepBegin,
    CycleAfterInputSync,
    CycleAfterRunSingle,
    CycleAfterEmitOutputs,
    CycleAfterKernelRun,
    CycleAfterPortGroupEndCycle,
    CycleAfterKernelEndCycle,
    CycleStepEnd,
    KernelRunBegin,
    KernelAfterInputSync,
    KernelAfterRunSingle,
    KernelAfterEmitOutputs,
    KernelRunEnd,
    KernelEndCycleEnd,
    ComponentRunBegin,
    ComponentAfterInputSync,
    ComponentAfterRunSingle,
    ComponentAfterEmitOutputs,
    ComponentRunEnd,
    ComponentEndCycleEnd,
};

// 单个端口的运行时快照。
// 只保存运行态，不保存连接拓扑；恢复目标必须已经按同样结构构造并连线。
struct PortSnapshot {
    std::string name;
    PortDirection direction = PortDirection::Input;
    std::size_t width_bits = 0;
    std::size_t data_size = 0;
    std::type_index type_index{typeid(void)};
    std::vector<std::byte> visible_storage;
    std::vector<std::byte> pending_storage;
    std::vector<std::byte> bound_storage;
    bool valid = false;
    bool pending_valid = false;
};

// 端口组运行时快照。
struct PortGroupSnapshot {
    std::string name;
    std::vector<PortSnapshot> inputs;
    std::vector<PortSnapshot> outputs;
};

// 单状态运行时快照。
struct StateSnapshot {
    std::string name;
    std::string type_name;
    std::type_index type_index{typeid(void)};
    std::size_t data_size = 0;
    std::size_t width_bits = 0;
    std::vector<std::byte> value_storage;
};

// 单状态目录运行时快照。
struct StateSetSnapshot {
    std::vector<StateSnapshot> states;
};

// 单个数组状态运行时快照。
struct StateArraySnapshot {
    std::string name;
    std::string type_name;
    std::type_index type_index{typeid(void)};
    std::size_t data_size = 0;
    std::size_t width_bits = 0;
    std::vector<std::size_t> shape;
    std::vector<std::byte> value_storage;
};

// 数组状态目录运行时快照。
struct StateArrayRegistrySnapshot {
    std::vector<StateArraySnapshot> arrays;
};

// 组件运行时快照。
struct KernelComponentSnapshot {
    std::string name;
    std::uint64_t current_cycle = 0;
    long double frequency_hz = 1.0L;
    std::uint64_t align_remaining = 0;
    std::uint64_t phase = 0;
    StateSetSnapshot states;
    StateArrayRegistrySnapshot state_arrays;
    std::vector<PortGroupSnapshot> port_groups;
};

// 组件快照采集记录。
struct KernelComponentSnapshotRecord {
    std::uint64_t sequence = 0;
    SnapshotCaptureStage stage = SnapshotCaptureStage::Manual;
    std::string checkpoint_path;
    std::string checkpoint_role;
    KernelComponentSnapshot snapshot;
};

// Kernel 运行时快照。
struct KernelSnapshot {
    std::string name;
    std::uint64_t current_cycle = 0;
    long double frequency_hz = 1.0L;
    std::uint64_t elapsed_cycles = 0;
    bool terminate_requested = false;
    StateSetSnapshot states;
    StateArrayRegistrySnapshot state_arrays;
    std::vector<PortGroupSnapshot> port_groups;
    std::vector<KernelComponentSnapshot> components;
};

// Kernel 快照采集记录。
struct KernelSnapshotRecord {
    std::uint64_t sequence = 0;
    SnapshotCaptureStage stage = SnapshotCaptureStage::Manual;
    std::string checkpoint_path;
    std::string checkpoint_role;
    KernelSnapshot snapshot;
};

// 周期模拟器运行时快照。
struct CycleSimulatorSnapshot {
    std::string name;
    std::uint64_t current_cycle = 0;
    std::uint64_t max_cycles = 0;
    long double frequency_hz = 1.0L;
    bool finished = false;
    StateSetSnapshot states;
    StateArrayRegistrySnapshot state_arrays;
    std::vector<PortGroupSnapshot> port_groups;
    std::vector<KernelSnapshot> kernels;
};

// 周期模拟器快照采集记录。
struct CycleSimulatorSnapshotRecord {
    std::uint64_t sequence = 0;
    SnapshotCaptureStage stage = SnapshotCaptureStage::Manual;
    std::string checkpoint_path;
    std::string checkpoint_role;
    CycleSimulatorSnapshot snapshot;
};

// session 内单个被调度 simulator 的运行时快照。
struct ScheduledSimulatorSnapshot {
    std::string name;
    long double accumulated_hz = 0.0L;
    CycleSimulatorSnapshot simulator;
};

// 仿真会话运行时快照。
struct SimulationSessionSnapshot {
    long double frequency_hz = 1.0L;
    long double run_time_seconds = 0.0L;
    std::uint64_t current_tick = 0;
    bool finished = false;
    std::vector<ScheduledSimulatorSnapshot> simulators;
};

// 仿真会话快照采集记录。
struct SimulationSessionSnapshotRecord {
    std::uint64_t sequence = 0;
    SnapshotCaptureStage stage = SnapshotCaptureStage::Manual;
    std::string checkpoint_path;
    std::string checkpoint_role;
    SimulationSessionSnapshot snapshot;
};

// 波形采样对象种类。
// 第一版只做通用分类，后续导出 VCD 时可继续扩展映射规则。
enum class WaveformSignalKind {
    State,
    StateArrayElement,
    PortInput,
    PortOutput,
    RuntimeCycle,
};

// 单个信号在某一拍的采样值。
struct WaveformSignalSample {
    std::string scope;
    std::string name;
    WaveformSignalKind kind = WaveformSignalKind::State;
    std::string type_name;
    std::size_t width_bits = 0;
    std::string value;
    bool valid = true;
    std::vector<std::string> name_path;
    std::vector<std::vector<std::string>> name_options_path;
    std::vector<std::string> description_path;
    std::vector<std::string> timing_kind_path;
    std::vector<long double> timing_frequency_hz_path;
    std::vector<std::uint64_t> timing_cycle_path;
};

// 某一拍的全部波形采样。
struct WaveformFrame {
    std::uint64_t cycle = 0;
    std::vector<WaveformSignalSample> signals;
};

// 一段连续仿真的波形记录。
struct WaveformTrace {
    std::string name;
    std::vector<WaveformFrame> frames;

    bool empty() const { return frames.empty(); }
    void clear() { frames.clear(); }
};

// 采样一个 simulator 当前可见状态，生成单帧波形数据。
// 这个函数只观察公开状态，不改变仿真状态；适合在每次 step() 后调用。
WaveformFrame capture_waveform_frame(const CycleSimulator& simulator,
                                     std::uint64_t cycle,
                                     PortValueBase base = PortValueBase::Decimal);

// 采样一个 simulator 当前可见状态，cycle 默认使用 simulator.current_cycle()。
WaveformFrame capture_waveform_frame(const CycleSimulator& simulator,
                                     PortValueBase base = PortValueBase::Decimal);

// 把当前 simulator 状态追加到一段波形记录中。
void append_waveform_frame(WaveformTrace& trace,
                           const CycleSimulator& simulator,
                           PortValueBase base = PortValueBase::Decimal);

// 快照/波形采集文件格式辅助接口。
// 自动挡会把连续波形写入一个 segment 目录下的 waveform.jsonl；
// segment 的首尾 checkpoint 使用二进制保存完整运行态，可用于 restore。
const char* snapshot_capture_mode_name(SnapshotCaptureMode mode);
const char* snapshot_capture_stage_name(SnapshotCaptureStage stage);
const char* waveform_signal_kind_name(WaveformSignalKind kind);

// 默认采集目录。运行时无法可靠知道源文件 main.cpp 所在目录，
// 因此这里使用当前工作目录下的 trace；示例 main 可按需覆盖到源码同级目录。
std::string default_snapshot_capture_directory();

// 采集根目录固定命名为 trace。传入 trace 自身时保持不变，传入父目录时追加 trace。
std::string normalize_snapshot_capture_directory(const std::string& directory);

// 创建并返回某个自动采集 segment 的目录。
std::string prepare_snapshot_capture_segment_directory(const std::string& root_directory,
                                                       const std::string& object_kind,
                                                       const std::string& object_name,
                                                       std::uint64_t start_cycle,
                                                       std::uint64_t segment_index,
                                                       const std::string& capture_name = {});

// 返回手动 checkpoint 文件路径，并确保目录存在。
std::string prepare_manual_checkpoint_path(const std::string& root_directory,
                                           const std::string& object_kind,
                                           const std::string& object_name,
                                           std::uint64_t sequence,
                                           const std::string& capture_name = {});

std::string snapshot_capture_waveform_path(const std::string& segment_directory);
std::string snapshot_capture_first_checkpoint_path(const std::string& segment_directory);
std::string snapshot_capture_last_checkpoint_path(const std::string& segment_directory);

void write_snapshot_capture_manifest(const std::string& segment_directory,
                                     const std::string& object_kind,
                                     const std::string& object_name,
                                     std::uint64_t segment_index,
                                     std::uint64_t frame_count,
                                     bool closed,
                                     const std::string& capture_name = {});

// 调用 Python 渲染器，把自动采集 segment 转成自包含 waveform.html。
void render_snapshot_capture_html(const std::string& segment_directory);

void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const KernelComponent& component,
                                 PortValueBase base = PortValueBase::Decimal);
void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const Kernel& kernel,
                                 PortValueBase base = PortValueBase::Decimal);
void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const CycleSimulator& simulator,
                                 PortValueBase base = PortValueBase::Decimal);
void append_waveform_jsonl_frame(const std::string& path,
                                 std::uint64_t sequence,
                                 SnapshotCaptureStage stage,
                                 const SimulationSession& session,
                                 PortValueBase base = PortValueBase::Decimal);

void save_checkpoint(const std::string& path, const KernelComponentSnapshot& snapshot);
void save_checkpoint(const std::string& path, const KernelSnapshot& snapshot);
void save_checkpoint(const std::string& path, const CycleSimulatorSnapshot& snapshot);
void save_checkpoint(const std::string& path, const SimulationSessionSnapshot& snapshot);

KernelComponentSnapshot load_kernel_component_checkpoint(
    const std::string& path,
    const KernelComponentSnapshot& layout);
KernelSnapshot load_kernel_checkpoint(const std::string& path,
                                      const KernelSnapshot& layout);
CycleSimulatorSnapshot load_cycle_simulator_checkpoint(
    const std::string& path,
    const CycleSimulatorSnapshot& layout);
SimulationSessionSnapshot load_simulation_session_checkpoint(
    const std::string& path,
    const SimulationSessionSnapshot& layout);

}  // namespace project_xs::sim

#endif
