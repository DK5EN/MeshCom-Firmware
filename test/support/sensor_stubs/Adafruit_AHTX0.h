// Adafruit_AHTX0 stub for env:native_temp_offset: getEvent() returns the raw
// reading the test planted in mc_test_aht_temp / mc_test_aht_hum.
#pragma once

#include <Wire.h>

struct sensors_event_t
{
    float temperature;
    float relative_humidity;
};

inline float mc_test_aht_temp = 0.0f;
inline float mc_test_aht_hum = 0.0f;

class Adafruit_AHTX0
{
public:
    bool begin(TwoWire *) { return true; }
    bool getEvent(sensors_event_t *humidity, sensors_event_t *temp)
    {
        humidity->relative_humidity = mc_test_aht_hum;
        temp->temperature = mc_test_aht_temp;
        return true;
    }
};
