#include "Vled_waterfall.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <verilated.h>
#include <verilated_vcd_c.h>

namespace {

constexpr vluint64_t kCycles = 12;
constexpr const char* kSamplePath = "samples.jsonl";

void tick(Vled_waterfall& top, VerilatedVcdC& trace, vluint64_t& time) {
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

void write_sample(std::ofstream& samples, const Vled_waterfall& top, vluint64_t cycle) {
    samples << "{\"format\":\"project_xs.verilator_cycle_sample\","
            << "\"version\":1,"
            << "\"stage\":\"cycle_end\","
            << "\"cycle\":" << cycle << ","
            << "\"signals\":{"
            << "\"rst_n\":" << static_cast<int>(top.rst_n) << ","
            << "\"cnt\":" << static_cast<unsigned int>(top.debug_cnt) << ","
            << "\"wrap_pulse\":" << static_cast<int>(top.debug_wrap_pulse) << ","
            << "\"led\":" << static_cast<int>(top.led)
            << "}}\n";
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto top = std::make_unique<Vled_waterfall>();
    auto trace = std::make_unique<VerilatedVcdC>();
    top->trace(trace.get(), 99);
    trace->open("led_waterfall.vcd");
    std::ofstream samples(kSamplePath);
    if (!samples) {
        std::cerr << "[verilator] cannot open " << kSamplePath << "\n";
        return 1;
    }

    vluint64_t time = 0;
    top->clk = 0;
    top->rst_n = 0;
    top->eval();
    trace->dump(time++);

    for (vluint64_t cycle = 0; cycle < kCycles; ++cycle) {
        top->rst_n = (cycle != 0);
        tick(*top, *trace, time);
        write_sample(samples, *top, cycle);
        std::cout << "[verilator][cycle " << cycle << "] rst_n="
                  << static_cast<int>(top->rst_n)
                  << " cnt=" << static_cast<unsigned int>(top->debug_cnt)
                  << " wrap=" << static_cast<int>(top->debug_wrap_pulse)
                  << " led=0x" << std::hex << static_cast<int>(top->led)
                  << std::dec << "\n";
    }

    samples.close();
    trace->close();
    top->final();
    std::cout << "[verilator] vcd=led_waterfall.vcd samples=" << kSamplePath << "\n";
    return 0;
}
