#pragma once

#include <GL/gl.h>
#include <stdexcept>
#include <windows.h>

namespace cupid::desktop::opengl {

constexpr GLenum framebuffer = 0x8d40, read_framebuffer = 0x8ca8, draw_framebuffer = 0x8ca9;
constexpr GLenum color_attachment = 0x8ce0, framebuffer_complete = 0x8cd5;
constexpr GLenum bgra = 0x80e1, clamp_to_edge = 0x812f, rgba8 = 0x8058;

template <typename Function> Function load(const char *name) {
  const auto address = wglGetProcAddress(name);
  if (!address || address == reinterpret_cast<PROC>(1) || address == reinterpret_cast<PROC>(2) ||
      address == reinterpret_cast<PROC>(3) || address == reinterpret_cast<PROC>(-1))
    throw std::runtime_error("The OpenGL presentation driver lacks a required function.");
  return reinterpret_cast<Function>(address);
}

struct Functions {
  using CreateContext = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
  using SwapInterval = BOOL(WINAPI *)(int);
  using GenFramebuffers = void(APIENTRY *)(GLsizei, GLuint *);
  using DeleteFramebuffers = void(APIENTRY *)(GLsizei, const GLuint *);
  using BindFramebuffer = void(APIENTRY *)(GLenum, GLuint);
  using FramebufferTexture = void(APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
  using CheckFramebuffer = GLenum(APIENTRY *)(GLenum);
  using BlitFramebuffer = void(APIENTRY *)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                           GLbitfield, GLenum);
  using GenerateMipmap = void(APIENTRY *)(GLenum);

  GenFramebuffers gen_framebuffers = load<GenFramebuffers>("glGenFramebuffers");
  DeleteFramebuffers delete_framebuffers = load<DeleteFramebuffers>("glDeleteFramebuffers");
  BindFramebuffer bind_framebuffer = load<BindFramebuffer>("glBindFramebuffer");
  FramebufferTexture framebuffer_texture = load<FramebufferTexture>("glFramebufferTexture2D");
  CheckFramebuffer check_framebuffer = load<CheckFramebuffer>("glCheckFramebufferStatus");
  BlitFramebuffer blit_framebuffer = load<BlitFramebuffer>("glBlitFramebuffer");
  GenerateMipmap generate_mipmap = load<GenerateMipmap>("glGenerateMipmap");
};

class Context {
public:
  Context(HDC display, HGLRC context)
      : display_(wglGetCurrentDC()), context_(wglGetCurrentContext()) {
    if (!wglMakeCurrent(display, context))
      throw std::runtime_error("Could not acquire the OpenGL presentation context.");
  }
  ~Context() {
    wglMakeCurrent(display_, context_);
  }
  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;

private:
  HDC display_;
  HGLRC context_;
};

} // namespace cupid::desktop::opengl
