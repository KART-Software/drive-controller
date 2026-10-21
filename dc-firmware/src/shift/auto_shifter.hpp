#pragma once

#include <Arduino.h>

#include "constants.hpp"
#include "proto/drive_controller.pb.h"
#include "interfaces/sensor/rotation_sensor.hpp"
#include "interfaces/sensor/wheel_speed_sensor.hpp"
#include "sensor/sensors.hpp"

namespace shift {

// オートシフター。詳細仕様は dc-firmware/auto_shifter_spec.md。
//
// 出力は OFF/ON 両モードとも「シフト要求 (エッジ or 自動判断) → 整形パルス
// (Idle→Pulsing→Cooldown)」で統一。パルス幅は transmission/ギア/方向/停車状態で決まる。
// パルスの開始から次の開始までは最短 minIntervalMs_ (既定 0.5 s)。その間に来た要求は捨てる。
//
// 呼ばれる文脈 (docs/loop_nonblocking_spec.md §6.2 / §6.3): どちらも優先度 64 なので互いに割り込まない。
//   onPaddles() : canRxISR。0x741 を受けたその場でエッジを処理し、パルスを始める
//   tick()      : tickISR (1 ms)。パルスの終端・最短間隔・自動判断・途絶したパドルの解放
//
// 手動オーバーライド: auto 実行中にドライバーが手動シフト操作をすると手動へ切り替わり、
// CAN が 手動→auto に遷移するまで手動を保持する (§5)。
//
// 層構成:
//   ① 判断ロジック (evaluate)   auto 時の自動シフト判断 (RPM/車輪速/ギア/APPS)
//   ② 調停 (update 内)          手動(CAN OFF/手動ラッチ/停車低速帯)→入力 / auto → evaluate
//   ③ 出力整形 (状態機械)        pulseWidthFor + Idle→Pulsing→Cooldown
//   ④ I/O                       入力エッジ検出 / センサー getter / digitalWrite
class AutoShifter {
   public:
    AutoShifter(const RotationSensor& engine,
                const WheelSpeedSensor& wheelFL,
                const WheelSpeedSensor& wheelFR,
                const GearPositionSensor& gps,
                const Apps& apps);

    void begin();  // pinMode 設定、出力を非アサート初期化、入力ベースライン取得
    void setConfig(const dc_AutoShiftConfig& cfg, dc_TransmissionType tx);
    // tickISR から毎 ms。autoOn = オートシフト指令 (ON=auto / OFF=manual)。
    // オートシフターは ETC プラウシビリティに依存しない (安全フォールバックは CAN 断→manual,
    // ControlInput::update)。ETC の停止/安全は別系統 (main.cpp の ETC アーミング)。
    // 入力はすべて呼び出し側から渡す (入手元 = ControlInput: CAN 0x740 / 0x741 または console override)。
    // パドルは押下状態 (レベル) で渡し、立ち上がり検出はここで行う。0x741 の途絶で押下が false に
    // 戻るのを追うためにも毎回渡す。
    struct Inputs {
        bool autoOn;       // オートシフト ON
        bool upPressed;    // シフトアップパドル押下中
        bool downPressed;  // シフトダウンパドル押下中
    };
    // 時刻は µs (sysClock)。tick は tick の時刻、onPaddles は受信した瞬間の時刻 (sysClock.nowUs()) を渡す。
    // パルスの開始時刻が出力を立てた時刻と一致するので、幅は指定値〜指定値 + 1 ms (終端の判定が tick ごとのため)
    void tick(const Inputs& in, uint32_t nowUs);
    // canRxISR から 0x741 受信のたび。押下状態を渡す (チャタリング除去は送信側の責務)
    void onPaddles(bool upPressed, bool downPressed, uint32_t nowUs);
    // true の間は新しいパルスを出さない (要求は捨てる)。flash 書き込み中に割り込みが止まると
    // パルスが伸びるため、書き込み待ちの間 tickISR が立てる (docs/loop_nonblocking_spec.md §6.8)
    void setSuspended(bool suspended) { suspended_ = suspended; }

    enum class State { Idle, Pulsing, Cooldown };
    State state() const { return state_; }
    // auto 指令中にドライバー操作で手動へ落ちているか (observability)
    bool manualOverride() const { return manualOverride_; }

   private:
    enum class Dir { None, Up, Down };

    // -- 入力 (SensorHub 全体ではなく必要分だけ const 参照) --
    const RotationSensor& engine_;
    const WheelSpeedSensor& wheelFL_;  // 非駆動輪 = 真の車速 (ホイールスピン非依存)
    const WheelSpeedSensor& wheelFR_;
    const GearPositionSensor& gps_;
    const Apps& apps_;  // ドライバーのアクセル開度 (スロットルゲート)

    // -- config --
    float upshiftRpm_ = 0.0f;
    float downshiftRpm_ = 0.0f;
    float minWheelHz_ = 0.0f;
    float throttleOnPct_ = 50.0f;
    uint32_t minIntervalMs_ = SHIFT_MIN_INTERVAL_MS;  // パルス開始→次の開始の最短 (config cooldown_ms、下限 0.5 s)
    uint32_t istPulseMs_ = 15;
    uint32_t normalDrivePulseMs_ = 100;
    uint32_t normalNeutralPulseMs_ = 25;
    dc_TransmissionType tx_ = dc_TransmissionType_TRANSMISSION_IST;
    int8_t maxGear_ = 4;  // tx から決定 (IST=4 / NORMAL=6)

    // -- 出力状態機械 --
    volatile State state_ = State::Idle;
    uint32_t pulseStartUs_ = 0;
    uint32_t activePulseMs_ = 0;
    bool prevUpIn_ = false;
    bool prevDownIn_ = false;
    bool prevAutoOn_ = false;
    bool autoOn_ = false;  // 直近の tick で受けた指令 (onPaddles が参照)
    bool suspended_ = false;
    // auto 実行中にドライバーが手動シフト → 手動へラッチ。CAN 手動→auto 遷移で解除。
    bool manualOverride_ = false;

    // -- helpers --
    void writeOutputs(bool up, bool down);
    float engineRpm() const;
    float wheelHz() const;
    bool moving() const;
    Dir evaluate() const;  // ① 判断ロジック (純粋判断, 副作用なし)
    uint32_t pulseWidthFor(int8_t gear, Dir dir) const;
    void startPulse(Dir dir, int8_t gear, uint32_t nowUs);
    void trackPaddles(bool up, bool down, uint32_t nowUs);  // エッジ検出 → onDriverEdge
    void onDriverEdge(bool upEdge, bool downEdge, uint32_t nowUs);
};

}  // namespace shift
