#include "cupid/test_report.hpp"

#include <charconv>
#include <limits>

namespace cupid {
namespace {

bool parse_number(std::string_view text, u64& result) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    return error == std::errc{} && end == text.data() + text.size();
}

constexpr std::array<std::string_view, 4> flags{"base=", "timing=", "cycle=", "cp0-hazards="};
constexpr std::array<std::string_view, 5> names{
    "Base", "Timing", "Cycle", "CP0-hazards", "Poorly-understood-quirk",
};

} // namespace

void TestReport::append(std::string_view bytes) {
    for (const char character : bytes) {
        if (character == '\n') {
            consume_line();
            line_.clear();
        } else if (character != '\r') {
            if (line_.size() < 65536)
                line_ += character;
            else
                failed = true;
        }
    }
}

void TestReport::consume_line() {
    const std::string_view line = line_;
    if (line.find(" failed:") != std::string_view::npos ||
        line.find(" failed with ") != std::string_view::npos ||
        line.find("panicked") != std::string_view::npos || line.find("Panic") != std::string_view::npos)
        failed = true;

    if (line.starts_with("n64-systemtest ")) {
        if (summary_)
            failed = true;
        summary_ = true;
        for (std::size_t index = 0; index < flags.size(); ++index) {
            const auto position = line.find(flags[index]);
            if (position == std::string_view::npos || position + flags[index].size() >= line.size()) {
                failed = true;
                continue;
            }
            const auto value_index = position + flags[index].size();
            if (line[value_index] != '0' && line[value_index] != '1')
                failed = true;
            else
                enabled_[index] = line[value_index] == '1';
            if (value_index + 1 < line.size() && line[value_index + 1] != ' ' && line[value_index + 1] != ')')
                failed = true;
        }
    }

    const auto marker = line.find(": Failed ");
    if (summary_ && marker != std::string_view::npos) {
        const std::string_view rest = line.substr(marker + 9);
        const auto separator = rest.find(" of ");
        const auto suffix =
            separator == std::string_view::npos ? std::string_view::npos : rest.find(" tests", separator + 4);
        u64 count = 0;
        u64 failures = 0;
        if (separator == std::string_view::npos || suffix == std::string_view::npos ||
            !parse_number(rest.substr(0, separator), failures) ||
            !parse_number(rest.substr(separator + 4, suffix - separator - 4), count) || failures > count ||
            count == 0 || count > std::numeric_limits<u64>::max() - tests) {
            failed = true;
        } else {
            failed = failed || failures != 0;
            tests += count;
        }
        std::size_t category = names.size();
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (line.substr(0, marker).ends_with(names[index])) {
                category = index;
                break;
            }
        }
        if (category == names.size())
            failed = true;
        else {
            if (categories_[category])
                failed = true;
            categories_[category] = true;
        }
    }

    if (summary_ && line.starts_with("Slowest tests:")) {
        for (std::size_t index = 0; index < enabled_.size(); ++index) {
            if (enabled_[index] && !categories_[index])
                failed = true;
        }
        failed = failed || tests == 0;
        complete = true;
    }
    if (line.find("Done, but no tests were executed") != std::string_view::npos) {
        failed = complete = true;
    }
}

} // namespace cupid
