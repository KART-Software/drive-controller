#include "can_bus.hpp"
#include <cstring>

void CanBus::begin()
{
    can1.begin();
    can1.setBaudRate(CAN_BITRATE);
    can1.enableFIFO();
}

int16_t CanBus::floatToInt16(float val, float scale)
{
    float scaled = val * scale;
    if (scaled > 32767.0f)
        return 32767;
    if (scaled < -32768.0f)
        return -32768;
    return static_cast<int16_t>(scaled);
}

void CanBus::sendFrame(uint32_t id, const uint8_t *data, uint8_t len)
{
    CAN_message_t msg;
    msg.id = id;
    msg.len = len;
    memcpy(msg.buf, data, len);
    can1.write(msg);
}

// 0x100: APPS1(16) APPS2(16) TPS1(16) TPS2(16)  — scale ×100
void CanBus::sendThrottleFrame(float apps1, float apps2, float tps1, float tps2)
{
    uint8_t buf[8];
    int16_t a1 = floatToInt16(apps1, 100.0f);
    int16_t a2 = floatToInt16(apps2, 100.0f);
    int16_t t1 = floatToInt16(tps1, 100.0f);
    int16_t t2 = floatToInt16(tps2, 100.0f);
    buf[0] = a1 >> 8;
    buf[1] = a1 & 0xFF;
    buf[2] = a2 >> 8;
    buf[3] = a2 & 0xFF;
    buf[4] = t1 >> 8;
    buf[5] = t1 & 0xFF;
    buf[6] = t2 >> 8;
    buf[7] = t2 & 0xFF;
    sendFrame(CAN_ID_THROTTLE, buf, 8);
}

// 0x101: target(16) BPS(16) errorFlags(16) mode(8) reserved(8)
void CanBus::sendStatusFrame(float target, float bps, uint16_t errorFlags, uint8_t mode)
{
    uint8_t buf[8];
    int16_t t = floatToInt16(target, 100.0f);
    int16_t b = floatToInt16(bps, 100.0f);
    buf[0] = t >> 8;
    buf[1] = t & 0xFF;
    buf[2] = b >> 8;
    buf[3] = b & 0xFF;
    buf[4] = errorFlags >> 8;
    buf[5] = errorFlags & 0xFF;
    buf[6] = mode;
    buf[7] = 0;
    sendFrame(CAN_ID_STATUS, buf, 8);
}

// 0x102: FL(16) FR(16) RL(16) RR(16)  — Hz ×10
void CanBus::sendWheelSpeedFrame(float wfl, float wfr, float wrl, float wrr)
{
    uint8_t buf[8];
    int16_t fl = floatToInt16(wfl, 10.0f);
    int16_t fr = floatToInt16(wfr, 10.0f);
    int16_t rl = floatToInt16(wrl, 10.0f);
    int16_t rr = floatToInt16(wrr, 10.0f);
    buf[0] = fl >> 8;
    buf[1] = fl & 0xFF;
    buf[2] = fr >> 8;
    buf[3] = fr & 0xFF;
    buf[4] = rl >> 8;
    buf[5] = rl & 0xFF;
    buf[6] = rr >> 8;
    buf[7] = rr & 0xFF;
    sendFrame(CAN_ID_WHEEL_SPEED, buf, 8);
}

// 0x103: RPM(16) ax(16) ay(16) az(16)  — accel mg, RPM ×1
void CanBus::sendEngineImuFrame(float rpm, float ax, float ay, float az)
{
    uint8_t buf[8];
    int16_t r = floatToInt16(rpm, 1.0f);
    int16_t x = floatToInt16(ax, 1000.0f); // m/s² → mg approx (×1000)
    int16_t y = floatToInt16(ay, 1000.0f);
    int16_t z = floatToInt16(az, 1000.0f);
    buf[0] = r >> 8;
    buf[1] = r & 0xFF;
    buf[2] = x >> 8;
    buf[3] = x & 0xFF;
    buf[4] = y >> 8;
    buf[5] = y & 0xFF;
    buf[6] = z >> 8;
    buf[7] = z & 0xFF;
    sendFrame(CAN_ID_ENGINE_IMU, buf, 8);
}

// 0x104: gx(16) gy(16) gz(16) reserved(16)  — dps ×10
void CanBus::sendGyroFrame(float gx, float gy, float gz)
{
    uint8_t buf[8];
    int16_t x = floatToInt16(gx, 10.0f);
    int16_t y = floatToInt16(gy, 10.0f);
    int16_t z = floatToInt16(gz, 10.0f);
    buf[0] = x >> 8;
    buf[1] = x & 0xFF;
    buf[2] = y >> 8;
    buf[3] = y & 0xFF;
    buf[4] = z >> 8;
    buf[5] = z & 0xFF;
    buf[6] = 0;
    buf[7] = 0;
    sendFrame(CAN_ID_GYRO, buf, 8);
}

// Poll RX FIFO. If mode-select frame (0x200) received, return true and set modeOut.
bool CanBus::poll(uint8_t &modeOut)
{
    CAN_message_t msg;
    bool modeReceived = false;
    while (can1.read(msg))
    {
        if (msg.id == CAN_ID_MODE_SELECT && msg.len >= 1)
        {
            modeOut = msg.buf[0];
            modeReceived = true;
        }
    }
    return modeReceived;
}

uint8_t CanBus::modeStringToId(const char *mode)
{
    if (strcmp(mode, "Normal") == 0)
        return 1;
    if (strcmp(mode, "Restrict") == 0)
        return 2;
    return 0; // Calib or unknown
}
