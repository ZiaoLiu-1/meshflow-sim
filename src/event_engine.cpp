#include "meshflow/model.hpp"

#include <queue>
#include <stdexcept>
#include <tuple>

namespace meshflow {
namespace {

// A completion owns the operands captured at issue. Wake records carry no
// architectural operation; they only request a single ordered issue pass.
struct Event {
    Tick tick{0};
    unsigned phase{1};
    std::size_t stable_sequence{0};
    std::size_t pc{0};
    Instruction instruction;
    std::int32_t value{0};
    std::int64_t link{-1};
    std::uint64_t message_sequence{0};
};

struct LaterEvent {
    bool operator()(const Event& left, const Event& right) const {
        return std::tie(left.tick, left.phase, left.stable_sequence) >
               std::tie(right.tick, right.phase, right.stable_sequence);
    }
};

} // namespace

Result EventEngine::run(const Program& program, const Config& config) const {
    if (const auto error = validate(program, config); !error.empty()) {
        throw std::invalid_argument(error);
    }

    Result result;
    result.pes.resize(program.pes.size());
    for (std::size_t pe = 0; pe < program.pes.size(); ++pe) {
        result.pes[pe].memory = program.pes[pe].memory;
    }
    result.links.resize(2 * (program.pes.size() - 1));

    std::priority_queue<Event, std::vector<Event>, LaterEvent> events;
    events.push(Event{}); // Tick zero starts with issue, without a completion.
    std::size_t pending_operations = 0;

    for (;;) {
        // If the next event is past the inclusive model limit, still inspect
        // the boundary once. No later completion may change boundary state.
        result.tick = events.top().tick > config.max_ticks
                          ? config.max_ticks
                          : events.top().tick;
        ++result.stats.clock_steps;

        while (!events.empty() && events.top().tick == result.tick) {
            const Event event = events.top();
            events.pop();
            ++result.stats.scheduler_events;
            if (event.phase == 1) {
                continue;
            }

            auto& pe = result.pes[event.stable_sequence];
            const auto& instruction = event.instruction;
            switch (instruction.op) {
            case Op::Const:
            case Op::Load:
            case Op::Add:
            case Op::Recv:
                pe.regs[static_cast<std::size_t>(instruction.a)] = event.value;
                break;
            case Op::Store:
                pe.memory[static_cast<std::size_t>(instruction.b)] = event.value;
                break;
            case Op::Send: {
                auto& link = result.links[static_cast<std::size_t>(event.link)];
                --link.in_flight;
                const Message message{event.message_sequence, event.value};
                link.fifo.push_back(message);
                link.delivered.push_back(message);
                break;
            }
            case Op::Halt:
                break;
            }
            pe.status = instruction.op == Op::Halt ? Status::Halted : Status::Running;
            ++pe.pc;
            --pending_operations;
            ++result.stats.instructions;
            if (config.trace) {
                result.trace.push_back({result.tick, event.stable_sequence, event.pc,
                                        instruction.op, event.value, event.link,
                                        event.message_sequence});
            }
        }

        // All due writes/deliveries are visible before this ordered issue pass.
        for (std::size_t id = 0; id < result.pes.size(); ++id) {
            ++result.stats.pe_checks;
            auto& pe = result.pes[id];
            if (pe.status == Status::Busy || pe.status == Status::Halted) {
                continue;
            }
            const auto fail = [&](const std::string& error) {
                pe.status = Status::Faulted;
                result.termination = Termination::Fault;
                result.diagnostic = "PE " + std::to_string(id) + " PC " +
                                    std::to_string(pe.pc) + ": " + error;
            };
            if (pe.pc >= program.pes[id].code.size()) {
                fail("PC outside program");
                return result;
            }
            const Instruction instruction = program.pes[id].code[pe.pc];
            if (const auto error = instruction_error(instruction, id, result.pes.size(),
                                                     pe.memory.size());
                !error.empty()) {
                fail(error);
                return result;
            }

            Event completion;
            completion.phase = 0;
            completion.stable_sequence = id;
            completion.pc = pe.pc;
            completion.instruction = instruction;
            Tick latency = config.compute_latency;
            switch (instruction.op) {
            case Op::Const:
                completion.value = instruction.value;
                break;
            case Op::Load:
                completion.value = pe.memory[static_cast<std::size_t>(instruction.b)];
                latency = config.memory_latency;
                break;
            case Op::Store:
                completion.value = pe.regs[static_cast<std::size_t>(instruction.a)];
                latency = config.memory_latency;
                break;
            case Op::Add: {
                const auto sum = checked_add(pe.regs[static_cast<std::size_t>(instruction.b)],
                                             pe.regs[static_cast<std::size_t>(instruction.c)]);
                if (!sum) {
                    fail("ADD overflow");
                    return result;
                }
                completion.value = *sum;
                break;
            }
            case Op::Send: {
                const auto index = link_index(id, static_cast<std::size_t>(instruction.b));
                auto& link = result.links[index];
                if (link.fifo.size() + link.in_flight >= config.capacity) {
                    pe.status = Status::BlockedSend;
                    continue;
                }
                const Message message{link.next_sequence++,
                                      pe.regs[static_cast<std::size_t>(instruction.a)]};
                ++link.in_flight;
                link.sent.push_back(message);
                completion.value = message.value;
                completion.link = static_cast<std::int64_t>(index);
                completion.message_sequence = message.sequence;
                latency = config.link_latency;
                break;
            }
            case Op::Recv: {
                const auto index = link_index(static_cast<std::size_t>(instruction.b), id);
                auto& link = result.links[index];
                if (link.fifo.empty()) {
                    pe.status = Status::BlockedRecv;
                    continue;
                }
                const Message message = link.fifo.front();
                link.fifo.pop_front();
                link.received.push_back(message);
                completion.value = message.value;
                completion.link = static_cast<std::int64_t>(index);
                completion.message_sequence = message.sequence;
                break;
            }
            case Op::Halt:
                break;
            }
            completion.tick = result.tick + latency;
            pe.status = Status::Busy;
            ++pending_operations;
            events.push(completion);
        }

        bool all_halted = true;
        bool retry_eligible = false;
        for (std::size_t id = 0; id < result.pes.size(); ++id) {
            ++result.stats.pe_checks;
            const auto& pe = result.pes[id];
            all_halted = all_halted && pe.status == Status::Halted;
            if (pe.status == Status::BlockedSend) {
                const auto destination = static_cast<std::size_t>(program.pes[id].code[pe.pc].b);
                const auto& link = result.links[link_index(id, destination)];
                retry_eligible = retry_eligible ||
                                 link.fifo.size() + link.in_flight < config.capacity;
            } else if (pe.status == Status::BlockedRecv) {
                const auto source = static_cast<std::size_t>(program.pes[id].code[pe.pc].b);
                retry_eligible = retry_eligible || !result.links[link_index(source, id)].fifo.empty();
            }
        }

        if (all_halted && pending_operations == 0) {
            result.termination = Termination::Completed;
            for (const auto& link : result.links) {
                if (!link.fifo.empty() || link.in_flight != 0) {
                    result.termination = Termination::OrphanedMessages;
                    break;
                }
            }
            return result;
        }
        if (pending_operations == 0 && !retry_eligible) {
            result.termination = Termination::Deadlock;
            return result;
        }
        if (result.tick == config.max_ticks) {
            result.termination = Termination::ModelLimit;
            return result;
        }
        if (retry_eligible) {
            // A later PE may have freed a FIFO slot after an earlier sender
            // already blocked. Coalesce every such retry into one phase-1 wake.
            Event wake;
            wake.tick = result.tick + 1;
            events.push(wake);
        }
        // A pending operation or the coalesced retry guarantees a future event.
    }
}

} // namespace meshflow
