#include "meshflow/model.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace {
using namespace meshflow;

constexpr std::uint32_t random_seed = 0x508913U;
constexpr unsigned default_cases = 40000;

void print_case(const Program& program, const Config& config) {
    std::cerr << "Config{capacity=" << config.capacity
              << ", compute_latency=" << config.compute_latency
              << ", memory_latency=" << config.memory_latency
              << ", link_latency=" << config.link_latency
              << ", max_ticks=" << config.max_ticks
              << ", trace=" << config.trace << "}\n";
    for (std::size_t pe = 0; pe < program.pes.size(); ++pe) {
        const auto& initial = program.pes[pe];
        std::cerr << "PE " << pe << " memory={";
        for (std::size_t i = 0; i < initial.memory.size(); ++i) {
            if (i != 0) std::cerr << ',';
            std::cerr << initial.memory[i];
        }
        std::cerr << "}\n";
        for (std::size_t pc = 0; pc < initial.code.size(); ++pc) {
            const auto& instruction = initial.code[pc];
            std::cerr << "  PC " << pc << ": {op=" << static_cast<int>(instruction.op)
                      << " (" << name(instruction.op) << "), a=" << instruction.a
                      << ", b=" << instruction.b << ", c=" << instruction.c
                      << ", value=" << instruction.value << "}\n";
        }
    }
}

std::string compare(const Program& program, const Config& config) {
    const auto tick = TickEngine{}.run(program, config);
    const auto event = EventEngine{}.run(program, config);
    return difference(tick, event);
}

bool arbitrary_programs(unsigned cases) {
    // mt19937's output stream is standardized; modulo selection intentionally
    // avoids implementation-dependent uniform_int_distribution mappings.
    std::mt19937 rng(random_seed);
    for (unsigned trial = 0; trial < cases; ++trial) {
        Program program;
        const std::size_t count = 1 + rng() % 5;
        program.pes.resize(count);
        for (std::size_t pe = 0; pe < count; ++pe) {
            auto& initial = program.pes[pe];
            initial.memory = {0, 1, -1, 99};
            const auto length = rng() % 18;
            for (unsigned j = 0; j < length; ++j) {
                Instruction instruction;
                // Opcode 7 and edge neighbors deliberately exercise faults.
                instruction.op = static_cast<Op>(rng() % 8);
                instruction.a = static_cast<int>(rng() % 8);
                instruction.b = static_cast<int>(rng() % 8);
                instruction.c = static_cast<int>(rng() % 8);
                instruction.value = static_cast<std::int32_t>(rng() % 19) - 9;
                if (instruction.op == Op::Load || instruction.op == Op::Store) {
                    instruction.b %= 4;
                }
                if (instruction.op == Op::Send || instruction.op == Op::Recv) {
                    instruction.b = static_cast<int>(pe) + (rng() % 2 ? 1 : -1);
                }
                initial.code.push_back(instruction);
            }
            if (rng() % 2) initial.code.push_back({Op::Halt});
        }

        Config config;
        config.capacity = 1 + rng() % 4;
        config.compute_latency = 1 + rng() % 6;
        config.memory_latency = 1 + rng() % 9;
        config.link_latency = 1 + rng() % 12;
        config.max_ticks = rng() % 90;
        config.trace = trial % 2 == 0;
        std::string error;
        try {
            error = compare(program, config);
        } catch (const std::exception& exception) {
            error = std::string("unexpected exception: ") + exception.what();
        }
        if (!error.empty()) {
            std::cerr << "FAIL arbitrary seed=" << random_seed << " trial=" << trial
                      << ": " << error << "\nReplay: meshflow-fuzz " << trial + 1 << '\n';
            print_case(program, config);
            return false;
        }
    }
    return true;
}

bool workload_oracles() {
    for (const char* kind : {"relay", "scan", "reduction"}) {
        for (std::size_t pes = 1; pes <= 8; ++pes) {
            for (std::size_t size = 0; size <= 25; ++size) {
                const auto workload = make_workload(kind, pes, size,
                                                     static_cast<std::uint32_t>(size));
                Config config;
                config.compute_latency = 2;
                config.memory_latency = 3;
                config.link_latency = 7;
                std::string error;
                try {
                    const auto tick = TickEngine{}.run(workload.program, config);
                    const auto event = EventEngine{}.run(workload.program, config);
                    error = difference(tick, event);
                    if (error.empty()) error = oracle_error(workload, event);
                } catch (const std::exception& exception) {
                    error = std::string("unexpected exception: ") + exception.what();
                }
                if (!error.empty()) {
                    std::cerr << "FAIL workload=" << kind << " pes=" << pes
                              << " size=" << size << " seed=" << size << ": " << error
                              << "\nReplay: meshflow --workload " << kind << " --pes " << pes
                              << " --size " << size << " --seed " << size
                              << " --compute-latency 2 --memory-latency 3 --link-latency 7"
                              << " --compare-engines\n";
                    print_case(workload.program, config);
                    return false;
                }
            }
        }
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    unsigned cases = default_cases;
    if (argc > 2) {
        std::cerr << "usage: meshflow-fuzz [arbitrary-case-count: 1..1000000]\n";
        return 2;
    }
    if (argc == 2) {
        const std::string_view argument(argv[1]);
        const auto [end, error] = std::from_chars(argument.data(), argument.data() + argument.size(), cases);
        if (error != std::errc{} || end != argument.data() + argument.size() ||
            cases == 0 || cases > 1000000) {
            std::cerr << "arbitrary-case-count must be an integer in [1,1000000]\n";
            return 2;
        }
    }
    if (!arbitrary_programs(cases) || !workload_oracles()) return 1;
    std::cout << "PASS " << cases << " arbitrary-program differential cases (seed="
              << random_seed << ") and 624 workload oracle cases\n";
    return 0;
}
