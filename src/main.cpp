#include "meshflow/model.hpp"
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace {
using namespace meshflow;
std::string json_string(std::string_view s) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '\\' || c == '"') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4U]; out += hex[c & 15U]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::uint64_t number(std::string_view text, std::string_view flag) {
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty())
        throw std::invalid_argument("invalid unsigned integer for " + std::string(flag));
    return value;
}
std::uint64_t peak_rss_bytes() {
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
#else
    return 0;
#endif
}
void trace_file(const std::string& path, const Result& result) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("cannot open trace file: " + path);
    for (const auto& e : result.trace)
        output << "{\"tick\":" << e.tick << ",\"pe\":" << e.pe << ",\"pc\":" << e.pc
               << ",\"op\":" << json_string(name(e.op)) << ",\"value\":" << e.value
               << ",\"link\":" << e.link << ",\"sequence\":" << e.sequence << "}\n";
    output.close();
    if (!output) throw std::runtime_error("trace write failed: " + path);
}
}

int main(int argc, char** argv) {
    using namespace meshflow;
    std::string workload = "scan", engine = "event", trace_path;
    std::size_t pes = 4, size = 64; std::uint32_t seed = 7;
    std::uint64_t repeats = 1, warmup = 0; bool compare = false, no_trace = false;
    Config config;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--help") {
                std::cout << "meshflow --workload relay|scan|reduction|deadlock --pes N --size N\n"
                             "  --engine tick|event --seed N --capacity N --compute-latency N\n"
                             "  --memory-latency N --link-latency N --max-ticks N\n"
                             "  --compare-engines --trace PATH --no-trace --warmup N --repeat N\n"
                             "Bounds: 1<=pes<=256; size<=100000; relay size*pes<=1000000;\n"
                             "seed<=UINT32_MAX; 1<=capacity<=1000000; positive latencies<=INT64_MAX;\n"
                             "max-ticks<=INT64_MAX (inclusive); repeat=1..10000; warmup=0..10000.\n"
                             "JSONL stdout; elapsed_ns measures engine.run only; RSS is process peak.\n"
                             "Trace enabled for --trace/--compare-engines unless --no-trace.\n"
                             "Trace file is replaced and records only the final measured run.\n"
                             "Exit: 0 verified completion, 1 model/oracle/differential failure, 2 usage/IO.\n";
                return 0;
            }
            if (flag == "--compare-engines") { compare = true; continue; }
            if (flag == "--no-trace") { no_trace = true; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + flag);
            const std::string value = argv[++i];
            if (flag == "--workload") workload = value;
            else if (flag == "--engine") engine = value;
            else if (flag == "--trace") {
                if (value.empty()) throw std::invalid_argument("trace path must not be empty");
                trace_path = value;
            }
            else {
                const auto n = number(value, flag);
                if (flag == "--pes") { if (n > 256) throw std::invalid_argument("pes exceeds 256"); pes = static_cast<std::size_t>(n); }
                else if (flag == "--size") { if (n > 100000) throw std::invalid_argument("size exceeds 100000"); size = static_cast<std::size_t>(n); }
                else if (flag == "--seed") { if (n > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("seed exceeds UINT32_MAX"); seed = static_cast<std::uint32_t>(n); }
                else if (flag == "--capacity") { if (n > 1000000) throw std::invalid_argument("capacity exceeds 1000000"); config.capacity = static_cast<std::size_t>(n); }
                else if (flag == "--compute-latency") config.compute_latency = n;
                else if (flag == "--memory-latency") config.memory_latency = n;
                else if (flag == "--link-latency") config.link_latency = n;
                else if (flag == "--max-ticks") config.max_ticks = n;
                else if (flag == "--repeat") repeats = n;
                else if (flag == "--warmup") warmup = n;
                else throw std::invalid_argument("unknown argument: " + flag);
            }
        }
        if (engine != "tick" && engine != "event") throw std::invalid_argument("engine must be tick or event");
        if (repeats == 0 || repeats > 10000 || warmup > 10000) throw std::invalid_argument("repeat/warmup out of bounds");
        if (no_trace && !trace_path.empty()) throw std::invalid_argument("--trace conflicts with --no-trace");
        config.trace = !no_trace && (compare || !trace_path.empty());
        const auto w = make_workload(workload, pes, size, seed);
        if (const auto error = validate(w.program, config); !error.empty()) throw std::invalid_argument(error);
        const auto run = [&]() { return engine == "tick" ? TickEngine{}.run(w.program, config) : EventEngine{}.run(w.program, config); };
        for (std::uint64_t n = 0; n < warmup + repeats; ++n) {
            const bool warming = n < warmup;
            const auto start = std::chrono::steady_clock::now();
            auto r = run();
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            const auto rss = peak_rss_bytes();
            auto error = oracle_error(w, r);
            if (warming && error.empty()) continue;
            if (compare) {
                const auto other = engine == "tick" ? EventEngine{}.run(w.program, config) : TickEngine{}.run(w.program, config);
                const auto diff = difference(r, other);
                if (!diff.empty()) error = "engine mismatch: " + diff;
                else if (other.termination != Termination::Completed && error.empty()) error = "comparison requires two completed models";
            }
            if (!trace_path.empty() && n + 1 == warmup + repeats) trace_file(trace_path, r);
            std::cout << "{\"workload\":" << json_string(workload) << ",\"engine\":" << json_string(engine)
                      << ",\"pes\":" << pes << ",\"size\":" << size << ",\"seed\":" << seed
                      << ",\"capacity\":" << config.capacity << ",\"compute_latency\":" << config.compute_latency
                      << ",\"memory_latency\":" << config.memory_latency << ",\"link_latency\":" << config.link_latency
                      << ",\"max_ticks\":" << config.max_ticks << ",\"trace_enabled\":" << (config.trace ? "true" : "false")
                      << ",\"phase\":" << json_string(warming ? "warmup" : "measured")
                      << ",\"repeat\":" << (warming ? n : n - warmup) << ",\"termination\":" << json_string(name(r.termination))
                      << ",\"verified\":" << (error.empty() ? "true" : "false") << ",\"compared\":" << (compare ? "true" : "false")
                      << ",\"simulated_ticks\":" << r.tick << ",\"instructions\":" << r.stats.instructions
                      << ",\"scheduler_events\":" << r.stats.scheduler_events << ",\"clock_steps\":" << r.stats.clock_steps
                      << ",\"pe_checks\":" << r.stats.pe_checks << ",\"elapsed_ns\":" << elapsed
                      << ",\"peak_rss_bytes\":" << rss << ",\"checksum\":" << checksum(r)
                      << ",\"diagnostic\":" << json_string(r.diagnostic) << ",\"error\":" << json_string(error) << "}\n";
            if (!error.empty()) {
                std::cerr << error << "\nreplay: meshflow --workload " << workload << " --engine " << engine
                          << " --pes " << pes << " --size " << size << " --seed " << seed << " --capacity " << config.capacity
                          << " --compute-latency " << config.compute_latency << " --memory-latency " << config.memory_latency
                          << " --link-latency " << config.link_latency << " --max-ticks " << config.max_ticks << " --compare-engines\n";
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "meshflow: " << e.what() << '\n'; return 2;
    }
}
