#include "cupid/system.hpp"

#include <algorithm>

namespace cupid {

void Cpu::synchronize() {
    if (!executing_step_)
        return;
    const u64 elapsed = instruction_cycles_ - synchronized_instruction_cycles_;
    synchronized_instruction_cycles_ = instruction_cycles_;
    if (elapsed != 0)
        update_clocks(elapsed);
}

u64 Cpu::next_buffered_write() const {
    return write_buffer_count_ == 0 ? 0 : write_buffer_[write_buffer_head_].remaining;
}

void Cpu::tick_write_buffer(u64 rcp_cycles) {
    while (write_buffer_count_ != 0 && rcp_cycles != 0) {
        auto& entry = write_buffer_[write_buffer_head_];
        const u64 elapsed = std::min(rcp_cycles, entry.remaining);
        entry.remaining -= elapsed;
        rcp_cycles -= elapsed;
        if (entry.remaining != 0)
            break;
        for (unsigned index = 0; index < entry.count; ++index) {
            const auto& transfer = entry.transfers[index];
            system_.bus.write(static_cast<u32>(transfer.address), transfer.width, transfer.value);
        }
        write_buffer_head_ = (write_buffer_head_ + 1) & 3U;
        --write_buffer_count_;
    }
}

void Cpu::buffer_write(u32 physical, unsigned width, u64 value) {
    const MemoryWrite transfer{physical, width, value};
    buffer_writes(std::span{&transfer, 1});
}

void Cpu::buffer_writes(std::span<const MemoryWrite> transfers) {
    synchronize();
    if (write_buffer_count_ == write_buffer_.size()) {
        add_cycles(system_.cpu_cycles_for_rcp(next_buffered_write()));
        synchronize();
    }
    // SysAD sends one address cycle followed by 32-bit data cycles at the Config.EP rate.
    const u64 data_spacing = ((cp0[16] >> 24) & 15U) == 6 ? 3 : 1;
    unsigned bytes = 0;
    for (const auto& transfer : transfers)
        bytes += transfer.width;
    // Five- through seven-byte stores require two separate address/data requests.
    const u64 transfer_cycles = bytes <= 4 ? 2 : bytes < 8 ? 4 : 2 + data_spacing;
    const unsigned tail = (write_buffer_head_ + write_buffer_count_) & 3U;
    auto& entry = write_buffer_[tail];
    std::copy(transfers.begin(), transfers.end(), entry.transfers.begin());
    entry.count = static_cast<unsigned>(transfers.size());
    entry.remaining = transfer_cycles;
    ++write_buffer_count_;
}

void Cpu::drain_write_buffer() {
    if (!executing_step_)
        return;
    synchronize();
    while (write_buffer_count_ != 0) {
        add_cycles(system_.cpu_cycles_for_rcp(next_buffered_write()));
        synchronize();
    }
}

} // namespace cupid
