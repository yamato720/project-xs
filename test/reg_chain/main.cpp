#include "base/CycleSimulator.h"
#include "base/Kernel.h"
#include "base/Port.h"
#include "base/SimulationSession.h"
#include "base/State.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

bool file_contains(const std::filesystem::path& path, const std::string& needle) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>())
        .find(needle) != std::string::npos;
}

std::filesystem::path latest_trace_segment(const std::filesystem::path& root) {
    std::filesystem::path best;
    std::filesystem::file_time_type best_time{};
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto manifest = entry.path() / "manifest.json";
        if (!std::filesystem::exists(manifest)) {
            continue;
        }
        const auto time = std::filesystem::last_write_time(manifest);
        if (best.empty() || time > best_time) {
            best = entry.path();
            best_time = time;
        }
    }
    if (best.empty()) {
        throw std::runtime_error("no trace segment under " + root.string());
    }
    return best;
}

class RegChainKernel final : public project_xs::sim::Kernel {
  public:
    explicit RegChainKernel(std::string name)
        : project_xs::sim::Kernel(std::move(name)),
          reg_outputs_(create_port_group("reg_outputs")) {
        set_description("寄存器链测试 kernel；每个 run 等价于一次 clk 上升沿，A <= A + 1，B <= A_old");
        create_state<std::uint64_t>("A", "寄存器 A；检测到上升沿后从 0 开始每拍自增 1", 0);
        create_state<std::uint64_t>("B", "寄存器 B；每拍采样上一拍的 A", 0);

        ports().add_output(state<std::uint64_t>("A").make_wire_output_port("wire_A"));
        ports().add_output(state<std::uint64_t>("B").make_wire_output_port("wire_B"));
        reg_outputs_.add_output(state<std::uint64_t>("A").make_reg_output_port("reg_A"));
        reg_outputs_.add_output(state<std::uint64_t>("B").make_reg_output_port("reg_B"));
    }

  protected:
    void reset_extra() override {
        state<std::uint64_t>("A") = 0;
        state<std::uint64_t>("B") = 0;
    }

    void run_single(std::uint64_t cycle) override {
        (void)cycle;
        const auto old_a = state<std::uint64_t>("A").value();
        state<std::uint64_t>("A") = old_a + 1;
        state<std::uint64_t>("B") = old_a;
    }

    std::string debug_info(std::uint64_t cycle) const override {
        return "[reg_chain][cycle " + std::to_string(cycle) + "] A=" +
               std::to_string(state<std::uint64_t>("A").value()) +
               " B=" + std::to_string(state<std::uint64_t>("B").value()) +
               " wire: " + ports().info_outputs() +
               " reg: " + reg_outputs_.info_outputs();
    }

  private:
    project_xs::sim::PortGroup& reg_outputs_;
};

}  // namespace

int main() {
    const std::string trace_dir = "test/reg_chain/trace";
    constexpr long double kFrequencyHz = 1.0L;
    constexpr std::int64_t kCycles = 8;

    project_xs::sim::SimulationSession session(kFrequencyHz, static_cast<long double>(kCycles));
    session.set_snapshot_capture_directory(trace_dir);

    auto simulator =
        std::make_shared<project_xs::sim::CycleSimulator>("reg_chain_simulator",
                                                          kCycles,
                                                          kFrequencyHz);
    simulator->set_description("reg_chain 测试 simulator；一个 step 对应 Verilog clk 的一个上升沿");
    simulator->set_snapshot_capture_directory(trace_dir);

    auto kernel = std::make_shared<RegChainKernel>("reg_chain");
    kernel->set_snapshot_capture_directory(trace_dir);
    simulator->add_kernel(kernel);

    session.add_simulator(simulator);
    session.reset();
    session.initialize_zero();

    std::cout << session.start_info() << "\n";
    session.start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Automatic,
                                   "reg_chain_session");
    simulator->start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Automatic,
                                      "reg_chain_simulator");
    kernel->start_snapshot_capture(project_xs::sim::SnapshotCaptureMode::Automatic,
                                   "reg_chain_kernel");
    session.run();
    kernel->stop_snapshot_capture("reg_chain_kernel");
    simulator->stop_snapshot_capture("reg_chain_simulator");
    session.stop_snapshot_capture("reg_chain_session");
    std::cout << session.finish_info() << "\n";

    if (simulator->current_cycle() != static_cast<std::uint64_t>(kCycles)) {
        throw std::runtime_error("reg_chain simulator did not reach expected cycles");
    }
    if (session.snapshot_history().empty()) {
        throw std::runtime_error("reg_chain session did not produce waveform records");
    }

    const auto session_segment =
        latest_trace_segment(std::filesystem::path(trace_dir) / "simulation_session_session");
    const auto simulator_segment =
        latest_trace_segment(std::filesystem::path(trace_dir) / "cycle_simulator_reg_chain_simulator");
    const auto kernel_segment =
        latest_trace_segment(std::filesystem::path(trace_dir) / "kernel_reg_chain");

    const auto session_waveform = session_segment / "waveform.jsonl";
    const auto session_html = session_segment / "waveform.html";
    const auto simulator_waveform = simulator_segment / "waveform.jsonl";
    const auto kernel_waveform = kernel_segment / "waveform.jsonl";
    const auto kernel_html = kernel_segment / "waveform.html";
    if (!std::filesystem::exists(session_waveform) || !std::filesystem::exists(session_html) ||
        !std::filesystem::exists(simulator_waveform) || !std::filesystem::exists(kernel_waveform) ||
        !std::filesystem::exists(kernel_html)) {
        throw std::runtime_error("reg_chain waveform files were not generated");
    }
    if (!file_contains(kernel_waveform, "\"name_path\":[\"reg_chain\",\"reg_chain_ports\",\"output\",\"wire_A\"]") ||
        !file_contains(kernel_waveform, "\"name_path\":[\"reg_chain\",\"reg_outputs\",\"output\",\"reg_A\"]") ||
        !file_contains(kernel_waveform, "\"name_path\":[\"reg_chain\",\"A\"]") ||
        !file_contains(kernel_html, "寄存器链测试 kernel")) {
        throw std::runtime_error("reg_chain waveform metadata is incomplete");
    }

    std::cout << "[reg_chain_waveform_test] records=" << session.snapshot_history().size()
              << " cycles=" << simulator->current_cycle()
              << " html=" << session_html.string()
              << " kernel_html=" << kernel_html.string() << "\n";

    return 0;
}
