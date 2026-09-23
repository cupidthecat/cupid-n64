#include "cupid/rsp/pipeline.hpp"

namespace cupid {

RspPipeline::DecodedWord RspPipeline::decode(u32 word) {
    DecodedWord decoded;
    auto& ports = decoded.ports;
    auto& operation = decoded.operation;
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
            operation = Operation::Sll;
            break;
        case 0x02:
            operation = Operation::Srl;
            break;
        case 0x03:
            operation = Operation::Sra;
            break;
        case 0x04:
            operation = Operation::Sllv;
            break;
        case 0x06:
            operation = Operation::Srlv;
            break;
        case 0x07:
            operation = Operation::Srav;
            break;
        case 0x08:
            operation = Operation::Jr;
            break;
        case 0x09:
            operation = Operation::Jalr;
            break;
        case 0x0d:
            operation = Operation::Break;
            break;
        case 0x20:
        case 0x21:
            operation = Operation::Addu;
            break;
        case 0x22:
        case 0x23:
            operation = Operation::Subu;
            break;
        case 0x24:
            operation = Operation::And;
            break;
        case 0x25:
            operation = Operation::Or;
            break;
        case 0x26:
            operation = Operation::Xor;
            break;
        case 0x27:
            operation = Operation::Nor;
            break;
        case 0x2a:
            operation = Operation::Slt;
            break;
        case 0x2b:
            operation = Operation::Sltu;
            break;
        default:
            operation = Operation::ReservedSpecial;
            break;
        }
    } else if (opcode == 1U) {
        switch (rt) {
        case 0x00:
            operation = Operation::Bltz;
            break;
        case 0x01:
            operation = Operation::Bgez;
            break;
        case 0x10:
            operation = Operation::Bltzal;
            break;
        case 0x11:
            operation = Operation::Bgezal;
            break;
        default:
            break;
        }
    } else {
        switch (opcode) {
        case 0x02:
            operation = Operation::J;
            break;
        case 0x03:
            operation = Operation::Jal;
            break;
        case 0x04:
            operation = Operation::Beq;
            break;
        case 0x05:
            operation = Operation::Bne;
            break;
        case 0x06:
            operation = Operation::Blez;
            break;
        case 0x07:
            operation = Operation::Bgtz;
            break;
        case 0x08:
        case 0x09:
            operation = Operation::Addiu;
            break;
        case 0x0a:
            operation = Operation::Slti;
            break;
        case 0x0b:
            operation = Operation::Sltiu;
            break;
        case 0x0c:
            operation = Operation::Andi;
            break;
        case 0x0d:
            operation = Operation::Ori;
            break;
        case 0x0e:
            operation = Operation::Xori;
            break;
        case 0x0f:
            operation = Operation::Lui;
            break;
        case 0x10:
            operation = Operation::Cop0;
            break;
        case 0x12:
            operation = Operation::Cop2;
            break;
        case 0x20:
            operation = Operation::Lb;
            break;
        case 0x21:
            operation = Operation::Lh;
            break;
        case 0x23:
        case 0x27:
            operation = Operation::Lw;
            break;
        case 0x24:
            operation = Operation::Lbu;
            break;
        case 0x25:
            operation = Operation::Lhu;
            break;
        case 0x28:
            operation = Operation::Sb;
            break;
        case 0x29:
            operation = Operation::Sh;
            break;
        case 0x2b:
            operation = Operation::Sw;
            break;
        case 0x32:
            operation = Operation::VectorLoad;
            break;
        case 0x3a:
            operation = Operation::VectorStore;
            break;
        default:
            break;
        }
    }

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
    return decoded;
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

bool RspPipeline::instruction_is_local(u32 word) {
    if ((word >> 26U) == 0x10U) {
        // DMA rows and device events bound these constant register reads. SP status
        // and semaphore retain synchronization; DP clock depends on elapsed time.
        const unsigned operation = (word >> 21U) & 31U;
        const unsigned index = (word >> 11U) & 15U;
        return operation == 0U && index != 4U && index != 7U && index != 12U;
    }
    return (word & 0xfc00003fU) != 0x0000000dU;
}

unsigned RspPipeline::cache_index(u32 address, bool pairing_allowed) {
    const unsigned word_index = static_cast<unsigned>((address >> 2U) & 1023U);
    return word_index * 2U + static_cast<unsigned>(pairing_allowed);
}

RspPipeline::DecodedFetch& RspPipeline::current_fetch() {
    return decoded_[current_index_];
}

const RspPipeline::DecodedFetch& RspPipeline::current_fetch() const {
    return decoded_[current_index_];
}

RspPipeline::DecodedFetch& RspPipeline::prepare(u32 first, u32 second, bool pairing_allowed, u32 address) {
    const unsigned decoded_index = cache_index(address, pairing_allowed);
    auto& cached = decoded_[decoded_index];
    if (cached.count != 0U && cached.words[0] == first && cached.words[1] == second) {
        return cached;
    }

    const DecodedWord first_decoded = decode(first);
    const bool first_local = instruction_is_local(first);
    const bool second_local = instruction_is_local(second);
    Ports ports = first_decoded.ports;
    std::array<Operation, 2> operations{};
    operations[0] = first_decoded.operation;
    u8 count = 1;
    bool issued_local = first_local;
    if (pairing_allowed && (ports.flags & branch) == 0U) {
        const DecodedWord second_decoded = decode(second);
        if (can_pair(ports, second_decoded.ports)) {
            count = 2;
            operations[1] = second_decoded.operation;
            issued_local = issued_local && second_local;
            ports.scalar_reads |= second_decoded.ports.scalar_reads;
            ports.scalar_result |= second_decoded.ports.scalar_result;
            ports.vector_reads |= second_decoded.ports.vector_reads;
            ports.vector_result |= second_decoded.ports.vector_result;
            ports.flags |= second_decoded.ports.flags;
        }
    }

    // Control and element-field dependencies have already decided pairing. Only
    // register dependencies and issue flags remain relevant to the live packet.
    cached = {
        {first, second}, {ports.scalar_reads, ports.scalar_result, ports.vector_reads, ports.vector_result},
        operations,      count,
        issued_local,    static_cast<u8>(ports.flags)};
    return cached;
}

RspPipeline::DecodedFetch& RspPipeline::prepare(std::span<const u8, 4096> imem, u64 revision,
                                                bool pairing_allowed, u32 address) {
    const unsigned decoded_index = cache_index(address, pairing_allowed);
    auto& cached = decoded_[decoded_index];
    if (cached.count != 0U && decoded_revision_[decoded_index] == revision)
        return cached;

    const auto read_word = [&](u32 word_address) {
        return read_be32(imem.data() + (word_address & 0x0ffcU));
    };
    auto& decoded = prepare(read_word(address), read_word(address + 4U), pairing_allowed, address);
    decoded_revision_[decoded_index] = revision;
    return decoded;
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

RspPipeline::LocalIssue RspPipeline::local_issue(u32 first, u32 second, u32 address) {
    if (count_ != 0U)
        return LocalIssue::Blocked;

    const bool pairing_allowed = !single_issue_;
    const unsigned decoded_index = cache_index(address, pairing_allowed);
    auto& decoded = prepare(first, second, pairing_allowed, address);
    if (!decoded.issued_local)
        return LocalIssue::Blocked;

    // Check the selected issue group before a branch bubble without latching
    // the prepared packet until its real fetch cycle.
    if (advance_branch_wait())
        return LocalIssue::Advanced;

    current_index_ = decoded_index;
    count_ = decoded.count;
    if (advance_operand_wait())
        return LocalIssue::Advanced;
    return LocalIssue::Ready;
}

RspPipeline::LocalIssue RspPipeline::local_issue(std::span<const u8, 4096> imem, LocalWindow& window,
                                                 u32 address) {
    if (count_ != 0U)
        return LocalIssue::Blocked;

    const bool pairing_allowed = !single_issue_;
    const unsigned decoded_index = cache_index(address, pairing_allowed);
    DecodedFetch* decoded = &decoded_[decoded_index];
    const unsigned word_index = static_cast<unsigned>((address >> 2U) & 1023U);
    if (!window.validated.test(word_index)) {
        const auto read_word = [&](u32 word_address) {
            return read_be32(imem.data() + (word_address & 0x0ffcU));
        };
        const u32 first = read_word(address);
        const u32 second = read_word(address + 4U);
        static_cast<void>(prepare(first, second, false, address));
        static_cast<void>(prepare(first, second, true, address));
        window.validated.set(word_index);
        decoded = &decoded_[decoded_index];
    }

    if (!decoded->issued_local)
        return LocalIssue::Blocked;

    // Check the selected issue group before consuming the target bubble. Once an
    // address is validated in this run_local() call, its two IMEM words cannot change.
    if (advance_branch_wait())
        return LocalIssue::Advanced;

    current_index_ = decoded_index;
    count_ = decoded->count;
    if (advance_operand_wait())
        return LocalIssue::Advanced;
    return LocalIssue::Ready;
}

RspPipeline::LocalIssue RspPipeline::local_issue(std::span<const u8, 4096> imem, u64 revision, u32 address) {
    if (count_ != 0U)
        return LocalIssue::Blocked;

    const bool pairing_allowed = !single_issue_;
    const unsigned decoded_index = cache_index(address, pairing_allowed);
    auto& decoded = prepare(imem, revision, pairing_allowed, address);
    if (!decoded.issued_local)
        return LocalIssue::Blocked;

    if (advance_branch_wait())
        return LocalIssue::Advanced;

    current_index_ = decoded_index;
    count_ = decoded.count;
    if (advance_operand_wait())
        return LocalIssue::Advanced;
    return LocalIssue::Ready;
}

RspPipeline::LocalIssue RspPipeline::local_issue() {
    if (count_ == 0U || !current_fetch().issued_local)
        return LocalIssue::Blocked;
    if (advance_branch_wait())
        return LocalIssue::Advanced;
    if (advance_operand_wait())
        return LocalIssue::Advanced;
    return LocalIssue::Ready;
}

void RspPipeline::fetch(u32 first, u32 second, bool single_step, u32 address) {
    if (count_ != 0U)
        return;
    const bool pairing_allowed = !single_step && !single_issue_;
    const unsigned decoded_index = cache_index(address, pairing_allowed);
    auto& decoded = prepare(first, second, pairing_allowed, address);
    current_index_ = decoded_index;
    count_ = decoded.count;
}

void RspPipeline::fetch(std::span<const u8, 4096> imem, u64 revision, bool single_step, u32 address) {
    if (count_ != 0U)
        return;
    const bool pairing_allowed = !single_step && !single_issue_;
    const unsigned decoded_index = cache_index(address, pairing_allowed);
    auto& decoded = prepare(imem, revision, pairing_allowed, address);
    current_index_ = decoded_index;
    count_ = decoded.count;
}

bool RspPipeline::advance_operand_wait() {
    const auto& fetched = current_fetch();
    const auto& current = fetched.ports;
    const bool scalar_wait =
        (current.scalar_reads & (previous_[0].scalar_result | previous_[1].scalar_result)) != 0U;
    const bool vector_wait =
        (current.vector_reads &
         (previous_[0].vector_result | previous_[1].vector_result | previous_[2].vector_result)) != 0U;
    const bool store_wait = (fetched.flags & store) != 0U && previous_[1].load;
    if (!scalar_wait && !vector_wait && !store_wait)
        return false;
    advance({});
    return true;
}

void RspPipeline::retire(bool taken_delay_slot, u32 next_pc) {
    const auto& fetched = current_fetch();
    const auto& current = fetched.ports;
    const u8 flags = fetched.flags;
    advance({current.scalar_result, current.vector_result, (flags & load) != 0U});
    single_issue_ = (flags & branch) != 0U;
    count_ = 0;
    if (taken_delay_slot) {
        branch_wait_ = true;
        single_issue_ = single_issue_ || (next_pc & 4U) != 0U;
    }
}

} // namespace cupid
