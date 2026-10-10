#include "rdp_device.hpp"
#include "renderer/state/renderer_state.hpp"

namespace cupid::n64 {
namespace {

void scissor(state::Archive &a, RDP::ScissorState &s, std::string_view name) {
  a.member_label(name, "xlo", sizeof(s.xlo));
  a.bounded(s.xlo, 0u, 0xfffu);
  a.member_label(name, "ylo", sizeof(s.ylo));
  a.bounded(s.ylo, 0u, 0xfffu);
  a.member_label(name, "xhi", sizeof(s.xhi));
  a.bounded(s.xhi, 0u, 0xfffu);
  a.member_label(name, "yhi", sizeof(s.yhi));
  a.bounded(s.yhi, 0u, 0xfffu);
}

void raster(state::Archive &a, RDP::StaticRasterizationState &s, std::string_view name) {
  for (unsigned cycle = 0; cycle < std::size(s.combiner); ++cycle) {
    auto &c = s.combiner[cycle];
    a.indexed_label(name, cycle, "rgb.muladd", sizeof(c.rgb.muladd));
    a.bounded(c.rgb.muladd, RDP::RGBMulAdd::Combined, static_cast<RDP::RGBMulAdd>(15));
    a.indexed_label(name, cycle, "rgb.mulsub", sizeof(c.rgb.mulsub));
    a.bounded(c.rgb.mulsub, RDP::RGBMulSub::Combined, static_cast<RDP::RGBMulSub>(15));
    a.indexed_label(name, cycle, "rgb.mul", sizeof(c.rgb.mul));
    a.bounded(c.rgb.mul, RDP::RGBMul::Combined, static_cast<RDP::RGBMul>(31));
    a.indexed_label(name, cycle, "rgb.add", sizeof(c.rgb.add));
    a.bounded(c.rgb.add, RDP::RGBAdd::Combined, RDP::RGBAdd::Zero);
    a.indexed_label(name, cycle, "alpha.muladd", sizeof(c.alpha.muladd));
    a.bounded(c.alpha.muladd, RDP::AlphaAddSub::CombinedAlpha, RDP::AlphaAddSub::Zero);
    a.indexed_label(name, cycle, "alpha.mulsub", sizeof(c.alpha.mulsub));
    a.bounded(c.alpha.mulsub, RDP::AlphaAddSub::CombinedAlpha, RDP::AlphaAddSub::Zero);
    a.indexed_label(name, cycle, "alpha.mul", sizeof(c.alpha.mul));
    a.bounded(c.alpha.mul, RDP::AlphaMul::LODFrac, RDP::AlphaMul::Zero);
    a.indexed_label(name, cycle, "alpha.add", sizeof(c.alpha.add));
    a.bounded(c.alpha.add, RDP::AlphaAddSub::CombinedAlpha, RDP::AlphaAddSub::Zero);
  }
  a.member_label(name, "flags", sizeof(s.flags));
  a.bounded(s.flags, 0u, 0x7fffffffu);
  a.member_label(name, "dither", sizeof(s.dither));
  a.bounded(s.dither, 0u, 15u);
  a.member_label(name, "texture_size", sizeof(s.texture_size));
  a.bounded(s.texture_size, 0u, 3u);
  a.member_label(name, "texture_fmt", sizeof(s.texture_fmt));
  a.bounded(s.texture_fmt, 0u, 7u);
}

void blend(state::Archive &a, RDP::DepthBlendState &s, std::string_view name) {
  for (unsigned cycle = 0; cycle < std::size(s.blend_cycles); ++cycle) {
    auto &c = s.blend_cycles[cycle];
    a.indexed_label(name, cycle, "blend_1a", sizeof(c.blend_1a));
    a.bounded(c.blend_1a, RDP::BlendMode1A::PixelColor, RDP::BlendMode1A::FogColor);
    a.indexed_label(name, cycle, "blend_1b", sizeof(c.blend_1b));
    a.bounded(c.blend_1b, RDP::BlendMode1B::PixelAlpha, RDP::BlendMode1B::Zero);
    a.indexed_label(name, cycle, "blend_2a", sizeof(c.blend_2a));
    a.bounded(c.blend_2a, RDP::BlendMode2A::PixelColor, RDP::BlendMode2A::FogColor);
    a.indexed_label(name, cycle, "blend_2b", sizeof(c.blend_2b));
    a.bounded(c.blend_2b, RDP::BlendMode2B::InvPixelAlpha, RDP::BlendMode2B::Zero);
  }
  a.member_label(name, "flags", sizeof(s.flags));
  state::Archive::require((a.field(s.flags) & ~0x1fbu) == 0);
  a.member_label(name, "coverage_mode", sizeof(s.coverage_mode));
  a.bounded(s.coverage_mode, RDP::CoverageMode::Clamp, RDP::CoverageMode::Save);
  a.member_label(name, "z_mode", sizeof(s.z_mode));
  a.bounded(s.z_mode, RDP::ZMode::Opaque, RDP::ZMode::Decal);
}

} // namespace

void RendererState::registers(state::Archive &a, RDP::CommandProcessor &p) {
  scissor(a, p.scissor_state, "gpu.command.scissor");
  raster(a, p.static_state, "gpu.command.raster");
  blend(a, p.depth_blend, "gpu.command.blend");
  a.label("gpu.texture_image.addr", sizeof(p.texture_image.addr));
  a.bounded(p.texture_image.addr, 0u, 0xffffffu);
  a.label("gpu.texture_image.width", sizeof(p.texture_image.width));
  a.bounded(p.texture_image.width, 0u, 1024u);
  a.label("gpu.texture_image.fmt", sizeof(p.texture_image.fmt));
  a.bounded(p.texture_image.fmt, RDP::TextureFormat::RGBA, static_cast<RDP::TextureFormat>(7));
  a.label("gpu.texture_image.size", sizeof(p.texture_image.size));
  a.bounded(p.texture_image.size, RDP::TextureSize::Bpp4, RDP::TextureSize::Bpp32);
  a.label("gpu.options.native_resolution_tex_rect",
          sizeof(p.quirks.u.options.native_resolution_tex_rect));
  a.field(p.quirks.u.options.native_resolution_tex_rect);
  a.label("gpu.options.native_texture_lod", sizeof(p.quirks.u.options.native_texture_lod));
  a.field(p.quirks.u.options.native_texture_lod);
  auto &r = p.renderer;
  a.label("gpu.capabilities.upscaling", sizeof(r.caps.upscaling));
  a.identity(r.caps.upscaling);
  a.label("gpu.framebuffer.addr", sizeof(r.fb.addr));
  a.bounded(r.fb.addr, 0u, 0xffffffu);
  a.label("gpu.framebuffer.depth_addr", sizeof(r.fb.depth_addr));
  a.bounded(r.fb.depth_addr, 0u, 0xffffffu);
  a.label("gpu.framebuffer.width", sizeof(r.fb.width));
  a.bounded(r.fb.width, 0u, 1024u);
  a.label("gpu.framebuffer.deduced_height", sizeof(r.fb.deduced_height));
  a.field(r.fb.deduced_height);
  a.label("gpu.framebuffer.fmt", sizeof(r.fb.fmt));
  a.bounded(r.fb.fmt, RDP::FBFormat::I4, RDP::FBFormat::RGBA8888);
  a.label("gpu.framebuffer.depth_write_pending", sizeof(r.fb.depth_write_pending));
  a.field(r.fb.depth_write_pending);
  a.label("gpu.framebuffer.color_write_pending", sizeof(r.fb.color_write_pending));
  a.field(r.fb.color_write_pending);
  scissor(a, r.stream.scissor_state, "gpu.stream.scissor");
  raster(a, r.stream.static_raster_state, "gpu.stream.raster");
  blend(a, r.stream.depth_blend_state, "gpu.stream.blend");
  for (unsigned index = 0; index < std::size(r.tiles); ++index) {
    auto &tile = r.tiles[index];
    a.indexed_label("gpu.tiles", index, "size.slo", sizeof(tile.size.slo));
    a.bounded(tile.size.slo, 0u, 0xfffu);
    a.indexed_label("gpu.tiles", index, "size.shi", sizeof(tile.size.shi));
    a.bounded(tile.size.shi, 0u, 0xfffu);
    a.indexed_label("gpu.tiles", index, "size.tlo", sizeof(tile.size.tlo));
    a.bounded(tile.size.tlo, 0u, 0xfffu);
    a.indexed_label("gpu.tiles", index, "size.thi", sizeof(tile.size.thi));
    a.bounded(tile.size.thi, 0u, 0xfffu);
    a.indexed_label("gpu.tiles", index, "meta.offset", sizeof(tile.meta.offset));
    state::Archive::require((a.bounded(tile.meta.offset, 0u, 4088u) & 7) == 0);
    a.indexed_label("gpu.tiles", index, "meta.stride", sizeof(tile.meta.stride));
    state::Archive::require((a.bounded(tile.meta.stride, 0u, 4088u) & 7) == 0);
    a.indexed_label("gpu.tiles", index, "meta.fmt", sizeof(tile.meta.fmt));
    a.bounded(tile.meta.fmt, RDP::TextureFormat::RGBA, static_cast<RDP::TextureFormat>(7));
    a.indexed_label("gpu.tiles", index, "meta.size", sizeof(tile.meta.size));
    a.bounded(tile.meta.size, RDP::TextureSize::Bpp4, RDP::TextureSize::Bpp32);
    a.indexed_label("gpu.tiles", index, "meta.palette", sizeof(tile.meta.palette));
    a.bounded<std::uint8_t>(tile.meta.palette, 0, 15);
    a.indexed_label("gpu.tiles", index, "meta.mask_s", sizeof(tile.meta.mask_s));
    a.bounded<std::uint8_t>(tile.meta.mask_s, 0, 10);
    a.indexed_label("gpu.tiles", index, "meta.shift_s", sizeof(tile.meta.shift_s));
    a.bounded<std::uint8_t>(tile.meta.shift_s, 0, 15);
    a.indexed_label("gpu.tiles", index, "meta.mask_t", sizeof(tile.meta.mask_t));
    a.bounded<std::uint8_t>(tile.meta.mask_t, 0, 10);
    a.indexed_label("gpu.tiles", index, "meta.shift_t", sizeof(tile.meta.shift_t));
    a.bounded<std::uint8_t>(tile.meta.shift_t, 0, 15);
    a.indexed_label("gpu.tiles", index, "meta.flags", sizeof(tile.meta.flags));
    a.bounded<std::uint8_t>(tile.meta.flags, 0, 15);
  }
  auto &c = r.constants;
  a.label("gpu.constants.blend_color", sizeof(c.blend_color));
  a.field(c.blend_color);
  a.label("gpu.constants.fog_color", sizeof(c.fog_color));
  a.field(c.fog_color);
  a.label("gpu.constants.env_color", sizeof(c.env_color));
  a.field(c.env_color);
  a.label("gpu.constants.primitive_color", sizeof(c.primitive_color));
  a.field(c.primitive_color);
  a.label("gpu.constants.fill_color", sizeof(c.fill_color));
  a.field(c.fill_color);
  a.label("gpu.constants.min_level", sizeof(c.min_level));
  a.field(c.min_level);
  a.label("gpu.constants.prim_lod_frac", sizeof(c.prim_lod_frac));
  a.field(c.prim_lod_frac);
  a.label("gpu.constants.prim_depth", sizeof(c.prim_depth));
  a.field(c.prim_depth);
  a.label("gpu.constants.prim_dz", sizeof(c.prim_dz));
  a.field(c.prim_dz);
  a.label("gpu.constants.use_prim_depth", sizeof(c.use_prim_depth));
  a.field(c.use_prim_depth);
  a.label("gpu.constants.convert", sizeof(c.convert[0]));
  a.span(std::span(c.convert));
  a.label("gpu.constants.key_width", sizeof(c.key_width[0]));
  a.span(std::span(c.key_width));
  a.label("gpu.constants.key_center", sizeof(c.key_center[0]));
  a.span(std::span(c.key_center));
  a.label("gpu.constants.key_scale", sizeof(c.key_scale[0]));
  a.span(std::span(c.key_scale));
  a.label("gpu.base_primitive_index", sizeof(r.base_primitive_index));
  a.field(r.base_primitive_index);
  auto &v = p.vi;
  a.label("gpu.vi.vi_registers", sizeof(v.vi_registers[0]));
  a.span(std::span(v.vi_registers));
  a.label("gpu.vi.per_line_state.flags", sizeof(v.per_line_state.flags));
  const auto flags = a.bounded(v.per_line_state.flags, 0u, 3u);
  a.label("gpu.vi.per_line_state.line", sizeof(v.per_line_state.line));
  const auto line = a.bounded(v.per_line_state.line, 0u, RDP::VI_V_END_MAX - 1);
  a.label("gpu.vi.per_line_state.ended", sizeof(v.per_line_state.ended));
  const auto ended = a.field(v.per_line_state.ended);
  const auto count = ended ? RDP::VI_V_END_MAX : line + 1;
  if (flags & RDP::VideoInterface::PER_SCANLINE_HSTART_BIT) {
    a.label("gpu.vi.per_line_state.h_start.latched_state",
            sizeof(v.per_line_state.h_start.latched_state));
    a.field(v.per_line_state.h_start.latched_state);
    a.label("gpu.vi.per_line_state.h_start.line_state",
            sizeof(v.per_line_state.h_start.line_state[0]));
    a.span(std::span(v.per_line_state.h_start.line_state).first(count));
  }
  if (flags & RDP::VideoInterface::PER_SCANLINE_XSCALE_BIT) {
    a.label("gpu.vi.per_line_state.x_scale.latched_state",
            sizeof(v.per_line_state.x_scale.latched_state));
    a.field(v.per_line_state.x_scale.latched_state);
    a.label("gpu.vi.per_line_state.x_scale.line_state",
            sizeof(v.per_line_state.x_scale.line_state[0]));
    a.span(std::span(v.per_line_state.x_scale.line_state).first(count));
  }
  a.label("gpu.vi.previous_frame_blank", sizeof(v.previous_frame_blank));
  a.field(v.previous_frame_blank);
  a.label("gpu.vi.frame_count", sizeof(v.frame_count));
  a.field(v.frame_count);
  a.label("gpu.vi.last_valid_frame_count", sizeof(v.last_valid_frame_count));
  a.field(v.last_valid_frame_count);
}

} // namespace cupid::n64
