#include "../../support/test.hpp"
#include "desktop/windows/video/presenter.hpp"
#include <array>
#include <atomic>
#include <future>

using namespace cupid::desktop;

namespace {
struct Fixture {
  VideoPresenter video;
  std::unique_ptr<PresentationWorker> owner;
  VideoSurface surface{64, 48, DisplayScale::Stretch, DisplayAspect::Pixels, false};
  std::vector<PresentationWorker::Tag> delivered;
  std::exception_ptr error;
  unsigned paints = 0;
  bool closed = false;
};

LRESULT CALLBACK message(HWND window, UINT event, WPARAM wparam, LPARAM lparam) {
  auto fixture = reinterpret_cast<Fixture *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (event == WM_NCCREATE) {
    fixture = static_cast<Fixture *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fixture));
  }
  if (!fixture)
    return DefWindowProcW(window, event, wparam, lparam);
  try {
    if (event == WM_SIZE && fixture->owner) {
      fixture->owner->quiesce();
      if (wparam != SIZE_MINIMIZED && LOWORD(lparam) && HIWORD(lparam)) {
        fixture->surface.width = LOWORD(lparam);
        fixture->surface.height = HIWORD(lparam);
        fixture->video.prepare(fixture->surface);
      }
      return 0;
    }
    if (event == WM_PAINT) {
      PAINTSTRUCT paint{};
      BeginPaint(window, &paint);
      EndPaint(window, &paint);
      ++fixture->paints;
      return 0;
    }
    if (event == WM_APP + 2 && fixture->owner) {
      fixture->owner->poll();
      return 0;
    }
    if (event == WM_CLOSE && fixture->owner) {
      fixture->owner->quiesce();
      PresentationWorker::Submission release;
      release.operation = PresentationWorker::Submission::Operation::Release;
      fixture->owner->submit(std::move(release), [&](const auto &completion) {
        test::equal(completion.failure == nullptr, true);
      });
      fixture->owner->shutdown();
      fixture->video.close();
      fixture->closed = true;
      DestroyWindow(window);
      return 0;
    }
  } catch (...) {
    fixture->error = std::current_exception();
  }
  return DefWindowProcW(window, event, wparam, lparam);
}

void pump() {
  MSG event{};
  while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&event);
    DispatchMessageW(&event);
  }
}
} // namespace

int main(int argc, char **) {
  const bool software = argc == 1;
  const auto ui = std::this_thread::get_id();
  Fixture fixture;
  WNDCLASSW type{};
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpszClassName = L"CupidPresentationOwnerTest";
  type.lpfnWndProc = message;
  type.style = CS_OWNDC;
  test::equal(RegisterClassW(&type) != 0, true);
  const auto parent =
      CreateWindowW(type.lpszClassName, L"Presentation owner test", WS_OVERLAPPEDWINDOW, 0, 0, 128,
                    128, nullptr, nullptr, type.hInstance, &fixture);
  test::equal(parent != nullptr, true);
  if (!parent)
    return 1;
  const auto initialized = fixture.video.initialize(parent, software);
  if (!initialized && !software) {
    std::cerr << fixture.video.error() << '\n';
    DestroyWindow(parent);
    return 77;
  }
  test::equal(initialized, true);
  if (!fixture.video.available()) {
    std::cerr << fixture.video.error() << '\n';
    DestroyWindow(parent);
    return 1;
  }
  ShowWindow(parent, SW_SHOWNOACTIVATE);
  fixture.video.prepare(fixture.surface);
  fixture.video.show(true);
  pump();

  std::promise<void> entered, release;
  auto released = release.get_future().share();
  std::atomic<bool> held = true, backend_thread = true, restored = true;
  VideoSurface observed_surface;
  fixture.owner = std::make_unique<PresentationWorker>([&](const auto &submission) {
    backend_thread = backend_thread && std::this_thread::get_id() != ui;
    restored = restored && wglGetCurrentContext() == nullptr;
    if (held.exchange(false)) {
      entered.set_value();
      if (released.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
        throw std::runtime_error("The held graphics transaction was not released.");
    }
    if (submission.operation == PresentationWorker::Submission::Operation::Release) {
      fixture.video.release_graphics();
      return PresentationWorker::Result{};
    }
    if (submission.operation == PresentationWorker::Submission::Operation::Capture) {
      auto pixels = fixture.video.capture();
      restored = restored && wglGetCurrentContext() == nullptr && wglGetCurrentDC() == nullptr;
      return PresentationWorker::Result{false, fixture.video.available(), fixture.video.error(),
                                        std::move(pixels)};
    }
    observed_surface = submission.surface;
    const auto presented = fixture.video.render(submission.pixels, submission.width,
                                                submission.height, submission.surface);
    restored = restored && wglGetCurrentContext() == nullptr && wglGetCurrentDC() == nullptr;
    return PresentationWorker::Result{
        presented, fixture.video.available(), fixture.video.error(), {}};
  });
  fixture.owner->notify([&] { PostMessageW(parent, WM_APP + 2, 0, 0); });
  const std::array<std::uint8_t, 16> image{0,   0, 255, 255, 0,   255, 0,   255,
                                           255, 0, 0,   255, 255, 255, 255, 255};
  const auto request = [&](unsigned field) {
    PresentationWorker::Submission submission;
    submission.tag.field = field;
    submission.pixels.assign(image.begin(), image.end());
    submission.width = submission.height = 2;
    submission.surface = fixture.surface;
    return submission;
  };
  const auto delivered = [&](const auto &completion) {
    test::equal(std::this_thread::get_id() == ui, true);
    test::equal(completion.result.presented, true);
    test::equal(completion.failure == nullptr, true);
    fixture.delivered.push_back(completion.tag);
  };
  fixture.owner->submit(request(31), delivered);
  test::equal(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready,
              true);
  const auto paints = fixture.paints;
  InvalidateRect(parent, nullptr, FALSE);
  UpdateWindow(parent);
  test::equal(fixture.paints > paints, true);
  test::equal(fixture.owner->pending(), true);
  {
    std::jthread releaser([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      release.set_value();
    });
    SetWindowPos(parent, nullptr, 0, 0, 160, 140, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  test::equal(fixture.error == nullptr, true);
  test::equal(fixture.delivered.size(), 1);
  test::equal(fixture.delivered[0].field, 31);
  test::equal(observed_surface.width, 64);
  test::equal(observed_surface.height, 48);
  test::equal(fixture.surface.width != 64, true);
  fixture.owner->submit(request(32), delivered);
  ShowWindow(parent, SW_MINIMIZE);
  test::equal(IsIconic(parent) != FALSE, true);
  test::equal(fixture.delivered.size(), 2);
  test::equal(fixture.error == nullptr, true);
  PresentationWorker::Submission capture;
  capture.operation = PresentationWorker::Submission::Operation::Capture;
  std::vector<std::uint8_t> pixels;
  fixture.owner->submit(std::move(capture),
                        [&](const auto &completion) { pixels = completion.result.captured; });
  fixture.owner->quiesce();
  test::equal(pixels.size(), std::size_t(fixture.surface.width) * fixture.surface.height * 4);
  if (!pixels.empty()) {
    test::equal(pixels[0], 0);
    test::equal(pixels[1], 0);
    test::equal(pixels[2], 255);
    const auto corner = (std::size_t(fixture.surface.height - 1) * fixture.surface.width +
                         fixture.surface.width - 1) *
                        4;
    test::equal(pixels[corner], 255);
    test::equal(pixels[corner + 1], 255);
    test::equal(pixels[corner + 2], 255);
  }
  ShowWindow(parent, SW_RESTORE);
  pump();
  fixture.owner->new_generation();
  entered = {};
  release = {};
  released = release.get_future().share();
  held = true;
  fixture.owner->submit(request(31), delivered);
  test::equal(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready,
              true);
  {
    std::jthread releaser([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      release.set_value();
    });
    SendMessageW(parent, WM_CLOSE, 0, 0);
  }
  test::equal(fixture.closed, true);
  test::equal(IsWindow(parent) != FALSE, false);
  test::equal(fixture.error == nullptr, true);
  test::equal(fixture.delivered.size(), 3);
  test::equal(fixture.delivered[2].field, 31);
  test::equal(fixture.delivered[2].generation > fixture.delivered[0].generation, true);
  test::equal(backend_thread, true);
  test::equal(restored, true);
  test::equal(fixture.video.available(), false);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
