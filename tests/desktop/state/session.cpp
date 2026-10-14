#include "../../support/test.hpp"
#include "core/state/archive.hpp"
#include "desktop/state/rewind.hpp"
#include "desktop/state/slots.hpp"
#include "fixture.hpp"
#if defined(CUPID_SESSION_HARDWARE_TESTS)
#include "renderer/vulkan/renderer.hpp"
#endif
#include <future>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

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
  auto playback = audio.settings();
  playback.levels = {25, -50};
  audio.configure(playback);
  test::equal(session.capture_state() == initial, true);
  playback.levels = {100, 0};
  audio.configure(playback);
  test::equal(session.capture_state() == initial, true);
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

void pending_audio(const std::filesystem::path &directory) {
  using namespace cupid::desktop;
  Audio audio;
  Session session(directory / "game.z64", directory / "pif.bin", audio, {}, {},
                  cupid::n64::ArcadeProfile::Disabled, 0, false);
  const auto initial = session.capture_state();
  test::equal(initial[8], 1);
  // Version 1 places pending PCM after the empty frame and resampler state.
  constexpr unsigned offset = 313;
  test::equal(initial[offset] == 0 && initial[offset + 1] == 0 && initial[offset + 2] == 0 &&
                  initial[offset + 3] == 0,
              true);
  auto legacy = initial;
  legacy[offset] = 4;
  const std::array<std::uint8_t, 8> pcm{1, 0, 255, 255, 255, 127, 0, 128};
  legacy.insert(legacy.begin() + offset + 4, pcm.begin(), pcm.end());
  seal(legacy);
  session.restore_state(legacy);
  test::equal(session.capture_state() == legacy, true);
  audio.muted = true;
  audio.play_saved_output();
  test::equal(session.capture_state() == legacy, true);
  audio.muted = false;
  audio.suspended = true;
  audio.play_saved_output();
  test::equal(session.capture_state() == legacy, true);
  audio.suspended = false;
  audio.wait_interrupt([] { return true; });
  audio.play_saved_output();
  test::equal(session.capture_state() == legacy, true);
  audio.wait_interrupt({});
  test::equal(audio.available(), true);
  audio.play_saved_output();
  test::equal(session.capture_state() == initial, true);
  legacy[offset] = 3;
  seal(legacy);
  test::equal(rejects([&] { session.restore_state(legacy); }), true);
  test::equal(session.capture_state() == initial, true);
}

void rewind_gameplay(const std::filesystem::path &directory, bool hardware) {
  using namespace cupid::desktop;
  Audio audio;
  audio.muted = true;
  Session session(directory / "game.z64", directory / "pif.bin", audio, {}, {},
                  cupid::n64::ArcadeProfile::Disabled, 0, hardware);
  RewindHistory history([&] { return session.capture_state(); },
                        [&](std::span<const std::uint8_t> bytes) { session.restore_state(bytes); },
                        {1, 3, std::size_t(1024) * 1024 * 1024});
  session_test::advance(session, 2);
  history.record(session.frames);
  const auto state2 = session.capture_state();
  session_test::advance(session, 3);
  history.record(session.frames);
  const auto state3 = session.capture_state();
  session_test::advance(session, 4);
  history.record(session.frames);
  test::equal(history.rewind(session.frames), true);
  test::equal(session.capture_state() == state3, true);
  test::equal(history.rewind(session.frames), true);
  test::equal(session.capture_state() == state2, true);
  test::equal(history.rewind(session.frames), false);
  session_test::advance(session, 3);
  test::equal(session.capture_state() == state3, true);
  test::equal(history.record(session.frames), true);
  test::equal(history.size(), 2);
  test::equal(history.memory() <= std::size_t(1024) * 1024 * 1024, true);
}

void presentation_handoff(const std::filesystem::path &directory, bool hardware) {
  using namespace cupid::desktop;
  namespace n64 = cupid::n64;
  Audio audio;
  audio.muted = true;
  Session session(directory / "game.z64", directory / "pif.bin", audio, {}, {},
                  cupid::n64::ArcadeProfile::Disabled, 0, hardware);
  std::promise<void> entered, release;
  auto released = release.get_future().share();
  const auto caller = std::this_thread::get_id();
  std::mutex mutex;
  std::vector<unsigned> fields;
  std::vector<n64::VideoFrame> images;
  std::vector<std::thread::id> threads;
  bool hold = true;
  session.connect_present([&](unsigned field, const n64::VideoFrame &frame) {
    {
      std::lock_guard lock(mutex);
      fields.push_back(field);
      images.push_back(frame);
      threads.push_back(std::this_thread::get_id());
    }
    if (std::exchange(hold, false)) {
      entered.set_value();
      if (released.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("The held session presentation was not released.");
    }
  });
  auto first = std::async(std::launch::async, [&] { session_test::advance(session, 1); });
  test::equal(first.wait_for(std::chrono::seconds(5)) == std::future_status::ready, true);
  test::equal(entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
              true);
  first.get();
  session.finish_frame();
  test::equal(session.frames, 1);
  test::equal(session.frame.rgba.empty(), false);
  {
    std::lock_guard lock(mutex);
    test::equal(images[0].rgba == session.frame.rgba, true);
    test::equal(threads[0] != caller, true);
  }
  auto second = std::async(std::launch::async, [&] { session_test::advance(session, 2); });
  test::equal(second.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout, true);
  release.set_value();
  second.get();
  session.wait_presented();
  session.finish_frame();
  test::equal(fields == std::vector<unsigned>{1, 2}, true);
  test::equal(images.back().rgba == session.frame.rgba, true);
  test::equal(threads[0] == threads[1], true);
  const auto state = session.capture_state();
  session_test::advance(session, 3);
  session.wait_presented();
  session.restore_state(state);
  session_test::advance(session, 3);
  session.wait_presented();
  test::equal(fields == std::vector<unsigned>{1, 2, 3, 3}, true);
  session.reset();
  session_test::advance(session, 1);
  session.wait_presented();
  test::equal(fields == std::vector<unsigned>{1, 2, 3, 3, 1}, true);
  const auto received = fields.size();
  session.connect_present({});
  session_test::advance(session, 2);
  session.wait_presented();
  test::equal(fields.size(), received);
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
      rewind_gameplay(directory.path, hardware);
      presentation_handoff(directory.path, hardware);
      if (!hardware) {
        pending_audio(directory.path);
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
