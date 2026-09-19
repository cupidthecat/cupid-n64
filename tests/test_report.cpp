#include "cupid/test_report.hpp"
#include "test.hpp"

#include <string>

namespace {

constexpr std::string_view header = "n64-systemtest 3.0.0 (base=1 timing=0 cycle=0 cp0-hazards=0)\n";
constexpr std::string_view success = "Finished in 12.00s. Base: Failed 0 of 1234 tests (100% success rate)\n";
constexpr std::string_view ending = "Slowest tests: arithmetic (1.50s), memory (0.90s)\n";

} // namespace

TEST(test_report_requires_a_complete_summary) {
    cupid::TestReport report;
    report.append("Running arithmetic...\n");
    CHECK(!report.complete);
    report.append(header);
    report.append(success);
    CHECK(!report.complete);
    report.append(ending);
    CHECK(report.complete);
    CHECK(!report.failed);
    CHECK_EQ(report.tests, 1234ULL);
}

TEST(test_report_handles_fragmented_serial_output_and_crlf) {
    cupid::TestReport report;
    std::string output(header);
    output += success;
    output += ending;
    for (const char character : output) {
        if (character == '\n')
            report.append("\r");
        report.append(std::string_view(&character, 1));
    }
    CHECK(report.complete);
    CHECK(!report.failed);
    CHECK_EQ(report.tests, 1234ULL);
}

TEST(test_report_rejects_failures_even_with_a_successful_aggregate) {
    cupid::TestReport report;
    report.append("An instruction failed: unexpected register\n");
    report.append(header);
    report.append(success);
    report.append(ending);
    CHECK(report.complete);
    CHECK(report.failed);
}

TEST(test_report_detects_aggregate_failures) {
    cupid::TestReport report;
    report.append(header);
    report.append("Finished in 1.0s. Base: Failed 1 of 1234 tests (99% success rate)\n");
    report.append(ending);
    CHECK(report.complete);
    CHECK(report.failed);
    CHECK_EQ(report.tests, 1234ULL);
}

TEST(test_report_requires_results_for_every_enabled_category) {
    cupid::TestReport report;
    report.append("n64-systemtest 3.0.0 (base=1 timing=1 cycle=1 cp0-hazards=1)\n");
    report.append(success);
    report.append("Timing: Failed 0 of 20 tests (100% success rate)\n");
    report.append("Cycle: Failed 0 of 10 tests (100% success rate)\n");
    report.append(ending);
    CHECK(report.complete);
    CHECK(report.failed);
    CHECK_EQ(report.tests, 1264ULL);
}

TEST(test_report_accepts_optional_categories_only_after_their_results) {
    cupid::TestReport report;
    report.append("n64-systemtest 3.0.0 (base=1 timing=1 cycle=1 cp0-hazards=1)\n");
    report.append(success);
    report.append("Timing: Failed 0 of 20 tests (100% success rate)\n");
    report.append("Cycle: Failed 0 of 10 tests (100% success rate)\n");
    report.append("CP0-hazards: Failed 0 of 4 tests (100% success rate)\n");
    report.append("Poorly-understood-quirk: Failed 0 of 3 tests (100% success rate)\n");
    report.append(ending);
    CHECK(report.complete);
    CHECK(!report.failed);
    CHECK_EQ(report.tests, 1271ULL);
}

TEST(test_report_empty_or_truncated_runs_cannot_pass) {
    cupid::TestReport empty;
    empty.append("Done, but no tests were executed\n");
    CHECK(empty.complete);
    CHECK(empty.failed);
    cupid::TestReport truncated;
    truncated.append(header);
    truncated.append(success);
    truncated.append("Slowest tests:");
    CHECK(!truncated.complete);
    cupid::TestReport missing;
    missing.append(header);
    missing.append(ending);
    CHECK(missing.complete);
    CHECK(missing.failed);
}

TEST(test_report_ignores_completion_text_before_the_header) {
    cupid::TestReport report;
    report.append(success);
    report.append(ending);
    CHECK(!report.complete);
    CHECK_EQ(report.tests, 0ULL);
}

TEST(test_report_rejects_duplicate_or_invalid_counts) {
    const std::string_view invalid[] = {
        "Base: Failed -1 of 10 tests\n",
        "Base: Failed 11 of 10 tests\n",
        "Base: Failed 0 of 0 tests\n",
        "Base: Failed 0 of abc tests\n",
        "Base: Failed 0 of 18446744073709551616 tests\n",
    };
    for (const auto line : invalid) {
        cupid::TestReport report;
        report.append(header);
        report.append(line);
        report.append(ending);
        CHECK(report.complete);
        CHECK(report.failed);
    }
    cupid::TestReport duplicate;
    duplicate.append(header);
    duplicate.append(success);
    duplicate.append(success);
    duplicate.append(ending);
    CHECK(duplicate.failed);
}

TEST(test_report_keeps_panics_and_output_overflow_as_failures) {
    cupid::TestReport report;
    report.append("Panic: unexpected exception\n");
    report.append(header);
    report.append(success);
    report.append(ending);
    CHECK(report.failed);
    cupid::TestReport long_line;
    long_line.append(std::string(65537, 'x'));
    long_line.append("\n");
    long_line.append(header);
    long_line.append(success);
    long_line.append(ending);
    CHECK(long_line.failed);
}
