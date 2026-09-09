#include "meshflow/model.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace meshflow;

struct Suite {
    std::size_t cases{0}, failures{0}, checks{0};
    void check(bool condition, const std::string& why) {
        ++checks;
        if (!condition) throw std::runtime_error(why);
    }
    template<class F> void run(const std::string& label, F test) {
        ++cases;
        try { test(); }
        catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << label << ": " << e.what() << '\n';
        }
    }
};

Instruction constant(int reg, std::int32_t value) { return {Op::Const, reg, 0, 0, value}; }
Instruction load(int reg, int address) { return {Op::Load, reg, address}; }
Instruction store(int reg, int address) { return {Op::Store, reg, address}; }
Instruction add(int dst, int lhs, int rhs) { return {Op::Add, dst, lhs, rhs}; }
Instruction send(int reg, int peer) { return {Op::Send, reg, peer}; }
Instruction recv(int reg, int peer) { return {Op::Recv, reg, peer}; }
Instruction halt() { return {Op::Halt}; }
PEInit pe(std::vector<Instruction> code, std::vector<std::int32_t> memory = {}) {
    return {std::move(code), std::move(memory)};
}
Config config(Tick compute = 1, Tick memory = 2, Tick link = 3) {
    Config c;
    c.compute_latency = compute; c.memory_latency = memory; c.link_latency = link;
    return c;
}
PEState state(std::size_t pc, Status status, std::vector<std::int32_t> memory = {},
              std::initializer_list<std::pair<std::size_t, std::int32_t>> regs = {}) {
    PEState p;
    p.pc = pc; p.status = status; p.memory = std::move(memory);
    for (const auto& [index, value] : regs) p.regs.at(index) = value;
    return p;
}
TraceEntry trace(Tick tick, std::size_t pe_id, std::size_t pc, Op op,
                 std::int32_t value = 0, std::int64_t link = -1, std::uint64_t sequence = 0) {
    return {tick, pe_id, pc, op, value, link, sequence};
}
LinkState link(std::vector<Message> sent, std::vector<Message> delivered,
               std::vector<Message> received, std::deque<Message> fifo = {}, std::size_t flight = 0) {
    LinkState l;
    l.next_sequence = sent.size(); l.sent = std::move(sent); l.delivered = std::move(delivered);
    l.received = std::move(received); l.fifo = std::move(fifo); l.in_flight = flight;
    return l;
}
Result golden(Termination termination, Tick tick, std::vector<PEState> pes,
              std::vector<LinkState> links, std::vector<TraceEntry> entries) {
    Result r;
    r.termination = termination; r.tick = tick; r.pes = std::move(pes);
    r.links = std::move(links); r.stats.instructions = entries.size(); r.trace = std::move(entries);
    return r;
}
bool same_program(const Program& a, const Program& b) {
    if (a.pes.size() != b.pes.size()) return false;
    for (std::size_t i = 0; i < a.pes.size(); ++i)
        if (a.pes[i].code != b.pes[i].code || a.pes[i].memory != b.pes[i].memory) return false;
    return true;
}
void expect_architecture(Suite& s, const Result& actual, const Result& expected) {
    s.check(actual.termination == expected.termination, std::string("termination: ") + name(actual.termination));
    s.check(actual.tick == expected.tick, "tick: actual " + std::to_string(actual.tick) + ", expected " + std::to_string(expected.tick));
    s.check(actual.pes == expected.pes, "PE PC/status/register/memory differs from hand golden");
    s.check(actual.links == expected.links, "FIFO/reservation/sequence/history differs from hand golden");
    s.check(actual.stats.instructions == expected.stats.instructions, "committed instruction count differs");
    s.check(actual.trace == expected.trace, "committed trace differs from hand golden");
    if (actual.termination == Termination::Fault) s.check(!actual.diagnostic.empty(), "fault lacks diagnostic");
}
void check_pair(Suite& s, const Program& p, const Config& c, const Result& expected) {
    const Program before = p;
    const auto tick = TickEngine{}.run(p, c);
    expect_architecture(s, tick, expected);
    const auto event = EventEngine{}.run(p, c);
    expect_architecture(s, event, expected);
    const auto error = difference(tick, event);
    s.check(error.empty(), "engine difference: " + error);
    s.check(same_program(p, before), "engine mutated input program/memory");
}

void link_invariants(Suite& s, const Result& r, const Config& c) {
    for (const auto& l : r.links) {
        s.check(l.fifo.size() + l.in_flight <= c.capacity, "capacity exceeded");
        s.check(l.next_sequence == l.sent.size(), "sequence assigned without successful SEND");
        s.check(l.delivered.size() <= l.sent.size() && l.received.size() <= l.delivered.size(), "history lengths invalid");
        s.check(l.sent.size() - l.delivered.size() == l.in_flight, "reservation/history accounting invalid");
        s.check(l.delivered.size() - l.received.size() == l.fifo.size(), "FIFO/history accounting invalid");
        for (std::size_t i = 0; i < l.sent.size(); ++i)
            s.check(l.sent[i].sequence == i, "per-link SEND sequence has gap or duplicate");
        for (std::size_t i = 0; i < l.delivered.size(); ++i)
            s.check(l.delivered[i] == l.sent[i], "delivery reordered or changed message");
        for (std::size_t i = 0; i < l.received.size(); ++i)
            s.check(l.received[i] == l.delivered[i], "receive reordered or changed message");
        for (std::size_t i = 0; i < l.fifo.size(); ++i)
            s.check(l.fifo[i] == l.delivered[l.received.size() + i], "visible FIFO not unconsumed delivery suffix");
    }
    if (c.trace) {
        s.check(r.stats.instructions == r.trace.size(), "trace count differs from commit count");
        for (std::size_t i = 1; i < r.trace.size(); ++i) {
            const auto& a = r.trace[i - 1]; const auto& b = r.trace[i];
            s.check(a.tick < b.tick || (a.tick == b.tick && a.pe < b.pe), "completion order is not tick then PE");
        }
    } else s.check(r.trace.empty(), "trace disabled but entries recorded");
}

void scalar_goldens(Suite& s) {
    // Independently calculated: issue/completion times are C 0/2, L 2/5,
    // ADD 5/7, STORE 7/10, HALT 10/12. Nothing reads simulator output here.
    const Program p{{pe({constant(0, -9), load(1, 1), add(7, 0, 1), store(7, 0),
                        {Op::Halt, -999, -999, -999, 999}}, {11, -3, 99})}};
    const std::vector<TraceEntry> prefix{
        trace(2, 0, 0, Op::Const, -9), trace(5, 0, 1, Op::Load, -3), trace(7, 0, 2, Op::Add, -12)};
    auto c = config(2, 3, 4);
    s.run("golden scalar all noncommunication instructions", [&] {
        auto entries = prefix;
        entries.push_back(trace(10, 0, 3, Op::Store, -12)); entries.push_back(trace(12, 0, 4, Op::Halt));
        check_pair(s, p, c, golden(Termination::Completed, 12,
            {state(5, Status::Halted, {-12, -3, 99}, {{0, -9}, {1, -3}, {7, -12}})}, {}, entries));
    });
    s.run("golden STORE memory remains unchanged until completion", [&] {
        auto limited = c; limited.max_ticks = 9;
        check_pair(s, p, limited, golden(Termination::ModelLimit, 9,
            {state(3, Status::Busy, {11, -3, 99}, {{0, -9}, {1, -3}, {7, -12}})}, {}, prefix));
    });
    s.run("golden inclusive model boundary commits STORE and issues HALT", [&] {
        auto limited = c; limited.max_ticks = 10; auto entries = prefix;
        entries.push_back(trace(10, 0, 3, Op::Store, -12));
        check_pair(s, p, limited, golden(Termination::ModelLimit, 10,
            {state(4, Status::Busy, {-12, -3, 99}, {{0, -9}, {1, -3}, {7, -12}})}, {}, entries));
    });
    s.run("golden zero max_ticks still issues at tick zero", [&] {
        auto limited = c; limited.max_ticks = 0;
        check_pair(s, p, limited, golden(Termination::ModelLimit, 0, {state(0, Status::Busy, {11, -3, 99})}, {}, {}));
    });
    s.run("golden LOAD writes only at completion", [&] {
        auto limited = c; limited.max_ticks = 2;
        const Program lp{{pe({load(7, 0), halt()}, {-5})}};
        check_pair(s, lp, limited, golden(Termination::ModelLimit, 2, {state(0, Status::Busy, {-5})}, {}, {}));
        limited.max_ticks = 3;
        check_pair(s, lp, limited, golden(Termination::ModelLimit, 3,
            {state(1, Status::Busy, {-5}, {{7, -5}})}, {}, {trace(3, 0, 0, Op::Load, -5)}));
    });
    s.run("golden completion at max_ticks takes priority over limit", [&] {
        auto limited = c; limited.max_ticks = 2;
        const Program hp{{pe({halt()})}};
        check_pair(s, hp, limited, golden(Termination::Completed, 2,
            {state(1, Status::Halted)}, {}, {trace(2, 0, 0, Op::Halt)}));
    });
    s.run("golden ADD aliases destination and accepts int32 boundaries", [&] {
        const auto min = std::numeric_limits<std::int32_t>::min();
        const auto max = std::numeric_limits<std::int32_t>::max();
        const Program ap{{pe({constant(0, max), add(0, 0, 1), constant(2, min),
                              add(2, 2, 1), add(7, 0, 2), halt()})}};
        check_pair(s, ap, config(), golden(Termination::Completed, 6,
            {state(6, Status::Halted, {}, {{0, max}, {2, min}, {7, -1}})}, {}, {
                trace(1, 0, 0, Op::Const, max), trace(2, 0, 1, Op::Add, max),
                trace(3, 0, 2, Op::Const, min), trace(4, 0, 3, Op::Add, min),
                trace(5, 0, 4, Op::Add, -1), trace(6, 0, 5, Op::Halt)}));
    });
}

void communication_goldens(Suite& s) {
    // At tick 5 PE0 retries a full SEND before PE1 removes the first message.
    // Hence SEND #1 issues at 6 (not 5), becomes visible at 7, and RECV writes at 8.
    const Program p{{
        pe({constant(0, 5), send(0, 1), constant(0, 8), send(0, 1), halt()}),
        pe({load(7, 0), recv(0, 0), recv(0, 0), halt()}, {99})}};
    const auto c = config(1, 5, 1);
    const std::vector<Message> one{{0, 5}}, two{{0, 5}, {1, 8}};
    const std::vector<TraceEntry> initial{
        trace(1, 0, 0, Op::Const, 5), trace(2, 0, 1, Op::Send, 5, 0), trace(3, 0, 2, Op::Const, 8)};
    const std::vector<TraceEntry> all{
        initial[0], initial[1], initial[2], trace(5, 1, 0, Op::Load, 99),
        trace(6, 1, 1, Op::Recv, 5, 0), trace(7, 0, 3, Op::Send, 8, 0, 1),
        trace(8, 0, 4, Op::Halt), trace(8, 1, 2, Op::Recv, 8, 0, 1), trace(9, 1, 3, Op::Halt)};
    s.run("golden capacity1 later receiver requires next-tick sender retry", [&] {
        check_pair(s, p, c, golden(Termination::Completed, 9,
            {state(5, Status::Halted, {}, {{0, 8}}), state(4, Status::Halted, {99}, {{0, 8}, {7, 99}})},
            {link(two, two, two), {}}, all));
    });
    s.run("golden SEND reserves at issue but is invisible in flight", [&] {
        auto limited = c; limited.max_ticks = 1;
        check_pair(s, p, limited, golden(Termination::ModelLimit, 1,
            {state(1, Status::Busy, {}, {{0, 5}}), state(0, Status::Busy, {99})},
            {link(one, {}, {}, {}, 1), {}}, {initial[0]}));
    });
    s.run("golden full SEND neither advances PC nor consumes sequence", [&] {
        auto limited = c; limited.max_ticks = 4;
        check_pair(s, p, limited, golden(Termination::ModelLimit, 4,
            {state(3, Status::BlockedSend, {}, {{0, 8}}), state(0, Status::Busy, {99})},
            {link(one, one, {}, {{0, 5}}), {}}, initial));
    });
    s.run("golden RECV frees slot before its register write", [&] {
        auto limited = c; limited.max_ticks = 5;
        auto entries = initial; entries.push_back(trace(5, 1, 0, Op::Load, 99));
        check_pair(s, p, limited, golden(Termination::ModelLimit, 5,
            {state(3, Status::BlockedSend, {}, {{0, 8}}), state(1, Status::Busy, {99}, {{7, 99}})},
            {link(one, one, one), {}}, entries));
    });
    s.run("golden resumed SEND reserves sequence1 while second RECV waits", [&] {
        auto limited = c; limited.max_ticks = 6;
        std::vector<TraceEntry> entries(all.begin(), all.begin() + 5);
        check_pair(s, p, limited, golden(Termination::ModelLimit, 6,
            {state(3, Status::Busy, {}, {{0, 8}}), state(2, Status::BlockedRecv, {99}, {{0, 5}, {7, 99}})},
            {link(two, one, one, {}, 1), {}}, entries));
    });
    s.run("golden SEND completion visible before whole issue pass", [&] {
        auto limited = c; limited.max_ticks = 7;
        std::vector<TraceEntry> entries(all.begin(), all.begin() + 6);
        check_pair(s, p, limited, golden(Termination::ModelLimit, 7,
            {state(4, Status::Busy, {}, {{0, 8}}), state(2, Status::Busy, {99}, {{0, 5}, {7, 99}})},
            {link(two, two, two), {}}, entries));
    });
    s.run("golden reverse direction earlier receiver frees slot same tick", [&] {
        const Program reverse{{
            pe({load(7, 0), recv(0, 1), recv(0, 1), halt()}, {99}),
            pe({constant(0, 5), send(0, 0), constant(0, 8), send(0, 0), halt()})}};
        check_pair(s, reverse, c, golden(Termination::Completed, 8,
            {state(4, Status::Halted, {99}, {{0, 8}, {7, 99}}), state(5, Status::Halted, {}, {{0, 8}})},
            {{}, link(two, two, two)}, {
                trace(1, 1, 0, Op::Const, 5), trace(2, 1, 1, Op::Send, 5, 1),
                trace(3, 1, 2, Op::Const, 8), trace(5, 0, 0, Op::Load, 99),
                trace(6, 0, 1, Op::Recv, 5, 1), trace(6, 1, 3, Op::Send, 8, 1, 1),
                trace(7, 0, 2, Op::Recv, 8, 1, 1), trace(7, 1, 4, Op::Halt), trace(8, 0, 3, Op::Halt)}));
    });
    s.run("golden simultaneous bidirectional links have independent sequences", [&] {
        const Program both{{pe({constant(0, 3), send(0, 1), recv(1, 1), halt()}),
                            pe({constant(0, 4), send(0, 0), recv(1, 0), halt()})}};
        check_pair(s, both, config(1, 1, 2), golden(Termination::Completed, 5,
            {state(4, Status::Halted, {}, {{0, 3}, {1, 4}}), state(4, Status::Halted, {}, {{0, 4}, {1, 3}})},
            {link({{0, 3}}, {{0, 3}}, {{0, 3}}), link({{0, 4}}, {{0, 4}}, {{0, 4}})}, {
                trace(1, 0, 0, Op::Const, 3), trace(1, 1, 0, Op::Const, 4),
                trace(3, 0, 1, Op::Send, 3, 0), trace(3, 1, 1, Op::Send, 4, 1),
                trace(4, 0, 2, Op::Recv, 4, 1), trace(4, 1, 2, Op::Recv, 3, 0),
                trace(5, 0, 3, Op::Halt), trace(5, 1, 3, Op::Halt)}));
    });
    s.run("golden halted producer and pending consumer is legal waiting", [&] {
        const Program waiting{{pe({send(0, 1), halt()}), pe({load(7, 0), recv(0, 0), store(0, 0), halt()}, {42})}};
        auto cfg = config(1, 9, 5); cfg.max_ticks = 8;
        check_pair(s, waiting, cfg, golden(Termination::ModelLimit, 8,
            {state(2, Status::Halted), state(0, Status::Busy, {42})},
            {link({{0, 0}}, {{0, 0}}, {}, {{0, 0}}), {}}, {trace(5, 0, 0, Op::Send, 0, 0), trace(6, 0, 1, Op::Halt)}));
        cfg.max_ticks = 20;
        check_pair(s, waiting, cfg, golden(Termination::Completed, 20,
            {state(2, Status::Halted), state(4, Status::Halted, {0}, {{7, 42}})},
            {link({{0, 0}}, {{0, 0}}, {{0, 0}}), {}}, {
                trace(5, 0, 0, Op::Send, 0, 0), trace(6, 0, 1, Op::Halt), trace(9, 1, 0, Op::Load, 42),
                trace(10, 1, 1, Op::Recv, 0, 0), trace(19, 1, 2, Op::Store, 0), trace(20, 1, 3, Op::Halt)}));
    });
}

void termination_goldens(Suite& s) {
    s.run("golden mutual empty RECV is immediate deadlock", [&] {
        const Program p{{pe({recv(0, 1), halt()}), pe({recv(0, 0), halt()})}};
        auto cfg = config(); cfg.max_ticks = 0;
        check_pair(s, p, cfg, golden(Termination::Deadlock, 0,
            {state(0, Status::BlockedRecv), state(0, Status::BlockedRecv)}, {{}, {}}, {}));
    });
    s.run("golden full queues without pending work is deadlock", [&] {
        const Program p{{pe({constant(0, 2), send(0, 1), send(0, 1), halt()}),
                         pe({constant(0, 3), send(0, 0), send(0, 0), halt()})}};
        check_pair(s, p, config(1, 1, 1), golden(Termination::Deadlock, 2,
            {state(2, Status::BlockedSend, {}, {{0, 2}}), state(2, Status::BlockedSend, {}, {{0, 3}})},
            {link({{0, 2}}, {{0, 2}}, {}, {{0, 2}}), link({{0, 3}}, {{0, 3}}, {}, {{0, 3}})}, {
                trace(1, 0, 0, Op::Const, 2), trace(1, 1, 0, Op::Const, 3),
                trace(2, 0, 1, Op::Send, 2, 0), trace(2, 1, 1, Op::Send, 3, 1)}));
    });
    s.run("golden all HALT with unconsumed delivery is orphaned", [&] {
        const Program p{{pe({constant(0, 7), send(0, 1), halt()}), pe({halt()})}};
        check_pair(s, p, config(), golden(Termination::OrphanedMessages, 5,
            {state(3, Status::Halted, {}, {{0, 7}}), state(1, Status::Halted)},
            {link({{0, 7}}, {{0, 7}}, {}, {{0, 7}}), {}}, {
                trace(1, 0, 0, Op::Const, 7), trace(1, 1, 0, Op::Halt),
                trace(4, 0, 1, Op::Send, 7, 0), trace(5, 0, 2, Op::Halt)}));
    });
    s.run("golden fault keeps previously issued lower PE reservation", [&] {
        const Program p{{pe({send(0, 1), halt()}), pe({constant(8, 1)})}};
        check_pair(s, p, config(), golden(Termination::Fault, 0,
            {state(0, Status::Busy), state(0, Status::Faulted)},
            {link({{0, 0}}, {}, {}, {}, 1), {}}, {}));
    });
    s.run("golden empty program faults without implicit HALT", [&] {
        check_pair(s, Program{{pe({})}}, config(), golden(Termination::Fault, 0, {state(0, Status::Faulted)}, {}, {}));
    });
    s.run("golden falling off end retains committed effects", [&] {
        check_pair(s, Program{{pe({constant(0, 9)})}}, config(), golden(Termination::Fault, 1,
            {state(1, Status::Faulted, {}, {{0, 9}})}, {}, {trace(1, 0, 0, Op::Const, 9)}));
    });
    for (const auto sign : {1, -1}) s.run("golden ADD overflow sign=" + std::to_string(sign), [&] {
        const auto edge = sign > 0 ? std::numeric_limits<std::int32_t>::max() : std::numeric_limits<std::int32_t>::min();
        const Program p{{pe({constant(0, edge), constant(1, sign), add(2, 0, 1), halt()})}};
        check_pair(s, p, config(), golden(Termination::Fault, 2,
            {state(2, Status::Faulted, {}, {{0, edge}, {1, sign}})}, {}, {
                trace(1, 0, 0, Op::Const, edge), trace(2, 0, 1, Op::Const, sign)}));
    });
}

void invalid_inputs(Suite& s) {
    std::vector<std::pair<std::string, Instruction>> bad;
    for (const int index : {-1, 8}) {
        const auto suffix = std::to_string(index);
        bad.emplace_back("CONST dst " + suffix, constant(index, 1));
        bad.emplace_back("LOAD dst " + suffix, load(index, 0));
        bad.emplace_back("STORE src " + suffix, store(index, 0));
        bad.emplace_back("ADD dst " + suffix, add(index, 0, 1));
        bad.emplace_back("ADD lhs " + suffix, add(0, index, 1));
        bad.emplace_back("ADD rhs " + suffix, add(0, 1, index));
        bad.emplace_back("SEND src " + suffix, send(index, 1));
        bad.emplace_back("RECV dst " + suffix, recv(index, 1));
    }
    for (const int address : {-1, 1}) {
        bad.emplace_back("LOAD address " + std::to_string(address), load(0, address));
        bad.emplace_back("STORE address " + std::to_string(address), store(0, address));
    }
    for (const int peer : {-1, 0, 2}) {
        bad.emplace_back("SEND peer " + std::to_string(peer), send(0, peer));
        bad.emplace_back("RECV peer " + std::to_string(peer), recv(0, peer));
    }
    bad.emplace_back("unknown opcode", Instruction{static_cast<Op>(999)});
    for (const auto& [label, instruction] : bad) s.run("invalid instruction " + label, [&] {
        const Program p{{pe({instruction, halt()}, {23}), pe({halt()})}};
        check_pair(s, p, config(), golden(Termination::Fault, 0,
            {state(0, Status::Faulted, {23}), state(0, Status::Running)}, {{}, {}}, {}));
    });
    for (const Op op : {Op::Send, Op::Recv}) s.run(std::string("invalid nonadjacent ") + name(op), [&] {
        const Program p{{pe({{op, 0, 2}}), pe({halt()}), pe({halt()})}};
        check_pair(s, p, config(), golden(Termination::Fault, 0,
            {state(0, Status::Faulted), state(0, Status::Running), state(0, Status::Running)}, {{}, {}, {}, {}}, {}));
    });
    for (const Op op : {Op::Load, Op::Store}) s.run(std::string("invalid empty local memory ") + name(op), [&] {
        check_pair(s, Program{{pe({{op, 0, 0}})}}, config(),
            golden(Termination::Fault, 0, {state(0, Status::Faulted)}, {}, {}));
    });
    const Program p{{pe({halt()})}};
    std::vector<std::pair<std::string, Config>> invalid;
    auto c = config(); c.capacity = 0; invalid.emplace_back("zero capacity", c);
    c = config(); c.compute_latency = 0; invalid.emplace_back("zero compute latency", c);
    c = config(); c.memory_latency = 0; invalid.emplace_back("zero memory latency", c);
    c = config(); c.link_latency = 0; invalid.emplace_back("zero link latency", c);
    const Tick over = (Tick{1} << 63);
    c = config(); c.compute_latency = over; invalid.emplace_back("oversized compute latency", c);
    c = config(); c.memory_latency = over; invalid.emplace_back("oversized memory latency", c);
    c = config(); c.link_latency = over; invalid.emplace_back("oversized link latency", c);
    c = config(); c.max_ticks = over; invalid.emplace_back("oversized model limit", c);
    for (const auto& [label, cfg] : invalid) s.run("invalid config " + label, [&] {
        bool tick_threw = false, event_threw = false;
        try { (void)TickEngine{}.run(p, cfg); } catch (const std::invalid_argument&) { tick_threw = true; }
        try { (void)EventEngine{}.run(p, cfg); } catch (const std::invalid_argument&) { event_threw = true; }
        s.check(tick_threw && event_threw, "both engines must reject config with invalid_argument");
    });
    s.run("invalid empty PE array", [&] {
        bool tick_threw = false, event_threw = false;
        try { (void)TickEngine{}.run(Program{}, config()); } catch (const std::invalid_argument&) { tick_threw = true; }
        try { (void)EventEngine{}.run(Program{}, config()); } catch (const std::invalid_argument&) { event_threw = true; }
        s.check(tick_threw && event_threw, "empty PE array must be rejected");
    });
    s.run("maximum permitted latency with zero model limit is safe", [&] {
        auto cfg = config(over - 1, over - 1, over - 1); cfg.max_ticks = 0;
        check_pair(s, p, cfg, golden(Termination::ModelLimit, 0, {state(0, Status::Busy)}, {}, {}));
    });
}

void comparator_coverage(Suite& s) {
    // Synthetic state makes comparator tests independent of either scheduler.
    auto base = golden(Termination::Completed, 7,
        {state(2, Status::Halted, {4, 9}, {{0, 5}})}, {link({{0, 5}}, {{0, 5}}, {{0, 5}})},
        {trace(7, 0, 1, Op::Recv, 5, 0)});
    using Mutation = std::function<void(Result&)>;
    const std::vector<std::pair<std::string, Mutation>> mutations{
        {"termination", [](Result& r) { r.termination = Termination::Fault; }},
        {"tick", [](Result& r) { ++r.tick; }},
        {"diagnostic", [](Result& r) { r.diagnostic = "different"; }},
        {"PE count", [](Result& r) { r.pes.push_back({}); }},
        {"PC", [](Result& r) { ++r.pes[0].pc; }},
        {"status", [](Result& r) { r.pes[0].status = Status::Busy; }},
        {"register", [](Result& r) { ++r.pes[0].regs[7]; }},
        {"memory", [](Result& r) { ++r.pes[0].memory[1]; }},
        {"allocated memory size", [](Result& r) { r.pes[0].memory.push_back(0); }},
        {"link count", [](Result& r) { r.links.push_back({}); }},
        {"FIFO", [](Result& r) { r.links[0].fifo.push_back({1, 7}); }},
        {"in-flight", [](Result& r) { ++r.links[0].in_flight; }},
        {"next sequence", [](Result& r) { ++r.links[0].next_sequence; }},
        {"sent history", [](Result& r) { ++r.links[0].sent[0].value; }},
        {"delivered history", [](Result& r) { ++r.links[0].delivered[0].value; }},
        {"received history", [](Result& r) { ++r.links[0].received[0].sequence; }},
        {"instruction count", [](Result& r) { ++r.stats.instructions; }},
        {"trace length", [](Result& r) { r.trace.push_back({}); }},
        {"trace tick", [](Result& r) { ++r.trace[0].tick; }},
        {"trace PE", [](Result& r) { ++r.trace[0].pe; }},
        {"trace PC", [](Result& r) { ++r.trace[0].pc; }},
        {"trace opcode", [](Result& r) { r.trace[0].op = Op::Send; }},
        {"trace value", [](Result& r) { ++r.trace[0].value; }},
        {"trace link", [](Result& r) { ++r.trace[0].link; }},
        {"trace sequence", [](Result& r) { ++r.trace[0].sequence; }}
    };
    for (const auto& [label, mutate] : mutations) s.run("comparator detects " + label, [&] {
        auto changed = base; mutate(changed);
        s.check(!difference(base, changed).empty(), "comparator omitted " + label);
        s.check(!difference(changed, base).empty(), "comparator not symmetric for " + label);
    });
    s.run("comparator ignores engine work statistics", [&] {
        auto changed = base;
        changed.stats.scheduler_events += 4; changed.stats.clock_steps += 9; changed.stats.pe_checks += 17;
        s.check(difference(base, changed).empty(), "implementation statistics affect architecture comparison");
    });
}

void workloads(Suite& s) {
    std::size_t index = 0;
    for (const std::string kind : {"relay", "scan", "reduction"})
        for (const std::size_t pes : {1U, 2U, 3U, 4U, 8U})
            for (const std::size_t capacity : {1U, 2U, 4U})
                for (const std::size_t size : std::vector<std::size_t>{0, 1, pes + 1, 17})
                    for (const auto& latency : std::vector<std::array<Tick, 3>>{{1, 1, 1}, {1, 2, 3}, {3, 7, 13}}) {
                        const std::uint32_t seed = std::array<std::uint32_t, 3>{1, 7, 0x12345678U}[index++ % 3];
                        const std::string label = "workload " + kind + " P=" + std::to_string(pes) +
                            " N=" + std::to_string(size) + " cap=" + std::to_string(capacity) +
                            " latency=" + std::to_string(latency[0]) + "/" + std::to_string(latency[1]) + "/" +
                            std::to_string(latency[2]) + " seed=" + std::to_string(seed);
                        s.run(label, [&] {
                            const auto w = make_workload(kind, pes, size, seed);
                            const auto before = w.program;
                            auto cfg = config(latency[0], latency[1], latency[2]); cfg.capacity = capacity;
                            const auto a = TickEngine{}.run(w.program, cfg), b = EventEngine{}.run(w.program, cfg);
                            s.check(a.termination == Termination::Completed, "tick workload did not complete");
                            s.check(b.termination == Termination::Completed, "event workload did not complete");
                            const auto delta = difference(a, b);
                            s.check(delta.empty(), "full differential: " + delta);
                            const auto ao = oracle_error(w, a), bo = oracle_error(w, b);
                            s.check(ao.empty(), "tick serial oracle: " + ao);
                            s.check(bo.empty(), "event serial oracle: " + bo);
                            s.check(same_program(w.program, before), "workload inputs were mutated");
                            link_invariants(s, a, cfg); link_invariants(s, b, cfg);
                        });
                    }
    s.run("generator seeds are deterministic and bounded", [&] {
        const auto a = make_workload("scan", 3, 31, 7), b = make_workload("scan", 3, 31, 7);
        s.check(a.input == b.input && same_program(a.program, b.program), "same seed generated different workload");
        s.check(a.input.size() == 31, "input size ignored");
        for (const auto value : a.input) s.check(value >= -8 && value <= 8, "input outside defined integer range");
        const auto c = make_workload("scan", 3, 31, 8);
        s.check(a.input != c.input, "distinct selected seeds generated identical inputs");
    });
    s.run("reusing engine instances resets all state and preserves inputs", [&] {
        const TickEngine tick; const EventEngine event;
        const auto w = make_workload("scan", 4, 17, 7);
        const auto before = w.program; const auto cfg = config(2, 5, 11);
        const auto a = tick.run(w.program, cfg), b = event.run(w.program, cfg);
        const auto interrupted = make_workload("relay", 3, 6, 9);
        auto limited = cfg; limited.max_ticks = 2;
        (void)tick.run(interrupted.program, limited); (void)event.run(interrupted.program, limited);
        for (int repeat = 0; repeat < 3; ++repeat) {
            s.check(difference(a, tick.run(w.program, cfg)).empty(), "TickEngine leaked state between runs");
            s.check(difference(b, event.run(w.program, cfg)).empty(), "EventEngine leaked state between runs");
        }
        s.check(same_program(w.program, before), "repeated run mutated initial code/memory");
    });
    s.run("trace disabled preserves architectural state and commits", [&] {
        const auto w = make_workload("reduction", 4, 11, 7); auto cfg = config();
        auto expected = TickEngine{}.run(w.program, cfg); expected.trace.clear(); cfg.trace = false;
        const auto a = TickEngine{}.run(w.program, cfg), b = EventEngine{}.run(w.program, cfg);
        s.check(difference(expected, a).empty(), "disabling trace changed TickEngine architecture");
        s.check(difference(a, b).empty(), "trace-disabled engines differ");
        link_invariants(s, a, cfg); link_invariants(s, b, cfg);
    });
    for (const std::string kind : {"relay", "scan", "reduction"}) s.run("serial oracle rejects corrupted " + kind, [&] {
        const auto w = make_workload(kind, 3, 7, 7); auto result = TickEngine{}.run(w.program, config());
        for (auto& p : result.pes) for (auto& value : p.memory) ++value;
        s.check(!oracle_error(w, result).empty(), "oracle accepted corrupted final memory");
    });
}

std::uint32_t random_word(std::uint32_t& state_value) {
    state_value ^= state_value << 13; state_value ^= state_value >> 17; state_value ^= state_value << 5;
    return state_value;
}
void bounded_protocol_differential(Suite& s) {
    // This seeded family intentionally includes blocked/orphaned/limited programs;
    // it supplements the successful workload matrix and the independent goldens.
    for (std::uint32_t seed = 1; seed <= 120; ++seed) s.run("bounded protocol differential seed=" + std::to_string(seed), [&] {
        auto rng = seed; const std::size_t pes = 2 + random_word(rng) % 3;
        Program p;
        for (std::size_t id = 0; id < pes; ++id) {
            auto local = pe({}, {3, -2, 1});
            for (int instruction = 0; instruction < 9; ++instruction) {
                const auto word = random_word(rng); const int reg = static_cast<int>((word >> 8) % 8);
                const int peer = id == 0 ? 1 : (id + 1 == pes ? static_cast<int>(id) - 1 :
                    static_cast<int>(id) + ((word & 128U) ? 1 : -1));
                switch (word % 6) {
                    case 0: local.code.push_back(constant(reg, static_cast<std::int32_t>((word >> 16) % 17) - 8)); break;
                    case 1: local.code.push_back(load(reg, static_cast<int>((word >> 16) % 3))); break;
                    case 2: local.code.push_back(store(reg, static_cast<int>((word >> 16) % 3))); break;
                    case 3: local.code.push_back(add(reg, static_cast<int>((word >> 16) % 8), static_cast<int>((word >> 24) % 8))); break;
                    case 4: local.code.push_back(send(reg, peer)); break;
                    case 5: local.code.push_back(recv(reg, peer)); break;
                }
            }
            local.code.push_back(halt()); p.pes.push_back(std::move(local));
        }
        auto cfg = config(1 + seed % 3, 1 + seed % 5, 1 + seed % 11);
        cfg.capacity = 1 + seed % 4; cfg.max_ticks = (seed % 7 == 0) ? 5 : 150;
        const auto a = TickEngine{}.run(p, cfg), b = EventEngine{}.run(p, cfg);
        const auto delta = difference(a, b);
        s.check(delta.empty(), "full protocol differential: " + delta);
        link_invariants(s, a, cfg); link_invariants(s, b, cfg);
    });
}
} // namespace

int main() {
    Suite s;
    scalar_goldens(s); communication_goldens(s); termination_goldens(s);
    invalid_inputs(s); comparator_coverage(s); workloads(s); bounded_protocol_differential(s);
    std::cout << "cases=" << s.cases << " passed=" << s.cases - s.failures
              << " failed=" << s.failures << " checks=" << s.checks << '\n';
    return s.failures == 0 ? 0 : 1;
}
