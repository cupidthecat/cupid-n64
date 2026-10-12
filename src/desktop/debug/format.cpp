#include "desktop/debug/format.hpp"
#include <charconv>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace cupid::desktop {
namespace {

bool space(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

unsigned digit(char value) {
  if (value >= '0' && value <= '9')
    return unsigned(value - '0');
  if (value >= 'a' && value <= 'f')
    return unsigned(value - 'a') + 10;
  if (value >= 'A' && value <= 'F')
    return unsigned(value - 'A') + 10;
  throw std::runtime_error("Enter hexadecimal byte pairs, such as 12 34 AB CD.");
}

} // namespace

std::uint64_t inspection_address(std::string_view text) {
  while (!text.empty() && space(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && space(text.back()))
    text.remove_suffix(1);
  if (text.starts_with("0x") || text.starts_with("0X"))
    text.remove_prefix(2);
  if (text.empty())
    throw std::runtime_error("Enter a hexadecimal address within the selected region.");
  std::uint64_t address = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), address, 16);
  if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
    throw std::runtime_error("Enter a hexadecimal address within the selected region.");
  return address;
}

std::uint32_t inspection_offset(const n64::MemoryRegion &region, std::uint64_t address) {
  const auto start = region.address.value_or(0);
  if ((start >> 32) == 0xffffffff && address >= 0x80000000 && address <= 0xffffffff)
    address |= 0xffffffff00000000ull;
  if (address < start || address - start >= region.size)
    throw std::runtime_error("This address is outside the selected memory region.");
  return static_cast<std::uint32_t>(address - start);
}

std::uint64_t instruction_address(n64::DebugProcessor processor, std::string_view text) {
  auto address = inspection_address(text);
  while (!text.empty() && space(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && space(text.back()))
    text.remove_suffix(1);
  if (text.starts_with("0x") || text.starts_with("0X"))
    text.remove_prefix(2);
  if (processor == n64::DebugProcessor::Cpu && text.size() <= 8 && address >= 0x80000000 &&
      address <= 0xffffffff)
    address |= 0xffffffff00000000ull;
  return address;
}

std::vector<std::uint8_t> inspection_bytes(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  while (!text.empty()) {
    if (space(text.front())) {
      text.remove_prefix(1);
      continue;
    }
    if (text.size() < 2 || space(text[1]))
      throw std::runtime_error("Enter complete hexadecimal byte pairs.");
    bytes.push_back(static_cast<std::uint8_t>((digit(text[0]) << 4) | digit(text[1])));
    text.remove_prefix(2);
    if (bytes.size() > n64::Inspection::maximum_transfer)
      throw std::runtime_error("An edit can contain at most 4096 bytes.");
  }
  if (bytes.empty())
    throw std::runtime_error("Enter bytes to write.");
  return bytes;
}

std::string inspection_dump(std::uint64_t address, std::span<const std::uint8_t> bytes) {
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << std::hex << std::uppercase << std::setfill('0');
  for (unsigned offset = 0; offset < bytes.size(); offset += 16) {
    text << std::setw(address > 0xffffffff ? 16 : 8) << address + offset << "  ";
    for (unsigned n = 0; n < 16; ++n) {
      if (offset + n < bytes.size())
        text << std::setw(2) << unsigned(bytes[offset + n]) << ' ';
      else
        text << "   ";
    }
    text << " |";
    for (unsigned n = offset; n < bytes.size() && n < offset + 16; ++n)
      text << (bytes[n] >= 32 && bytes[n] <= 126 ? char(bytes[n]) : '.');
    text << "|\r\n";
  }
  return text.str();
}

std::string inspection_properties(std::span<const n64::DeviceProperty> properties) {
  std::string text;
  for (const auto &property : properties)
    text += property.name + " = " + property.value + "\r\n";
  return text;
}

} // namespace cupid::desktop
