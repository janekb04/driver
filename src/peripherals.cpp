#include "peripherals.h"

#include "peripherals_detail.h"
#include <bme280.h>
#include <cassert>
#include <pigpio.h>
#include <unistd.h>

#define CALL(x, cat)                                                                              \
    do                                                                                             \
    {                                                                                              \
        if (auto ec = ::peripherals::detail::make_##cat##_error(x); ec.value() < 0)                \
        {                                                                                          \
            throw std::system_error{ec};                                                           \
        }                                                                                          \
    }                                                                                              \
    while (false)
#define PIGPIO_CALL(x) CALL(x, gpio)
#define BME280_CALL(x) CALL(x, bme280)

namespace peripherals {
namespace {

void i2c_delay_us(u32 period, void*)
{
    usleep(period);
}

i8 i2c_read(u8 reg_addr, u8* reg_data, u32 len, void* intf_ptr)
{
    assert(intf_ptr);
    i32 handle = *static_cast<i32*>(intf_ptr);
    return i2cReadI2CBlockData(handle, reg_addr, (char*)reg_data, len) > 0 ? 0 : -1;
}

i8 i2c_write(u8 reg_addr, const u8* reg_data, u32 len, void* intf_ptr)
{
    assert(intf_ptr);
    i32 handle = *static_cast<i32*>(intf_ptr);
    return i2cWriteI2CBlockData(handle, reg_addr, (char*)reg_data, len) > 0 ? 0 : -1;
}

} // namespace

motor::motor(u32 broadcom) : pin{broadcom}
{
    set_speed(0.0f);
}

motor::~motor()
{
    gpioServo(pin, 1000);
}

void motor::set_speed(f32 value)
{
    assert(value >= 0.0f && value <= 1.0f);
    u32 us = 1000 + static_cast<u32>(value * 1000);
    PIGPIO_CALL(gpioServo(pin, us));
}

f32 motor::speed() const
{
    u32 us;
    PIGPIO_CALL(us = gpioGetServoPulsewidth(pin));
    return static_cast<f32>(us - 1000) / 1000.0f;
}

mcp3008::mcp3008(bool aux, u32 channel, f32 vref) : vref{vref}
{
    PIGPIO_CALL(handle = spiOpen(channel, 1000000, PI_SPI_FLAGS_AUX_SPI(aux)));
}

mcp3008::~mcp3008()
{
    if (handle >= 0)
    {
        spiClose(handle);
    }
}

f32 mcp3008::read(u32 channel) const
{
    assert(channel <= 7);

    // Single-ended channel selection
    char tx[3], rx[3];
    tx[0] = 1;
    tx[1] = (8 + channel) << 4;
    PIGPIO_CALL(spiXfer(handle, tx, rx, 3));

    // Assemble digital output code
    u32 code = (rx[1] << 8) | rx[2];
    code &= 0x3FF;

    // Calculate input voltage
    return static_cast<f32>(code) * vref / 1023.0f;
}

bme280::bme280(u32 bus, u32 address, std::pair<u32, u32> broadcom) :
    pins{broadcom}, dev{std::make_unique<bme280_dev>()}
{
    // Enable pull-ups on I2C pins
    PIGPIO_CALL(gpioSetPullUpDown(pins.first, PI_PUD_UP));
    PIGPIO_CALL(gpioSetPullUpDown(pins.second, PI_PUD_UP));

    // Open I2C bus
    PIGPIO_CALL(handle = i2cOpen(bus, address, 0));

    // Initialize BME280 driver
    dev->intf = BME280_I2C_INTF;
    dev->intf_ptr = &handle;
    dev->read = i2c_read;
    dev->write = i2c_write;
    dev->delay_us = i2c_delay_us;
    BME280_CALL(bme280_init(dev.get()));

    // Configure sensor settings and mode
    dev->settings.osr_h = BME280_OVERSAMPLING_1X;
    dev->settings.osr_p = BME280_OVERSAMPLING_16X;
    dev->settings.osr_t = BME280_OVERSAMPLING_2X;
    dev->settings.filter = BME280_FILTER_COEFF_16;
    dev->settings.standby_time = BME280_STANDBY_TIME_62_5_MS;
    u8 settings_sel = BME280_OSR_PRESS_SEL;
    settings_sel |= BME280_OSR_TEMP_SEL;
    settings_sel |= BME280_OSR_HUM_SEL;
    settings_sel |= BME280_STANDBY_SEL;
    settings_sel |= BME280_FILTER_SEL;
    BME280_CALL(bme280_set_sensor_settings(settings_sel, dev.get()));
    BME280_CALL(bme280_set_sensor_mode(BME280_NORMAL_MODE, dev.get()));
}

bme280::~bme280()
{
    // Close I2C bus
    if (handle >= 0)
    {
        i2cClose(handle);
    }

    // Reset pull-ups on I2C pins
    gpioSetPullUpDown(pins.first, PI_PUD_OFF);
    gpioSetPullUpDown(pins.second, PI_PUD_OFF);
}

bme280_readout bme280::read()
{
    bme280_data data;
    BME280_CALL(bme280_get_sensor_data(BME280_ALL, &data, dev.get()));

    bme280_readout result;
    result.humidity = data.humidity;
    result.temperature = data.temperature;
    result.pressure = data.pressure;
    return result;
}

output_pin::output_pin(u32 broadcom, bool on) : pin{broadcom}
{
    PIGPIO_CALL(gpioSetMode(pin, PI_OUTPUT));
    set_value(on);
}

output_pin::~output_pin()
{
    gpioWrite(pin, 0);
}

void output_pin::set_value(bool on)
{
    PIGPIO_CALL(gpioWrite(pin, on));
    state = on;
}

bool output_pin::value() const
{
    return state;
}

} // namespace peripherals
