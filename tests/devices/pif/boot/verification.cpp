#include "../../fixture.hpp"
#include "core/devices/pif/pif.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
using namespace cupid::n64;

struct VerificationFixture : MemoryFixture {
  Cic cic;
  Pif pif;
  unsigned resets = 0;
  VerificationFixture(CicModel model, BootVerification verification, VideoRegion region)
      : cic(model), pif(cic, ram, verification, region) {
    initialize();
    pif.connect_reset([this] {
      ++resets;
      cpu.request_nmi();
    });
  }
  void state(Pif::State expected) {
    equal(static_cast<unsigned>(pif.state()), static_cast<unsigned>(expected));
  }
  void nmi() {
    const auto pc = cpu.state().pc, clocks = cpu.state().clocks;
    pif.tick();
    equal(resets, 1u);
    equal(cpu.state().pc, pc);
    equal(cpu.state().clocks, clocks);
    cpu.step();
    equal(cpu.state().pc, 0xffffffffbfc00000ull);
    equal(cpu.read_control(ErrorEpc), pc);
    equal(cpu.state().clocks, clocks + 2);
  }
};

bool pal(CicModel model) {
  return model == CicModel::N7101 || model == CicModel::N7102 || model == CicModel::N7103 ||
         model == CicModel::N7105 || model == CicModel::N7106;
}

void boot(VerificationFixture &f, std::uint64_t checksum) {
  f.pif.write_word(0x1fc007fc, 0x10);
  f.state(Pif::State::GetChecksum);
  f.pif.write_word(0x1fc007f0, static_cast<std::uint32_t>(checksum >> 32));
  f.pif.write_word(0x1fc007f4, static_cast<std::uint32_t>(checksum));
  f.pif.write_word(0x1fc007fc, 0x20);
  f.state(Pif::State::CheckChecksum);
  equal(f.pif.ram()[63], 0xa0);
  f.pif.write_word(0x1fc007fc, 0x40);
}
} // namespace

void pif_verification_tests() {
  equal(BootVerification{}.checksum, true);
  equal(BootVerification{}.region, false);
  for (unsigned model = 0; model < pif_boot::Checksums.size(); ++model)
    for (auto region : {VideoRegion::Ntsc, VideoRegion::Pal})
      for (bool checksum_check : {false, true})
        for (bool region_check : {false, true})
          for (bool valid : {false, true}) {
            VerificationFixture f(static_cast<CicModel>(model), {checksum_check, region_check},
                                  region);
            f.pif.tick();
            const bool rejected_region =
                region_check && pal(static_cast<CicModel>(model)) != (region == VideoRegion::Pal);
            f.state(rejected_region ? Pif::State::Error : Pif::State::Lockout);
            equal(f.resets, 0u);
            equal(f.pif.reset_enabled(), false);
            if (rejected_region) {
              f.nmi();
              continue;
            }
            const auto checksum = pif_boot::Checksums[model] ^ (valid ? 0ull : 1ull);
            boot(f, checksum);
            const bool rejected_checksum = checksum_check && !valid;
            f.state(rejected_checksum ? Pif::State::Error : Pif::State::Terminate);
            equal(f.resets, 0u);
            if (rejected_checksum) {
              f.nmi();
              continue;
            }
            f.pif.write_word(0x1fc007fc, 8);
            f.state(Pif::State::Run);
            equal(f.pif.reset_enabled(), true);
            equal(f.pif.ram()[63], 0);
            f.pif.tick();
            equal(f.resets, 0u);
            f.pif.power();
            f.cic.power(static_cast<CicModel>(model));
            f.pif.tick();
            f.state(Pif::State::Lockout);
          }
  for (bool checksum_check : {false, true})
    for (bool region_check : {false, true}) {
      VerificationFixture f(CicModel::N6102, {checksum_check, region_check}, VideoRegion::Ntsc);
      f.pif.tick();
      boot(f, pif_boot::Checksums[2] ^ (checksum_check ? 0ull : 1ull));
      f.state(Pif::State::Terminate);
      f.pif.elapse(pif_boot::TickClocks * 6);
      f.pif.advance((pif_boot::TimeoutClocks / pif_boot::TickClocks) * pif_boot::TickClocks);
      f.state(Pif::State::Terminate);
      equal(f.resets, 0u);
      f.pif.advance(1);
      f.state(Pif::State::Error);
      equal(f.resets, 0u);
      f.nmi();
    }
}

} // namespace test
