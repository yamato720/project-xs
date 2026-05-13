#ifndef PROJECT_XS_TEST_RISCV_REGFILE_KERNEL_HPP
#define PROJECT_XS_TEST_RISCV_REGFILE_KERNEL_HPP

#include "base/CycleSimulator.h"
#include "base/Error.h"
#include "base/Kernel.h"
#include "base/KernelComponent.h"
#include "base/Port.h"
#include "base/State.h"
#include "base/StateArray.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace project_xs::sim::test::riscv_regfile {

// RISC-V 整数寄存器 ABI 名表。
// 这些“看起来有点莫名其妙”的名字来自 RISC-V 调用约定（ABI）：
// - zero / ra / sp / gp / tp：固定语义寄存器
// - t0~t6：temporary，临时寄存器
// - s0~s11：saved，需由被调用者保存
// - a0~a7：argument / return，参数与返回值寄存器
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

inline std::string riscv_reg_label(std::uint8_t index) {
    return riscv_reg_name(index) + "(x" + std::to_string(index) + ")";
}

inline std::size_t riscv_row(std::uint8_t index) {
    return static_cast<std::size_t>(index / 8U);
}

inline std::size_t riscv_col(std::uint8_t index) {
    return static_cast<std::size_t>(index % 8U);
}

// 第一段：输入采样。
// 只负责把外部输入采样后，经端口继续传给后面的访问段。
class RegFileInputStage final : public project_xs::sim::KernelComponent {
  public:
    RegFileInputStage()
        : project_xs::sim::KernelComponent("regfile_input_stage", 1, 0) {
        set_description("输入采样段：从 simulator 接收 rs1/rs2/rd/rd_wdata/rd_we，并在本拍采样后送入访问段。");
        add_name_alias("input");
        add_name_alias("输入采样段");
        add_name_alias("IF");
        create_state<std::uint8_t>("rs1_addr", "读端口 1 地址", std::uint8_t{0}, 5);
        create_state<std::uint8_t>("rs2_addr", "读端口 2 地址", std::uint8_t{0}, 5);
        create_state<std::uint8_t>("rd_addr", "写回地址", std::uint8_t{0}, 5);
        create_state<std::uint64_t>("rd_wdata", "写回数据", 0);
        create_state<bool>("rd_we", "写回使能", false);
        create_state<std::uint8_t>(
            "sampled_rs1_addr",
            "采样后的读端口 1 地址",
            std::uint8_t{0},
            5);
        create_state<std::uint8_t>(
            "sampled_rs2_addr",
            "采样后的读端口 2 地址",
            std::uint8_t{0},
            5);
        create_state<std::uint8_t>(
            "sampled_rd_addr",
            "采样后的写回地址",
            std::uint8_t{0},
            5);
        create_state<std::uint64_t>("sampled_rd_wdata", "采样后的写回数据", 0);
        create_state<bool>("sampled_rd_we", "采样后的写回使能", false);

        ports().add_input(state<std::uint8_t>("rs1_addr").make_wire_input_port());
        ports().add_input(state<std::uint8_t>("rs2_addr").make_wire_input_port());
        ports().add_input(state<std::uint8_t>("rd_addr").make_wire_input_port());
        ports().add_input(state<std::uint64_t>("rd_wdata").make_wire_input_port());
        ports().add_input(state<bool>("rd_we").make_wire_input_port());
        ports().add_output(state<std::uint8_t>("sampled_rs1_addr").make_wire_output_port("rs1_addr"));
        ports().add_output(state<std::uint8_t>("sampled_rs2_addr").make_wire_output_port("rs2_addr"));
        ports().add_output(state<std::uint8_t>("sampled_rd_addr").make_wire_output_port("rd_addr"));
        ports().add_output(state<std::uint64_t>("sampled_rd_wdata").make_wire_output_port("rd_wdata"));
        ports().add_output(state<bool>("sampled_rd_we").make_wire_output_port("rd_we"));
    }

  protected:
    void reset_extra() override {
        state<std::uint8_t>("rs1_addr") = std::uint8_t{0};
        state<std::uint8_t>("rs2_addr") = std::uint8_t{0};
        state<std::uint8_t>("rd_addr") = std::uint8_t{0};
        state<std::uint64_t>("rd_wdata") = 0;
        state<bool>("rd_we") = false;
        state<std::uint8_t>("sampled_rs1_addr") = std::uint8_t{0};
        state<std::uint8_t>("sampled_rs2_addr") = std::uint8_t{0};
        state<std::uint8_t>("sampled_rd_addr") = std::uint8_t{0};
        state<std::uint64_t>("sampled_rd_wdata") = 0;
        state<bool>("sampled_rd_we") = false;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        state<std::uint8_t>("sampled_rs1_addr") = state<std::uint8_t>("rs1_addr").value();
        state<std::uint8_t>("sampled_rs2_addr") = state<std::uint8_t>("rs2_addr").value();
        state<std::uint8_t>("sampled_rd_addr") = state<std::uint8_t>("rd_addr").value();
        state<std::uint64_t>("sampled_rd_wdata") = state<std::uint64_t>("rd_wdata").value();
        state<bool>("sampled_rd_we") = state<bool>("rd_we").value();
    }

    std::string debug_info(std::uint64_t cycle) const override {
        return "[regfile_input_stage][cycle " + std::to_string(cycle) + "] "
               "sample rs1=" + riscv_reg_label(state<std::uint8_t>("rs1_addr").value()) +
               ", rs2=" + riscv_reg_label(state<std::uint8_t>("rs2_addr").value()) +
               ", rd=" + riscv_reg_label(state<std::uint8_t>("rd_addr").value()) +
               ", rd_wdata=" + std::to_string(state<std::uint64_t>("rd_wdata").value()) +
               ", rd_we=" + std::string(state<bool>("rd_we").value() ? "1" : "0");
    }
};

// 第二段：寄存器访问。
// 它独占持有真正的寄存器存储体，所有阶段信号都通过端口进入，不共享控制状态。
class RegFileAccessStage final : public project_xs::sim::KernelComponent {
  public:
    RegFileAccessStage()
        : project_xs::sim::KernelComponent("regfile_access_stage", 1, 1) {
        set_description("寄存器访问段：持有 32 个 RISC-V GPR，处理写回、x0 恒 0 约束和两个读口输出。");
        add_name_alias("access");
        add_name_alias("寄存器访问段");
        add_name_alias("RF");
        create_state<std::uint8_t>(
            "rs1_addr",
            "访问段输入的读端口 1 地址",
            std::uint8_t{0},
            5);
        create_state<std::uint8_t>(
            "rs2_addr",
            "访问段输入的读端口 2 地址",
            std::uint8_t{0},
            5);
        create_state<std::uint8_t>(
            "rd_addr",
            "访问段输入的写回地址",
            std::uint8_t{0},
            5);
        create_state<std::uint64_t>("rd_wdata", "访问段输入的写回数据", 0);
        create_state<bool>("rd_we", "访问段输入的写回使能", false);
        create_state<std::uint64_t>("rs1_rdata", "访问段输出的读端口 1 数据", 0);
        create_state<std::uint64_t>("rs2_rdata", "访问段输出的读端口 2 数据", 0);
        create_state_array<std::uint64_t>(
            "gpr",
            "RISC-V 32 个通用整数寄存器，按 4x8 二维组织",
            {4, 8},
            0ULL);
        for (std::uint8_t index = 0; index < 32; ++index) {
            state_array<std::uint64_t>("gpr").set_element_name(
                {riscv_row(index), riscv_col(index)},
                kRiscvAbiNames[index]);
            state_array<std::uint64_t>("gpr").add_element_alias(
                {riscv_row(index), riscv_col(index)},
                "x" + std::to_string(index));
        }
        require_state_array_shape(state_array<std::uint64_t>("gpr"), {4, 8}, "gpr");

        ports().add_input(state<std::uint8_t>("rs1_addr").make_reg_input_port());
        ports().add_input(state<std::uint8_t>("rs2_addr").make_reg_input_port());
        ports().add_input(state<std::uint8_t>("rd_addr").make_reg_input_port());
        ports().add_input(state<std::uint64_t>("rd_wdata").make_reg_input_port());
        ports().add_input(state<bool>("rd_we").make_reg_input_port());
        ports().add_output(state<std::uint64_t>("rs1_rdata").make_wire_output_port());
        ports().add_output(state<std::uint64_t>("rs2_rdata").make_wire_output_port());
    }

    std::uint64_t reg_value(std::uint8_t index) const {
        if (index == 0U || index >= 32U) {
            return 0;
        }
        return state_array<std::uint64_t>("gpr").at({riscv_row(index), riscv_col(index)});
    }

  protected:
    void reset_extra() override {
        state_array<std::uint64_t>("gpr").fill(0ULL);
        state<std::uint8_t>("rs1_addr") = std::uint8_t{0};
        state<std::uint8_t>("rs2_addr") = std::uint8_t{0};
        state<std::uint8_t>("rd_addr") = std::uint8_t{0};
        state<std::uint64_t>("rd_wdata") = 0;
        state<bool>("rd_we") = false;
        state<std::uint64_t>("rs1_rdata") = 0;
        state<std::uint64_t>("rs2_rdata") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;

        if (!phase_valid()) {
            return;
        }

        state_array<std::uint64_t>("gpr").at_name("zero") = 0;

        if (state<bool>("rd_we").value() &&
            state<std::uint8_t>("rd_addr").value() != 0U &&
            state<std::uint8_t>("rd_addr").value() < 32U) {
            state_array<std::uint64_t>("gpr").at({
                riscv_row(state<std::uint8_t>("rd_addr").value()),
                riscv_col(state<std::uint8_t>("rd_addr").value()),
            }) = state<std::uint64_t>("rd_wdata").value();
        }

        state_array<std::uint64_t>("gpr").at_name("zero") = 0;
        state<std::uint64_t>("rs1_rdata") = read_reg(state<std::uint8_t>("rs1_addr").value());
        state<std::uint64_t>("rs2_rdata") = read_reg(state<std::uint8_t>("rs2_addr").value());
    }

    std::string debug_info(std::uint64_t cycle) const override {
        if (!phase_valid()) {
            return "[regfile_access_stage][cycle " + std::to_string(cycle) + "] waiting for align";
        }
        return "[regfile_access_stage][cycle " + std::to_string(cycle) + "] "
               "write " + std::string(state<bool>("rd_we").value() ? "enable " : "disable ") +
               riscv_reg_label(state<std::uint8_t>("rd_addr").value()) + " <= " +
               std::to_string(state<std::uint64_t>("rd_wdata").value()) + ", read rs1=" +
               riscv_reg_label(state<std::uint8_t>("rs1_addr").value()) + " -> " +
               std::to_string(state<std::uint64_t>("rs1_rdata").value()) + ", rs2=" +
               riscv_reg_label(state<std::uint8_t>("rs2_addr").value()) + " -> " +
               std::to_string(state<std::uint64_t>("rs2_rdata").value()) + ", t0=" +
               std::to_string(state_array<std::uint64_t>("gpr").at_name("t0")) + ", a0=" +
               std::to_string(state_array<std::uint64_t>("gpr").at_name("a0")) + ", a1=" +
               std::to_string(state_array<std::uint64_t>("gpr").at_name("a1")) + ", a2=" +
               std::to_string(state_array<std::uint64_t>("gpr").at_name("a2"));
    }

  private:
    std::uint64_t read_reg(std::uint8_t addr) const {
        if (addr == 0U || addr >= 32U) {
            return 0;
        }
        return state_array<std::uint64_t>("gpr").at({riscv_row(addr), riscv_col(addr)});
    }
};

// 第三段：读口输出。
// 只负责把访问段给出的两个读口结果继续对外转发。
class RegFileReadStage final : public project_xs::sim::KernelComponent {
  public:
    RegFileReadStage()
        : project_xs::sim::KernelComponent("regfile_read_stage", 1, 2) {
        set_description("读口输出段：接收访问段的两个读口结果，并转发到顶层 kernel 输出。");
        add_name_alias("read");
        add_name_alias("读口输出段");
        add_name_alias("WB");
        create_state<std::uint64_t>("forwarded_rs1_rdata", "转发输入的读端口 1 数据", 0);
        create_state<std::uint64_t>("forwarded_rs2_rdata", "转发输入的读端口 2 数据", 0);
        create_state<std::uint64_t>("rs1_rdata", "读端口 1 数据", 0);
        create_state<std::uint64_t>("rs2_rdata", "读端口 2 数据", 0);
        ports().add_input(state<std::uint64_t>("forwarded_rs1_rdata").make_reg_input_port("rs1_rdata"));
        ports().add_input(state<std::uint64_t>("forwarded_rs2_rdata").make_reg_input_port("rs2_rdata"));
        ports().add_output(state<std::uint64_t>("rs1_rdata").make_wire_output_port());
        ports().add_output(state<std::uint64_t>("rs2_rdata").make_wire_output_port());
    }

    std::uint64_t rs1_rdata_value() const {
        return state<std::uint64_t>("rs1_rdata").value();
    }

    std::uint64_t rs2_rdata_value() const {
        return state<std::uint64_t>("rs2_rdata").value();
    }

  protected:
    void reset_extra() override {
        state<std::uint64_t>("forwarded_rs1_rdata") = 0;
        state<std::uint64_t>("forwarded_rs2_rdata") = 0;
        state<std::uint64_t>("rs1_rdata") = 0;
        state<std::uint64_t>("rs2_rdata") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        if (!phase_valid()) {
            return;
        }
        state<std::uint64_t>("rs1_rdata") =
            state<std::uint64_t>("forwarded_rs1_rdata").value();
        state<std::uint64_t>("rs2_rdata") =
            state<std::uint64_t>("forwarded_rs2_rdata").value();
    }

    std::string debug_info(std::uint64_t cycle) const override {
        if (!phase_valid()) {
            return "[regfile_read_stage][cycle " + std::to_string(cycle) + "] waiting for align";
        }
        return "[regfile_read_stage][cycle " + std::to_string(cycle) + "] "
               "forward rs1_rdata=" + std::to_string(state<std::uint64_t>("rs1_rdata").value()) +
               ", rs2_rdata=" + std::to_string(state<std::uint64_t>("rs2_rdata").value());
    }
};

// 顶层 kernel。
// 三段结构：
// 1. 输入采样
// 2. 寄存器访问
// 3. 读口输出
//
// 除了真正的寄存器存储体在访问段内部持有外，
// 其余阶段信号全部通过 ports 传递，不共享运行时控制状态。
class RiscvRegFileKernel final : public project_xs::sim::Kernel {
  public:
    RiscvRegFileKernel(std::string name,
                       std::shared_ptr<RegFileInputStage> input_stage,
                       std::shared_ptr<RegFileAccessStage> access_stage,
                       std::shared_ptr<RegFileReadStage> read_stage)
        : project_xs::sim::Kernel(std::move(name)),
          input_stage_(std::move(input_stage)),
          access_stage_(std::move(access_stage)),
          read_stage_(std::move(read_stage)) {
        set_description("RISC-V 整数寄存器堆顶层 kernel：由输入采样、寄存器访问、读口输出三段组成。");
        add_name_alias("riscv_regfile");
        add_name_alias("RISC-V RegFile");
        add_name_alias("寄存器堆");
        if (!input_stage_ || !access_stage_ || !read_stage_) {
            project_xs::sim::error::raise(
                project_xs::sim::error::Stage::Elaboration,
                project_xs::sim::error::Kind::InvalidArgument,
                "RiscvRegFileKernel",
                "requires prebuilt input, access and read stages");
        }

        create_state<std::uint64_t>("rs1_rdata", "顶层读端口 1 数据", 0);
        create_state<std::uint64_t>("rs2_rdata", "顶层读端口 2 数据", 0);
        ports().add_output(state<std::uint64_t>("rs1_rdata").make_wire_output_port());
        ports().add_output(state<std::uint64_t>("rs2_rdata").make_wire_output_port());

        require_component("regfile_input_stage");
        require_component("regfile_access_stage");
        require_component("regfile_read_stage");

        add_component(input_stage_);
        add_component(access_stage_);
        add_component(read_stage_);
    }

    std::uint64_t reg_value(std::uint8_t index) const {
        return access_stage_->reg_value(index);
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
        state<std::uint64_t>("rs1_rdata") = 0;
        state<std::uint64_t>("rs2_rdata") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        project_xs::sim::Kernel::run_single(cycle);
        state<std::uint64_t>("rs1_rdata") = read_stage_->rs1_rdata_value();
        state<std::uint64_t>("rs2_rdata") = read_stage_->rs2_rdata_value();
    }

  private:
    std::shared_ptr<RegFileInputStage> input_stage_;
    std::shared_ptr<RegFileAccessStage> access_stage_;
    std::shared_ptr<RegFileReadStage> read_stage_;
};

inline std::shared_ptr<RiscvRegFileKernel> build_riscv_regfile_kernel(std::string name) {
    auto input_stage = std::make_shared<RegFileInputStage>();
    auto access_stage = std::make_shared<RegFileAccessStage>();
    auto read_stage = std::make_shared<RegFileReadStage>();

    access_stage->ports().get_input("rs1_addr")->connect(
        input_stage->ports().get_output("rs1_addr"));
    access_stage->ports().get_input("rs2_addr")->connect(
        input_stage->ports().get_output("rs2_addr"));
    access_stage->ports().get_input("rd_addr")->connect(
        input_stage->ports().get_output("rd_addr"));
    access_stage->ports().get_input("rd_wdata")->connect(
        input_stage->ports().get_output("rd_wdata"));
    access_stage->ports().get_input("rd_we")->connect(
        input_stage->ports().get_output("rd_we"));

    read_stage->ports().get_input("rs1_rdata")->connect(
        access_stage->ports().get_output("rs1_rdata"));
    read_stage->ports().get_input("rs2_rdata")->connect(
        access_stage->ports().get_output("rs2_rdata"));

    return std::make_shared<RiscvRegFileKernel>(
        std::move(name),
        std::move(input_stage),
        std::move(access_stage),
        std::move(read_stage));
}

}  // namespace project_xs::sim::test::riscv_regfile

#endif
