#include "LedWaterfallKernel.hpp"

#include "base/CycleSimulator.h"
#include "base/Port.h"
#include "base/SimulationSession.h"

#include <cstdint>
#include <iostream>
#include <memory>

namespace {

// 这个 simulator 负责提供 led_waterfall 所需的外部输入 rst_n。
// 这里把“每个 step 就是一拍上升沿”作为抽象前提，
// 所以不再单独建一个 clk 端口，而是直接让 step() 本身代表时钟推进。
class LedWaterfallSimulator final : public project_xs::sim::CycleSimulator {
  public:
    LedWaterfallSimulator(std::int64_t max_cycles, long double frequency_hz)
        : project_xs::sim::CycleSimulator(max_cycles, frequency_hz),
          rst_n_(false) {
        ports().add_output(project_xs::sim::WirePort::make_output<bool>("rst_n", &rst_n_));
    }

  private:
    void reset_extra() override {
        rst_n_ = false;
    }

    void run_single(std::uint64_t cycle) override {
        // 这里故意把 reset 行为写得很简单：
        // - cycle 0 保持复位
        // - 从 cycle 1 开始释放复位
        //
        // 这样输出里能直观看到：
        // - 第 0 拍 led 被复位到 0x01
        // - 后续 cnt 正常累加
        rst_n_ = (cycle != 0);
    }

    bool rst_n_;
};

}  // namespace

int main() {
    // 为了让输出更容易人工检查，这里把 session 和 simulator 都设成 1Hz：
    // - 1 个 session tick = 1 秒
    // - 1 个 simulator step = 1 个周期
    //
    // run_time = 12 秒，意味着总共跑 12 个周期。
    project_xs::sim::SimulationSession session(1.0L, 12.0L);

    auto simulator = std::make_shared<LedWaterfallSimulator>(12, 1.0L);

    // 这里把 CNT_MAX 设成 4，而不是 Verilog 里的 25_000_000，
    // 只是为了在短测试里更快看到 led 移动。
    // 对应效果就是每 4 拍 wrap 一次：
    // cycle 4 -> led 从 0x01 变成 0x02
    // cycle 8 -> led 从 0x02 变成 0x04
    simulator->add_kernel(
        std::make_shared<project_xs::sim::test::led_waterfall::LedWaterfallKernel>(
            "led_waterfall",
            4));

    // session 负责时间维度推进，simulator 负责周期维度推进。
    session.add_simulator(simulator);
    session.reset();
    session.initialize_zero();

    std::cout << session.start_info() << "\n";
    session.run();
    std::cout << session.finish_info() << "\n";

    return 0;
}
