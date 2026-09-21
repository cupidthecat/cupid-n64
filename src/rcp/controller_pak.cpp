#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {
namespace {
constexpr std::size_t BankBytes = 32U * 1024U;
constexpr unsigned MaxBanks = 62;
} // namespace

bool Bus::configure_controller_pak(unsigned port, unsigned banks) {
    if (port >= controller_paks.size() || banks == 0 || banks > MaxBanks)
        return false;
    auto& storage = controller_paks[port];
    const std::size_t size = static_cast<std::size_t>(banks) * BankBytes;
    if (storage.size() == size)
        return true;
    storage.resize(size);
    controller_pak_bank_[port] = 0;
    if (controllers_[port].device == ControllerDevice::Gamepad &&
        controllers_[port].accessory == ControllerAccessory::ControllerPak)
        controller_pak_changed_[port] = true;
    return true;
}

u8 Bus::read_controller_pak(unsigned port, u16 address) const {
    if (address >= BankBytes)
        return 0;
    const auto& storage = controller_paks[port];
    const std::size_t offset = static_cast<std::size_t>(controller_pak_bank_[port]) * BankBytes + address;
    return offset < storage.size() ? storage[offset] : 0;
}

void Bus::write_controller_pak(unsigned port, u16 address, std::span<const u8> data) {
    auto& storage = controller_paks[port];
    if (address == 0x8000) {
        if (!data.empty() && data.front() < storage.size() / BankBytes)
            controller_pak_bank_[port] = data.front();
        return;
    }
    if (address >= BankBytes)
        return;
    const std::size_t offset = static_cast<std::size_t>(controller_pak_bank_[port]) * BankBytes + address;
    if (offset >= storage.size())
        return;
    const std::size_t length = std::min({data.size(), BankBytes - address, storage.size() - offset});
    std::copy_n(data.begin(), length, storage.begin() + static_cast<std::ptrdiff_t>(offset));
}

} // namespace cupid
