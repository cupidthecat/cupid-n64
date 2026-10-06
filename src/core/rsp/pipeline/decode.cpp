#include "core/rsp/rsp.hpp"

namespace cupid::n64 {

Rsp::OpInfo Rsp::decode_info(std::uint32_t instruction) {
  OpInfo info;
  const auto operation = instruction >> 26;
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto dest = (instruction >> 11) & 31;
  const auto function = instruction & 63;
  const auto r = [](unsigned reg) { return 1u << reg; };
  if (operation == 0) {
    switch (function) {
    case 0:
    case 2:
    case 3:
      info = {Bypass, r(target), r(dest)};
      break;
    case 4:
    case 6:
    case 7:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 42:
    case 43:
      info = {Bypass, r(source) | r(target), r(dest)};
      break;
    case 8:
      info = {Branch, r(source)};
      break;
    case 9:
      info = {Branch | Bypass, r(source), r(dest)};
      break;
    case 13:
      info.flags = Branch;
      break;
    default:
      break;
    }
  } else if (operation == 1) {
    if (target == 0 || target == 1)
      info = {Branch, r(source)};
    if (target == 16 || target == 17)
      info = {Branch | Bypass, r(source), r(31)};
  } else if (operation == 2 || operation == 3) {
    info.flags = Branch;
    if (operation == 3) {
      info.flags |= Bypass;
      info.write_gpr = r(31);
    }
  } else if (operation >= 4 && operation <= 7) {
    info = {Branch, r(source)};
    if (operation <= 5)
      info.read_gpr |= r(target);
  } else if (operation >= 8 && operation <= 15) {
    info = {Bypass, operation == 15 ? 0 : r(source), r(target)};
  } else if (operation == 16) {
    if (source == 0)
      info = {Load | Store, 0, r(target)};
    if (source == 4)
      info = {Load | Store, r(target)};
  } else if (operation == 18) {
    if (source < 16) {
      if (source == 0)
        info = {Load | Store, 0, r(target), r(dest)};
      if (source == 2) {
        info = {Load | Store, 0, r(target)};
        info.read_control = r(dest & 3);
      }
      if (source == 4)
        info = {Load | Store | NopGroup, r(target), 0, 0, r(dest)};
      if (source == 6) {
        info = {Load | Store, r(target)};
        info.write_control = r(dest & 3);
      }
    } else {
      const auto vector_dest = (instruction >> 6) & 31;
      info.flags = Vector;
      if (function == 0x37) {
        info.flags |= NopGroup;
        info.fake_vector = r(vector_dest);
      } else if (function != 0x3f) {
        const bool reserved = function == 0x12 || (function >= 0x16 && function <= 0x1c) ||
                              function == 0x1e || function == 0x1f || function == 0x2e ||
                              function == 0x2f || (function >= 0x38 && function <= 0x3e);
        if (!reserved) {
          info.write_vector = r(vector_dest);
          if (function != 0x0b && function != 0x1d)
            info.read_vector = r(target);
          if (function <= 0x2d && function != 0x02 && function != 0x0a && function != 0x0b &&
              function != 0x1d)
            info.read_vector |= r(dest);
          if (function >= 0x30 && function <= 0x36)
            info.fake_vector = r(dest);
          if (function == 0x10 || function == 0x11 || function == 0x13 || function == 0x14 ||
              function == 0x15)
            info.read_control = info.write_control = 1;
          if (function >= 0x20 && function <= 0x27)
            info.read_control = info.write_control = 3;
          if (function >= 0x24 && function <= 0x26)
            info.read_control = info.write_control = 7;
        }
      }
    }
  } else if (operation == 32 || operation == 33 || operation == 35 || operation == 36 ||
             operation == 37 || operation == 39) {
    info = {Load, r(source), r(target)};
  } else if (operation == 40 || operation == 41 || operation == 43) {
    info = {Store, r(source) | r(target)};
  } else if (operation == 50 || operation == 58) {
    if (dest <= 11 && !(operation == 50 && dest == 10)) {
      info.flags = operation == 50 ? Load : Store;
      info.read_gpr = r(source);
      const auto registers = dest == 11 ? (255u << (target & ~7u)) : r(target);
      if (operation == 50) {
        info.write_vector = registers;
        if (dest == 11)
          info.flags |= NopGroup;
      } else
        info.read_vector = registers;
    }
  }
  return info;
}

} // namespace cupid::n64
