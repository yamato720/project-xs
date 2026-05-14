#include "RiscvRegFileKernel.hpp"

#include "base/CycleSimulator.h"
#include "base/Port.h"
#include "base/SimulationSession.h"
#include "base/State.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {

// 寄存器组测试驱动 simulator。
// 它每个周期都会驱动一组标准寄存器组输入：
// - 周期递增地切换 rs1/rs2 读地址
// - 周期递增地切换 rd 写地址
// - 给 rd_wdata 一个易于人工检查的值模式
// - 某一拍故意写 x0，验证 x0 不可被改坏
class RiscvRegFileSimulator final : public project_xs::sim::CycleSimulator {
  public:
    // 构造寄存器组测试驱动 simulator。
    RiscvRegFileSimulator(std::string name, std::int64_t max_cycles, long double frequency_hz)
        : project_xs::sim::CycleSimulator(std::move(name), max_cycles, frequency_hz) {
        set_description("RISC-V 寄存器堆测试驱动：按固定序列产生读地址、写地址、写数据和写使能。");
        add_name_alias("regfile_driver");
        add_name_alias("寄存器堆驱动");
        create_state<std::uint8_t>("rs1_addr", "驱动读端口 1 地址", std::uint8_t{0}, 5);
        create_state<std::uint8_t>("rs2_addr", "驱动读端口 2 地址", std::uint8_t{0}, 5);
        create_state<std::uint8_t>("rd_addr", "驱动写回地址", std::uint8_t{0}, 5);
        create_state<std::uint64_t>("rd_wdata", "驱动写回数据", 0);
        create_state<bool>("rd_we", "驱动写回使能", false);
        ports().add_output(state<std::uint8_t>("rs1_addr").make_wire_output_port());
        ports().add_output(state<std::uint8_t>("rs2_addr").make_wire_output_port());
        ports().add_output(state<std::uint8_t>("rd_addr").make_wire_output_port());
        ports().add_output(state<std::uint64_t>("rd_wdata").make_wire_output_port());
        ports().add_output(state<bool>("rd_we").make_wire_output_port());
    }

  private:
    void reset_extra() override {
        state<std::uint8_t>("rs1_addr") = std::uint8_t{0};
        state<std::uint8_t>("rs2_addr") = std::uint8_t{0};
        state<std::uint8_t>("rd_addr") = std::uint8_t{0};
        state<std::uint64_t>("rd_wdata") = 0;
        state<bool>("rd_we") = false;
    }

    void run_single(std::uint64_t cycle) override {
        static constexpr std::uint8_t kReadSequence[] = {
            5, 10, 11, 12, 0, 5, 10, 11, 12, 5
        };
        static constexpr std::uint8_t kReadSequence2[] = {
            10, 11, 12, 5, 0, 11, 12, 5, 10, 11
        };
        static constexpr std::uint8_t kWriteSequence[] = {
            5, 10, 11, 0, 12, 5, 10, 11, 12, 5
        };

        state<std::uint8_t>("rs1_addr") = kReadSequence[cycle % 10];
        state<std::uint8_t>("rs2_addr") = kReadSequence2[cycle % 10];

        // cycle 3 故意写 zero/x0，验证标准 RISC-V 约束“x0 恒为 0”。
        state<std::uint8_t>("rd_addr") = kWriteSequence[cycle % 10];

        state<std::uint64_t>("rd_wdata") = 100 + cycle * 7;
        state<bool>("rd_we") = true;
    }
};

// 第一组：完全不使用 snapshot，按正常 session 入口从 0 跑到 max_cycles。
void run_without_snapshot_demo() {
    std::cout << "\n[demo_without_snapshot] begin\n";

    project_xs::sim::SimulationSession session(1.0L, 10.0L);
    auto simulator = std::make_shared<RiscvRegFileSimulator>(
        "riscv_regfile_without_snapshot_simulator",
        10,
        1.0L);
    simulator->add_kernel(
        project_xs::sim::test::riscv_regfile::build_riscv_regfile_kernel(
            "riscv_regfile_without_snapshot"));

    session.add_simulator(simulator);
    session.reset();
    session.initialize_zero();

    std::cout << session.start_info() << "\n";
    session.run();
    std::cout << session.finish_info() << "\n";
}

// 第二组：先跑到某个周期保存 snapshot，再恢复到另一份同构 simulator 上继续跑。
void run_with_snapshot_demo() {
    std::cout << "\n[demo_with_snapshot] begin\n";

    const std::string trace_dir = "test/riscv_regfile/trace";

    // 这里用 session 级 snapshot。
    // session=2Hz、simulator=1Hz，保存点选在 session tick=5，
    // 此时 session 调度累计器不是 0，可以一起验证 accumulated_hz 被恢复。
    project_xs::sim::SimulationSession source_session(2.0L, 10.0L);
    source_session.set_snapshot_capture_directory(trace_dir);
    auto source_simulator = std::make_shared<RiscvRegFileSimulator>(
        "riscv_regfile_with_snapshot_simulator",
        10,
        1.0L);
    source_simulator->add_kernel(
        project_xs::sim::test::riscv_regfile::build_riscv_regfile_kernel(
            "riscv_regfile_with_snapshot"));
    source_session.add_simulator(source_simulator);
    source_session.reset();
    source_session.initialize_zero();

    // 先跑几拍，让寄存器数组、stage phase、reg 端口 pending/visible 状态都不再是初始 0。
    // 如果只在第 0 拍保存，很容易漏掉真正的运行态恢复问题。
    std::cout << "[demo_with_snapshot] run source to checkpoint\n";
    source_session.start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Automatic,
                                          "riscv_regfile_source_auto");
    while (source_session.current_tick() < 5) {
        source_session.step();
    }
    source_session.stop_snapshot_capture("riscv_regfile_source_auto");
    std::cout << "[demo_with_snapshot] automatic session snapshot records="
              << source_session.snapshot_history().size() << "\n";

    // 手动挡只在显式 capture_snapshot() 时采样；这里用它保存恢复点。
    // 捕获到的 session 快照包含 session 自己的 current_tick / 调度累计器，
    // 以及内部 simulator/kernel/component 的运行态。
    source_session.start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Manual,
                                          "riscv_regfile_restore_point");
    const auto& checkpoint_record =
        source_session.capture_snapshot(project_xs::sim::SnapshotCaptureStage::Manual);
    const std::string checkpoint_path = checkpoint_record.checkpoint_path;
    source_session.stop_snapshot_capture("riscv_regfile_restore_point");
    std::cout << "[demo_with_snapshot] saved snapshot at session_tick="
              << source_session.current_tick()
              << ", simulator_cycle=" << source_simulator->current_cycle()
              << ", checkpoint=" << checkpoint_path << "\n";

    // 原 session 可以继续往前跑；后面的 restored session 会从保存点重新开始。
    std::cout << "[demo_with_snapshot] source session takes one extra step after snapshot\n";
    source_session.step();
    std::cout << "[demo_with_snapshot] source session_tick="
              << source_session.current_tick()
              << ", simulator_cycle=" << source_simulator->current_cycle() << "\n";

    // 重新构造一份同名、同结构、同连接顺序的 session/simulator 对象图，
    // 再把 session snapshot 灌进去。
    // 这里仍然直接使用 base 的 add_kernel() 完成装配；名字也要一致，
    // 因为 snapshot 会校验 simulator/kernel/component 名字和布局。
    project_xs::sim::SimulationSession restored_session(2.0L, 10.0L);
    restored_session.set_snapshot_capture_directory(trace_dir);
    auto restored_simulator = std::make_shared<RiscvRegFileSimulator>(
        "riscv_regfile_with_snapshot_simulator",
        10,
        1.0L);
    restored_simulator->add_kernel(
        project_xs::sim::test::riscv_regfile::build_riscv_regfile_kernel(
            "riscv_regfile_with_snapshot"));
    restored_session.add_simulator(restored_simulator);
    restored_session.restore_checkpoint(checkpoint_path);
    std::cout << "[demo_with_snapshot] restored session_tick="
              << restored_session.current_tick()
              << ", simulator_cycle=" << restored_simulator->current_cycle() << "\n";

    // 从恢复点继续跑到 session 结束。
    std::cout << "[demo_with_snapshot] continue restored session\n";
    restored_session.run();
    std::cout << restored_session.finish_info() << "\n";
}

}  // namespace

int main() {
    run_without_snapshot_demo();
    run_with_snapshot_demo();

    return 0;
}
