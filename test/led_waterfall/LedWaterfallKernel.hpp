#ifndef PROJECT_XS_TEST_LED_WATERFALL_KERNEL_HPP
#define PROJECT_XS_TEST_LED_WATERFALL_KERNEL_HPP

#include "base/CycleSimulator.h"
#include "base/Error.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/State.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

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
        : project_xs::sim::KernelComponent("counter") {
        set_description("流水灯分频计数组件；对应 Verilog 内部 cnt 寄存器和 cnt==CNT_MAX-1 的回卷判断");
        add_name_alias("cnt_block");
        add_name_alias("verilog_cnt");

        create_state<bool>("rst_n", "来自外部环境的低有效复位；0 时清零 count 并取消 wrap_pulse", false);
        create_state<bool>("wrap_pulse", "count 到达 cnt_max-1 时产生的一拍脉冲；驱动 led_shift 更新 LED", false);
        create_state<std::uint32_t>("count", "当前分频计数值；对应 Verilog reg [24:0] cnt", 0, 25);
        create_state<std::uint32_t>("cnt_max", "计数回卷阈值；测试中设为 4，对应 Verilator 参数 CNT_MAX=4", cnt_max, 25);
        if (state<std::uint32_t>("cnt_max").value() == 0) {
            project_xs::sim::error::raise(
                project_xs::sim::error::Stage::Elaboration,
                project_xs::sim::error::Kind::ConstraintViolation,
                "CounterComponent",
                "cnt_max must be > 0");
        }

        ports().add_input(state<bool>("rst_n").make_wire_input_port("rst_n"));
        ports().add_output(state<bool>("wrap_pulse").make_wire_output_port("wrap_pulse"));
        ports().add_output(state<std::uint32_t>("count").make_wire_output_port("cnt"));

    }
    std::uint32_t cnt_max() const { return state<std::uint32_t>("cnt_max").value(); }

    bool rst_n_value() const { return state<bool>("rst_n").value(); }

    bool wrap_pulse_value() const { return state<bool>("wrap_pulse").value(); }

    std::uint32_t count_value() const { return state<std::uint32_t>("count").value(); }

  protected:
    void reset_extra() override {
        state<bool>("rst_n") = false;
        state<bool>("wrap_pulse") = false;
        state<std::uint32_t>("count") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!state<bool>("rst_n").value()) {
            state<std::uint32_t>("count") = 0;
            state<bool>("wrap_pulse") = false;
            return;
        }

        if (state<std::uint32_t>("count").value() ==
            state<std::uint32_t>("cnt_max").value() - 1) {
            state<std::uint32_t>("count") = 0;
            state<bool>("wrap_pulse") = true;
        } else {
            ++state<std::uint32_t>("count");
            state<bool>("wrap_pulse") = false;
        }
    }
};

// LED 组件：
// - 接收 rst_n 和 wrap_pulse
// - wrap 时左移 led，最高位后回到最低位
class LedShiftComponent final : public project_xs::sim::KernelComponent {
  public:
    LedShiftComponent()
        : project_xs::sim::KernelComponent("led_shift") {
        set_description("流水灯移位组件；对应 Verilog 中 led 寄存器在 wrap 时左移或回到 0x01 的逻辑");
        add_name_alias("led_block");
        add_name_alias("verilog_led");

        create_state<bool>("rst_n", "来自外部环境的低有效复位；0 时 led 强制为 0x01", false);
        create_state<bool>("wrap_pulse", "来自 counter 的一拍回卷脉冲；为 1 时 led 左移", false);
        create_state<std::uint8_t>("led", "当前 LED 8 位显示模式；对应 Verilog output reg [7:0] led", std::uint8_t{0x01}, 8);

        ports().add_input(state<bool>("rst_n").make_wire_input_port("rst_n"));
        ports().add_input(state<bool>("wrap_pulse").make_wire_input_port("wrap_pulse"));
        ports().add_output(state<std::uint8_t>("led").make_wire_output_port("led"));

    }

    std::uint8_t led_value() const { return state<std::uint8_t>("led").value(); }

  protected:
    void reset_extra() override {
        state<bool>("rst_n") = false;
        state<bool>("wrap_pulse") = false;
        state<std::uint8_t>("led") = 0x01;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!state<bool>("rst_n").value()) {
            state<std::uint8_t>("led") = 0x01;
            return;
        }

        if (!state<bool>("wrap_pulse").value()) {
            return;
        }

        if (state<std::uint8_t>("led").value() == 0x80) {
            state<std::uint8_t>("led") = 0x01;
        } else {
            state<std::uint8_t>("led") =
                static_cast<std::uint8_t>(state<std::uint8_t>("led").value() << 1);
        }
    }
};

// 顶层 kernel：
// - 组件 A(counter) 根据 cycle 计数
// - 组件 B(led_shift) 接收 wrap_pulse 更新 led
// - simulator 负责提供 rst_n
class LedWaterfallKernel final : public project_xs::sim::Kernel {
  public:
    LedWaterfallKernel(std::string label,
                       std::shared_ptr<CounterComponent> counter,
                       std::shared_ptr<LedShiftComponent> shifter)
        : project_xs::sim::Kernel(std::move(label)),
          counter_(std::move(counter)),
          shifter_(std::move(shifter)) {
        if (!counter_ || !shifter_) {
            project_xs::sim::error::raise(
                project_xs::sim::error::Stage::Elaboration,
                project_xs::sim::error::Kind::InvalidArgument,
                "LedWaterfallKernel",
                "requires prebuilt counter and led_shift components");
        }

        set_description("流水灯顶层 kernel；组合 counter 和 led_shift，行为用于对照 test/led_waterfall/verilator_sim 的 Verilog/VCD");
        add_name_alias("led_waterfall_top");
        add_name_alias("verilog_led_waterfall");

        create_state<std::uint8_t>("led", "顶层对外 LED 输出；从 led_shift.led 汇总得到，对照 Verilog 顶层 led", std::uint8_t{0x01}, 8);
        ports().add_output(state<std::uint8_t>("led").make_wire_output_port("led"));

        require_component("counter");
        require_component("led_shift");

        add_component(counter_);
        add_component(shifter_);
    }

  protected:
    // rst_n 不是从别的 kernel 来，而是从 simulator 自带默认 portgroup 提供。
    // 这正对应“外部环境/板级输入由 simulator 自己提供”的建模方式。
    void on_attached_to_simulator(project_xs::sim::CycleSimulator& simulator) override {
        counter_->ports().get_input("rst_n")->connect(simulator.ports().get_output("rst_n"));
        shifter_->ports().get_input("rst_n")->connect(simulator.ports().get_output("rst_n"));
    }

    void reset_extra() override {
        state<std::uint8_t>("led") = 0x01;
    }

    void run_single(std::uint64_t cycle) override {
        // 先按注册顺序推进内部两个 component。
        // counter 先决定 count / wrap_pulse，
        // led_shift 再据此决定当前拍的 led。
        project_xs::sim::Kernel::run_single(cycle);

        // 对外只暴露 led 的最终结果。
        state<std::uint8_t>("led") = shifter_->led_value();
    }

    // 这个调试输出会在每拍自动打印，方便和 Verilog 里的内部寄存器语义对照。
    std::string debug_info(std::uint64_t cycle) const override {
        return "[" + name() + "][cycle " + std::to_string(cycle) + "] rst_n=" +
               std::string(counter_->rst_n_value() ? "1" : "0") + " cnt=" +
               std::to_string(counter_->count_value()) + " wrap=" +
               std::string(counter_->wrap_pulse_value() ? "1" : "0") +
               " led=0x" + hex_byte(state<std::uint8_t>("led").value());
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
    // 是为了让 on_attached_to_simulator()/debug_info() 能直接访问它们的端口和状态。
    std::shared_ptr<CounterComponent> counter_;
    std::shared_ptr<LedShiftComponent> shifter_;
};

inline std::shared_ptr<LedWaterfallKernel> build_led_waterfall_kernel(std::string label,
                                                                      std::uint32_t cnt_max) {
    auto counter = std::make_shared<CounterComponent>(cnt_max);
    auto shifter = std::make_shared<LedShiftComponent>();

    // 组件实例化和局部连线放在外部装配层，避免把 kernel 内部构造 component
    // 当作后续自动生成器必须直接生成的形态。
    shifter->ports().get_input("wrap_pulse")->connect(counter->ports().get_output("wrap_pulse"));

    return std::make_shared<LedWaterfallKernel>(
        std::move(label),
        std::move(counter),
        std::move(shifter));
}

}  // namespace project_xs::sim::test::led_waterfall

#endif
