#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace meshflow {
using Tick = std::uint64_t;
enum class Op { Const, Load, Store, Add, Send, Recv, Halt };
// CONST a=dst,value; LOAD a=dst,b=address; STORE a=src,b=address;
// ADD a=dst,b=lhs,c=rhs; SEND a=src,b=neighbor; RECV a=dst,b=neighbor.
struct Instruction {
    Op op{Op::Halt}; int a{0}; int b{0}; int c{0}; std::int32_t value{0};
    bool operator==(const Instruction&) const = default;
};
struct PEInit { std::vector<Instruction> code; std::vector<std::int32_t> memory; };
struct Program { std::vector<PEInit> pes; };
struct Config {
    std::size_t capacity{1}; Tick compute_latency{1}; Tick memory_latency{2};
    Tick link_latency{3}; Tick max_ticks{1000000}; bool trace{true};
};
enum class Status { Running, Busy, BlockedSend, BlockedRecv, Halted, Faulted };
enum class Termination { Completed, Fault, Deadlock, OrphanedMessages, ModelLimit };
struct Message {
    std::uint64_t sequence{0}; std::int32_t value{0};
    bool operator==(const Message&) const = default;
};
struct PEState {
    std::size_t pc{0}; std::array<std::int32_t, 8> regs{};
    std::vector<std::int32_t> memory; Status status{Status::Running};
    bool operator==(const PEState&) const = default;
};
struct LinkState {
    std::deque<Message> fifo; std::size_t in_flight{0}; std::uint64_t next_sequence{0};
    std::vector<Message> sent, delivered, received;
    bool operator==(const LinkState&) const = default;
};
struct TraceEntry {
    Tick tick{0}; std::size_t pe{0}, pc{0}; Op op{Op::Halt};
    std::int32_t value{0}; std::int64_t link{-1}; std::uint64_t sequence{0};
    bool operator==(const TraceEntry&) const = default;
};
struct Statistics {
    std::uint64_t instructions{0}, scheduler_events{0}, clock_steps{0}, pe_checks{0};
};
struct Result {
    std::vector<PEState> pes; std::vector<LinkState> links; std::vector<TraceEntry> trace;
    Termination termination{Termination::Deadlock}; Tick tick{0}; Statistics stats;
    std::string diagnostic;
};
// Pure helpers only. Engines independently own issue, completion, blocking and termination.
std::string validate(const Program&, const Config&);
std::string instruction_error(const Instruction&, std::size_t pe, std::size_t pes, std::size_t memory);
std::optional<std::int32_t> checked_add(std::int32_t, std::int32_t);
std::size_t link_index(std::size_t source, std::size_t destination);
const char* name(Op); const char* name(Status); const char* name(Termination);
std::string difference(const Result&, const Result&);
std::uint64_t checksum(const Result&);
class TickEngine { public: Result run(const Program&, const Config& = {}) const; };
class EventEngine { public: Result run(const Program&, const Config& = {}) const; };

struct Workload { Program program; std::vector<std::int32_t> input; std::string kind; };
Workload make_workload(const std::string& kind, std::size_t pes, std::size_t size, std::uint32_t seed);
std::string oracle_error(const Workload&, const Result&);
} // namespace meshflow
