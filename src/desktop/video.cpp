#include "cupid/desktop/video.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_render.h>

namespace cupid::desktop {

VideoOutput::VideoOutput(SDL_Renderer* renderer) : renderer_(renderer) {}
VideoOutput::~VideoOutput() {
    clear();
}

void VideoOutput::clear() {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    width_ = height_ = 0;
}

bool VideoOutput::set_smoothing(bool enabled, std::string& error) {
    error.clear();
    if (texture_ &&
        !SDL_SetTextureScaleMode(texture_, enabled ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST)) {
        error = SDL_GetError();
        return false;
    }
    smoothing_ = enabled;
    return true;
}

bool VideoOutput::update(const host::DisplayFrame& frame, std::string& error) {
    error.clear();
    if (frame.width == 0 && frame.height == 0 && frame.pixels.empty()) {
        clear();
        return true;
    }
    if (frame.width == 0 || frame.height == 0 || frame.width > host::max_video_field_width ||
        frame.height > host::max_video_field_height * 2 ||
        frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height) {
        error = "Video frame dimensions do not match its pixel buffer.";
        return false;
    }
    SDL_Texture* target = texture_;
    const bool replacement = !target || width_ != frame.width || height_ != frame.height;
    if (replacement) {
        target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
                                   static_cast<int>(frame.width), static_cast<int>(frame.height));
        if (!target) {
            error = SDL_GetError();
            return false;
        }
    }
    if (!SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureScaleMode(target, smoothing_ ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST) ||
        !SDL_UpdateTexture(target, nullptr, frame.pixels.data(), static_cast<int>(frame.width * 4))) {
        error = SDL_GetError();
        if (replacement)
            SDL_DestroyTexture(target);
        return false;
    }
    if (replacement) {
        SDL_DestroyTexture(texture_);
        texture_ = target;
        width_ = frame.width;
        height_ = frame.height;
    }
    return true;
}

bool VideoOutput::draw(const SDL_FRect& destination, std::string& error) {
    error.clear();
    if (!texture_)
        return true;
    if (!SDL_RenderTexture(renderer_, texture_, nullptr, &destination)) {
        error = SDL_GetError();
        return false;
    }
    return true;
}

} // namespace cupid::desktop
