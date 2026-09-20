#include "cupid/cpu.hpp"

namespace cupid {

void Cpu::request_nmi() {
    nmi_pending_ = true;
}

void Cpu::accept_nmi() {
    nmi_pending_ = false;
    cp0[30] = pc - (in_delay_slot_ ? 4U : 0U);
    cp0[12] = (cp0[12] & ~0x00200000ULL) | 0x00500004ULL;
    set_pc(0xffffffffbfc00000ULL);
    exception_pending = true;
}

} // namespace cupid
