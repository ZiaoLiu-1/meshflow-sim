#include "meshflow/model.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace meshflow {
namespace {

// TickEngine deliberately owns its pending-operation representation and all
// transitions; the event engine shares only the public model's pure helpers.
struct TickOperation {
    Tick due;
    std::size_t pc;
    Instruction instruction;
    std::int32_t value;
    std::int64_t link{-1};
    std::uint64_t sequence{0};
};

} // namespace

Result TickEngine::run(const Program& program, const Config& config) const {
    if (const auto error = validate(program, config); !error.empty()) {
        throw std::invalid_argument(error);
    }

    Result result;
    result.pes.resize(program.pes.size());
    result.links.resize(2 * (program.pes.size() - 1));
    for (std::size_t pe = 0; pe < program.pes.size(); ++pe) {
        result.pes[pe].memory = program.pes[pe].memory;
    }
    std::vector<std::optional<TickOperation>> pending(program.pes.size());

    for (Tick tick = 0;; ++tick) {
        result.tick = tick;
        ++result.stats.clock_steps;

        // pe_checks counts these completion and issue scan visits, including
        // busy/halted PEs. Initialization and termination bookkeeping are not
        // scheduler work. On a fault the issue scan stops at the faulting PE.
        for (std::size_t pe = 0; pe < result.pes.size(); ++pe) {
            ++result.stats.pe_checks;
            if (!pending[pe] || pending[pe]->due != tick) {
                continue;
            }

            const auto operation = *pending[pe];
            const auto& instruction = operation.instruction;
            auto& state = result.pes[pe];
            switch (instruction.op) {
            case Op::Const:
            case Op::Load:
            case Op::Add:
            case Op::Recv:
                state.regs[static_cast<std::size_t>(instruction.a)] = operation.value;
                break;
            case Op::Store:
                state.memory[static_cast<std::size_t>(instruction.b)] = operation.value;
                break;
            case Op::Send: {
                auto& link = result.links[static_cast<std::size_t>(operation.link)];
                const Message message{operation.sequence, operation.value};
                link.fifo.push_back(message);
                link.delivered.push_back(message);
                --link.in_flight;
                break;
            }
            case Op::Halt:
                break;
            }

            state.status = instruction.op == Op::Halt ? Status::Halted : Status::Running;
            ++state.pc;
            ++result.stats.instructions;
            ++result.stats.scheduler_events;
            if (config.trace) {
                result.trace.push_back({tick, pe, operation.pc, instruction.op,
                                        operation.value, operation.link, operation.sequence});
            }
            pending[pe].reset();
        }

        // All writebacks/deliveries above are visible before any PE issues.
        // Within this pass, reservations and RECV slot releases are immediate.
        for (std::size_t pe = 0; pe < result.pes.size(); ++pe) {
            ++result.stats.pe_checks;
            auto& state = result.pes[pe];
            if (pending[pe] || state.status == Status::Halted) {
                continue;
            }

            const auto fault = [&](const std::string& error) {
                state.status = Status::Faulted;
                result.termination = Termination::Fault;
                result.diagnostic = "PE " + std::to_string(pe) + " PC " +
                                    std::to_string(state.pc) + ": " + error;
            };
            if (state.pc >= program.pes[pe].code.size()) {
                fault("PC outside program");
                return result;
            }
            const auto instruction = program.pes[pe].code[state.pc];
            if (const auto error = instruction_error(instruction, pe, result.pes.size(),
                                                     state.memory.size());
                !error.empty()) {
                fault(error);
                return result;
            }

            TickOperation operation{tick + config.compute_latency, state.pc, instruction, 0};
            switch (instruction.op) {
            case Op::Const:
                operation.value = instruction.value;
                break;
            case Op::Load:
                operation.value = state.memory[static_cast<std::size_t>(instruction.b)];
                operation.due = tick + config.memory_latency;
                break;
            case Op::Store:
                operation.value = state.regs[static_cast<std::size_t>(instruction.a)];
                operation.due = tick + config.memory_latency;
                break;
            case Op::Add: {
                const auto sum = checked_add(state.regs[static_cast<std::size_t>(instruction.b)],
                                             state.regs[static_cast<std::size_t>(instruction.c)]);
                if (!sum) {
                    fault("ADD overflow");
                    return result;
                }
                operation.value = *sum;
                break;
            }
            case Op::Send: {
                const auto index = link_index(pe, static_cast<std::size_t>(instruction.b));
                auto& link = result.links[index];
                if (link.fifo.size() >= config.capacity - link.in_flight) {
                    state.status = Status::BlockedSend;
                    continue;
                }
                operation.value = state.regs[static_cast<std::size_t>(instruction.a)];
                operation.due = tick + config.link_latency;
                operation.link = static_cast<std::int64_t>(index);
                operation.sequence = link.next_sequence++;
                ++link.in_flight;
                link.sent.push_back({operation.sequence, operation.value});
                break;
            }
            case Op::Recv: {
                const auto index = link_index(static_cast<std::size_t>(instruction.b), pe);
                auto& link = result.links[index];
                if (link.fifo.empty()) {
                    state.status = Status::BlockedRecv;
                    continue;
                }
                const auto message = link.fifo.front();
                link.fifo.pop_front();
                link.received.push_back(message);
                operation.value = message.value;
                operation.link = static_cast<std::int64_t>(index);
                operation.sequence = message.sequence;
                break;
            }
            case Op::Halt:
                break;
            }
            pending[pe] = operation;
            state.status = Status::Busy;
        }

        bool all_halted = true;
        bool any_pending = false;
        bool retry_can_progress = false;
        for (std::size_t pe = 0; pe < result.pes.size(); ++pe) {
            const auto& state = result.pes[pe];
            all_halted = all_halted && state.status == Status::Halted;
            any_pending = any_pending || pending[pe].has_value();

            // A higher-ID receiver may have freed capacity after an earlier
            // sender blocked. Diagnose current eligibility, not only whether
            // something issued during this pass, before declaring deadlock.
            if (state.status == Status::BlockedSend) {
                const auto& instruction = program.pes[pe].code[state.pc];
                const auto& link = result.links[link_index(
                    pe, static_cast<std::size_t>(instruction.b))];
                retry_can_progress = retry_can_progress ||
                                     link.fifo.size() < config.capacity - link.in_flight;
            } else if (state.status == Status::BlockedRecv) {
                const auto& instruction = program.pes[pe].code[state.pc];
                const auto& link = result.links[link_index(
                    static_cast<std::size_t>(instruction.b), pe)];
                retry_can_progress = retry_can_progress || !link.fifo.empty();
            }
        }

        if (all_halted && !any_pending) {
            bool messages_remain = false;
            for (const auto& link : result.links) {
                messages_remain = messages_remain || !link.fifo.empty() || link.in_flight != 0;
            }
            result.termination = messages_remain ? Termination::OrphanedMessages
                                                : Termination::Completed;
            return result;
        }
        if (!any_pending && !retry_can_progress) {
            result.termination = Termination::Deadlock;
            return result;
        }
        if (tick == config.max_ticks) {
            result.termination = Termination::ModelLimit;
            return result;
        }
    }
}

} // namespace meshflow
