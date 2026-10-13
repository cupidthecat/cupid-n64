#include "desktop/windows/video/presenter.hpp"
#include "desktop/windows/video/opengl/functions.hpp"
#include "renderer/video/settings.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop {
namespace {
LRESULT CALLBACK video_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCHITTEST)
    return HTTRANSPARENT;
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    BeginPaint(window, &paint);
    EndPaint(window, &paint);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void check() {
  if (glGetError() != GL_NO_ERROR)
    throw std::runtime_error("OpenGL video presentation failed.");
}
} // namespace

struct VideoPresenter::Implementation {
  HWND window = nullptr;
  HDC display = nullptr;
  HGLRC context = nullptr;
  std::unique_ptr<opengl::Functions> gl;
  opengl::Functions::SwapInterval swap_interval = nullptr;
  GLuint texture = 0, framebuffer = 0;
  unsigned width = 0, height = 0, image_width = 0, image_height = 0;
  Viewport viewport{};
  std::string error;
  bool attempted = false;
  bool blocking = false;

  ~Implementation() {
    if (context && wglMakeCurrent(display, context)) {
      if (framebuffer)
        gl->delete_framebuffers(1, &framebuffer);
      if (texture)
        glDeleteTextures(1, &texture);
      wglMakeCurrent(nullptr, nullptr);
    }
    if (context)
      wglDeleteContext(context);
    if (display)
      ReleaseDC(window, display);
    if (IsWindow(window))
      DestroyWindow(window);
  }

  void initialize(HWND parent, bool software_device) {
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"CupidVideoSurface";
    type.lpfnWndProc = video_message;
    type.style = CS_OWNDC;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      throw std::runtime_error("Could not register the video surface.");
    window = CreateWindowW(type.lpszClassName, L"", WS_CHILD, 0, 0, 1, 1, parent, nullptr,
                           type.hInstance, nullptr);
    if (!window || !(display = GetDC(window)))
      throw std::runtime_error("Could not create the video surface.");
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cAlphaBits = 8;
    int selected = ChoosePixelFormat(display, &format);
    if (software_device) {
      selected = 0;
      const auto count = DescribePixelFormat(display, 1, sizeof(format), &format);
      for (int index = 1; index <= count; ++index) {
        DescribePixelFormat(display, index, sizeof(format), &format);
        if ((format.dwFlags &
             (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_GENERIC_FORMAT)) ==
                (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_GENERIC_FORMAT) &&
            format.iPixelType == PFD_TYPE_RGBA && format.cColorBits >= 24) {
          selected = index;
          break;
        }
      }
    }
    if (!selected || !DescribePixelFormat(display, selected, sizeof(format), &format) ||
        !SetPixelFormat(display, selected, &format) || !(context = wglCreateContext(display)))
      throw std::runtime_error("Could not initialize OpenGL video presentation.");
    opengl::Context current(display, context);
    if (!software_device) {
      const int attributes[]{0x2091, 3, 0x2092, 2, 0x9126, 1, 0};
      const auto create =
          opengl::load<opengl::Functions::CreateContext>("wglCreateContextAttribsARB");
      const auto core = create(display, nullptr, attributes);
      if (!core)
        throw std::runtime_error("Could not create the OpenGL 3.2 presentation context.");
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(context);
      context = core;
      if (!wglMakeCurrent(display, context))
        throw std::runtime_error("Could not acquire the OpenGL presentation context.");
      gl = std::make_unique<opengl::Functions>();
      gl->gen_framebuffers(1, &framebuffer);
      const auto swap = wglGetProcAddress("wglSwapIntervalEXT");
      if (swap) {
        swap_interval = reinterpret_cast<opengl::Functions::SwapInterval>(swap);
        swap_interval(0);
      }
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl ? opengl::clamp_to_edge : GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl ? opengl::clamp_to_edge : GL_CLAMP);
    check();
  }

  void resize(unsigned next_width, unsigned next_height) {
    if (width == next_width && height == next_height)
      return;
    width = next_width;
    height = next_height;
    SetWindowPos(window, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  void upload(std::span<const std::uint8_t> pixels, unsigned next_width, unsigned next_height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (image_width != next_width || image_height != next_height) {
      glTexImage2D(GL_TEXTURE_2D, 0, gl ? opengl::rgba8 : GL_RGBA, static_cast<GLsizei>(next_width),
                   static_cast<GLsizei>(next_height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      image_width = next_width;
      image_height = next_height;
      if (gl) {
        gl->bind_framebuffer(opengl::framebuffer, framebuffer);
        gl->framebuffer_texture(opengl::framebuffer, opengl::color_attachment, GL_TEXTURE_2D,
                                texture, 0);
        if (gl->check_framebuffer(opengl::framebuffer) != opengl::framebuffer_complete)
          throw std::runtime_error("Could not create the OpenGL image framebuffer.");
        gl->bind_framebuffer(opengl::framebuffer, 0);
      }
    }
    if (gl) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(image_width),
                      static_cast<GLsizei>(image_height), opengl::bgra, GL_UNSIGNED_BYTE,
                      pixels.data());
      gl->generate_mipmap(GL_TEXTURE_2D);
    } else {
      std::vector<std::uint8_t> rgba(pixels.begin(), pixels.end());
      for (std::size_t offset = 0; offset < rgba.size(); offset += 4)
        std::swap(rgba[offset], rgba[offset + 2]);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(image_width),
                      static_cast<GLsizei>(image_height), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }
    check();
  }

  void draw() {
    if (gl)
      gl->bind_framebuffer(opengl::draw_framebuffer, 0);
    glDrawBuffer(GL_BACK);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const auto bottom = static_cast<GLint>(height - viewport.y - viewport.height);
    if (gl) {
      gl->bind_framebuffer(opengl::read_framebuffer, framebuffer);
      gl->blit_framebuffer(
          0, static_cast<GLint>(image_height), static_cast<GLint>(image_width), 0,
          static_cast<GLint>(viewport.x), bottom, static_cast<GLint>(viewport.x + viewport.width),
          bottom + static_cast<GLint>(viewport.height), GL_COLOR_BUFFER_BIT, GL_NEAREST);
      gl->bind_framebuffer(opengl::read_framebuffer, 0);
    } else {
      glViewport(static_cast<GLint>(viewport.x), bottom, static_cast<GLsizei>(viewport.width),
                 static_cast<GLsizei>(viewport.height));
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, texture);
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      glColor4f(1, 1, 1, 1);
      glBegin(GL_QUADS);
      glTexCoord2f(0, 1);
      glVertex2f(-1, -1);
      glTexCoord2f(1, 1);
      glVertex2f(1, -1);
      glTexCoord2f(1, 0);
      glVertex2f(1, 1);
      glTexCoord2f(0, 0);
      glVertex2f(-1, 1);
      glEnd();
      glDisable(GL_TEXTURE_2D);
    }
    check();
  }
};

VideoPresenter::VideoPresenter() : implementation_(std::make_unique<Implementation>()) {}
VideoPresenter::~VideoPresenter() = default;

bool VideoPresenter::initialize(HWND parent, bool software_device) {
  auto &state = *implementation_;
  if (state.attempted)
    return available();
  state.attempted = true;
  try {
    state.initialize(parent, software_device);
    return true;
  } catch (const std::exception &exception) {
    state.error = exception.what();
    if (IsWindow(state.window))
      ShowWindow(state.window, SW_HIDE);
    return false;
  }
}

bool VideoPresenter::present(std::span<const std::uint8_t> pixels, unsigned width, unsigned height,
                             unsigned viewport_width, unsigned viewport_height, DisplayScale scale,
                             DisplayAspect aspect, bool blocking) {
  if (!available())
    return false;
  auto &state = *implementation_;
  try {
    if (!width || !height || !viewport_width || !viewport_height ||
        width > n64::maximum_frame_width || height > n64::maximum_frame_height ||
        pixels.size() != std::size_t(width) * height * 4)
      throw std::runtime_error("The video presentation image has invalid dimensions.");
    opengl::Context current(state.display, state.context);
    if (state.swap_interval && state.blocking != blocking) {
      if (!state.swap_interval(blocking ? 1 : 0))
        throw std::runtime_error("Could not change the OpenGL swap interval.");
      state.blocking = blocking;
    }
    state.resize(viewport_width, viewport_height);
    state.upload(pixels, width, height);
    state.viewport = image_viewport(width, height, viewport_width, viewport_height, scale, aspect);
    state.draw();
    ShowWindow(state.window, SW_SHOWNOACTIVATE);
    if (!SwapBuffers(state.display))
      throw std::runtime_error("Could not swap the OpenGL video buffers.");
    return true;
  } catch (const std::exception &exception) {
    state.error = exception.what();
    ShowWindow(state.window, SW_HIDE);
    return false;
  }
}

std::vector<std::uint8_t> VideoPresenter::capture() {
  auto &state = *implementation_;
  if (!available() || !state.image_width || !state.image_height)
    return {};
  opengl::Context current(state.display, state.context);
  state.draw();
  glReadBuffer(GL_BACK);
  std::vector<std::uint8_t> pixels(std::size_t(state.width) * state.height * 4);
  glReadPixels(0, 0, static_cast<GLsizei>(state.width), static_cast<GLsizei>(state.height), GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  check();
  const auto pitch = std::size_t(state.width) * 4;
  for (unsigned row = 0; row < state.height / 2; ++row)
    std::swap_ranges(pixels.begin() + row * pitch, pixels.begin() + (row + 1) * pitch,
                     pixels.begin() + (state.height - row - 1) * pitch);
  for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
    std::swap(pixels[offset], pixels[offset + 2]);
  return pixels;
}

bool VideoPresenter::available() const {
  return implementation_->context && implementation_->error.empty();
}

const std::string &VideoPresenter::error() const {
  return implementation_->error;
}

void VideoPresenter::hide() {
  if (IsWindow(implementation_->window))
    ShowWindow(implementation_->window, SW_HIDE);
}

} // namespace cupid::desktop
