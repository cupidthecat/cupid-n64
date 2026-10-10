#include "core/disk/image/geometry.hpp"
#include "fixture.hpp"

namespace test::disk_boundaries {

void transfers(Fixture &fixture) {
  for (unsigned epoch : {0u, 0xfffffff0u})
    for (unsigned track : {0u, 158u, 1061u})
      for (unsigned head : {0u, 1u})
        for (unsigned sector : {0u, 84u, 88u, 90u})
          for (unsigned bytes : {0u, 231u})
            for (unsigned reading : {0u, 1u})
              for (unsigned transfer : {0u, 512u}) {
                fixture.reset(true, epoch);
                fixture.command(reading ? 1 : 2, track | (head << 12));
                fixture.advance(300000000);
                fixture.write(16, 0x100);
                fixture.write(40, bytes);
                fixture.write(44, 0x5900 | bytes);
                fixture.watch = fixture.transfer_offset(sector ? sector - 1 : 0);
                equal(fixture.watch + 256 <= cupid::n64::disk_geometry::PhysicalSize, true);
                equal(fixture.select(0x05000400), true);
                for (unsigned half = 0; half < 128; ++half)
                  fixture.write_half(static_cast<std::uint16_t>(0xb500 ^ (half * 79)));
                fixture.write(16, 0x8000 | (reading << 14) | transfer | sector);
                fixture.advance(49999 + track / 15);
                fixture.observe("transfer before deadline");
                fixture.advance(1);
                fixture.observe("transfer request");
                fixture.advance(37999 + track / 15);
                fixture.observe("transfer acknowledge before deadline");
                fixture.advance(1);
                fixture.observe("transfer acknowledge deadline");
                fixture.write(16, 0x1000);
                fixture.write(16, 0);
                fixture.advance(50000 + track / 15);
                fixture.observe("transfer block reset");
                fixture.write(32, 0xaaaa);
                fixture.advance(50000);
                fixture.observe("transfer hard reset");
              }
}

} // namespace test::disk_boundaries
