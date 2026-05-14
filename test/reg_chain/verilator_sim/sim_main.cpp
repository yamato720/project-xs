#include "Vreg_chain.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <verilated.h>
#include <verilated_vcd_c.h>

namespace {

constexpr vluint64_t kCycles = 8;
constexpr const char* kSamplePath = "samples.jsonl";

void tick(Vreg_chain& top, VerilatedVcdC& trace, vluint64_t& time) {
    top.clk = 0;
    top.eval();
    trace.dump(time++);

    top.clk = 1;
    top.eval();
    trace.dump(time++);

    top.clk = 0;
    top.eval();
    trace.dump(time++);
}

void write_sample(std::ofstream& samples, const Vreg_chain& top, vluint64_t cycle) {
    samples << "{\"format\":\"project_xs.verilator_cycle_sample\","
            << "\"version\":1,"
            << "\"stage\":\"cycle_end\","
            << "\"cycle\":" << cycle << ","
            << "\"signals\":{"
            << "\"A\":" << static_cast<unsigned int>(top.debug_A) << ","
            << "\"B\":" << static_cast<unsigned int>(top.debug_B) << ","
            << "\"wire_A\":" << static_cast<unsigned int>(top.wire_A) << ","
            << "\"wire_B\":" << static_cast<unsigned int>(top.wire_B) << ","
            << "\"reg_A\":" << static_cast<unsigned int>(top.reg_A) << ","
            << "\"reg_B\":" << static_cast<unsigned int>(top.reg_B)
            << "}}\n";
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto top = std::make_unique<Vreg_chain>();
    auto trace = std::make_unique<VerilatedVcdC>();
    top->trace(trace.get(), 99);
    trace->open("reg_chain.vcd");

    std::ofstream samples(kSamplePath);
    if (!samples) {
        std::cerr << "[verilator] cannot open " << kSamplePath << "\n";
        return 1;
    }

    vluint64_t time = 0;
    top->clk = 0;
    top->eval();
    trace->dump(time++);

    for (vluint64_t cycle = 0; cycle < kCycles; ++cycle) {
        tick(*top, *trace, time);
        write_sample(samples, *top, cycle);
        std::cout << "[verilator][cycle " << cycle << "] A="
                  << static_cast<unsigned int>(top->debug_A)
                  << " B=" << static_cast<unsigned int>(top->debug_B)
                  << " wire_A=" << static_cast<unsigned int>(top->wire_A)
                  << " wire_B=" << static_cast<unsigned int>(top->wire_B)
                  << " reg_A=" << static_cast<unsigned int>(top->reg_A)
                  << " reg_B=" << static_cast<unsigned int>(top->reg_B)
                  << "\n";
    }

    samples.close();
    trace->close();
    top->final();
    std::cout << "[verilator] vcd=reg_chain.vcd samples=" << kSamplePath << "\n";
    return 0;
}
