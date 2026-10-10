// SHTSensor stub for env:native_temp_offset: readSample() succeeds and the
// getters return the raw reading the test planted in mc_test_sht_*.
#pragma once

inline float mc_test_sht_temp = 0.0f;
inline float mc_test_sht_hum = 0.0f;

class SHTSensor
{
public:
    bool init() { return true; }
    bool readSample() { return true; }
    float getTemperature() const { return mc_test_sht_temp; }
    float getHumidity() const { return mc_test_sht_hum; }
};
