#include "desktop/video/frame_delivery.hpp"
#include "../../support/test.hpp"
#include <future>

using namespace cupid::desktop;

namespace {
template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::logic_error &) {
    return true;
  }
}
} // namespace

int main() {
  FrameDelivery<unsigned> delivery;
  test::equal(delivery.take().has_value(), false);
  delivery.metadata(17, 101);
  test::equal(delivery.take().has_value(), false);
  test::equal(rejects([&] { delivery.new_generation(); }), true);
  FrameDelivery<unsigned>::Presented first;
  first.image_valid = true;
  first.capture = cupid::n64::VideoFrame{1, 1, {11, 22, 33, 255}};
  first.completion.completed = std::chrono::steady_clock::now();
  const auto completed = first.completion.completed;
  delivery.presented(17, first, {{33, 22, 11, 255}, 1, 1});
  first.capture->rgba[0] = 0;
  auto output = delivery.take();
  test::equal(output.has_value(), true);
  test::equal(output->metadata, 101);
  test::equal(output->presentation.capture->rgba[0], 11);
  test::equal(output->presentation.completion.tag.field, 17);
  test::equal(output->presentation.completion.tag.sequence, 1);
  test::equal(output->presentation.completion.completed == completed, true);
  const auto generation = output->presentation.completion.tag.generation;
  FrameDelivery<unsigned> independent;
  independent.metadata(17, 101);
  independent.presented(17, {}, {});
  test::equal(independent.take()->presentation.completion.tag.generation != generation, true);
  auto image = delivery.take_image();
  test::equal(image->pixels == std::vector<std::uint8_t>{33, 22, 11, 255}, true);
  test::equal(delivery.take_image().has_value(), false);

  // A repeated field number remains a distinct transaction, even with graphics first.
  delivery.presented(17, {}, {{44, 55, 66, 255}, 1, 1});
  test::equal(delivery.take().has_value(), false);
  delivery.metadata(17, 102);
  output = delivery.take();
  test::equal(output->metadata, 102);
  test::equal(output->presentation.completion.tag.sequence, 2);
  delivery.new_generation();
  delivery.metadata(17, 103);
  delivery.presented(17, {}, {});
  output = delivery.take();
  test::equal(output->presentation.completion.tag.generation > generation, true);
  test::equal(output->presentation.completion.tag.sequence, 1);

  delivery.new_generation();
  std::promise<void> started;
  auto metadata = std::async(std::launch::async, [&] {
    started.set_value();
    for (unsigned field = 1; field <= 1000; ++field)
      delivery.metadata(field, field * 3);
  });
  started.get_future().wait();
  test::equal(metadata.wait_for(std::chrono::seconds(2)) == std::future_status::ready, true);
  metadata.get();
  auto graphics = std::async(std::launch::async, [&] {
    for (unsigned field = 1; field <= 1000; ++field) {
      FrameDelivery<unsigned>::Presented result;
      result.image_valid = true;
      result.completion.result.presented = true;
      if (field == 31)
        result.completion.failure = std::make_exception_ptr(std::runtime_error("Held result"));
      if (field % 100 == 0)
        result.capture = cupid::n64::VideoFrame{1, 1, {std::uint8_t(field), 2, 3, 255}};
      delivery.presented(field, std::move(result), {{std::uint8_t(field), 2, 3, 255}, 1, 1});
    }
  });
  test::equal(graphics.wait_for(std::chrono::seconds(2)) == std::future_status::ready, true);
  graphics.get();
  // No UI drain was needed by either producer; only the newest display image is retained.
  image = delivery.take_image();
  test::equal(image->pixels[0], std::uint8_t(1000));
  for (unsigned field = 1; field <= 1000; ++field) {
    output = delivery.take();
    test::equal(output.has_value(), true);
    test::equal(output->metadata, field * 3);
    test::equal(output->presentation.completion.tag.field, field);
    test::equal(output->presentation.completion.tag.sequence, field);
    test::equal(output->presentation.capture.has_value(), field % 100 == 0);
    test::equal(output->presentation.completion.failure != nullptr, field == 31);
  }
  test::equal(delivery.take().has_value(), false);
  delivery.new_generation();
  FrameDelivery<unsigned> mismatched;
  mismatched.metadata(7, 1);
  mismatched.presented(8, {}, {});
  test::equal(rejects([&] { (void)mismatched.take(); }), true);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
