#include "desktop/linux/video/presenter.hpp"
#include "renderer/video/settings.hpp"
#include <X11/Xlib.h>
#include <algorithm>
#include <epoxy/gl.h>
#include <epoxy/glx.h>
#include <stdexcept>
#include <thread>

namespace cupid::desktop::linux_host {
namespace {
void check() {
  if (glGetError() != GL_NO_ERROR)
    throw std::runtime_error("The OpenGL display operation failed.");
}

class Current {
public:
  Current(Display *display, GLXDrawable drawable, GLXContext context)
      : owned_(display), display_(glXGetCurrentDisplay()), context_(glXGetCurrentContext()),
        draw_(glXGetCurrentDrawable()), read_(glXGetCurrentReadDrawable()) {
    if (!glXMakeContextCurrent(display, drawable, drawable, context))
      throw std::runtime_error("Could not acquire the OpenGL display context.");
  }
  ~Current() {
    if (context_)
      glXMakeContextCurrent(display_, draw_, read_, context_);
    else
      glXMakeContextCurrent(owned_, 0, 0, nullptr);
  }
  Current(const Current &) = delete;
  Current &operator=(const Current &) = delete;

private:
  Display *owned_, *display_;
  GLXContext context_;
  GLXDrawable draw_, read_;
};
} // namespace

struct VideoPresenter::Implementation {
  const std::thread::id ui_thread = std::this_thread::get_id();
  Display *display = nullptr;
  ::Window window = 0;
  Colormap colormap = 0;
  GLXContext context = nullptr;
  GLuint texture = 0, framebuffer = 0;
  unsigned width = 1, height = 1, image_width = 0, image_height = 0;
  int screen = 0;
  VideoSurface surface;
  Viewport viewport;
  bool blocking = false;
  std::string error;

  ~Implementation() {
    release();
  }

  void ui() const {
    if (std::this_thread::get_id() != ui_thread)
      throw std::logic_error("The video surface belongs to its window thread.");
  }

  void release_graphics() {
    if (!context)
      return;
    {
      const Current current(display, window, context);
      if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
      if (texture)
        glDeleteTextures(1, &texture);
    }
    if (glXGetCurrentContext() == context)
      glXMakeContextCurrent(display, 0, 0, nullptr);
    glXDestroyContext(display, context);
    context = nullptr;
    framebuffer = texture = 0;
    image_width = image_height = 0;
  }

  void release() {
    release_graphics();
    if (window)
      XDestroyWindow(display, window);
    if (colormap)
      XFreeColormap(display, colormap);
    if (display)
      XCloseDisplay(display);
    display = nullptr;
    window = colormap = 0;
  }

  void initialize(std::uintptr_t parent) {
    if (!parent || !(display = XOpenDisplay(nullptr)))
      throw std::runtime_error("Could not open the display for OpenGL video.");
    XWindowAttributes parent_attributes{};
    if (!XGetWindowAttributes(display, static_cast<::Window>(parent), &parent_attributes))
      throw std::runtime_error("Could not locate the game display surface.");
    int major = 0, minor = 0;
    if (!glXQueryVersion(display, &major, &minor) || major < 1 || (major == 1 && minor < 3))
      throw std::runtime_error("The display does not support GLX 1.3.");
    screen = XScreenNumberOfScreen(parent_attributes.screen);
    const int attributes[]{GLX_DRAWABLE_TYPE,
                           GLX_WINDOW_BIT,
                           GLX_RENDER_TYPE,
                           GLX_RGBA_BIT,
                           GLX_DOUBLEBUFFER,
                           True,
                           GLX_RED_SIZE,
                           8,
                           GLX_GREEN_SIZE,
                           8,
                           GLX_BLUE_SIZE,
                           8,
                           0};
    int count = 0;
    auto configs = glXChooseFBConfig(display, screen, attributes, &count);
    if (!configs || !count) {
      if (configs)
        XFree(configs);
      throw std::runtime_error("Could not choose an OpenGL display format.");
    }
    XVisualInfo *visual = nullptr;
    GLXFBConfig config = nullptr;
    for (int index = 0; index < count && !visual; ++index) {
      visual = glXGetVisualFromFBConfig(display, configs[index]);
      if (visual)
        config = configs[index];
    }
    XFree(configs);
    if (!visual)
      throw std::runtime_error("Could not choose an OpenGL window visual.");
    colormap = XCreateColormap(display, RootWindow(display, screen), visual->visual, AllocNone);
    XSetWindowAttributes selected{};
    selected.colormap = colormap;
    selected.border_pixel = 0;
    window = XCreateWindow(display, static_cast<::Window>(parent), 0, 0, 1, 1, 0, visual->depth,
                           InputOutput, visual->visual, CWBorderPixel | CWColormap, &selected);
    XFree(visual);
    if (!window)
      throw std::runtime_error("Could not create the OpenGL display surface.");
    context = glXCreateNewContext(display, config, GLX_RGBA_TYPE, nullptr, True);
    if (!context)
      throw std::runtime_error("Could not create the OpenGL display context.");
    const Current current(display, window, context);
    if (epoxy_gl_version() < 30)
      throw std::runtime_error("The game display requires OpenGL 3.0.");
    glGenTextures(1, &texture);
    glGenFramebuffers(1, &framebuffer);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    swap_interval(false);
    check();
  }

  void swap_interval(bool selected) {
    if (epoxy_has_glx_extension(display, screen, "GLX_EXT_swap_control"))
      glXSwapIntervalEXT(display, window, selected ? 1 : 0);
    else if (epoxy_has_glx_extension(display, screen, "GLX_MESA_swap_control")) {
      if (glXSwapIntervalMESA(selected ? 1 : 0) != 0)
        throw std::runtime_error("Could not change the OpenGL display interval.");
    }
    blocking = selected;
  }

  void upload(std::span<const std::uint8_t> pixels, unsigned next_width, unsigned next_height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    if (image_width != next_width || image_height != next_height) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(next_width),
                   static_cast<GLsizei>(next_height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      image_width = next_width;
      image_height = next_height;
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Could not create the OpenGL image framebuffer.");
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(image_width),
                    static_cast<GLsizei>(image_height), GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    check();
  }

  void draw() {
    viewport =
        image_viewport(image_width, image_height, width, height, surface.scale, surface.aspect);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const auto bottom = static_cast<GLint>(height - viewport.y - viewport.height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBlitFramebuffer(
        0, static_cast<GLint>(image_height), static_cast<GLint>(image_width), 0,
        static_cast<GLint>(viewport.x), bottom, static_cast<GLint>(viewport.x + viewport.width),
        bottom + static_cast<GLint>(viewport.height), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    check();
  }
};

VideoPresenter::VideoPresenter() : implementation_(std::make_unique<Implementation>()) {}
VideoPresenter::~VideoPresenter() = default;

bool VideoPresenter::initialize(std::uintptr_t parent) {
  auto &state = *implementation_;
  state.ui();
  state.release();
  state.error.clear();
  try {
    state.initialize(parent);
    return true;
  } catch (const std::exception &error) {
    state.error = error.what();
    state.release();
    return false;
  }
}

void VideoPresenter::prepare(const VideoSurface &surface) {
  auto &state = *implementation_;
  state.ui();
  if (!state.window || !surface.width || !surface.height)
    return;
  state.width = surface.width;
  state.height = surface.height;
  state.surface = surface;
  XResizeWindow(state.display, state.window, surface.width, surface.height);
  XSync(state.display, False);
}

void VideoPresenter::show(bool visible) {
  auto &state = *implementation_;
  state.ui();
  if (!state.window)
    return;
  if (visible)
    XMapWindow(state.display, state.window);
  else
    XUnmapWindow(state.display, state.window);
  XFlush(state.display);
}

bool VideoPresenter::render(std::span<const std::uint8_t> pixels, unsigned width, unsigned height,
                            const VideoSurface &surface) {
  auto &state = *implementation_;
  if (!available())
    return false;
  try {
    if (!width || !height || !surface.width || !surface.height ||
        width > n64::maximum_frame_width || height > n64::maximum_frame_height ||
        pixels.size() != std::size_t(width) * height * 4)
      throw std::runtime_error("The video presentation image has invalid dimensions.");
    const Current current(state.display, state.window, state.context);
    if (state.blocking != surface.blocking)
      state.swap_interval(surface.blocking);
    state.upload(pixels, width, height);
    state.surface = surface;
    state.draw();
    glXSwapBuffers(state.display, state.window);
    return true;
  } catch (const std::exception &error) {
    state.error = error.what();
    return false;
  }
}

std::vector<std::uint8_t> VideoPresenter::capture() {
  auto &state = *implementation_;
  if (!available() || !state.image_width || !state.image_height)
    return {};
  const Current current(state.display, state.window, state.context);
  state.draw();
  glReadBuffer(GL_BACK);
  std::vector<std::uint8_t> pixels(std::size_t(state.width) * state.height * 4);
  glReadPixels(0, 0, static_cast<GLsizei>(state.width), static_cast<GLsizei>(state.height), GL_BGRA,
               GL_UNSIGNED_BYTE, pixels.data());
  check();
  const auto pitch = std::size_t(state.width) * 4;
  for (unsigned row = 0; row < state.height / 2; ++row)
    std::swap_ranges(pixels.begin() + row * pitch, pixels.begin() + (row + 1) * pitch,
                     pixels.begin() + (state.height - row - 1) * pitch);
  return pixels;
}

void VideoPresenter::release_graphics() {
  implementation_->release_graphics();
}

void VideoPresenter::close() {
  implementation_->ui();
  implementation_->release();
}

bool VideoPresenter::available() const {
  return implementation_->context && implementation_->error.empty();
}

const std::string &VideoPresenter::error() const {
  return implementation_->error;
}

} // namespace cupid::desktop::linux_host
