#pragma once

#include <Arduino.h>

#include "proto/drive_controller.pb.h"
#include "interfaces/sensor/rotation_sensor.hpp"

namespace starter {

// セルモーターリレー (docs/starter_spec.md)。tickISR から毎 ms tick() される。
//
//   Off      : セルスイッチを押した瞬間 (0 → 1) に、エンジン回転がしきい値以下なら Cranking (リレー ON)。
//              回っていれば ON にしない (押し直すまで待つ)
//   Cranking : 回転がしきい値を超えた状態が running_confirm_ms 続いたら Off (エンジンがかかった)。
//              ON にしてから min_hold_ms の間はスイッチや CAN に関係なく回し、それ以降はスイッチが 0
//              (CAN 途絶による 0 を含む) になったら Off。押し続ける限り上限なく回す
//
// 安全条件 (プラウシビリティ・SIG_IN・MOTOR_OFF) とは無関係に動く。
class StarterController {
   public:
    explicit StarterController(const RotationSensor& engine) : engine_(engine) {}

    void begin();
    void setConfig(const dc_StarterConfig& cfg);

    void tick(bool switchPressed, uint32_t nowMs);

    bool relayOn() const { return state_ == State::Cranking; }

   private:
    enum class State : uint8_t { Off, Cranking };

    float engineRpm() const;
    void setRelay(bool on);

    const RotationSensor& engine_;
    float runningRpm_ = 2000.0f;
    uint32_t runningConfirmMs_ = 1000;
    uint32_t minHoldMs_ = 5000;

    volatile State state_ = State::Off;  // loop (SD ログ) が relayOn() で読む
    bool prevSwitch_ = false;
    uint32_t crankStartMs_ = 0;
    bool aboveRpm_ = false;
    uint32_t aboveSinceMs_ = 0;
};

}  // namespace starter
