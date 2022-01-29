#include "peripherals.h"
#include <boost/log/trivial.hpp>

int main()
{
    // motor left{13};
    // motor right{12};
    // output_pin eyes{6};
    // output_pin fan{7};
    // adc ain{false, 0, 3.3f};

    bme280 env{22, 0x76, {19, 21}};
    while (true)
    {
        bme280_readout data = env.read();
        BOOST_LOG_TRIVIAL(info) << data.humidity << ' ' << data.temperature << ' ' << data.pressure;
        usleep(70000);
    }
}
