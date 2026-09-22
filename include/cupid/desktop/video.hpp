#pragma once

#include "cupid/host/video.hpp"

#include <SDL3/SDL_rect.h>

#include <string>

struct SDL_Renderer;
struct SDL_Texture;

namespace cupid::desktop {

class VideoOutput {
  public:
    explicit VideoOutput(SDL_Renderer* renderer);
    ~VideoOutput();
    VideoOutput(const VideoOutput&) = delete;
    VideoOutput& operator=(const VideoOutput&) = delete;

    bool update(const host::DisplayFrame& frame, std::string& error);
    bool draw(const SDL_FRect& destination, std::string& error);
    bool set_smoothing(bool enabled, std::string& error);
    void clear();
    [[nodiscard]] bool has_frame() const {
        return texture_ != nullptr;
    }

  private:
    SDL_Renderer* renderer_;
    SDL_Texture* texture_{};
    unsigned width_{};
    unsigned height_{};
    bool smoothing_{true};
};

} // namespace cupid::desktop
