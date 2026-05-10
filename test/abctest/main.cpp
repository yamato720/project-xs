#include "base/CycleSimulator.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/PortGroup.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::string read_or_z(const std::shared_ptr<project_xs::sim::Port>& port) {
    std::uint64_t value = 0;
    if (!port || !port->read(value)) {
        return "z";
    }
    return std::to_string(value);
}

class KernelAComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelAComponent()
        : project_xs::sim::KernelComponent("kernel_a", 1),
          output_to_b_(0),
          output_to_c_(0),
          output_value_(0) {
        ports().add_output(project_xs::sim::WirePort::make_output<std::uint64_t>(
            "kernel_a_out_to_b",
            &output_to_b_));
        ports().add_output(project_xs::sim::WirePort::make_output<std::uint64_t>(
            "kernel_a_out_to_c",
            &output_to_c_));
    }

  protected:
    void reset() override {
        project_xs::sim::KernelComponent::reset();
        output_to_b_ = 0;
        output_to_c_ = 0;
        output_value_ = 0;
    }

    void run_single(std::uint64_t cycle) override {
        if (!phase_valid()) {
            return;
        }

        output_value_ = cycle;
        output_to_b_ = output_value_;
        output_to_c_ = output_value_;
        ports().get_output("kernel_a_out_to_b")->emit_bound_value();
        ports().get_output("kernel_a_out_to_c")->emit_bound_value();
    }

  private:
    std::uint64_t output_to_b_;
    std::uint64_t output_to_c_;
    std::uint64_t output_value_;
};

class KernelBComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelBComponent()
        : project_xs::sim::KernelComponent("kernel_b", 2),
          input_value_(0),
          captured_value_(0) {
        ports().add_input(project_xs::sim::WirePort::make_input<std::uint64_t>(
            "kernel_b_in",
            &input_value_));
    }

    std::uint64_t output_value() const { return output_value_; }

  protected:
    void reset() override {
        project_xs::sim::KernelComponent::reset();
        input_value_ = 0;
        captured_value_ = 0;
        output_value_ = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        if (phase() == 0) {
            captured_value_ = input_value_;
            return;
        }

        if (phase() == 1) {
            output_value_ = captured_value_ + 4;
        }
    }

  private:
    std::uint64_t input_value_;
    std::uint64_t captured_value_;
    std::uint64_t output_value_ = 0;
};

class KernelCComponent final : public project_xs::sim::KernelComponent {
  public:
    KernelCComponent()
        : project_xs::sim::KernelComponent("kernel_c", 3, 1),
          input_value_(0),
          captured_value_(0) {
        ports().add_input(project_xs::sim::WirePort::make_input<std::uint64_t>(
            "kernel_c_in",
            &input_value_));
    }

    std::uint64_t output_value() const { return output_value_; }

  protected:
    void reset() override {
        project_xs::sim::KernelComponent::reset();
        input_value_ = 0;
        captured_value_ = 0;
        output_value_ = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }

        if (phase() == 0) {
            captured_value_ = input_value_;
            return;
        }

        if (phase() == 1) {
            output_value_ = captured_value_ * 3;
        }
    }

  private:
    std::uint64_t input_value_;
    std::uint64_t captured_value_;
    std::uint64_t output_value_ = 0;
};

class AbcDemoKernel final : public project_xs::sim::Kernel {
  public:
    AbcDemoKernel(KernelAComponent* a_component,
                  KernelBComponent* b_component,
                  KernelCComponent* c_component)
        : project_xs::sim::Kernel("demo_kernel"),
          a_component_(a_component),
          b_component_(b_component),
          c_component_(c_component),
          wire_a_(0),
          wire_b_(0),
          wire_c_(0),
          reg_a_(0),
          reg_b_(0),
          reg_c_(0),
          reg_outputs_("reg_outputs") {
        ports().add_output(project_xs::sim::WirePort::make_output<std::uint64_t>(
            "wire_kernel_a_out",
            &wire_a_));
        ports().add_output(project_xs::sim::WirePort::make_output<std::uint64_t>(
            "wire_kernel_b_out",
            &wire_b_));
        ports().add_output(project_xs::sim::WirePort::make_output<std::uint64_t>(
            "wire_kernel_c_out",
            &wire_c_));

        reg_outputs_.add_output(project_xs::sim::RegPort::make_output<std::uint64_t>(
            "reg_kernel_a_out",
            &reg_a_));
        reg_outputs_.add_output(project_xs::sim::RegPort::make_output<std::uint64_t>(
            "reg_kernel_b_out",
            &reg_b_));
        reg_outputs_.add_output(project_xs::sim::RegPort::make_output<std::uint64_t>(
            "reg_kernel_c_out",
            &reg_c_));
    }

  protected:
    void reset() override {
        project_xs::sim::Kernel::reset();
        reg_outputs_.clear();
        wire_a_ = wire_b_ = wire_c_ = 0;
        reg_a_ = reg_b_ = reg_c_ = 0;
    }

    void initialize_zero() override {
        project_xs::sim::Kernel::initialize_zero();
        reg_outputs_.initialize_zero();
    }

    void run_single(std::uint64_t cycle) override {
        ports().clear();

        project_xs::sim::Kernel::run_single(cycle);

        std::uint64_t a_value = 0;
        if (a_component_->ports().get_output("kernel_a_out_to_b")->read(a_value)) {
            wire_a_ = a_value;
        }
        wire_b_ = b_component_->output_value();
        wire_c_ = c_component_->output_value();

        reg_a_ = wire_a_;
        reg_b_ = wire_b_;
        reg_c_ = wire_c_;

        ports().get_output("wire_kernel_a_out")->emit_bound_value();
        ports().get_output("wire_kernel_b_out")->emit_bound_value();
        ports().get_output("wire_kernel_c_out")->emit_bound_value();

        reg_outputs_.get_output("reg_kernel_a_out")->emit_bound_value();
        reg_outputs_.get_output("reg_kernel_b_out")->emit_bound_value();
        reg_outputs_.get_output("reg_kernel_c_out")->emit_bound_value();

        std::cout << "[cycle " << cycle << "] wire: "
                  << "A=" << read_or_z(ports().get_output("wire_kernel_a_out")) << " "
                  << "B=" << read_or_z(ports().get_output("wire_kernel_b_out")) << " "
                  << "C=" << read_or_z(ports().get_output("wire_kernel_c_out")) << "\n";

        std::cout << "[cycle " << cycle << "] reg : "
                  << "A=" << read_or_z(reg_outputs_.get_output("reg_kernel_a_out")) << " "
                  << "B=" << read_or_z(reg_outputs_.get_output("reg_kernel_b_out")) << " "
                  << "C=" << read_or_z(reg_outputs_.get_output("reg_kernel_c_out")) << "\n";
    }

    void end_cycle() override {
        project_xs::sim::Kernel::end_cycle();
        reg_outputs_.end_cycle();
    }

  private:
    KernelAComponent* a_component_;
    KernelBComponent* b_component_;
    KernelCComponent* c_component_;
    std::uint64_t wire_a_;
    std::uint64_t wire_b_;
    std::uint64_t wire_c_;
    std::uint64_t reg_a_;
    std::uint64_t reg_b_;
    std::uint64_t reg_c_;
    project_xs::sim::PortGroup reg_outputs_;
};

}  // namespace

int main() {
    project_xs::sim::CycleSimulator simulator(12);

    auto component_a = std::make_shared<KernelAComponent>();
    auto component_b = std::make_shared<KernelBComponent>();
    auto component_c = std::make_shared<KernelCComponent>();

    component_b->ports().get_input("kernel_b_in")->connect(
        component_a->ports().get_output("kernel_a_out_to_b"));
    component_c->ports().get_input("kernel_c_in")->connect(
        component_a->ports().get_output("kernel_a_out_to_c"));

    auto demo_kernel = std::make_shared<AbcDemoKernel>(
        component_a.get(),
        component_b.get(),
        component_c.get());

    demo_kernel->add_component(component_a);
    demo_kernel->add_component(component_b);
    demo_kernel->add_component(component_c);

    simulator.add_kernel(demo_kernel);

    // 先做一次全局 reset，再把所有寄存器型端口初始化成 0。
    simulator.reset();
    simulator.initialize_zero();

    std::cout << "start cycle simulation, max_cycles=12, compare wire group and reg group\n";
    simulator.run();
    std::cout << "simulation finished at cycle " << simulator.current_cycle() << "\n";

    return 0;
}
