#pragma once

#include "cupid/types.hpp"

namespace cupid {

struct RdpTile {
    u16 tmem_address{};
    u16 line_stride{};
    u8 format{};
    u8 size{};
    u8 palette{};
    u8 s_shift{};
    u8 s_mask{};
    u8 t_shift{};
    u8 t_mask{};
    bool s_mirror{};
    bool s_clamp{};
    bool t_mirror{};
    bool t_clamp{};
    u16 s_low{};
    u16 t_low{};
    u16 s_high{};
    u16 t_high{};
};

} // namespace cupid
