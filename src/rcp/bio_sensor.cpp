#include "cupid/bus.hpp"

namespace cupid {

void Bus::set_bio_sensor_pulse(unsigned port, bool active) {
    if (port >= controllers_.size())
        return;
    const auto& controller = controllers_[port];
    if (controller.connected && controller.device == ControllerDevice::Gamepad &&
        controller.accessory == ControllerAccessory::BioSensor)
        bio_sensor_pulse_[port] = active;
}

u8 Bus::read_bio_sensor(unsigned port, u16 address) const {
    if (address < 0x8000)
        return 0;
    if (address < 0xc000)
        return 0x81;
    return bio_sensor_pulse_[port] ? 0 : 3;
}

} // namespace cupid
