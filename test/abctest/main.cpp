#include "base/CycleSimulator.h"
#include "base/Error.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/PortGroup.h"
#include "base/RuntimeTrace.h"
#include "base/SimulationSession.h"
#include "base/State.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

class DemoCycleSimulator final : public project_xs::sim::CycleSimulator {
  public:
    DemoCycleSimulator(std::string name, std::int64_t max_cycles, long double frequency_hz)
        : project_xs::sim::CycleSimulator(std::move(name), max_cycles, frequency_hz) {
        create_state<std::uint64_t>("source_out", "simulator 级输入源输出", 0);
        // 以后如果要接 HBM / AXI / 其他跨周期环境交互，这里就是 simulator 级默认接口入口。
        ports().add_output(state<std::uint64_t>("source_out").make_wire_output_port());
    }

  private:
    void reset_extra() override {
        state<std::uint64_t>("source_out") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        state<std::uint64_t>("source_out") = cycle;
    }
};

class KernelAComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelAComponent()
        : project_xs::sim::KernelComponent("kernel_a", 1) {
        create_state<std::uint64_t>("in", "A 组件输入", 0);
        create_state<std::uint64_t>("out", "A 组件输出", 0);
        ports().add_input(state<std::uint64_t>("in").make_wire_input_port());
        ports().add_output(state<std::uint64_t>("out").make_wire_output_port());
    }

  protected:
    void reset_extra() override {
        state<std::uint64_t>("in") = 0;
        state<std::uint64_t>("out") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        state<std::uint64_t>("out") = state<std::uint64_t>("in").value();
    }
};

class KernelBComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelBComponent()
        : project_xs::sim::KernelComponent("kernel_b", 2) {
        create_state<std::uint64_t>("in", "B 组件输入", 0);
        create_state<std::uint64_t>("captured", "B 组件拍内暂存", 0);
        create_state<std::uint64_t>("out", "B 组件输出", 0);
        ports().add_input(state<std::uint64_t>("in").make_wire_input_port());
        ports().add_output(state<std::uint64_t>("out").make_wire_output_port());
    }

  protected:
    void reset_extra() override {
        state<std::uint64_t>("in") = 0;
        state<std::uint64_t>("captured") = 0;
        state<std::uint64_t>("out") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        if (phase() == 0) {
            state<std::uint64_t>("captured") = state<std::uint64_t>("in").value();
            return;
        }

        if (phase() == 1) {
            state<std::uint64_t>("out") = state<std::uint64_t>("captured").value() + 4;
        }
    }
};

class KernelCComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelCComponent()
        : project_xs::sim::KernelComponent("kernel_c", 3, 1) {
        create_state<std::uint64_t>("in", "C 组件输入", 0);
        create_state<std::uint64_t>("captured", "C 组件拍内暂存", 0);
        create_state<std::uint64_t>("out", "C 组件输出", 0);
        ports().add_input(state<std::uint64_t>("in").make_wire_input_port());
        ports().add_output(state<std::uint64_t>("out").make_wire_output_port());
    }

  protected:
    void reset_extra() override {
        state<std::uint64_t>("in") = 0;
        state<std::uint64_t>("captured") = 0;
        state<std::uint64_t>("out") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        if (phase() == 0) {
            state<std::uint64_t>("captured") = state<std::uint64_t>("in").value();
            return;
        }

        if (phase() == 1) {
            state<std::uint64_t>("out") = state<std::uint64_t>("captured").value() * 3;
        }
    }
};

class OrderProducerComponent final : public project_xs::sim::KernelComponent {
  public:
    OrderProducerComponent()
        : project_xs::sim::KernelComponent("order_producer") {
        create_state<std::uint64_t>("out", "顺序验证生产者输出", 0);
        ports().add_output(state<std::uint64_t>("out").make_wire_output_port());
    }

  protected:
    void reset_extra() override {
        state<std::uint64_t>("out") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        state<std::uint64_t>("out") = cycle;
    }
};

class OrderConsumerComponent final : public project_xs::sim::KernelComponent {
  public:
    OrderConsumerComponent()
        : project_xs::sim::KernelComponent("order_consumer") {
        create_state<std::uint64_t>("in", "顺序验证消费者输入", 0);
        create_state<std::uint64_t>("observed", "顺序验证消费者观测值", 0);
        ports().add_input(state<std::uint64_t>("in").make_wire_input_port());
    }

    std::uint64_t observed_value() const {
        return state<std::uint64_t>("observed").value();
    }

  protected:
    void reset_extra() override {
        state<std::uint64_t>("in") = 0;
        state<std::uint64_t>("observed") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        state<std::uint64_t>("observed") = state<std::uint64_t>("in").value();
    }
};

class AbcDemoKernel final : public project_xs::sim::Kernel {
  public:
    AbcDemoKernel(std::string label,
                  KernelAComponent* a_component,
                  KernelBComponent* b_component,
                  KernelCComponent* c_component)
        : project_xs::sim::Kernel(label),
          label_(std::move(label)),
          a_component_(a_component),
          b_component_(b_component),
          c_component_(c_component),
          reg_outputs_(create_port_group("reg_outputs")) {
        create_state<std::uint64_t>("A", "wire 观测 A", 0);
        create_state<std::uint64_t>("B", "wire 观测 B", 0);
        create_state<std::uint64_t>("C", "wire 观测 C", 0);
        create_state<std::uint64_t>("reg_A", "reg 观测 A", 0);
        create_state<std::uint64_t>("reg_B", "reg 观测 B", 0);
        create_state<std::uint64_t>("reg_C", "reg 观测 C", 0);

        // 这一组 wire 输出是“当前拍可见”的观测面。
        // demo kernel 自己不参与 A/B/C 的功能计算，只负责把子组件输出汇总出来用于观察。
        ports().add_output(state<std::uint64_t>("A").make_wire_output_port());
        ports().add_output(state<std::uint64_t>("B").make_wire_output_port());
        ports().add_output(state<std::uint64_t>("C").make_wire_output_port());

        // 这一组 reg 输出是“下一拍可见”的对照面。
        // reg_outputs_ 不是业务上必需的模块端口，而是测试里额外加的一层，
        // 专门用来和上面的 wire 输出做逐拍比较。
        reg_outputs_.add_output(state<std::uint64_t>("reg_A").make_reg_output_port("A"));
        reg_outputs_.add_output(state<std::uint64_t>("reg_B").make_reg_output_port("B"));
        reg_outputs_.add_output(state<std::uint64_t>("reg_C").make_reg_output_port("C"));

    }

    bool remove_demo_component(std::string_view name) {
        if (name == "kernel_c") {
            c_component_ = nullptr;
            return remove_component(name);
        }
        return false;
    }

  protected:
    void on_attached_to_simulator(project_xs::sim::CycleSimulator& simulator) override {
        a_component_->ports().get_input("in")->connect(simulator.ports().get_output("source_out"));
    }

    void reset_extra() override {
        state<std::uint64_t>("A") = 0;
        state<std::uint64_t>("B") = 0;
        state<std::uint64_t>("C") = 0;
        state<std::uint64_t>("reg_A") = 0;
        state<std::uint64_t>("reg_B") = 0;
        state<std::uint64_t>("reg_C") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        // 这里先清空默认 wire 观测口。
        // 不清空的话，当前拍如果不重新赋值，wire 口会保留上一拍的 visible 状态，
        // 就看不出“本拍是否真的重新发射了值”。
        ports().clear();

        // 默认 Kernel::run_single() 会按注册顺序推进 source -> A -> B -> C。
        // 也就是说，子组件内部的 sync_input / emit_outputs / phase 推进都已经发生完了；
        // 这里做的只是“把子组件结果汇总到 demo kernel 自己的观测口”。
        project_xs::sim::Kernel::run_single(cycle);

        // read_component_output() 是这个测试额外加的辅助函数。
        // 它的目的不是实现数据通路，而是把子组件端口上的可见值拷到 demo kernel 的观测变量里，
        // 这样外层就能继续用 PortGroup 打印 A/B/C 的统一视图。
        read_component_output(*a_component_, "out", state<std::uint64_t>("A").value());
        read_component_output(*b_component_, "out", state<std::uint64_t>("B").value());
        state<std::uint64_t>("C") = 0;
        if (c_component_) {
            read_component_output(*c_component_, "out", state<std::uint64_t>("C").value());
        }

        // reg_* 只是把同一组观测值再喂给 reg 端口，专门用来展示 wire/reg 的时序差异。
        // 这部分在 UE/游戏业务代码里通常会显得偏“测试脚手架”，不是核心业务逻辑。
        state<std::uint64_t>("reg_A") = state<std::uint64_t>("A").value();
        state<std::uint64_t>("reg_B") = state<std::uint64_t>("B").value();
        state<std::uint64_t>("reg_C") = state<std::uint64_t>("C").value();
    }

  private:
    std::string label_;
    KernelAComponent* a_component_;
    KernelBComponent* b_component_;
    KernelCComponent* c_component_;
    project_xs::sim::PortGroup& reg_outputs_;

    std::string debug_info(std::uint64_t cycle) const override {
        // 这里直接使用默认十进制输出。
        // PortGroup::info_outputs() 会顺序调用每个端口的 Port::info()。
        return "[" + label_ + "][cycle " + std::to_string(cycle) + "] wire: " +
               ports().info_outputs() + "\n[" + label_ + "][cycle " +
               std::to_string(cycle) + "] reg : " + reg_outputs_.info_outputs();
    }

    static void read_component_output(const project_xs::sim::KernelComponent& component,
                                      const std::string& port_name,
                                      std::uint64_t& out) {
        // 这个函数也是测试辅助函数，不是框架必需接口。
        // 当前 Port::info() 负责“可读打印”，而 read() 仍然负责“严格类型取值”；
        // 这里保留 read() 是为了把子组件输出拉到 demo kernel 自己的观测变量里。
        (void)component.ports().get_output(port_name)->read(out);
    }
};

class OrderProbeKernel final : public project_xs::sim::Kernel {
  public:
    OrderProbeKernel(std::string label,
                     OrderProducerComponent* producer,
                     OrderConsumerComponent* consumer)
        : project_xs::sim::Kernel(label),
          producer_(producer),
          consumer_(consumer) {}

  protected:
    void run_single(std::uint64_t cycle) override {
        project_xs::sim::Kernel::run_single(cycle);
        std::cout << "[" << name() << "][cycle " << cycle << "] observed="
                  << consumer_->observed_value() << " expected=" << cycle
                  << " match=" << (consumer_->observed_value() == cycle ? "yes" : "no") << "\n";
    }

  private:
    OrderProducerComponent* producer_;
    OrderConsumerComponent* consumer_;
};

std::shared_ptr<AbcDemoKernel> build_demo_kernel(const std::string& label) {
    auto component_a = std::make_shared<KernelAComponent>();
    auto component_b = std::make_shared<KernelBComponent>();
    auto component_c = std::make_shared<KernelCComponent>();

    component_b->ports().get_input("in")->connect(component_a->ports().get_output("out"));
    component_c->ports().get_input("in")->connect(component_b->ports().get_output("out"));

    auto demo_kernel = std::make_shared<AbcDemoKernel>(
        label,
        component_a.get(),
        component_b.get(),
        component_c.get());

    demo_kernel->add_component(component_a);
    demo_kernel->add_component(component_b);
    demo_kernel->add_component(component_c);

    return demo_kernel;
}

std::shared_ptr<OrderProbeKernel> build_order_probe_kernel(const std::string& label) {
    auto producer = std::make_shared<OrderProducerComponent>();
    auto consumer = std::make_shared<OrderConsumerComponent>();

    consumer->ports().get_input("in")->connect(producer->ports().get_output("out"));

    auto probe_kernel = std::make_shared<OrderProbeKernel>(
        label,
        producer.get(),
        consumer.get());
    probe_kernel->add_component(producer);
    probe_kernel->add_component(consumer);
    return probe_kernel;
}

void verify_base_snapshot_restore() {
    auto simulator = std::make_shared<DemoCycleSimulator>("snapshot_simulator", 6, 1.0L);
    auto kernel = build_demo_kernel("snapshot_kernel");
    simulator->add_kernel(kernel);
    simulator->reset();
    simulator->initialize_zero();

    simulator->step();
    simulator->step();
    const auto snapshot = simulator->snapshot();
    const std::uint64_t saved_cycle = simulator->current_cycle();
    const std::uint64_t saved_a =
        kernel->state_set().value<std::uint64_t>("A");

    simulator->step();
    if (simulator->current_cycle() == saved_cycle) {
        throw std::runtime_error("snapshot test did not advance after saving");
    }

    simulator->restore(snapshot);
    if (simulator->current_cycle() != saved_cycle ||
        kernel->state_set().value<std::uint64_t>("A") != saved_a) {
        throw std::runtime_error("base snapshot/restore did not recover saved runtime state");
    }

    std::cout << "[snapshot_test] restored cycle=" << simulator->current_cycle()
              << " A=" << kernel->state_set().value<std::uint64_t>("A") << "\n";
}

void verify_snapshot_capture_modes() {
    const std::string trace_dir = "test/abctest/snapshot_traces";
    auto simulator = std::make_shared<DemoCycleSimulator>("capture_simulator", 6, 1.0L);
    auto kernel = build_demo_kernel("capture_kernel");
    auto component = kernel->get_component("kernel_a");
    simulator->set_snapshot_capture_directory(trace_dir);
    kernel->set_snapshot_capture_directory(trace_dir);
    component->set_snapshot_capture_directory(trace_dir);
    simulator->add_kernel(kernel);
    simulator->reset();
    simulator->initialize_zero();

    simulator->start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Automatic);
    simulator->step();
    simulator->stop_snapshot_capture();

    if (simulator->snapshot_history().size() != 10) {
        throw std::runtime_error("automatic simulator snapshot did not record every step stage");
    }
    if (simulator->snapshot_history().front().checkpoint_role != "segment_first" ||
        simulator->snapshot_history().back().checkpoint_role != "segment_last") {
        throw std::runtime_error("automatic simulator snapshot did not write segment checkpoints");
    }
    if (!kernel->snapshot_history().empty() || !component->snapshot_history().empty()) {
        throw std::runtime_error("simulator snapshot capture leaked into nested histories");
    }

    kernel->start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Manual);
    const auto& manual_record =
        kernel->capture_snapshot(project_xs::sim::SnapshotCaptureStage::Manual);
    kernel->stop_snapshot_capture();
    if (kernel->snapshot_history().size() != 1 ||
        manual_record.stage != project_xs::sim::SnapshotCaptureStage::Manual ||
        manual_record.checkpoint_role != "manual") {
        throw std::runtime_error("manual kernel snapshot capture failed");
    }
    kernel->restore_checkpoint(manual_record.checkpoint_path);

    component->start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Automatic);
    simulator->step();
    component->stop_snapshot_capture();
    if (component->snapshot_history().size() != 8) {
        throw std::runtime_error("automatic component snapshot did not record run/end stages");
    }
    if (component->snapshot_history().front().checkpoint_role != "segment_first" ||
        component->snapshot_history().back().checkpoint_role != "segment_last") {
        throw std::runtime_error("automatic component snapshot did not write segment checkpoints");
    }

    std::cout << "[snapshot_capture_test] simulator_records="
              << simulator->snapshot_history().size()
              << " component_records=" << component->snapshot_history().size() << "\n";
}

void verify_waveform_capture() {
    auto simulator = std::make_shared<DemoCycleSimulator>("waveform_simulator", 3, 1.0L);
    simulator->add_kernel(build_demo_kernel("waveform_kernel"));
    simulator->reset();
    simulator->initialize_zero();
    simulator->step();

    project_xs::sim::WaveformTrace trace;
    project_xs::sim::append_waveform_frame(trace, *simulator);
    if (trace.frames.empty() || trace.frames.front().signals.empty()) {
        throw std::runtime_error("waveform capture did not collect any signal");
    }

    std::cout << "[waveform_test] cycle=" << trace.frames.front().cycle
              << " signals=" << trace.frames.front().signals.size() << "\n";
}

}  // namespace

int main() {
    verify_base_snapshot_restore();
    verify_snapshot_capture_modes();
    verify_waveform_capture();

    // session 自己有一套时间步频率，底层 simulator 则各自有自己的周期频率。
    // 当前 demo 同时挂两个 simulator：
    // - sim_a: 4Hz，最多跑 6 个周期
    // - sim_b: 2Hz，最多跑 6 个周期
    // session 会一直运行到总时间到达，或者全部 simulator 自己 finished。
    project_xs::sim::SimulationSession session(4.0L, 3.0L);
    auto simulator_a = std::make_shared<DemoCycleSimulator>("simulator_a", 6, 4.0L);
    auto simulator_b = std::make_shared<DemoCycleSimulator>("simulator_b", 6, 2.0L);

    auto kernel_a = build_demo_kernel("sim_a");
    simulator_a->add_kernel(kernel_a);
    simulator_a->add_kernel(build_order_probe_kernel("order_probe"));

    // 差异化实例直接在构造阶段生成，不再通过旧复制接口改写结构。
    auto kernel_b = build_demo_kernel("sim_b_without_c");
    kernel_b->remove_demo_component("kernel_c");
    simulator_b->add_kernel(kernel_b);

    session.add_simulator(simulator_a);
    session.add_simulator(simulator_b);

    // 先做一次全局 reset，再把所有寄存器型端口初始化成 0。
    // initialize_zero() 也是为了让 reg 观测更直观：
    // 这样仿真起始拍就能看到确定的 0，而不是一开始全是 z。
    session.reset();
    session.initialize_zero();

    std::cout << session.start_info() << "\n";
    session.run();
    std::cout << session.finish_info() << "\n";

    return 0;
}
