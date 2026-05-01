#ifndef _CAN_BUS_H_
#define _CAN_BUS_H_

#include <Arduino.h>
#include <FlexCAN_T4.h>
#include "constants.hpp"

class CanBus
{
public:
    void begin();

    // Send throttle data: APPS1, APPS2, TPS1, TPS2 (each 16-bit scaled ×100)
    void sendThrottleFrame(float apps1, float apps2, float tps1, float tps2);

    // Send status data: target, BPS, error flags, mode
    void sendStatusFrame(float target, float bps, uint16_t errorFlags, uint8_t mode);

    // Send wheel speed data: FL, FR, RL, RR (Hz ×10)
    void sendWheelSpeedFrame(float wfl, float wfr, float wrl, float wrr);

    // Send engine RPM + IMU accel: rpm, ax, ay, az
    void sendEngineImuFrame(float rpm, float ax, float ay, float az);

    // Send gyro data: gx, gy, gz
    void sendGyroFrame(float gx, float gy, float gz);

    // Poll for incoming CAN messages. Returns true if mode select received.
    bool poll(uint8_t &modeOut);

private:
    FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> can1;

    void sendFrame(uint32_t id, const uint8_t *data, uint8_t len);
    static int16_t floatToInt16(float val, float scale);

public:
    // Convert Target mode string to numeric ID for CAN
    static uint8_t modeStringToId(const char *mode);
};

#endif // _CAN_BUS_H_
