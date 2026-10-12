#include "desktop/windows/settings/transfer/document.hpp"
#include "desktop/storage/save.hpp"
#include <algorithm>
#include <stdexcept>
#include <windows.h>

namespace cupid::desktop::settings {
namespace {

wchar_t folded(wchar_t character) {
  return character >= L'A' && character <= L'Z' ? character + (L'a' - L'A') : character;
}

std::wstring_view trim(std::wstring_view text) {
  const auto first = text.find_first_not_of(L" \t\r");
  if (first == text.npos)
    return {};
  return text.substr(first, text.find_last_not_of(L" \t\r") - first + 1);
}

std::wstring decode(std::span<const std::uint8_t> data, bool exported) {
  std::wstring result;
  if (data.size() >= 2 && data[0] == 0xff && data[1] == 0xfe) {
    if (data.size() % 2)
      throw std::runtime_error("The settings file has an incomplete UTF-16 character.");
    for (std::size_t offset = 2; offset < data.size(); offset += 2)
      result.push_back(static_cast<wchar_t>(data[offset] | unsigned(data[offset + 1]) << 8));
  } else {
    const bool bom = data.size() >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf;
    if (bom)
      data = data.subspan(3);
    if (data.empty())
      return {};
    const auto codepage = exported || bom ? CP_UTF8 : CP_ACP;
    const auto flags = codepage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    const auto bytes = reinterpret_cast<const char *>(data.data());
    const auto count =
        MultiByteToWideChar(codepage, flags, bytes, static_cast<int>(data.size()), nullptr, 0);
    if (!count)
      throw std::runtime_error("The settings file has an invalid text encoding.");
    result.resize(static_cast<std::size_t>(count));
    if (MultiByteToWideChar(codepage, flags, bytes, static_cast<int>(data.size()), result.data(),
                            count) != count)
      throw std::runtime_error("The settings file has an invalid text encoding.");
  }
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto character = result[index];
    if (!character ||
        (character < 32 && character != L'\r' && character != L'\n' && character != L'\t'))
      throw std::runtime_error("The settings file contains an invalid text character.");
    if (character >= 0xd800 && character <= 0xdbff) {
      if (++index == result.size() || result[index] < 0xdc00 || result[index] > 0xdfff)
        throw std::runtime_error("The settings file contains an invalid Unicode character.");
    } else if (character >= 0xdc00 && character <= 0xdfff) {
      throw std::runtime_error("The settings file contains an invalid Unicode character.");
    }
  }
  return result;
}

} // namespace

bool NameLess::operator()(std::wstring_view first, std::wstring_view second) const {
  return std::lexicographical_compare(first.begin(), first.end(), second.begin(), second.end(),
                                      [](wchar_t a, wchar_t b) { return folded(a) < folded(b); });
}

Document read_document(const std::filesystem::path &path, bool exported) {
  const auto text = decode(read_file(path, maximum_size), exported);
  Document result;
  Section *section = nullptr;
  std::size_t offset = 0, count = 0;
  while (offset < text.size()) {
    const auto end = text.find(L'\n', offset);
    auto line =
        trim(std::wstring_view(text).substr(offset, end == text.npos ? text.npos : end - offset));
    offset = end == text.npos ? text.size() : end + 1;
    if (line.empty() || line.front() == L';' || line.front() == L'#')
      continue;
    if (line.size() > 65536)
      throw std::runtime_error("A settings file line is too long.");
    if (line.front() == L'[') {
      if (line.size() < 3 || line.back() != L']')
        throw std::runtime_error("The settings file has an invalid section heading.");
      const auto name = trim(line.substr(1, line.size() - 2));
      if (name.empty() || name.find_first_of(L"[]\t") != name.npos || result.size() >= 4096)
        throw std::runtime_error("The settings file has an invalid section heading.");
      const auto [found, inserted] = result.try_emplace(std::wstring(name));
      if (!inserted)
        throw std::runtime_error("The settings file repeats a section heading.");
      section = &found->second;
      continue;
    }
    const auto equals = line.find(L'=');
    if (!section || equals == line.npos)
      throw std::runtime_error("The settings file has an invalid setting.");
    const auto key = trim(line.substr(0, equals));
    auto value = trim(line.substr(equals + 1));
    if (value.size() >= 2 && (value.front() == L'\"' || value.front() == L'\'') &&
        value.back() == value.front())
      value = value.substr(1, value.size() - 2);
    if (key.empty() || key.find_first_of(L"[]\t") != key.npos || value.find(L'\t') != value.npos ||
        ++count > 65536 || !section->emplace(std::wstring(key), std::wstring(value)).second)
      throw std::runtime_error("The settings file has an invalid or repeated setting.");
  }
  if (exported) {
    const auto format = result.find(L"Settings export");
    if (format == result.end() || format->second.size() != 1 ||
        !format->second.contains(L"Version"))
      throw std::runtime_error("Select an exported Cupid-N64 settings file.");
    if (format->second.at(L"Version") != L"1")
      throw std::runtime_error("This settings export version is not supported.");
    result.erase(format);
  }
  return result;
}

std::vector<std::uint8_t> encode_document(const Document &document, bool exported) {
  std::wstring text = exported ? L"[Settings export]\r\nVersion=1\r\n\r\n" : L"";
  for (const auto &[section, entries] : document) {
    text += L"[" + section + L"]\r\n";
    for (const auto &[key, value] : entries) {
      const bool quoted = value.empty() || value.find_first_not_of(L"-0123456789") != value.npos;
      text += key + L"=" + (quoted ? L"\"" + value + L"\"" : value) + L"\r\n";
    }
    text += L"\r\n";
    if (text.size() > maximum_size / 2 - 1)
      throw std::runtime_error("The settings export is too large.");
  }
  if (exported) {
    const auto count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count || static_cast<std::size_t>(count) > maximum_size - 3)
      throw std::runtime_error("Could not encode the settings export.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count) + 3);
    bytes[0] = 0xef;
    bytes[1] = 0xbb;
    bytes[2] = 0xbf;
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            reinterpret_cast<char *>(bytes.data() + 3), count, nullptr, nullptr) != count)
      throw std::runtime_error("Could not encode the settings export.");
    return bytes;
  }
  std::vector<std::uint8_t> bytes{0xff, 0xfe};
  bytes.reserve(text.size() * 2 + 2);
  for (const auto character : text) {
    bytes.push_back(static_cast<std::uint8_t>(character));
    bytes.push_back(static_cast<std::uint8_t>(character >> 8));
  }
  return bytes;
}

} // namespace cupid::desktop::settings
