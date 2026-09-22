#include "cupid/rsp/pipeline.hpp"

namespace cupid {

RspPipeline::Ports RspPipeline::decode(u32 word) {
    Ports ports;
    const unsigned opcode = word >> 26U;
    const unsigned rs = (word >> 21U) & 31U;
    const unsigned rt = (word >> 16U) & 31U;
    const unsigned rd = (word >> 11U) & 31U;
    const unsigned function = word & 63U;
    const u32 source = 1U << rs;
    const u32 target = 1U << rt;

    if (opcode == 0U) {
        switch (function) {
        case 0x00:
        case 0x02:
        case 0x03:
            ports.scalar_reads = target;
            break;
        case 0x04:
        case 0x06:
        case 0x07:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x2a:
        case 0x2b:
            ports.scalar_reads = source | target;
            break;
        case 0x08:
        case 0x09:
            ports.scalar_reads = source;
            ports.flags = branch;
            break;
        case 0x0d:
            ports.flags = branch;
            break;
        default:
            break;
        }
    } else if (opcode == 1U) {
        if (rt == 0U || rt == 1U || rt == 16U || rt == 17U) {
            ports.scalar_reads = source;
            ports.flags = branch;
        }
    } else if (opcode >= 2U && opcode <= 7U) {
        ports.flags = branch;
        if (opcode >= 4U)
            ports.scalar_reads = source;
        if (opcode == 4U || opcode == 5U)
            ports.scalar_reads |= target;
    } else if (opcode >= 8U && opcode <= 14U) {
        ports.scalar_reads = source;
    } else if (opcode == 0x10U && (rs == 0U || rs == 4U)) {
        ports.flags = load | store;
        if (rs == 0U)
            ports.scalar_result = target & ~1U;
        else
            ports.scalar_reads = target;
    } else if (opcode == 0x12U && rs < 16U) {
        if (rs == 0U || rs == 2U || rs == 4U || rs == 6U) {
            ports.flags = load | store;
            if (rs < 4U)
                ports.scalar_result = target & ~1U;
            else
                ports.scalar_reads = target;
            if (rs == 0U)
                ports.vector_reads = 1U << rd;
            else if (rs == 2U)
                ports.control_reads = 1U << (rd & 3U);
            else if (rs == 4U) {
                ports.vector_result = 1U << rd;
                ports.flags |= nop_conflict;
            } else
                ports.control_result = 1U << (rd & 3U);
        }
    } else if (opcode == 0x12U) {
        ports.flags = vector_alu;
        const unsigned destination = (word >> 6U) & 31U;
        const u32 vector_source = 1U << rd;
        const bool binary = function <= 0x11U && function != 0x02U && function != 0x0aU && function != 0x0bU;
        if (binary || (function >= 0x13U && function <= 0x15U) || (function >= 0x20U && function <= 0x2dU)) {
            ports.vector_reads = vector_source | target;
            ports.vector_result = 1U << destination;
        } else if (function == 0x02U || function == 0x0aU || (function >= 0x30U && function <= 0x36U)) {
            ports.vector_reads = target;
            ports.vector_result = 1U << destination;
            if (function >= 0x30U)
                ports.field_reads = vector_source;
        } else if (function == 0x0bU || function == 0x1dU) {
            ports.vector_result = 1U << destination;
        } else if (function == 0x37U) {
            ports.field_reads = 1U << destination;
            ports.flags |= vector_nop;
        }

        if (function == 0x10U || function == 0x11U || (function >= 0x13U && function <= 0x15U)) {
            ports.control_reads = ports.control_result = 1U;
        } else if (function >= 0x20U && function <= 0x27U) {
            ports.control_reads = ports.control_result = function >= 0x24U && function <= 0x26U ? 7U : 3U;
        }
    } else if (opcode == 0x20U || opcode == 0x21U || opcode == 0x23U || opcode == 0x24U || opcode == 0x25U ||
               opcode == 0x27U) {
        ports.scalar_reads = source;
        ports.scalar_result = target & ~1U;
        ports.flags = load;
    } else if (opcode == 0x28U || opcode == 0x29U || opcode == 0x2bU) {
        ports.scalar_reads = source | target;
        ports.flags = store;
    } else if (opcode == 0x32U || opcode == 0x3aU) {
        const bool loading = opcode == 0x32U;
        if (rd <= 0x0bU && (!loading || rd != 0x0aU)) {
            ports.scalar_reads = source;
            ports.flags = loading ? load : store;
            const u32 registers = rd == 0x0bU ? 0xffU << (rt & ~7U) : target;
            if (loading) {
                ports.vector_result = registers;
                if (rd == 0x0bU)
                    ports.flags |= nop_conflict;
            } else
                ports.vector_reads = registers;
        }
    }
    return ports;
}

bool RspPipeline::can_pair(const Ports& first, const Ports& second) {
    if (((first.flags ^ second.flags) & vector_alu) == 0U ||
        (first.vector_result & (second.vector_reads | second.vector_result)) != 0U ||
        (first.control_result & (second.control_reads | second.control_result)) != 0U)
        return false;

    // Reciprocal/move element fields and VNOP's unused destination participate
    // in the pairing circuit even when they do not name an input register.
    return (first.vector_result & second.field_reads) == 0U ||
           ((second.flags & vector_nop) != 0U && (first.flags & nop_conflict) == 0U);
}

void RspPipeline::reset() {
    *this = RspPipeline{};
}

void RspPipeline::redirect() {
    count_ = 0;
    branch_wait_ = false;
}

void RspPipeline::advance(Stage stage) {
    previous_[2] = previous_[1];
    previous_[1] = previous_[0];
    previous_[0] = stage;
}

bool RspPipeline::advance_branch_wait() {
    if (!branch_wait_)
        return false;
    branch_wait_ = false;
    advance({});
    return true;
}

void RspPipeline::fetch(u32 first, u32 second, bool single_step, u32 address) {
    if (count_ != 0U)
        return;
    const bool pairing_allowed = !single_step && !single_issue_;
    const unsigned decoded_index = (address >> 2U) & (decoded_.size() - 1U);
    auto& decoded = decoded_[decoded_index];
    // This is derived instruction metadata. Checking both words also covers IMEM
    // writes and DMA without adding another hardware invalidation mechanism.
    if (decoded.count != 0 && decoded.words[0] == first && decoded.words[1] == second &&
        decoded.pairing_allowed == pairing_allowed) {
        current_index_ = decoded_index;
        count_ = decoded.count;
        return;
    }
    Ports ports = decode(first);
    unsigned count = 1;
    if (pairing_allowed && (ports.flags & branch) == 0U) {
        const auto next = decode(second);
        if (can_pair(ports, next)) {
            count = 2;
            ports.scalar_reads |= next.scalar_reads;
            ports.scalar_result |= next.scalar_result;
            ports.vector_reads |= next.vector_reads;
            ports.vector_result |= next.vector_result;
            ports.flags |= next.flags;
        }
    }
    decoded = {{first, second}, ports, count, pairing_allowed};
    current_index_ = decoded_index;
    count_ = count;
}

bool RspPipeline::advance_operand_wait() {
    const auto& current = decoded_[current_index_].ports;
    const bool scalar_wait =
        (current.scalar_reads & (previous_[0].scalar_result | previous_[1].scalar_result)) != 0U;
    const bool vector_wait =
        (current.vector_reads &
         (previous_[0].vector_result | previous_[1].vector_result | previous_[2].vector_result)) != 0U;
    const bool store_wait = (current.flags & store) != 0U && previous_[1].load;
    if (!scalar_wait && !vector_wait && !store_wait)
        return false;
    advance({});
    return true;
}

void RspPipeline::retire(bool taken_delay_slot, u32 next_pc) {
    const auto& current = decoded_[current_index_].ports;
    advance({current.scalar_result, current.vector_result, (current.flags & load) != 0U});
    single_issue_ = (current.flags & branch) != 0U;
    count_ = 0;
    if (taken_delay_slot) {
        branch_wait_ = true;
        single_issue_ = single_issue_ || (next_pc & 4U) != 0U;
    }
}

} // namespace cupid
