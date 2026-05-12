#ifndef PROJECT_XS_TEST_RISCV_REGFILE_KERNEL_HPP
#define PROJECT_XS_TEST_RISCV_REGFILE_KERNEL_HPP

#include "base/CycleSimulator.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/State.h"
#include "base/StateArray.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace project_xs::sim::test::riscv_regfile {

// RISC-V 整数寄存器 ABI 名表。
// 这些“看起来有点莫名其妙”的名字来自 RISC-V 调用约定（ABI）：
// - zero / ra / sp / gp / tp：固定语义寄存器
// - t0~t6：temporary，临时寄存器
// - s0~s11：saved，需由被调用者保存
// - a0~a7：argument / return，参数与返回值寄存器
//
// 所以在汇编、反汇编和大多数工具链输出里，通常优先看到的是这些 ABI 名，
// 而不是纯硬件编号 x0~x31。
inline constexpr const char* kRiscvAbiNames[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10","s11","t3", "t4", "t5", "t6",
};

inline std::string riscv_reg_name(std::uint8_t index) {
    if (index < 32) {
        return kRiscvAbiNames[index];
    }
    return "x?";
}

inline std::size_t riscv_row(std::uint8_t index) {
    return static_cast<std::size_t>(index / 8U);
}

inline std::size_t riscv_col(std::uint8_t index) {
    return static_cast<std::size_t>(index % 8U);
}

// 共享寄存器组状态块。
// 真正的寄存器阵列现在用二维 4x8 的 StateArray<uint64_t> 表达：
// - 第一维：行，共 4 行
// - 第二维：列，每行 8 个
//
// 逻辑上仍然对应标准 32 个整数寄存器，只是存储体改成二维布局，
// 便于验证当前高维数组能力。
struct RegFileSharedState {
    RegFileSharedState() : regs("gpr", "RISC-V 32 个通用整数寄存器，按 4x8 二维组织", {4, 8}, 0ULL) {
        for (std::uint8_t index = 0; index < 32; ++index) {
            regs.set_element_name({riscv_row(index), riscv_col(index)}, kRiscvAbiNames[index]);
            regs.add_element_alias({riscv_row(index), riscv_col(index)}, "x" + std::to_string(index));
        }
    }

    // 32 个通用寄存器，按二维 4x8 存储。
    StateArray<std::uint64_t> regs;

    // 三段流水共享的输入采样结果。
    std::uint8_t rs1_addr = 0;
    std::uint8_t rs2_addr = 0;
    std::uint8_t rd_addr = 0;
    std::uint64_t rd_wdata = 0;
    bool rd_we = false;

    // 三段流水共享的两个读口结果。
    std::uint64_t rs1_rdata = 0;
    std::uint64_t rs2_rdata = 0;
};

// 第一段：输入采样。
// 只负责把外部端口上的 rs1/rs2/rd 输入采样进共享状态。
class RegFileInputStage final : public project_xs::sim::KernelComponent {
  public:
    explicit RegFileInputStage(RegFileSharedState& shared_state)
        : project_xs::sim::KernelComponent("regfile_input_stage"),
          shared_state_(shared_state),
          rs1_addr_("rs1_addr", "读端口 1 地址", std::uint8_t{0}, 5),
          rs2_addr_("rs2_addr", "读端口 2 地址", std::uint8_t{0}, 5),
          rd_addr_("rd_addr", "写回地址", std::uint8_t{0}, 5),
          rd_wdata_("rd_wdata", "写回数据", 0),
          rd_we_("rd_we", "写回使能", false) {
        mutable_state_set().register_state(rs1_addr_);
        mutable_state_set().register_state(rs2_addr_);
        mutable_state_set().register_state(rd_addr_);
        mutable_state_set().register_state(rd_wdata_);
        mutable_state_set().register_state(rd_we_);

        ports().add_input(rs1_addr_.make_wire_input_port());
        ports().add_input(rs2_addr_.make_wire_input_port());
        ports().add_input(rd_addr_.make_wire_input_port());
        ports().add_input(rd_wdata_.make_wire_input_port());
        ports().add_input(rd_we_.make_wire_input_port());
    }

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        return nullptr;
    }

  protected:
    void reset_extra() override {
        rs1_addr_ = std::uint8_t{0};
        rs2_addr_ = std::uint8_t{0};
        rd_addr_ = std::uint8_t{0};
        rd_wdata_ = 0;
        rd_we_ = false;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        shared_state_.rs1_addr = rs1_addr_.value();
        shared_state_.rs2_addr = rs2_addr_.value();
        shared_state_.rd_addr = rd_addr_.value();
        shared_state_.rd_wdata = rd_wdata_.value();
        shared_state_.rd_we = rd_we_.value();
    }

  private:
    RegFileSharedState& shared_state_;
    project_xs::sim::State<std::uint8_t> rs1_addr_;
    project_xs::sim::State<std::uint8_t> rs2_addr_;
    project_xs::sim::State<std::uint8_t> rd_addr_;
    project_xs::sim::State<std::uint64_t> rd_wdata_;
    project_xs::sim::State<bool> rd_we_;
};

// 第二段：写回执行。
// 只负责处理 rd 写回，并维持 zero/x0 恒为 0。
class RegFileWriteStage final : public project_xs::sim::KernelComponent {
  public:
    explicit RegFileWriteStage(RegFileSharedState& shared_state)
        : project_xs::sim::KernelComponent("regfile_write_stage"), shared_state_(shared_state) {}

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        return nullptr;
    }

  protected:
    void run_single(std::uint64_t cycle) override {
        (void)cycle;

        // 标准 RISC-V 约束：zero / x0 永远为 0。
        shared_state_.regs.at_name("zero") = 0;

        if (shared_state_.rd_we && shared_state_.rd_addr != 0U && shared_state_.rd_addr < 32U) {
            shared_state_.regs.at({riscv_row(shared_state_.rd_addr), riscv_col(shared_state_.rd_addr)}) =
                shared_state_.rd_wdata;
        }

        shared_state_.regs.at_name("zero") = 0;
    }

  private:
    RegFileSharedState& shared_state_;
};

// 第三段：读口输出。
// 根据共享状态中的 rs1/rs2 地址，从二维寄存器存储体里给出两个读口数据。
class RegFileReadStage final : public project_xs::sim::KernelComponent {
  public:
    explicit RegFileReadStage(RegFileSharedState& shared_state)
        : project_xs::sim::KernelComponent("regfile_read_stage"),
          shared_state_(shared_state),
          rs1_rdata_("rs1_rdata", "读端口 1 数据", 0),
          rs2_rdata_("rs2_rdata", "读端口 2 数据", 0) {
        mutable_state_set().register_state(rs1_rdata_);
        mutable_state_set().register_state(rs2_rdata_);

        ports().add_output(rs1_rdata_.make_wire_output_port());
        ports().add_output(rs2_rdata_.make_wire_output_port());
    }

    std::shared_ptr<project_xs::sim::KernelComponent> clone() const override {
        return nullptr;
    }

  protected:
    void reset_extra() override {
        rs1_rdata_ = 0;
        rs2_rdata_ = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        shared_state_.rs1_rdata = read_reg(shared_state_.rs1_addr);
        shared_state_.rs2_rdata = read_reg(shared_state_.rs2_addr);
        rs1_rdata_ = shared_state_.rs1_rdata;
        rs2_rdata_ = shared_state_.rs2_rdata;
    }

  private:
    std::uint64_t read_reg(std::uint8_t addr) const {
        if (addr == 0U || addr >= 32U) {
            return 0;
        }
        return shared_state_.regs.at({riscv_row(addr), riscv_col(addr)});
    }

    RegFileSharedState& shared_state_;
    project_xs::sim::State<std::uint64_t> rs1_rdata_;
    project_xs::sim::State<std::uint64_t> rs2_rdata_;
};

// 顶层 kernel。
// 把寄存器组拆成三段：
// 1. 输入采样
// 2. 写回执行
// 3. 读口输出
//
// 三个 component 会按 vector 注册顺序依次运行，
// 所以时序关系就是：
// 输入先进共享状态 -> 再写二维寄存器阵列 -> 再从二维阵列读两个读口。
class RiscvRegFileKernel final : public project_xs::sim::Kernel {
  public:
    explicit RiscvRegFileKernel(std::string name)
        : project_xs::sim::Kernel(std::move(name)),
          input_stage_(std::make_shared<RegFileInputStage>(shared_state_)),
          write_stage_(std::make_shared<RegFileWriteStage>(shared_state_)),
          read_stage_(std::make_shared<RegFileReadStage>(shared_state_)),
          rs1_rdata_("rs1_rdata", "顶层读端口 1 数据", 0),
          rs2_rdata_("rs2_rdata", "顶层读端口 2 数据", 0) {
        mutable_state_set().register_state(rs1_rdata_);
        mutable_state_set().register_state(rs2_rdata_);

        ports().add_output(rs1_rdata_.make_wire_output_port());
        ports().add_output(rs2_rdata_.make_wire_output_port());

        add_component(input_stage_);
        add_component(write_stage_);
        add_component(read_stage_);
    }

    std::shared_ptr<project_xs::sim::Kernel> clone() const override {
        auto copy = std::make_shared<RiscvRegFileKernel>(name());
        copy->copy_kernel_runtime_from(*this);
        copy->shared_state_.regs.copy_values_from(shared_state_.regs);
        copy->shared_state_.rs1_addr = shared_state_.rs1_addr;
        copy->shared_state_.rs2_addr = shared_state_.rs2_addr;
        copy->shared_state_.rd_addr = shared_state_.rd_addr;
        copy->shared_state_.rd_wdata = shared_state_.rd_wdata;
        copy->shared_state_.rd_we = shared_state_.rd_we;
        copy->shared_state_.rs1_rdata = shared_state_.rs1_rdata;
        copy->shared_state_.rs2_rdata = shared_state_.rs2_rdata;
        return copy;
    }

  protected:
    void on_attached_to_simulator(project_xs::sim::CycleSimulator& simulator) override {
        input_stage_->ports().get_input("rs1_addr")->connect(
            simulator.ports().get_output("rs1_addr"));
        input_stage_->ports().get_input("rs2_addr")->connect(
            simulator.ports().get_output("rs2_addr"));
        input_stage_->ports().get_input("rd_addr")->connect(
            simulator.ports().get_output("rd_addr"));
        input_stage_->ports().get_input("rd_wdata")->connect(
            simulator.ports().get_output("rd_wdata"));
        input_stage_->ports().get_input("rd_we")->connect(
            simulator.ports().get_output("rd_we"));
    }

    void reset_extra() override {
        shared_state_ = RegFileSharedState{};
        rs1_rdata_ = 0;
        rs2_rdata_ = 0;
    }

    void run_single(std::uint64_t cycle) override {
        project_xs::sim::Kernel::run_single(cycle);
        rs1_rdata_ = shared_state_.rs1_rdata;
        rs2_rdata_ = shared_state_.rs2_rdata;
    }

    std::string debug_info(std::uint64_t cycle) const override {
        return "[" + name() + "][cycle " + std::to_string(cycle) + "] " +
               "rs1=" + riscv_reg_name(shared_state_.rs1_addr) + " -> " +
               std::to_string(shared_state_.rs1_rdata) + ", rs2=" +
               riscv_reg_name(shared_state_.rs2_addr) + " -> " +
               std::to_string(shared_state_.rs2_rdata) + ", rd=" +
               std::string(shared_state_.rd_we ? "we " : "no-we ") +
               riscv_reg_name(shared_state_.rd_addr) + " <= " +
               std::to_string(shared_state_.rd_wdata) + ", t0=" +
               std::to_string(shared_state_.regs.at_name("t0")) + ", a0=" +
               std::to_string(shared_state_.regs.at_name("a0")) + ", a1=" +
               std::to_string(shared_state_.regs.at_name("a1")) + ", a2=" +
               std::to_string(shared_state_.regs.at_name("a2"));
    }

  private:
    RegFileSharedState shared_state_;
    std::shared_ptr<RegFileInputStage> input_stage_;
    std::shared_ptr<RegFileWriteStage> write_stage_;
    std::shared_ptr<RegFileReadStage> read_stage_;
    project_xs::sim::State<std::uint64_t> rs1_rdata_;
    project_xs::sim::State<std::uint64_t> rs2_rdata_;
};

}  // namespace project_xs::sim::test::riscv_regfile

#endif
