#include "RiscvRegFileKernel.hpp"

#include "base/CycleSimulator.h"
#include "base/Port.h"
#include "base/SimulationSession.h"
#include "base/State.h"

#include <cstdint>
#include <iostream>
#include <memory>

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
        : project_xs::sim::CycleSimulator(std::move(name), max_cycles, frequency_hz),
          rs1_addr_("rs1_addr", "驱动读端口 1 地址", std::uint8_t{0}, 5),
          rs2_addr_("rs2_addr", "驱动读端口 2 地址", std::uint8_t{0}, 5),
          rd_addr_("rd_addr", "驱动写回地址", std::uint8_t{0}, 5),
          rd_wdata_("rd_wdata", "驱动写回数据", 0),
          rd_we_("rd_we", "驱动写回使能", false) {
        mutable_state_set().register_state(rs1_addr_);
        mutable_state_set().register_state(rs2_addr_);
        mutable_state_set().register_state(rd_addr_);
        mutable_state_set().register_state(rd_wdata_);
        mutable_state_set().register_state(rd_we_);

        ports().add_output(rs1_addr_.make_wire_output_port());
        ports().add_output(rs2_addr_.make_wire_output_port());
        ports().add_output(rd_addr_.make_wire_output_port());
        ports().add_output(rd_wdata_.make_wire_output_port());
        ports().add_output(rd_we_.make_wire_output_port());
    }

  private:
    void reset_extra() override {
        rs1_addr_ = std::uint8_t{0};
        rs2_addr_ = std::uint8_t{0};
        rd_addr_ = std::uint8_t{0};
        rd_wdata_ = 0;
        rd_we_ = false;
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

        rs1_addr_ = kReadSequence[cycle % 10];
        rs2_addr_ = kReadSequence2[cycle % 10];

        // cycle 3 故意写 zero/x0，验证标准 RISC-V 约束“x0 恒为 0”。
        rd_addr_ = kWriteSequence[cycle % 10];

        rd_wdata_ = 100 + cycle * 7;
        rd_we_ = true;
    }

    project_xs::sim::State<std::uint8_t> rs1_addr_;
    project_xs::sim::State<std::uint8_t> rs2_addr_;
    project_xs::sim::State<std::uint8_t> rd_addr_;
    project_xs::sim::State<std::uint64_t> rd_wdata_;
    project_xs::sim::State<bool> rd_we_;
};

}  // namespace

int main() {
    project_xs::sim::SimulationSession session(1.0L, 10.0L);
    auto simulator =
        std::make_shared<RiscvRegFileSimulator>("riscv_regfile_simulator", 10, 1.0L);

    simulator->add_kernel(
        std::make_shared<project_xs::sim::test::riscv_regfile::RiscvRegFileKernel>(
            "riscv_regfile"));

    session.add_simulator(simulator);
    session.reset();
    session.initialize_zero();

    std::cout << session.start_info() << "\n";
    session.run();
    std::cout << session.finish_info() << "\n";

    return 0;
}
