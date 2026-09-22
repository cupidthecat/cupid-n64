#include "host_fixture.hpp"

#include "cupid/host/session.hpp"

#include <chrono>
#include <thread>

using namespace cupid;
using namespace cupid::host;
using namespace std::chrono_literals;

namespace {

Options looping_options(test::host::TempDirectory& directory, bool write_save = false) {
    auto options = test::host::base_options(directory);
    std::vector<u8> firmware(1984);
    const std::vector<u32> words =
        write_save ? std::vector<u32>{0x3c08a800, 0x3c095a5a, 0x35295a5a, 0xad090000, 0x1000ffff, 0}
                   : std::vector<u32>{0x1000ffff, 0};
    for (std::size_t index = 0; index < words.size(); ++index)
        write_be32(firmware.data() + index * 4, words[index]);
    options.pif = directory.write("looping-pif.rom", firmware);
    options.ports[0].controller.connected = true;
    return options;
}

void completed(Session& session, u64 request) {
    CHECK(request != 0);
    CHECK(session.wait(request, 5s));
}

void executed(Session& session, u64 count) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (session.status().instructions < count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    CHECK(session.status().instructions >= count);
}

} // namespace

TEST(host_session_load_pause_resume_reset_and_stop_are_acknowledged) {
    test::host::TempDirectory directory;
    Session session;
    CHECK_EQ(session.status().state, SessionState::Empty);
    CHECK(!session.wait(0, 1ms));
    completed(session, session.load(looping_options(directory), true));
    CHECK(session.status().has_machine);
    CHECK_EQ(session.status().state, SessionState::Paused);
    CHECK_EQ(session.status().instructions, 0U);
    const auto loaded_epoch = session.status().output_epoch;
    const auto blank = session.take_output();
    CHECK(blank.video.has_value());
    CHECK(blank.video->pixels.empty());

    completed(session, session.pause(false));
    executed(session, 5000);
    completed(session, session.pause(true));
    const auto paused = session.status();
    CHECK_EQ(paused.state, SessionState::Paused);
    std::this_thread::sleep_for(3ms);
    CHECK_EQ(session.status().instructions, paused.instructions);
    CHECK(session.take_output().audio.empty());
    CHECK(paused.output_epoch > loaded_epoch);

    completed(session, session.reset());
    CHECK(session.status().output_epoch > paused.output_epoch);
    CHECK_EQ(session.status().state, SessionState::Paused);
    completed(session, session.pause(false));
    executed(session, paused.instructions + 5000);
    completed(session, session.stop());
    CHECK_EQ(session.status().state, SessionState::Empty);
    CHECK(!session.status().has_machine);
    CHECK(session.take_output().audio.empty());
}

TEST(host_session_failed_load_keeps_the_current_machine_paused) {
    test::host::TempDirectory directory;
    const auto options = looping_options(directory);
    Session session;
    completed(session, session.load(options, true));
    auto invalid = options;
    invalid.cartridge = directory.path() / "missing.z64";
    completed(session, session.load(invalid));
    const auto status = session.status();
    CHECK(status.has_machine);
    CHECK_EQ(status.state, SessionState::Paused);
    CHECK_EQ(status.cartridge, path_text(options.cartridge));
    CHECK(!status.error.empty());
    completed(session, session.pause(false));
    executed(session, 5000);
    CHECK(session.status().error.empty());
    completed(session, session.stop());
}

TEST(host_session_save_failure_keeps_the_machine_for_a_successful_retry) {
    test::host::TempDirectory directory;
    auto options = looping_options(directory);
    options.save = SaveType::Sram;
    options.save_file = directory.path() / "new-directory" / "session.sra";
    Session session;
    completed(session, session.load(options, true));
    completed(session, session.stop());
    CHECK(session.status().has_machine);
    CHECK_EQ(session.status().state, SessionState::Paused);
    CHECK(!session.status().error.empty());
    CHECK(!std::filesystem::exists(options.save_file));
    CHECK(std::filesystem::create_directory(options.save_file.parent_path()));
    completed(session, session.stop());
    CHECK(!session.status().has_machine);
    CHECK(session.status().error.empty());
    CHECK_EQ(std::filesystem::file_size(options.save_file), 32768U);
}

TEST(host_session_reload_reads_the_save_written_by_the_previous_machine) {
    test::host::TempDirectory directory;
    auto options = looping_options(directory, true);
    options.save = SaveType::Sram;
    options.save_file = directory.path() / "session.sra";
    Session session;
    completed(session, session.load(options));
    executed(session, 5000);
    completed(session, session.pause(true));
    completed(session, session.load(options, true));
    CHECK(session.status().error.empty());
    CHECK_EQ(session.status().instructions, 0U);
    completed(session, session.stop());
    const auto bytes = directory.read(options.save_file);
    CHECK_EQ(bytes.size(), 32768U);
    CHECK_EQ(bytes[0], 0x5aU);
    CHECK_EQ(bytes[1], 0x5aU);
    CHECK_EQ(bytes[2], 0x5aU);
    CHECK_EQ(bytes[3], 0x5aU);
}

TEST(host_session_repeated_controls_keep_monotonic_acknowledgements) {
    test::host::TempDirectory directory;
    Session session;
    completed(session, session.load(looping_options(directory), true));
    u64 last = session.status().completed_request;
    for (unsigned index = 0; index < 100; ++index) {
        const auto request = session.pause(true);
        if (request == 0) {
            completed(session, last);
            continue;
        }
        CHECK(request > last);
        last = request;
    }
    completed(session, last);
    CHECK_EQ(session.status().completed_request, last);
    CHECK_EQ(session.status().state, SessionState::Paused);
    completed(session, session.stop());
}
