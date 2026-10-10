#include "fixture.hpp"

namespace test::disk_boundaries {

void simultaneous(Fixture &fixture) {
  for (unsigned epoch : {0u, 0xfffffff0u})
    for (bool seek_first : {false, true}) {
      fixture.reset(true, epoch);
      fixture.write(40, 231);
      if (seek_first) {
        fixture.command(1, 0);
        fixture.advance(201643000);
        fixture.write(16, 0xc000);
      } else {
        fixture.write(16, 0xc000);
        fixture.advance(42000);
        fixture.command(27, 0);
      }
      fixture.observe("simultaneous scheduled");
      fixture.advance(seek_first ? 49999 : 7999);
      fixture.observe("simultaneous before deadline");
      fixture.advance(1);
      fixture.observe("simultaneous deadline");
      fixture.write(32, 0xaaaa);
      fixture.observe("simultaneous pending reset");
      fixture.advance(38000);
      fixture.observe("simultaneous cancelled");
    }
}

} // namespace test::disk_boundaries
