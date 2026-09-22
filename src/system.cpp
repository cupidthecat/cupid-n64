#include "cupid/system.hpp"

#include "cupid/rcp/clocks.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <vector>

namespace cupid {
namespace {

bool read_file(const std::filesystem::path& path, std::vector<u8>& bytes, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "Cannot open " + path.string();
        return false;
    }
    const auto length = input.tellg();
    if (length < 0 || static_cast<u64>(length) > std::numeric_limits<u32>::max()) {
        error = "Invalid file length: " + path.string();
        return false;
    }
    std::vector<u8> loaded(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!loaded.empty() && !input.read(reinterpret_cast<char*>(loaded.data()), length)) {
        error = "Cannot read " + path.string();
        return false;
    }
    bytes = std::move(loaded);
    return true;
}

} // namespace

System::System(VideoStandard video_standard)
    : video_standard_(video_standard), cpu(*this), rsp(*this), bus(*this) {
    reset();
}

void System::reset() {
    rcp_fraction_ = 0;
    event_gap_ = 0;
    event_valid_ = false;
    deferred_rcp_ = 0;
    peripheral_debt_ = 0;
    defer_limit_ = 0;
    settling_ = false;
    bus.reset();
    rsp.reset();
    cpu.reset();
}

void System::set_reset_button(bool pressed) {
    settle();
    bus.pif_boot.set_reset_button(pressed);
}

void System::advance(u64 cpu_cycles) {
    advance_deferred(cpu_cycles);
    settle();
}

// The CPU calls this once per instruction. Nothing observable happens on the devices
// before the next scheduled edge, so their clocks are only moved when an edge comes
// due, when a buffered write or output is waiting, or when the RSP needs to run.
void System::advance_deferred(u64 cpu_cycles) {
    const u64 whole = cpu_cycles / 3;
    const u64 fraction = (cpu_cycles % 3) * 2 + rcp_fraction_;
    deferred_rcp_ += whole * 2 + fraction / 3;
    rcp_fraction_ = fraction % 3;
    if (deferred_rcp_ < defer_limit_ && !bus.schedule_dirty_ && !rsp.running() &&
        bus.pending_outputs_.empty() && cpu.next_buffered_write() == 0)
        return;
    settle_deferred();
}

void System::settle_deferred() {
    settling_ = true;
    u64 rcp_cycles = deferred_rcp_;
    deferred_rcp_ = 0;
    if (bus.take_schedule_change())
        event_valid_ = false;
    while (rcp_cycles != 0) {
        if (!event_valid_) {
            settle_peripherals();
            event_gap_ = bus.next_event();
            event_valid_ = true;
        }
        u64 elapsed = std::min(rcp_cycles, rsp.next_dma_event());
        elapsed = std::min(elapsed, event_gap_);
        const u64 write_event = cpu.next_buffered_write();
        if (write_event != 0)
            elapsed = std::min(elapsed, write_event);
        if (elapsed == 0)
            elapsed = 1;
        if (rsp.running()) {
            // The RSP runs one cycle at a time against the RDRAM and RDP clocks. It
            // cannot observe the peripherals between their edges, so their time is
            // owed until an edge fires or a buffered CPU write reaches them.
            for (u64 cycle = 1; cycle < elapsed; ++cycle) {
                bus.tick_clocks(1);
                rsp.tick(1);
            }
            bus.tick_clocks(1);
            peripheral_debt_ += elapsed;
            if (event_gap_ <= elapsed || write_event == elapsed)
                settle_peripherals();
            rsp.tick(1);
        } else {
            bus.tick_clocks(elapsed);
            bus.tick_peripherals(peripheral_debt_ + elapsed);
            peripheral_debt_ = 0;
            rsp.tick(elapsed);
        }
        cpu.tick_write_buffer(elapsed);
        bus.dispatch_outputs();
        rcp_cycles -= elapsed;
        if (event_gap_ <= elapsed || bus.take_schedule_change())
            event_valid_ = false;
        else
            event_gap_ -= elapsed;
    }
    // VI lines count even without refresh or a presentation callback, so they bound
    // the deferral alongside the cached schedule and the RSP's DMA rows. A running
    // RSP never defers, so its limit is only worked out once it halts.
    if (rsp.running()) {
        defer_limit_ = 0;
    } else {
        settle_peripherals();
        if (!event_valid_) {
            event_gap_ = bus.next_event();
            event_valid_ = true;
        }
        defer_limit_ = std::min(std::min(event_gap_, bus.next_vi_line()), rsp.next_dma_event());
    }
    settling_ = false;
}

void System::settle_peripherals() {
    if (peripheral_debt_ == 0)
        return;
    bus.tick_peripherals(peripheral_debt_);
    peripheral_debt_ = 0;
}

u64 System::cpu_cycles_for_rcp(u64 rcp_cycles) const {
    return rcp::cpu_cycles_for_rcp(rcp_cycles, rcp_fraction_);
}

void System::advance_after_local_rsp(u64 cpu_cycles) {
    const u64 whole = cpu_cycles / 3;
    const u64 fraction = (cpu_cycles % 3) * 2 + rcp_fraction_;
    const u64 rcp_cycles = whole * 2 + fraction / 3;
    rcp_fraction_ = fraction % 3;

    // idle_loop_event_cycles() settled the machine before the RSP ran ahead. The
    // RSP work was local to IMEM/DMEM/register state and stopped before every
    // shared event, so clocks can catch up in one span without running the RSP twice.
    settling_ = true;
    bus.tick_clocks(rcp_cycles);
    bus.tick_peripherals(peripheral_debt_ + rcp_cycles);
    peripheral_debt_ = 0;
    cpu.tick_write_buffer(rcp_cycles);
    bus.dispatch_outputs();

    if (event_valid_) {
        if (event_gap_ <= rcp_cycles)
            event_valid_ = false;
        else
            event_gap_ -= rcp_cycles;
    }
    if (bus.take_schedule_change())
        event_valid_ = false;
    defer_limit_ =
        rsp.running() ? 0 : std::min(std::min(bus.next_event(), bus.next_vi_line()), rsp.next_dma_event());
    settling_ = false;
}

bool System::load_rom(const std::filesystem::path& path, std::string& error) {
    std::vector<u8> bytes;
    if (!read_file(path, bytes, error))
        return false;
    return bus.load_rom(std::move(bytes), error);
}

bool System::load_pif(const std::filesystem::path& path, std::string& error) {
    std::vector<u8> bytes;
    if (!read_file(path, bytes, error))
        return false;
    return load_pif(bytes, error);
}

bool System::load_pif(std::span<const u8> bytes, std::string& error) {
    if (bytes.size() != 0x7c0 && bytes.size() != 0x800) {
        error = "The PIF boot ROM must contain 1984 or 2048 bytes.";
        return false;
    }
    std::copy_n(bytes.begin(), 0x7c0, bus.pif.begin());
    pif_loaded_ = true;
    return true;
}

bool System::boot_cartridge(std::string& error) {
    if (bus.rom.size() < 0x1000) {
        error = "The cartridge must contain a header and bootcode.";
        return false;
    }
    if (!pif_loaded_) {
        error = "A PIF boot ROM is required. Supply its path with --pif.";
        return false;
    }
    cpu.set_pc(0xffffffffbfc00000ULL);
    return true;
}

} // namespace cupid
