#include "cupid/desktop/video.hpp"
#include "test.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <stdexcept>
#include <string>

using namespace cupid;
using namespace cupid::desktop;
using namespace cupid::host;

namespace {

[[noreturn]] void sdl_failure(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

class SoftwareTarget {
  public:
    SoftwareTarget(int width, int height) {
        surface_ = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
        if (!surface_)
            sdl_failure("SDL_CreateSurface");
        renderer_ = SDL_CreateSoftwareRenderer(surface_);
        if (!renderer_)
            sdl_failure("SDL_CreateSoftwareRenderer");
    }

    ~SoftwareTarget() {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroySurface(surface_);
    }

    SoftwareTarget(const SoftwareTarget&) = delete;
    SoftwareTarget& operator=(const SoftwareTarget&) = delete;

    [[nodiscard]] SDL_Renderer* renderer() const {
        return renderer_;
    }

    void clear(Uint8 red = 0, Uint8 green = 0, Uint8 blue = 0, Uint8 alpha = 255) {
        if (!SDL_SetRenderDrawColor(renderer_, red, green, blue, alpha) || !SDL_RenderClear(renderer_))
            sdl_failure("SDL_RenderClear");
    }

    void present() {
        if (!SDL_RenderPresent(renderer_))
            sdl_failure("SDL_RenderPresent");
    }

    [[nodiscard]] std::array<Uint8, 4> pixel(int x, int y) const {
        std::array<Uint8, 4> rgba{};
        if (!SDL_ReadSurfacePixel(surface_, x, y, &rgba[0], &rgba[1], &rgba[2], &rgba[3]))
            sdl_failure("SDL_ReadSurfacePixel");
        return rgba;
    }

  private:
    SDL_Surface* surface_{};
    SDL_Renderer* renderer_{};
};

void update(VideoOutput& output, const DisplayFrame& frame) {
    std::string error;
    if (!output.update(frame, error))
        throw std::runtime_error("VideoOutput::update: " + error);
}

void draw(VideoOutput& output, const SDL_FRect& destination) {
    std::string error;
    if (!output.draw(destination, error))
        throw std::runtime_error("VideoOutput::draw: " + error);
}

} // namespace

TEST(desktop_video_preserves_packed_rgba_channels_in_the_software_renderer) {
    SoftwareTarget target(2, 1);
    VideoOutput output(target.renderer());
    std::string error;
    CHECK(output.set_smoothing(false, error));
    update(output, DisplayFrame{2, 1, {0x123456ffU, 0xa1b2c3ffU}});

    target.clear();
    draw(output, SDL_FRect{0.0F, 0.0F, 2.0F, 1.0F});
    target.present();
    CHECK_EQ(target.pixel(0, 0), (std::array<Uint8, 4>{0x12, 0x34, 0x56, 0xff}));
    CHECK_EQ(target.pixel(1, 0), (std::array<Uint8, 4>{0xa1, 0xb2, 0xc3, 0xff}));
}

TEST(desktop_video_replaces_size_clears_blank_and_rejects_invalid_frames_atomically) {
    SoftwareTarget target(2, 1);
    VideoOutput output(target.renderer());
    std::string error;

    update(output, DisplayFrame{1, 1, {0x102030ffU}});
    CHECK(output.has_frame());
    target.clear();
    draw(output, SDL_FRect{0.0F, 0.0F, 1.0F, 1.0F});
    target.present();
    CHECK_EQ(target.pixel(0, 0), (std::array<Uint8, 4>{0x10, 0x20, 0x30, 0xff}));

    update(output, DisplayFrame{2, 1, {0x405060ffU, 0x708090ffU}});
    target.clear();
    draw(output, SDL_FRect{0.0F, 0.0F, 2.0F, 1.0F});
    target.present();
    CHECK_EQ(target.pixel(0, 0), (std::array<Uint8, 4>{0x40, 0x50, 0x60, 0xff}));
    CHECK_EQ(target.pixel(1, 0), (std::array<Uint8, 4>{0x70, 0x80, 0x90, 0xff}));

    CHECK(!output.update(DisplayFrame{2, 2, {1, 2, 3}}, error));
    CHECK(!error.empty());
    CHECK(output.has_frame());
    target.clear();
    draw(output, SDL_FRect{0.0F, 0.0F, 2.0F, 1.0F});
    target.present();
    CHECK_EQ(target.pixel(0, 0), (std::array<Uint8, 4>{0x40, 0x50, 0x60, 0xff}));
    CHECK_EQ(target.pixel(1, 0), (std::array<Uint8, 4>{0x70, 0x80, 0x90, 0xff}));

    CHECK(!output.update(DisplayFrame{max_video_field_width + 1U, 1, {}}, error));
    CHECK(output.has_frame());

    update(output, DisplayFrame{});
    CHECK(!output.has_frame());
    target.clear(7, 8, 9, 255);
    draw(output, SDL_FRect{0.0F, 0.0F, 2.0F, 1.0F});
    target.present();
    CHECK_EQ(target.pixel(0, 0), (std::array<Uint8, 4>{7, 8, 9, 255}));
    CHECK_EQ(target.pixel(1, 0), (std::array<Uint8, 4>{7, 8, 9, 255}));
}

TEST(desktop_video_applies_nearest_and_linear_smoothing_to_the_live_texture) {
    SoftwareTarget target(4, 1);
    VideoOutput output(target.renderer());
    std::string error;
    CHECK(output.set_smoothing(false, error));
    update(output, DisplayFrame{2, 1, {0x000000ffU, 0xffffffffU}});

    target.clear();
    draw(output, SDL_FRect{0.0F, 0.0F, 4.0F, 1.0F});
    target.present();
    const auto nearest_left = target.pixel(1, 0);
    const auto nearest_right = target.pixel(2, 0);
    CHECK_EQ(nearest_left, (std::array<Uint8, 4>{0, 0, 0, 255}));
    CHECK_EQ(nearest_right, (std::array<Uint8, 4>{255, 255, 255, 255}));

    CHECK(output.set_smoothing(true, error));
    target.clear();
    draw(output, SDL_FRect{0.0F, 0.0F, 4.0F, 1.0F});
    target.present();
    const auto linear_left = target.pixel(1, 0);
    const auto linear_right = target.pixel(2, 0);
    CHECK(linear_left[0] > 0 && linear_left[0] < 255);
    CHECK_EQ(linear_left[0], linear_left[1]);
    CHECK_EQ(linear_left[1], linear_left[2]);
    CHECK_EQ(linear_left[3], 255);
    CHECK(linear_right[0] > 0 && linear_right[0] < 255);
    CHECK_EQ(linear_right[0], linear_right[1]);
    CHECK_EQ(linear_right[1], linear_right[2]);
    CHECK_EQ(linear_right[3], 255);
    CHECK(linear_left != nearest_left);
    CHECK(linear_right != nearest_right);
}
