#pragma once

#include <Arduino.h>

#include "constants.hpp"
#include "proto/drive_controller.pb.h"
#include "sensor/pulse_counter.hpp"
#include "sensor/sensors.hpp"
#include "sensor/wheel_speed.hpp"

namespace shift {

// オートシフター。詳細仕様は dc-firmware/auto_shifter_spec.md。
//
// 出力は OFF/ON 両モードとも「シフト要求 (エッジ or 自動判断) → 整形パルス
// (Idle→Pulsing→Cooldown)」で統一。パルス幅は transmission/ギア/方向/停車状態で決まる。
//
// 層構成:
//   ① 判断ロジック (evaluate)   ON 時の自動シフト判断 (RPM/車輪速/ギア/APPS)
//   ② 調停 (update 内)          OFF/ON+停車低速帯 → ドライバー入力 / ON → evaluate
//   ③ 出力整形 (状態機械)        pulseWidthFor + Idle→Pulsing→Cooldown
//   ④ I/O                       入力エッジ検出 / センサー getter / digitalWrite
class AutoShifter {
   public:
    AutoShifter(const PulseCounter& engine,
                const WheelSpeedSensor& wheelFL,
                const WheelSpeedSensor& wheelFR,
                const GearPositionSensor& gps,
                const Apps& apps);

    void begin();  // pinMode 設定、出力を非アサート初期化、入力ベースライン取得
    void setConfig(const dc_AutoShiftConfig& cfg, dc_TransmissionType tx, uint32_t engineTeeth);
    void update(bool autoOn);  // 毎ループ呼ぶ

    enum class State { Idle, Pulsing, Cooldown };
    State state() const { return state_; }

   private:
    enum class Dir { None, Up, Down };

    // -- 入力 (SensorHub 全体ではなく必要分だけ const 参照) --
    const PulseCounter& engine_;
    const WheelSpeedSensor& wheelFL_;  // 非駆動輪 = 真の車速 (ホイールスピン非依存)
    const WheelSpeedSensor& wheelFR_;
    const GearPositionSensor& gps_;
    const Apps& apps_;  // ドライバーのアクセル開度 (スロットルゲート)

    // -- config --
    float upshiftRpm_ = 0.0f;
    float downshiftRpm_ = 0.0f;
    float minWheelHz_ = 0.0f;
    float throttleOnPct_ = 50.0f;
    uint32_t cooldownMs_ = 200;
    uint32_t istPulseMs_ = 15;
    uint32_t normalDrivePulseMs_ = 100;
    uint32_t normalNeutralPulseMs_ = 25;
    dc_TransmissionType tx_ = dc_TransmissionType_TRANSMISSION_IST;
    uint32_t engineTeeth_ = 1;
    int8_t maxGear_ = 4;  // tx から決定 (IST=4 / NORMAL=6)

    // -- 出力状態機械 --
    State state_ = State::Idle;
    unsigned long stateEnteredMs_ = 0;
    uint32_t activePulseMs_ = 0;
    bool prevUpIn_ = false;
    bool prevDownIn_ = false;
    bool prevAutoOn_ = false;

    // -- helpers --
    bool readUpIn() const;
    bool readDownIn() const;
    void writeOutputs(bool up, bool down);
    float engineRpm() const;
    float wheelHz() const;
    bool moving() const;
    Dir evaluate() const;  // ① 判断ロジック (純粋判断, 副作用なし)
    uint32_t pulseWidthFor(int8_t gear, Dir dir) const;
    void startPulse(Dir dir, int8_t gear, unsigned long now);
};

}  // namespace shift
