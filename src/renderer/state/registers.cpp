#include "rdp_device.hpp"
#include "renderer/state/renderer_state.hpp"

namespace cupid::n64 {
namespace {

void scissor(state::Archive &a, RDP::ScissorState &s) {
  a.bounded(s.xlo, 0u, 0xfffu);
  a.bounded(s.ylo, 0u, 0xfffu);
  a.bounded(s.xhi, 0u, 0xfffu);
  a.bounded(s.yhi, 0u, 0xfffu);
}

void raster(state::Archive &a, RDP::StaticRasterizationState &s) {
  for (auto &c : s.combiner) {
    a.bounded(c.rgb.muladd, RDP::RGBMulAdd::Combined, static_cast<RDP::RGBMulAdd>(15));
    a.bounded(c.rgb.mulsub, RDP::RGBMulSub::Combined, static_cast<RDP::RGBMulSub>(15));
    a.bounded(c.rgb.mul, RDP::RGBMul::Combined, static_cast<RDP::RGBMul>(31));
    a.bounded(c.rgb.add, RDP::RGBAdd::Combined, RDP::RGBAdd::Zero);
    a.bounded(c.alpha.muladd, RDP::AlphaAddSub::CombinedAlpha, RDP::AlphaAddSub::Zero);
    a.bounded(c.alpha.mulsub, RDP::AlphaAddSub::CombinedAlpha, RDP::AlphaAddSub::Zero);
    a.bounded(c.alpha.mul, RDP::AlphaMul::LODFrac, RDP::AlphaMul::Zero);
    a.bounded(c.alpha.add, RDP::AlphaAddSub::CombinedAlpha, RDP::AlphaAddSub::Zero);
  }
  a.bounded(s.flags, 0u, 0x7fffffffu);
  a.bounded(s.dither, 0u, 15u);
  a.bounded(s.texture_size, 0u, 3u);
  a.bounded(s.texture_fmt, 0u, 7u);
}

void blend(state::Archive &a, RDP::DepthBlendState &s) {
  for (auto &c : s.blend_cycles) {
    a.bounded(c.blend_1a, RDP::BlendMode1A::PixelColor, RDP::BlendMode1A::FogColor);
    a.bounded(c.blend_1b, RDP::BlendMode1B::PixelAlpha, RDP::BlendMode1B::Zero);
    a.bounded(c.blend_2a, RDP::BlendMode2A::PixelColor, RDP::BlendMode2A::FogColor);
    a.bounded(c.blend_2b, RDP::BlendMode2B::InvPixelAlpha, RDP::BlendMode2B::Zero);
  }
  state::Archive::require((a.field(s.flags) & ~0x1fbu) == 0);
  a.bounded(s.coverage_mode, RDP::CoverageMode::Clamp, RDP::CoverageMode::Save);
  a.bounded(s.z_mode, RDP::ZMode::Opaque, RDP::ZMode::Decal);
}

} // namespace

void RendererState::registers(state::Archive &a, RDP::CommandProcessor &p) {
  scissor(a, p.scissor_state);
  raster(a, p.static_state);
  blend(a, p.depth_blend);
  a.bounded(p.texture_image.addr, 0u, 0xffffffu);
  a.bounded(p.texture_image.width, 0u, 1024u);
  a.bounded(p.texture_image.fmt, RDP::TextureFormat::RGBA, static_cast<RDP::TextureFormat>(7));
  a.bounded(p.texture_image.size, RDP::TextureSize::Bpp4, RDP::TextureSize::Bpp32);
  a.fields(p.quirks.u.options.native_resolution_tex_rect, p.quirks.u.options.native_texture_lod);
  auto &r = p.renderer;
  a.identity(r.caps.upscaling);
  a.bounded(r.fb.addr, 0u, 0xffffffu);
  a.bounded(r.fb.depth_addr, 0u, 0xffffffu);
  a.bounded(r.fb.width, 0u, 1024u);
  a.field(r.fb.deduced_height);
  a.bounded(r.fb.fmt, RDP::FBFormat::I4, RDP::FBFormat::RGBA8888);
  a.fields(r.fb.depth_write_pending, r.fb.color_write_pending);
  scissor(a, r.stream.scissor_state);
  raster(a, r.stream.static_raster_state);
  blend(a, r.stream.depth_blend_state);
  for (auto &tile : r.tiles) {
    a.bounded(tile.size.slo, 0u, 0xfffu);
    a.bounded(tile.size.shi, 0u, 0xfffu);
    a.bounded(tile.size.tlo, 0u, 0xfffu);
    a.bounded(tile.size.thi, 0u, 0xfffu);
    state::Archive::require((a.bounded(tile.meta.offset, 0u, 4088u) & 7) == 0);
    state::Archive::require((a.bounded(tile.meta.stride, 0u, 4088u) & 7) == 0);
    a.bounded(tile.meta.fmt, RDP::TextureFormat::RGBA, static_cast<RDP::TextureFormat>(7));
    a.bounded(tile.meta.size, RDP::TextureSize::Bpp4, RDP::TextureSize::Bpp32);
    a.bounded<std::uint8_t>(tile.meta.palette, 0, 15);
    a.bounded<std::uint8_t>(tile.meta.mask_s, 0, 10);
    a.bounded<std::uint8_t>(tile.meta.shift_s, 0, 15);
    a.bounded<std::uint8_t>(tile.meta.mask_t, 0, 10);
    a.bounded<std::uint8_t>(tile.meta.shift_t, 0, 15);
    a.bounded<std::uint8_t>(tile.meta.flags, 0, 15);
  }
  auto &c = r.constants;
  a.fields(c.blend_color, c.fog_color, c.env_color, c.primitive_color, c.fill_color, c.min_level,
           c.prim_lod_frac, c.prim_depth, c.prim_dz, c.use_prim_depth);
  a.span(std::span(c.convert));
  a.span(std::span(c.key_width));
  a.span(std::span(c.key_center));
  a.span(std::span(c.key_scale));
  a.field(r.base_primitive_index);
  auto &v = p.vi;
  a.span(std::span(v.vi_registers));
  const auto flags = a.bounded(v.per_line_state.flags, 0u, 3u);
  const auto line = a.bounded(v.per_line_state.line, 0u, RDP::VI_V_END_MAX - 1);
  const auto ended = a.field(v.per_line_state.ended);
  const auto count = ended ? RDP::VI_V_END_MAX : line + 1;
  if (flags & RDP::VideoInterface::PER_SCANLINE_HSTART_BIT) {
    a.field(v.per_line_state.h_start.latched_state);
    a.span(std::span(v.per_line_state.h_start.line_state).first(count));
  }
  if (flags & RDP::VideoInterface::PER_SCANLINE_XSCALE_BIT) {
    a.field(v.per_line_state.x_scale.latched_state);
    a.span(std::span(v.per_line_state.x_scale.line_state).first(count));
  }
  a.fields(v.previous_frame_blank, v.frame_count, v.last_valid_frame_count);
}

} // namespace cupid::n64
