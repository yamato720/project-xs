#ifndef PROJECT_XS_TEST_LED_WATERFALL_KERNEL_HPP
#define PROJECT_XS_TEST_LED_WATERFALL_KERNEL_HPP

#include "base/CycleSimulator.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/State.h"

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
          rst_n_("rst_n", "异步低有效复位输入", false),
          wrap_pulse_("wrap_pulse", "计数回卷脉冲输出", false),
          count_("count", "当前计数值", 0, 25),
          cnt_max_("cnt_max", "计数上限配置", cnt_max, 25) {
        mutable_state_set().register_state(rst_n_);
        mutable_state_set().register_state(wrap_pulse_);
        mutable_state_set().register_state(count_);
        mutable_state_set().register_state(cnt_max_);
        if (cnt_max_.value() == 0) {
            throw std::runtime_error("CounterComponent cnt_max must be > 0");
        }

        ports().add_input(rst_n_.make_wire_input_port());
        ports().add_output(wrap_pulse_.make_wire_output_port());
        ports().add_output(count_.make_wire_output_port());
    }

    // clone() 让 fullcopy 这类操作可以深复制 component。
    // 这里保留当前 component 的运行时状态，但不共享同一个对象。
    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<CounterComponent>(cnt_max_.value());
        copy->copy_component_runtime_from(*this);
        return copy;
    }

    std::uint32_t cnt_max() const { return cnt_max_.value(); }

  protected:
    void reset_extra() override {
        rst_n_.value() = false;
        wrap_pulse_.value() = false;
        count_.value() = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!rst_n_.value()) {
            count_.value() = 0;
            wrap_pulse_.value() = false;
            return;
        }

        if (count_.value() == cnt_max_.value() - 1) {
            count_.value() = 0;
            wrap_pulse_.value() = true;
        } else {
            ++count_.value();
            wrap_pulse_.value() = false;
        }
    }

  private:
    friend class LedWaterfallKernel;

    project_xs::sim::State<bool> rst_n_;
    project_xs::sim::State<bool> wrap_pulse_;
    project_xs::sim::State<std::uint32_t> count_;
    project_xs::sim::State<std::uint32_t> cnt_max_;
};

// LED 组件：
// - 接收 rst_n 和 wrap_pulse
// - wrap 时左移 led，最高位后回到最低位
class LedShiftComponent final : public project_xs::sim::KernelComponent {
  public:
    LedShiftComponent()
        : project_xs::sim::KernelComponent("led_shift"),
          rst_n_("rst_n", "异步低有效复位输入", false),
          wrap_pulse_("wrap_pulse", "来自计数器的回卷脉冲", false),
          led_("led", "当前 LED 模式输出", std::uint8_t{0x01}, 8) {
        mutable_state_set().register_state(rst_n_);
        mutable_state_set().register_state(wrap_pulse_);
        mutable_state_set().register_state(led_);

        ports().add_input(rst_n_.make_wire_input_port());
        ports().add_input(wrap_pulse_.make_wire_input_port());
        ports().add_output(led_.make_wire_output_port());
    }

    // 同样支持深复制，用于 simulator fullcopy / kernel clone。
    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        auto copy = std::make_shared<LedShiftComponent>();
        copy->copy_component_runtime_from(*this);
        return copy;
    }

  protected:
    void reset_extra() override {
        rst_n_.value() = false;
        wrap_pulse_.value() = false;
        led_.value() = 0x01;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!rst_n_.value()) {
            led_.value() = 0x01;
            return;
        }

        if (!wrap_pulse_.value()) {
            return;
        }

        if (led_.value() == 0x80) {
            led_.value() = 0x01;
        } else {
            led_.value() = static_cast<std::uint8_t>(led_.value() << 1);
        }
    }

  private:
    friend class LedWaterfallKernel;

    project_xs::sim::State<bool> rst_n_;
    project_xs::sim::State<bool> wrap_pulse_;
    project_xs::sim::State<std::uint8_t> led_;
};

// 顶层 kernel：
// - 组件 A(counter) 根据 cycle 计数
// - 组件 B(led_shift) 接收 wrap_pulse 更新 led
// - simulator 负责提供 rst_n
class LedWaterfallKernel final : public project_xs::sim::Kernel {
  public:
    LedWaterfallKernel(std::string label, std::uint32_t cnt_max)
        : LedWaterfallKernel(std::move(label),
                             std::make_shared<CounterComponent>(cnt_max),
                             std::make_shared<LedShiftComponent>()) {}

    // 顶层 kernel 的深复制：
    // - 先各自 clone 两个 component
    // - 再重建它们之间的端口连接
    // - 最后复制 kernel 自身的运行时状态
    std::shared_ptr<project_xs::sim::Kernel> clone() const override {
        auto counter_copy =
            std::dynamic_pointer_cast<CounterComponent>(counter_->clone());
        auto shifter_copy =
            std::dynamic_pointer_cast<LedShiftComponent>(shifter_->clone());
        auto copy = std::shared_ptr<LedWaterfallKernel>(
            new LedWaterfallKernel(name(), counter_copy, shifter_copy));
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
        led_output_.value() = 0x01;
    }

    void run_single(std::uint64_t cycle) override {
        // 先按注册顺序推进内部两个 component。
        // counter 先决定 count / wrap_pulse，
        // led_shift 再据此决定当前拍的 led。
        project_xs::sim::Kernel::run_single(cycle);

        // 对外只暴露 led 的最终结果。
        led_output_.value() = shifter_->led_.value();
    }

    // 这个调试输出会在每拍自动打印，方便和 Verilog 里的内部寄存器语义对照。
    std::string debug_info(std::uint64_t cycle) const override {
        return "[" + name() + "][cycle " + std::to_string(cycle) + "] rst_n=" +
               std::string(counter_->rst_n_.value() ? "1" : "0") + " cnt=" +
               std::to_string(counter_->count_.value()) + " wrap=" +
               std::string(counter_->wrap_pulse_.value() ? "1" : "0") + " led=0x" +
               hex_byte(led_output_.value());
    }

  private:
    LedWaterfallKernel(std::string label,
                       std::shared_ptr<CounterComponent> counter,
                       std::shared_ptr<LedShiftComponent> shifter)
        : project_xs::sim::Kernel(std::move(label)),
          counter_(std::move(counter)),
          shifter_(std::move(shifter)),
          led_output_("led", "顶层对外 LED 输出", std::uint8_t{0x01}, 8) {
        mutable_state_set().register_state(led_output_);
        // 这里对应 Verilog 里“led <= ...”那个输出寄存器的可见观测面。
        // kernel 自身不直接实现状态机，而是把内部 component 的结果汇总后对外输出。
        shifter_->ports().get_input("wrap_pulse")->connect(
            counter_->ports().get_output("wrap_pulse"));

        ports().add_output(led_output_.make_wire_output_port());

        // 默认顺序是 counter 先更新，再 led_shift 更新。
        // 由于当前框架已经恢复成“按 vector 顺序逐个执行完整 run()”，
        // 这个顺序本身就是时序关系的一部分。
        add_component(counter_);
        add_component(shifter_);
    }

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
    project_xs::sim::State<std::uint8_t> led_output_;
};

}  // namespace project_xs::sim::test::led_waterfall

#endif
