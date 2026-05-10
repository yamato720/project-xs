#include "CgSolverGolden.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct CliOptions {
    std::filesystem::path dataset_dir;
    double tau = 1.0e-10;
    int max_iters = 0;
};

void usage(const char* argv0) {
    std::cerr << "用法: " << argv0 << " <dataset_dir> [--tau value] [--max-iters value]\n";
}

CliOptions parse_args(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("missing dataset_dir");
    }

    CliOptions options;
    options.dataset_dir = std::filesystem::path(argv[1]);

    for (int index = 2; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--tau") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--tau requires a value");
            }
            options.tau = std::stod(argv[++index]);
        } else if (arg == "--max-iters") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--max-iters requires a value");
            }
            options.max_iters = std::stoi(argv[++index]);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.tau <= 0.0) {
        throw std::runtime_error("--tau must be positive");
    }
    if (options.max_iters < 0) {
        throw std::runtime_error("--max-iters must be non-negative");
    }

    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        const project_xs::cgsolver::CsrDataset dataset =
            project_xs::cgsolver::CsrDataset::load(options.dataset_dir);

        project_xs::cgsolver::SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;

        const project_xs::cgsolver::GoldenResult golden =
            project_xs::cgsolver::run_jacobi_pcg(dataset, config);

        const double residual_norm =
            project_xs::cgsolver::compute_residual_norm(dataset, golden.solution);

        // golden_solution 先只保留在当前进程内存中，后续可直接接其他实现做对比。
        const std::vector<double>& golden_solution = golden.solution;
        (void)golden_solution;

        const int effective_max_iters =
            config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

        std::cout << "dataset: " << options.dataset_dir << "\n";
        std::cout << "n=" << dataset.n()
                  << " nnz=" << dataset.nnz()
                  << " tau=" << std::scientific << std::setprecision(12) << config.tau
                  << " max_iters=" << std::defaultfloat << effective_max_iters << "\n";
        std::cout << "converged=" << (golden.converged ? "yes" : "no")
                  << " iterations=" << golden.iterations << "\n";
        std::cout << std::scientific << std::setprecision(12);
        std::cout << "final_rr=" << golden.final_rr << "\n";
        std::cout << "residual_l2=" << residual_norm << "\n";
        std::cout << "golden_vector_size=" << golden_solution.size() << "\n";

        return golden.converged ? 0 : 2;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
