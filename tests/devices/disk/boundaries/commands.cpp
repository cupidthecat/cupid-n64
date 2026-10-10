#include "fixture.hpp"

namespace test::disk_boundaries {

void commands(Fixture &fixture) {
  for (bool present : {false, true})
    for (unsigned epoch : {0u, 0xfffffff0u})
      for (unsigned code : {1u, 9u, 12u, 27u, 0xeeeeu})
        for (unsigned parameter : {0u, 0xffffu}) {
          fixture.reset(present, epoch);
          fixture.observe("command power");
          fixture.command(code, parameter);
          fixture.observe("command issued");
          fixture.advance(7999);
          fixture.observe("command before deadline");
          fixture.advance(1);
          fixture.observe("command deadline");
          fixture.command(12, 0x5a80);
          fixture.observe("command overlap");
          fixture.advance(350000000);
          fixture.observe("command settled");
          fixture.write(16, 0x100);
          fixture.write(32, 0xaaaa);
          fixture.observe("command reset");
          fixture.advance(187500000);
          fixture.observe("command clock tick");
        }
}

} // namespace test::disk_boundaries
