#include "base/CycleSimulator.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/PortGroup.h"
#include "base/SimulationSession.h"
#include "base/State.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace {

class DemoCycleSimulator final : public project_xs::sim::CycleSimulator {
  public:
    DemoCycleSimulator(std::string name, std::int64_t max_cycles, long double frequency_hz)
        : project_xs::sim::CycleSimulator(std::move(name), max_cycles, frequency_hz),
          source_out_("source_out", "simulator 级输入源输出", 0) {
        mutable_state_set().register_state(source_out_);
        // 以后如果要接 HBM / AXI / 其他跨周期环境交互，这里就是 simulator 级默认接口入口。
        ports().add_output(source_out_.make_wire_output_port());
    }

  private:
    void reset_extra() override {
        source_out_.value() = 0;
    }

    void run_single(std::uint64_t cycle) override {
        source_out_.value() = cycle;
    }

    project_xs::sim::State<std::uint64_t> source_out_;
};

class KernelAComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelAComponent()
        : project_xs::sim::KernelComponent("kernel_a", 1),
          input_state_("in", "A 组件输入", 0),
          output_state_("out", "A 组件输出", 0) {
        mutable_state_set().register_state(input_state_);
        mutable_state_set().register_state(output_state_);
        ports().add_input(input_state_.make_wire_input_port());
        ports().add_output(output_state_.make_wire_output_port());
    }

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<KernelAComponent>();
        copy->copy_component_runtime_from(*this);
        return copy;
    }

  protected:
    void reset_extra() override {
        input_state_.value() = 0;
        output_state_.value() = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        output_state_.value() = input_state_.value();
    }

  private:
    project_xs::sim::State<std::uint64_t> input_state_;
    project_xs::sim::State<std::uint64_t> output_state_;
};

class KernelBComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelBComponent()
        : project_xs::sim::KernelComponent("kernel_b", 2),
          input_state_("in", "B 组件输入", 0),
          captured_state_("captured", "B 组件拍内暂存", 0),
          output_state_("out", "B 组件输出", 0) {
        mutable_state_set().register_state(input_state_);
        mutable_state_set().register_state(captured_state_);
        mutable_state_set().register_state(output_state_);
        ports().add_input(input_state_.make_wire_input_port());
        ports().add_output(output_state_.make_wire_output_port());
    }

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<KernelBComponent>();
        copy->copy_component_runtime_from(*this);
        return copy;
    }

  protected:
    void reset_extra() override {
        input_state_.value() = 0;
        captured_state_.value() = 0;
        output_state_.value() = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        if (phase() == 0) {
            captured_state_.value() = input_state_.value();
            return;
        }

        if (phase() == 1) {
            output_state_.value() = captured_state_.value() + 4;
        }
    }

  private:
    project_xs::sim::State<std::uint64_t> input_state_;
    project_xs::sim::State<std::uint64_t> captured_state_;
    project_xs::sim::State<std::uint64_t> output_state_;
};

class KernelCComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelCComponent()
        : project_xs::sim::KernelComponent("kernel_c", 3, 1),
          input_state_("in", "C 组件输入", 0),
          captured_state_("captured", "C 组件拍内暂存", 0),
          output_state_("out", "C 组件输出", 0) {
        mutable_state_set().register_state(input_state_);
        mutable_state_set().register_state(captured_state_);
        mutable_state_set().register_state(output_state_);
        ports().add_input(input_state_.make_wire_input_port());
        ports().add_output(output_state_.make_wire_output_port());
    }

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<KernelCComponent>();
        copy->copy_component_runtime_from(*this);
        return copy;
    }

  protected:
    void reset_extra() override {
        input_state_.value() = 0;
        captured_state_.value() = 0;
        output_state_.value() = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        if (phase() == 0) {
            captured_state_.value() = input_state_.value();
            return;
        }

        if (phase() == 1) {
            output_state_.value() = captured_state_.value() * 3;
        }
    }

  private:
    project_xs::sim::State<std::uint64_t> input_state_;
    project_xs::sim::State<std::uint64_t> captured_state_;
    project_xs::sim::State<std::uint64_t> output_state_;
};

class OrderProducerComponent final : public project_xs::sim::KernelComponent {
  public:
    OrderProducerComponent()
        : project_xs::sim::KernelComponent("order_producer"),
          output_state_("out", "顺序验证生产者输出", 0) {
        mutable_state_set().register_state(output_state_);
        ports().add_output(output_state_.make_wire_output_port());
    }

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<OrderProducerComponent>();
        copy->copy_component_runtime_from(*this);
        return copy;
    }

  protected:
    void reset_extra() override {
        output_state_.value() = 0;
    }

    void run_single(std::uint64_t cycle) override {
        output_state_.value() = cycle;
    }

  private:
    project_xs::sim::State<std::uint64_t> output_state_;
};

class OrderConsumerComponent final : public project_xs::sim::KernelComponent {
  public:
    OrderConsumerComponent()
        : project_xs::sim::KernelComponent("order_consumer"),
          input_state_("in", "顺序验证消费者输入", 0),
          observed_state_("observed", "顺序验证消费者观测值", 0) {
        mutable_state_set().register_state(input_state_);
        mutable_state_set().register_state(observed_state_);
        ports().add_input(input_state_.make_wire_input_port());
    }

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<OrderConsumerComponent>();
        copy->copy_component_runtime_from(*this);
        return copy;
    }

    std::uint64_t observed_value() const { return observed_state_.value(); }

  protected:
    void reset_extra() override {
        input_state_.value() = 0;
        observed_state_.value() = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        observed_state_.value() = input_state_.value();
    }

  private:
    project_xs::sim::State<std::uint64_t> input_state_;
    project_xs::sim::State<std::uint64_t> observed_state_;
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
          wire_a_("A", "wire 观测 A", 0),
          wire_b_("B", "wire 观测 B", 0),
          wire_c_("C", "wire 观测 C", 0),
          reg_outputs_(create_port_group("reg_outputs")) {
        mutable_state_set().register_state(wire_a_);
        mutable_state_set().register_state(wire_b_);
        mutable_state_set().register_state(wire_c_);
        // 这一组 wire 输出是“当前拍可见”的观测面。
        // demo kernel 自己不参与 A/B/C 的功能计算，只负责把子组件输出汇总出来用于观察。
        ports().add_output(wire_a_.make_wire_output_port());
        ports().add_output(wire_b_.make_wire_output_port());
        ports().add_output(wire_c_.make_wire_output_port());

        // 这一组 reg 输出是“下一拍可见”的对照面。
        // reg_outputs_ 不是业务上必需的模块端口，而是测试里额外加的一层，
        // 专门用来和上面的 wire 输出做逐拍比较。
        reg_a_ = std::make_unique<project_xs::sim::State<std::uint64_t>>("A", "reg 观测 A", 0);
        reg_b_ = std::make_unique<project_xs::sim::State<std::uint64_t>>("B", "reg 观测 B", 0);
        reg_c_ = std::make_unique<project_xs::sim::State<std::uint64_t>>("C", "reg 观测 C", 0);
        reg_states_.register_state(*reg_a_);
        reg_states_.register_state(*reg_b_);
        reg_states_.register_state(*reg_c_);
        reg_outputs_.add_output(reg_a_->make_reg_output_port());
        reg_outputs_.add_output(reg_b_->make_reg_output_port());
        reg_outputs_.add_output(reg_c_->make_reg_output_port());

    }

    std::shared_ptr<project_xs::sim::Kernel> clone() const override {
        auto a_copy = std::dynamic_pointer_cast<KernelAComponent>(a_component_->clone());
        auto b_copy = std::dynamic_pointer_cast<KernelBComponent>(b_component_->clone());
        std::shared_ptr<KernelCComponent> c_copy;
        if (c_component_) {
            c_copy = std::dynamic_pointer_cast<KernelCComponent>(c_component_->clone());
        }

        b_copy->ports().get_input("in")->connect(a_copy->ports().get_output("out"));
        if (c_copy) {
            c_copy->ports().get_input("in")->connect(b_copy->ports().get_output("out"));
        }

        auto copy = std::make_shared<AbcDemoKernel>(
            label_,
            a_copy.get(),
            b_copy.get(),
            c_copy.get());

        copy->add_component(a_copy);
        copy->add_component(b_copy);
        if (c_copy) {
            copy->add_component(c_copy);
        }

        copy->reg_outputs_.copy_runtime_from(reg_outputs_);
        copy->copy_kernel_runtime_from(*this);
        return copy;
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
        wire_a_.value() = 0;
        wire_b_.value() = 0;
        wire_c_.value() = 0;
        reg_a_->value() = 0;
        reg_b_->value() = 0;
        reg_c_->value() = 0;
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
        read_component_output(*a_component_, "out", wire_a_.value());
        read_component_output(*b_component_, "out", wire_b_.value());
        wire_c_.value() = 0;
        if (c_component_) {
            read_component_output(*c_component_, "out", wire_c_.value());
        }

        // reg_* 只是把同一组观测值再喂给 reg 端口，专门用来展示 wire/reg 的时序差异。
        // 这部分在 UE/游戏业务代码里通常会显得偏“测试脚手架”，不是核心业务逻辑。
        reg_a_->value() = wire_a_.value();
        reg_b_->value() = wire_b_.value();
        reg_c_->value() = wire_c_.value();
    }

  private:
    std::string label_;
    KernelAComponent* a_component_;
    KernelBComponent* b_component_;
    KernelCComponent* c_component_;
    project_xs::sim::State<std::uint64_t> wire_a_;
    project_xs::sim::State<std::uint64_t> wire_b_;
    project_xs::sim::State<std::uint64_t> wire_c_;
    project_xs::sim::PortGroup& reg_outputs_;
    project_xs::sim::StateSet reg_states_;
    std::unique_ptr<project_xs::sim::State<std::uint64_t>> reg_a_;
    std::unique_ptr<project_xs::sim::State<std::uint64_t>> reg_b_;
    std::unique_ptr<project_xs::sim::State<std::uint64_t>> reg_c_;

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

    std::shared_ptr<project_xs::sim::Kernel> clone() const override {
        auto producer_copy =
            std::dynamic_pointer_cast<OrderProducerComponent>(producer_->clone());
        auto consumer_copy =
            std::dynamic_pointer_cast<OrderConsumerComponent>(consumer_->clone());

        consumer_copy->ports().get_input("in")->connect(producer_copy->ports().get_output("out"));

        auto copy = std::make_shared<OrderProbeKernel>(
            name(),
            producer_copy.get(),
            consumer_copy.get());
        copy->add_component(producer_copy);
        copy->add_component(consumer_copy);
        copy->copy_kernel_runtime_from(*this);
        return copy;
    }

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

}  // namespace

int main() {
    // session 自己有一套时间步频率，底层 simulator 则各自有自己的周期频率。
    // 当前 demo 同时挂两个 simulator：
    // - sim_a: 4Hz，3 秒内会跑 12 个周期
    // - sim_b: 2Hz，3 秒内会跑 6 个周期
    // session 会一直运行到总时间到达，或者全部 simulator 自己 finished。
    project_xs::sim::SimulationSession session(4.0L, 3.0L);
    auto simulator_a = std::make_shared<DemoCycleSimulator>("simulator_a", 6, 4.0L);
    auto simulator_b = std::make_shared<DemoCycleSimulator>("simulator_b", -1, 1.0L);

    auto kernel_a = build_demo_kernel("sim_a");
    simulator_a->add_kernel(kernel_a);
    simulator_a->add_kernel(build_order_probe_kernel("order_probe"));

    // fullcopy 会把 simulator_a 的配置、运行时状态和 kernel 内容完整复制到 simulator_b。
    simulator_b->fullcopy(*simulator_a);
    (*simulator_b)(6, 2.0L);

    // 在完整复制出的 kernel 上删除 C 组件，验证 fullcopy 后仍能做局部裁剪。
    simulator_b->remove_kernel("sim_a");
    auto kernel_b = build_demo_kernel("sim_b_fullcopy_without_c");
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
