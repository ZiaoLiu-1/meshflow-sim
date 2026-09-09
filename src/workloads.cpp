#include "meshflow/model.hpp"
#include <algorithm>
#include <stdexcept>

namespace meshflow {
namespace {
Instruction constant(int r, std::int32_t v) { return {Op::Const, r, 0, 0, v}; }
std::size_t chunk_size(std::size_t n, std::size_t p, std::size_t id) { return n / p + (id < n % p ? 1 : 0); }
}
Workload make_workload(const std::string& kind, std::size_t pes, std::size_t size, std::uint32_t seed) {
    if (pes == 0 || pes > 256 || size > 100000) throw std::invalid_argument("workload bounds: 1<=pes<=256, 0<=size<=100000");
    if (kind != "relay" && kind != "scan" && kind != "reduction" && kind != "deadlock") throw std::invalid_argument("unknown workload: " + kind);
    // Unrolled instruction vectors are deliberately bounded, including replicated relay code.
    if (kind == "relay" && size * pes > 1000000) throw std::invalid_argument("relay requires size*pes<=1000000");
    Workload w; w.kind = kind; w.program.pes.resize(pes);
    std::uint32_t rng = seed == 0 ? 0x9e3779b9U : seed;
    for (std::size_t i = 0; i < size; ++i) {
        rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
        w.input.push_back(static_cast<std::int32_t>(rng % 17U) - 8);
    }
    if (kind == "deadlock") {
        if (pes < 2) throw std::invalid_argument("deadlock workload requires at least two PEs");
        for (std::size_t p = 0; p < pes; ++p) {
            w.program.pes[p].code = {{Op::Recv, 0, static_cast<int>(p == 0 ? 1 : p - 1)}, {Op::Halt}};
        }
        return w;
    }
    std::size_t offset = 0;
    for (std::size_t p = 0; p < pes; ++p) {
        auto& pe = w.program.pes[p]; auto& code = pe.code;
        if (kind == "relay") {
            pe.memory.resize(std::max<std::size_t>(size, 1));
            if (p == 0) std::copy(w.input.begin(), w.input.end(), pe.memory.begin());
            for (std::size_t i = 0; i < size; ++i) {
                if (p == 0) code.push_back({Op::Load, 0, static_cast<int>(i)});
                else {
                    code.push_back({Op::Recv, 0, static_cast<int>(p - 1)});
                    code.push_back({Op::Store, 0, static_cast<int>(i)});
                }
                if (p + 1 < pes) code.push_back({Op::Send, 0, static_cast<int>(p + 1)});
            }
        } else {
            const auto count = chunk_size(size, pes, p);
            const auto total_slot = kind == "scan" ? 2 * count : count;
            pe.memory.resize(total_slot + 1);
            std::copy_n(w.input.begin() + static_cast<std::ptrdiff_t>(offset), count, pe.memory.begin());
            offset += count;
            code.push_back(constant(0, 0));
            for (std::size_t i = 0; i < count; ++i) {
                code.push_back({Op::Load, 1, static_cast<int>(i)});
                code.push_back({Op::Add, 0, 0, 1});
                if (kind == "scan") code.push_back({Op::Store, 0, static_cast<int>(count + i)});
            }
            if (p == 0) code.push_back(constant(2, 0));
            else code.push_back({Op::Recv, 2, static_cast<int>(p - 1)});
            if (kind == "scan") {
                for (std::size_t i = 0; i < count; ++i) {
                    code.push_back({Op::Load, 1, static_cast<int>(count + i)});
                    code.push_back({Op::Add, 1, 1, 2});
                    code.push_back({Op::Store, 1, static_cast<int>(count + i)});
                }
            }
            code.push_back({Op::Add, 0, 0, 2});
            code.push_back({Op::Store, 0, static_cast<int>(total_slot)});
            if (p + 1 < pes) code.push_back({Op::Send, 0, static_cast<int>(p + 1)});
        }
        code.push_back({Op::Halt});
    }
    return w;
}
std::string oracle_error(const Workload& w, const Result& r) {
    if (r.termination != Termination::Completed) return "workload did not complete: " + std::string(name(r.termination));
    if (r.pes.size() != w.program.pes.size() || r.pes.empty()) return "oracle PE count mismatch";
    if (w.kind == "relay") {
        // Every hop's local memory must contain the original data, including source PE.
        for (std::size_t p = 0; p < r.pes.size(); ++p) {
            if (r.pes[p].memory.size() < w.input.size()) return "relay oracle memory size";
            for (std::size_t i = 0; i < w.input.size(); ++i)
                if (r.pes[p].memory[i] != w.input[i]) return "relay oracle PE " + std::to_string(p) + " element " + std::to_string(i);
        }
    } else if (w.kind == "scan" || w.kind == "reduction") {
        std::int64_t sum = 0; std::size_t offset = 0;
        for (std::size_t p = 0; p < r.pes.size(); ++p) {
            const auto count = chunk_size(w.input.size(), r.pes.size(), p);
            const auto total_slot = w.kind == "scan" ? 2 * count : count;
            if (r.pes[p].memory.size() != total_slot + 1) return "oracle memory size";
            for (std::size_t i = 0; i < count; ++i) {
                if (r.pes[p].memory[i] != w.input[offset]) return "oracle input modified";
                sum += w.input[offset++];
                if (w.kind == "scan" && r.pes[p].memory[count + i] != sum)
                    return "scan oracle PE " + std::to_string(p) + " element " + std::to_string(i);
            }
            if (r.pes[p].memory[total_slot] != sum) return "total oracle PE " + std::to_string(p);
        }
    } else return "no success oracle for workload " + w.kind;
    return {};
}
} // namespace meshflow
