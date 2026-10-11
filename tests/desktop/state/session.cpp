#include "../../support/test.hpp"
#include "core/state/archive.hpp"
#include "desktop/state/slots.hpp"
#include "fixture.hpp"
#if defined(CUPID_SESSION_HARDWARE_TESTS)
#include "renderer/vulkan/renderer.hpp"
#endif
#include <string_view>

namespace {

using cupid::desktop::Session;
using cupid::desktop::StateSlots;

StateSlots slots(const std::filesystem::path &image, Session &session) {
  return StateSlots(
      image, [&session] { return session.capture_state(); },
      [&session](std::span<const std::uint8_t> bytes) { session.restore_state(bytes); });
}

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

void seal(std::vector<std::uint8_t> &bytes) {
  const auto checksum =
      cupid::n64::state::Archive::fingerprint(std::span(bytes).first(bytes.size() - 8));
  for (unsigned byte = 0; byte < 8; ++byte)
    bytes[bytes.size() - 8 + byte] = static_cast<std::uint8_t>(checksum >> (byte * 8));
}

void continuation(const std::filesystem::path &directory, bool write, bool hardware) {
  using namespace cupid::desktop;
  const auto image = directory / "game.z64", firmware = directory / "pif.bin";
  Audio audio;
  audio.muted = true;
  auto session = std::make_unique<Session>(
      image, firmware, audio, std::filesystem::path{}, std::filesystem::path{},
      cupid::n64::ArcadeProfile::Disabled, write ? 0 : 913, hardware);
  auto states = slots(image, *session);
  if (write) {
    session_test::advance(*session, 2);
    states.save();
    write_save(directory / "pending.state", session->capture_state());
    session->finish_frame();
    write_save(directory / "displayed.state", session->capture_state());
    session_test::advance(*session, 5);
    session->finish_frame();
    write_save(directory / "expected.state", session->capture_state());
  } else {
    states.load();
    test::equal(session->frames, 2);
    const auto pending = read_file(directory / "pending.state", StateSlots::maximum_bytes);
    test::equal(session->capture_state() == pending, true);
    session->finish_frame();
    const auto displayed = read_file(directory / "displayed.state", StateSlots::maximum_bytes);
    test::equal(session->capture_state() == displayed, true);
    const auto &frame = session->frame;
    test::equal(frame.width, 640);
    const unsigned height = hardware ? 240 : 480;
    test::equal(frame.height, height);
    test::equal(frame.rgba.size(), 640 * height * 4);
    session_test::advance(*session, 5);
    session->finish_frame();
    const auto expected = read_file(directory / "expected.state", StateSlots::maximum_bytes);
    test::equal(session->capture_state() == expected, true);
  }
}

void invalid_and_undo(const std::filesystem::path &directory, bool hardware) {
  using namespace cupid::desktop;
  const auto image = directory / "game.z64", firmware = directory / "pif.bin";
  Audio audio;
  audio.muted = true;
  Session session(image, firmware, audio, {}, {}, cupid::n64::ArcadeProfile::Disabled, 0, hardware);
  auto states = slots(image, session);
  session_test::advance(session, 2);
  const auto initial = session.capture_state();
  states.save();
  session_test::advance(session, 3);
  const auto before = session.capture_state();
  states.load();
  test::equal(session.capture_state() == initial, true);
  session_test::advance(session, 4);
  states.undo_load();
  test::equal(session.capture_state() == before, true);
  auto corrupt = initial;
  corrupt[8] = 2;
  seal(corrupt);
  test::equal(rejects([&] { session.restore_state(corrupt); }), true);
  test::equal(session.capture_state() == before, true);
  corrupt = initial;
  corrupt[20] = 127;
  corrupt[corrupt.size() - 9] ^= 1;
  seal(corrupt);
  test::equal(rejects([&] { session.restore_state(corrupt); }), true);
  test::equal(session.capture_state() == before, true);
  corrupt = initial;
  corrupt.back() ^= 1;
  write_save(states.path(1), corrupt);
  test::equal(rejects([&] { states.load(); }), true);
  test::equal(session.capture_state() == before, true);
  corrupt = initial;
  corrupt.resize(corrupt.size() / 2);
  test::equal(rejects([&] { session.restore_state(corrupt); }), true);
  test::equal(session.capture_state() == before, true);
  auto incompatible = read_file(image, 4096);
  incompatible.back() = 1;
  write_save(directory / "other.z64", incompatible);
  Audio other_audio;
  other_audio.muted = true;
  Session other(directory / "other.z64", firmware, other_audio, {}, {},
                cupid::n64::ArcadeProfile::Disabled, 0, hardware);
  const auto other_initial = other.capture_state();
  test::equal(rejects([&] { other.restore_state(initial); }), true);
  test::equal(other.capture_state() == other_initial, true);
  if (hardware) {
    Audio software_audio;
    Session software(image, firmware, software_audio, {}, {}, cupid::n64::ArcadeProfile::Disabled,
                     0, false);
    const auto software_initial = software.capture_state();
    test::equal(rejects([&] { software.restore_state(initial); }), true);
    test::equal(software.capture_state() == software_initial, true);
  }
  write_save(states.path(1), initial);
  states.save();
  test::equal(read_file(states.path(1), StateSlots::maximum_bytes) == before, true);
  states.undo_save();
  test::equal(read_file(states.path(1), StateSlots::maximum_bytes) == initial, true);
  states.load();
  session.reset();
  states.undo_load();
  test::equal(session.capture_state() == before, true);
}

void stepping(const std::filesystem::path &directory, bool hardware) {
  using namespace cupid::desktop;
  Audio regular_audio, step_audio;
  regular_audio.muted = true;
  Session regular(directory / "game.z64", directory / "pif.bin", regular_audio, {}, {},
                  cupid::n64::ArcadeProfile::Disabled, 0, hardware);
  Session step(directory / "game.z64", directory / "pif.bin", step_audio, {}, {},
               cupid::n64::ArcadeProfile::Disabled, 0, hardware);
  session_test::advance(regular, 2);
  session_test::advance(step, 2);
  regular.finish_frame();
  step.finish_frame();
  for (unsigned frame = 3; frame <= 7; ++frame) {
    const bool muted = (frame & 1) != 0;
    step_audio.muted = muted;
    step.advance_frame(static_cast<std::uint16_t>((step.frames & 1) ? 0x8000 : 0), 22, -35);
    session_test::advance(regular, frame);
    regular.finish_frame();
    test::equal(step.frames, frame);
    test::equal(step.clocks(), regular.clocks());
    test::equal(step.frame.rgba == regular.frame.rgba, true);
    test::equal(step_audio.muted, muted);
  }
  session_test::advance(regular, 10);
  session_test::advance(step, 10);
  regular.finish_frame();
  step.finish_frame();
  test::equal(step.frames, regular.frames);
  test::equal(step.clocks(), regular.clocks());
  test::equal(step.frame.rgba == regular.frame.rgba, true);
}

void short_period_stepping(unsigned line_clocks) {
  using namespace cupid::desktop;
  session_test::Directory directory;
  session_test::media(directory.path, line_clocks);
  Audio audio;
  audio.muted = true;
  Session session(directory.path / "game.z64", directory.path / "pif.bin", audio, {}, {},
                  cupid::n64::ArcadeProfile::Disabled, 0, false);
  for (unsigned frame = 1; frame <= 12; ++frame) {
    session.advance_frame(0, 0, 0);
    test::equal(session.frames, frame);
    test::equal(!session.frame.rgba.empty(), true);
  }
  Audio resumed_audio;
  resumed_audio.muted = true;
  Session resumed(directory.path / "game.z64", directory.path / "pif.bin", resumed_audio, {}, {},
                  cupid::n64::ArcadeProfile::Disabled, 913, false);
  resumed.restore_state(session.capture_state());
  for (unsigned frame = 13; frame <= 16; ++frame) {
    session.advance_frame(0, 0, 0);
    resumed.advance_frame(0, 0, 0);
    test::equal(session.frames, frame);
    test::equal(resumed.capture_state() == session.capture_state(), true);
  }
  session.run(0, 0, 0);
  resumed.run(0, 0, 0);
  test::equal(session.frames > 17, true);
  test::equal(resumed.capture_state() == session.capture_state(), true);
}

} // namespace

int main(int argc, char **argv) {
  try {
    const bool hardware = argc > 1 && std::string_view(argv[argc - 1]) == "--hardware";
    if (hardware) {
#if defined(CUPID_SESSION_HARDWARE_TESTS)
      auto console =
          std::make_unique<cupid::n64::Console>(cupid::n64::ConsoleConfig{.random_seed = 0});
      try {
        cupid::n64::HardwareRenderer renderer(console->ram());
      } catch (const std::runtime_error &error) {
        std::cout << error.what() << '\n';
        return 77;
      }
#else
      throw std::runtime_error("Hardware rendering is not built.");
#endif
      --argc;
    }
    if (argc == 3) {
      session_test::Directory directory(argv[2]);
      const bool write = std::string_view(argv[1]) == "--write-state";
      if (!write && std::string_view(argv[1]) != "--read-state")
        throw std::runtime_error("Unknown session-state fixture mode.");
      if (write)
        session_test::media(directory.path);
      continuation(directory.path, write, hardware);
    } else if (argc == 1) {
      session_test::Directory directory;
      session_test::media(directory.path);
      continuation(directory.path, true, hardware);
      continuation(directory.path, false, hardware);
      invalid_and_undo(directory.path, hardware);
      stepping(directory.path, hardware);
      if (!hardware) {
        short_period_stepping(32);
        short_period_stepping(2);
      }
    } else {
      throw std::runtime_error("Invalid session-state fixture arguments.");
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
