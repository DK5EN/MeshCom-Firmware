// Wire stub for env:native_temp_offset: the sensor TUs only pass &Wire to the
// driver and reset the bus on MC_I2C_NEEDS_BUS_RESET boards.
#pragma once

class TwoWire
{
public:
    void begin(int = 0, int = 0) {}
    void end() {}
};

inline TwoWire Wire;
