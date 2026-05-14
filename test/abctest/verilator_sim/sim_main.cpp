#include "Vabc_demo.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <verilated.h>
#include <verilated_vcd_c.h>

namespace {

constexpr vluint64_t kCycles = 6;
constexpr const char* kSamplePath = "samples.jsonl";

void dump_eval(Vabc_demo& top, VerilatedVcdC& trace, vluint64_t& time) {
    top.eval();
    trace.dump(time++);
}

void tick(Vabc_demo& top, VerilatedVcdC& trace, vluint64_t& time) {
    top.clk = 0;
    dump_eval(top, trace, time);
    top.clk = 1;
    dump_eval(top, trace, time);
    top.clk = 0;
    dump_eval(top, trace, time);
}

void write_sample(std::ofstream& samples, const Vabc_demo& top, vluint64_t cycle) {
    samples << "{\"format\":\"project_xs.verilator_cycle_sample\","
            << "\"version\":1,"
            << "\"stage\":\"cycle_end\","
            << "\"cycle\":" << cycle << ","
            << "\"signals\":{"
            << "\"source_out\":" << top.source_out << ","
            << "\"wire_A\":" << top.wire_A << ","
            << "\"wire_B\":" << top.wire_B << ","
            << "\"wire_C\":" << top.wire_C << ","
            << "\"reg_A\":" << top.reg_A << ","
            << "\"reg_B\":" << top.reg_B << ","
            << "\"reg_C\":" << top.reg_C
            << "}}\n";
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto top = std::make_unique<Vabc_demo>();
    auto trace = std::make_unique<VerilatedVcdC>();
    top->trace(trace.get(), 99);
    trace->open("abc_demo.vcd");
    std::ofstream samples(kSamplePath);
    if (!samples) {
        std::cerr << "[verilator] cannot open " << kSamplePath << "\n";
        return 1;
    }

    vluint64_t time = 0;
    top->clk = 0;
    top->rst_n = 0;
    top->source_out = 0;
    tick(*top, *trace, time);

    top->rst_n = 1;
    for (vluint64_t cycle = 0; cycle < kCycles; ++cycle) {
        top->source_out = cycle;
        tick(*top, *trace, time);
        write_sample(samples, *top, cycle);
        std::cout << "[verilator][cycle " << cycle << "] wire: A="
                  << top->wire_A << " B=" << top->wire_B << " C=" << top->wire_C
                  << " | reg: A=" << top->reg_A << " B=" << top->reg_B
                  << " C=" << top->reg_C << "\n";
    }

    samples.close();
    trace->close();
    top->final();
    std::cout << "[verilator] vcd=abc_demo.vcd samples=" << kSamplePath << "\n";
    return 0;
}
