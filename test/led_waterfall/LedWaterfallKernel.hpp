#ifndef PROJECT_XS_TEST_LED_WATERFALL_KERNEL_HPP
#define PROJECT_XS_TEST_LED_WATERFALL_KERNEL_HPP

#include "base/CycleSimulator.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace project_xs::sim::test::led_waterfall {

// 这个文件是对 test/led_waterfall/verilog.v 的周期级仿真拆分。
//
// Verilog 原始行为可以概括成：
// 1. rst_n 低时：cnt 清零，led 复位到 8'b0000_0001
// 2. rst_n 高时：cnt 每拍加一
// 3. 当 cnt == CNT_MAX - 1 时：
//    - cnt 回到 0
//    - led 左移一位；若已经到最高位则回到最低位
//
// 这里把它拆成两个 component：
// - CounterComponent：只负责计数和 wrap_pulse
// - LedShiftComponent：只负责根据 wrap_pulse 更新 led
//
// 再由 LedWaterfallKernel 把两个 component 组合起来，形成一个完整 kernel。

// 计数组件：
// - 接收 rst_n
// - 维护计数器
// - 到达 CNT_MAX - 1 时发出 wrap_pulse
class CounterComponent final : public project_xs::sim::KernelComponent {
  public:
    explicit CounterComponent(std::uint32_t cnt_max)
        : project_xs::sim::KernelComponent("counter"),
          rst_n_(false),
          wrap_pulse_(false),
          count_(0),
          cnt_max_(cnt_max) {
        if (cnt_max_ == 0) {
            throw std::runtime_error("CounterComponent cnt_max must be > 0");
        }

        ports().add_input(project_xs::sim::WirePort::make_input<bool>("rst_n", &rst_n_));
        ports().add_output(project_xs::sim::WirePort::make_output<bool>("wrap_pulse", &wrap_pulse_));
        ports().add_output(
            project_xs::sim::WirePort::make_output<std::uint32_t>("count", &count_));
    }

    // clone() 让 fullcopy 这类操作可以深复制 component。
    // 这里保留当前 component 的运行时状态，但不共享同一个对象。
    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<CounterComponent>(cnt_max_);
        copy->copy_component_runtime_from(*this);
        return copy;
    }

    bool rst_n_value() const { return rst_n_; }
    bool wrap_pulse_value() const { return wrap_pulse_; }
    std::uint32_t count_value() const { return count_; }
    std::uint32_t cnt_max() const { return cnt_max_; }

  protected:
    void reset_extra() override {
        rst_n_ = false;
        wrap_pulse_ = false;
        count_ = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!rst_n_) {
            count_ = 0;
            wrap_pulse_ = false;
            return;
        }

        if (count_ == cnt_max_ - 1) {
            count_ = 0;
            wrap_pulse_ = true;
        } else {
            ++count_;
            wrap_pulse_ = false;
        }
    }

  private:
    bool rst_n_;
    bool wrap_pulse_;
    std::uint32_t count_;
    std::uint32_t cnt_max_;
};

// LED 组件：
// - 接收 rst_n 和 wrap_pulse
// - wrap 时左移 led，最高位后回到最低位
class LedShiftComponent final : public project_xs::sim::KernelComponent {
  public:
    LedShiftComponent()
        : project_xs::sim::KernelComponent("led_shift"),
          rst_n_(false),
          wrap_pulse_(false),
          led_(0x01) {
        ports().add_input(project_xs::sim::WirePort::make_input<bool>("rst_n", &rst_n_));
        ports().add_input(
            project_xs::sim::WirePort::make_input<bool>("wrap_pulse", &wrap_pulse_));
        ports().add_output(project_xs::sim::WirePort::make_output<std::uint8_t>("led", &led_));
    }

    // 同样支持深复制，用于 simulator fullcopy / kernel clone。
    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<LedShiftComponent>();
        copy->copy_component_runtime_from(*this);
        return copy;
    }

    bool rst_n_value() const { return rst_n_; }
    bool wrap_pulse_value() const { return wrap_pulse_; }
    std::uint8_t led_value() const { return led_; }

  protected:
    void reset_extra() override {
        rst_n_ = false;
        wrap_pulse_ = false;
        led_ = 0x01;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!rst_n_) {
            led_ = 0x01;
            return;
        }

        if (!wrap_pulse_) {
            return;
        }

        if (led_ == 0x80) {
            led_ = 0x01;
        } else {
            led_ = static_cast<std::uint8_t>(led_ << 1);
        }
    }

  private:
    bool rst_n_;
    bool wrap_pulse_;
    std::uint8_t led_;
};

// 顶层 kernel：
// - 组件 A(counter) 根据 cycle 计数
// - 组件 B(led_shift) 接收 wrap_pulse 更新 led
// - simulator 负责提供 rst_n
class LedWaterfallKernel final : public project_xs::sim::Kernel {
  public:
    LedWaterfallKernel(std::string label, std::uint32_t cnt_max)
        : project_xs::sim::Kernel(std::move(label)),
          counter_(std::make_shared<CounterComponent>(cnt_max)),
          shifter_(std::make_shared<LedShiftComponent>()),
          led_output_(0x01) {
        // 这里对应 Verilog 里“led <= ...”那个输出寄存器的可见观测面。
        // kernel 自身不直接实现状态机，而是把内部 component 的结果汇总后对外输出。
        shifter_->ports().get_input("wrap_pulse")->connect(
            counter_->ports().get_output("wrap_pulse"));

        ports().add_output(
            project_xs::sim::WirePort::make_output<std::uint8_t>("led", &led_output_));

        // 默认顺序是 counter 先更新，再 led_shift 更新。
        // 由于当前框架已经恢复成“按 vector 顺序逐个执行完整 run()”，
        // 这个顺序本身就是时序关系的一部分。
        add_component(counter_);
        add_component(shifter_);
    }

    // 顶层 kernel 的深复制：
    // - 先各自 clone 两个 component
    // - 再重建它们之间的端口连接
    // - 最后复制 kernel 自身的运行时状态
    std::shared_ptr<project_xs::sim::Kernel> clone() const override {
        auto counter_copy =
            std::dynamic_pointer_cast<CounterComponent>(counter_->clone());
        auto shifter_copy =
            std::dynamic_pointer_cast<LedShiftComponent>(shifter_->clone());

        shifter_copy->ports().get_input("wrap_pulse")->connect(
            counter_copy->ports().get_output("wrap_pulse"));

        auto copy = std::make_shared<LedWaterfallKernel>(name(), counter_copy->cnt_max());
        copy->counter_ = counter_copy;
        copy->shifter_ = shifter_copy;
        copy->led_output_ = led_output_;
        copy->copy_kernel_runtime_from(*this);
        return copy;
    }

  protected:
    // rst_n 不是从别的 kernel 来，而是从 simulator 自带默认 portgroup 提供。
    // 这正对应“外部环境/板级输入由 simulator 自己提供”的建模方式。
    void on_attached_to_simulator(project_xs::sim::CycleSimulator& simulator) override {
        counter_->ports().get_input("rst_n")->connect(simulator.ports().get_output("rst_n"));
        shifter_->ports().get_input("rst_n")->connect(simulator.ports().get_output("rst_n"));
    }

    void reset_extra() override {
        led_output_ = 0x01;
    }

    void run_single(std::uint64_t cycle) override {
        // 先按注册顺序推进内部两个 component。
        // counter 先决定 count / wrap_pulse，
        // led_shift 再据此决定当前拍的 led。
        project_xs::sim::Kernel::run_single(cycle);

        // 对外只暴露 led 的最终结果。
        led_output_ = shifter_->led_value();
    }

    // 这个调试输出会在每拍自动打印，方便和 Verilog 里的内部寄存器语义对照。
    std::string debug_info(std::uint64_t cycle) const override {
        return "[" + name() + "][cycle " + std::to_string(cycle) + "] rst_n=" +
               std::string(counter_->rst_n_value() ? "1" : "0") + " cnt=" +
               std::to_string(counter_->count_value()) + " wrap=" +
               std::string(counter_->wrap_pulse_value() ? "1" : "0") + " led=0x" +
               hex_byte(led_output_);
    }

  private:
    static std::string hex_byte(std::uint8_t value) {
        static constexpr char kDigits[] = "0123456789ABCDEF";
        std::string text = "00";
        text[0] = kDigits[(value >> 4U) & 0x0F];
        text[1] = kDigits[value & 0x0F];
        return text;
    }

    // 注意这里把两个 component 也保留成成员，
    // 是为了让 clone()/debug_info() 能直接访问它们的状态。
    std::shared_ptr<CounterComponent> counter_;
    std::shared_ptr<LedShiftComponent> shifter_;
    std::uint8_t led_output_;
};

}  // namespace project_xs::sim::test::led_waterfall

#endif
