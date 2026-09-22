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
    bus.reset();
    rsp.reset();
    cpu.reset();
}

void System::set_reset_button(bool pressed) {
    bus.pif_boot.set_reset_button(pressed);
}

void System::advance(u64 cpu_cycles) {
    const u64 whole = cpu_cycles / 3;
    const u64 fraction = (cpu_cycles % 3) * 2 + rcp_fraction_;
    u64 rcp_cycles = whole * 2 + fraction / 3;
    rcp_fraction_ = fraction % 3;
    if (bus.take_schedule_change())
        event_valid_ = false;
    while (rcp_cycles != 0) {
        if (!event_valid_) {
            event_gap_ = bus.next_event();
            event_valid_ = true;
        }
        u64 elapsed = rsp.running() ? 1 : rcp_cycles;
        elapsed = std::min({elapsed, rsp.next_dma_event(), event_gap_});
        if (const u64 write_event = cpu.next_buffered_write(); write_event != 0)
            elapsed = std::min(elapsed, write_event);
        if (elapsed == 0)
            elapsed = 1;
        bus.tick_devices(elapsed);
        rsp.tick(elapsed);
        cpu.tick_write_buffer(elapsed);
        bus.dispatch_outputs();
        rcp_cycles -= elapsed;
        if (event_gap_ <= elapsed || bus.take_schedule_change())
            event_valid_ = false;
        else
            event_gap_ -= elapsed;
    }
}

u64 System::cpu_cycles_for_rcp(u64 rcp_cycles) const {
    return rcp::cpu_cycles_for_rcp(rcp_cycles, rcp_fraction_);
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
