#include "meshflow/model.hpp"
#include <limits>
#include <sstream>

namespace meshflow {
std::string validate(const Program& p, const Config& c) {
    if (p.pes.empty()) return "at least one PE required";
    if (c.capacity == 0) return "FIFO capacity must be positive";
    constexpr auto bound = static_cast<Tick>(std::numeric_limits<std::int64_t>::max());
    if (!c.compute_latency || !c.memory_latency || !c.link_latency)
        return "latencies must be positive";
    if (c.compute_latency > bound || c.memory_latency > bound || c.link_latency > bound || c.max_ticks > bound)
        return "latencies and max_ticks must be <= 2^63-1";
    return {};
}
std::string instruction_error(const Instruction& i, std::size_t pe, std::size_t pes, std::size_t memory) {
    const auto reg = [](int r) { return r >= 0 && r < 8; };
    const auto address = [&](int a) { return a >= 0 && static_cast<std::size_t>(a) < memory; };
    const auto neighbor = [&](int n) {
        if (n < 0 || static_cast<std::size_t>(n) >= pes) return false;
        const auto q = static_cast<std::size_t>(n);
        return (q < pe && pe - q == 1) || (q > pe && q - pe == 1);
    };
    switch (i.op) {
    case Op::Halt: return {};
    case Op::Const: return reg(i.a) ? "" : "register index out of bounds";
    case Op::Load: case Op::Store:
        if (!reg(i.a)) return "register index out of bounds";
        return address(i.b) ? "" : "memory address out of bounds";
    case Op::Add:
        return reg(i.a) && reg(i.b) && reg(i.c) ? "" : "register index out of bounds";
    case Op::Send: case Op::Recv:
        if (!reg(i.a)) return "register index out of bounds";
        return neighbor(i.b) ? "" : "neighbor must be an adjacent PE";
    }
    return "invalid opcode";
}
std::optional<std::int32_t> checked_add(std::int32_t a, std::int32_t b) {
    const std::int64_t sum = static_cast<std::int64_t>(a) + b;
    if (sum < std::numeric_limits<std::int32_t>::min() || sum > std::numeric_limits<std::int32_t>::max()) return {};
    return static_cast<std::int32_t>(sum);
}
std::size_t link_index(std::size_t source, std::size_t destination) {
    return source < destination ? 2 * source : 2 * destination + 1;
}
const char* name(Op o) {
    switch (o) {
    case Op::Const: return "CONST"; case Op::Load: return "LOAD"; case Op::Store: return "STORE";
    case Op::Add: return "ADD"; case Op::Send: return "SEND"; case Op::Recv: return "RECV"; case Op::Halt: return "HALT";
    } return "INVALID";
}
const char* name(Status s) {
    switch (s) {
    case Status::Running: return "running"; case Status::Busy: return "busy";
    case Status::BlockedSend: return "blocked_send"; case Status::BlockedRecv: return "blocked_recv";
    case Status::Halted: return "halted"; case Status::Faulted: return "faulted";
    } return "invalid";
}
const char* name(Termination t) {
    switch (t) {
    case Termination::Completed: return "completed"; case Termination::Fault: return "fault";
    case Termination::Deadlock: return "deadlock"; case Termination::OrphanedMessages: return "orphaned_messages";
    case Termination::ModelLimit: return "model_limit";
    } return "invalid";
}
std::string difference(const Result& a, const Result& b) {
    if (a.termination != b.termination) return "termination: " + std::string(name(a.termination)) + " != " + name(b.termination);
    if (a.tick != b.tick) return "tick: " + std::to_string(a.tick) + " != " + std::to_string(b.tick);
    if (a.diagnostic != b.diagnostic) return "diagnostic: " + a.diagnostic + " != " + b.diagnostic;
    if (a.pes.size() != b.pes.size()) return "PE count";
    for (std::size_t p = 0; p < a.pes.size(); ++p) {
        const auto& x = a.pes[p]; const auto& y = b.pes[p];
        const auto label = "PE " + std::to_string(p) + " ";
        if (x.pc != y.pc) return label + "PC";
        if (x.status != y.status) return label + "status";
        for (std::size_t r = 0; r < 8; ++r)
            if (x.regs[r] != y.regs[r]) return label + "register " + std::to_string(r) + ": " + std::to_string(x.regs[r]) + " != " + std::to_string(y.regs[r]);
        if (x.memory.size() != y.memory.size()) return label + "memory size";
        for (std::size_t m = 0; m < x.memory.size(); ++m)
            if (x.memory[m] != y.memory[m]) return label + "memory[" + std::to_string(m) + "]: " + std::to_string(x.memory[m]) + " != " + std::to_string(y.memory[m]);
    }
    if (a.links.size() != b.links.size()) return "link count";
    for (std::size_t l = 0; l < a.links.size(); ++l) {
        const auto& x = a.links[l]; const auto& y = b.links[l];
        const auto label = "link " + std::to_string(l) + " ";
        if (x.fifo != y.fifo) return label + "FIFO";
        if (x.in_flight != y.in_flight) return label + "in_flight";
        if (x.next_sequence != y.next_sequence) return label + "next_sequence";
        if (x.sent != y.sent) return label + "sent history";
        if (x.delivered != y.delivered) return label + "delivered history";
        if (x.received != y.received) return label + "received history";
    }
    if (a.stats.instructions != b.stats.instructions) return "committed instructions";
    if (a.trace.size() != b.trace.size()) return "trace size";
    for (std::size_t i = 0; i < a.trace.size(); ++i)
        if (a.trace[i] != b.trace[i]) return "trace entry " + std::to_string(i);
    return {};
}
std::uint64_t checksum(const Result& r) {
    // Defined unsigned arithmetic. A compact output identity, never a substitute for difference().
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&](std::uint64_t value) {
        for (unsigned byte = 0; byte < 8; ++byte) { hash ^= value & 255U; hash *= 1099511628211ULL; value >>= 8U; }
    };
    for (const auto& pe : r.pes) {
        mix(pe.pc); mix(static_cast<std::uint64_t>(pe.status));
        for (auto v : pe.regs) mix(static_cast<std::uint32_t>(v));
        for (auto v : pe.memory) mix(static_cast<std::uint32_t>(v));
    }
    return hash;
}
} // namespace meshflow
