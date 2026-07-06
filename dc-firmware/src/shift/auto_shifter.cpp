#include "auto_shifter.hpp"

#include "serial/serial_protocol.hpp"

namespace shift {

AutoShifter::AutoShifter(const PulseCounter& engine,
                         const WheelSpeedSensor& wheelFL,
                         const WheelSpeedSensor& wheelFR,
                         const GearPositionSensor& gps,
                         const Apps& apps)
    : engine_(engine), wheelFL_(wheelFL), wheelFR_(wheelFR), gps_(gps), apps_(apps) {}

void AutoShifter::begin() {
    pinMode(AUTO_SHIFT_UP_IN_PIN, INPUT_PULLUP);
    pinMode(AUTO_SHIFT_DOWN_IN_PIN, INPUT_PULLUP);
    pinMode(AUTO_SHIFT_UP_OUT_PIN, OUTPUT);
    pinMode(AUTO_SHIFT_DOWN_OUT_PIN, OUTPUT);
    writeOutputs(false, false);
    prevUpIn_ = readUpIn();
    prevDownIn_ = readDownIn();
    prevAutoOn_ = false;
    manualOverride_ = false;
    state_ = State::Idle;
}

void AutoShifter::setConfig(const dc_AutoShiftConfig& cfg, dc_TransmissionType tx, uint32_t engineTeeth) {
    upshiftRpm_ = cfg.upshift_rpm;
    downshiftRpm_ = cfg.downshift_rpm;
    minWheelHz_ = cfg.min_wheel_hz;
    cooldownMs_ = cfg.cooldown_ms;
    istPulseMs_ = cfg.ist_pulse_ms;
    normalDrivePulseMs_ = cfg.normal_drive_pulse_ms;
    normalNeutralPulseMs_ = cfg.normal_neutral_pulse_ms;
    throttleOnPct_ = cfg.throttle_on_pct;
    tx_ = tx;
    engineTeeth_ = (engineTeeth > 0) ? engineTeeth : 1;
    maxGear_ = (tx == dc_TransmissionType_TRANSMISSION_IST) ? 4 : 6;
}

// ── ④ I/O ──────────────────────────────────────────────────────────────

bool AutoShifter::readUpIn() const {
    return digitalRead(AUTO_SHIFT_UP_IN_PIN) == AUTO_SHIFT_IN_ACTIVE;
}

bool AutoShifter::readDownIn() const {
    return digitalRead(AUTO_SHIFT_DOWN_IN_PIN) == AUTO_SHIFT_IN_ACTIVE;
}

void AutoShifter::writeOutputs(bool up, bool down) {
    digitalWrite(AUTO_SHIFT_UP_OUT_PIN, up ? AUTO_SHIFT_OUT_ACTIVE : AUTO_SHIFT_OUT_INACTIVE);
    digitalWrite(AUTO_SHIFT_DOWN_OUT_PIN, down ? AUTO_SHIFT_OUT_ACTIVE : AUTO_SHIFT_OUT_INACTIVE);
}

float AutoShifter::engineRpm() const {
    return engine_.getFrequencyHz() / static_cast<float>(engineTeeth_) * 60.0f;
}

float AutoShifter::wheelHz() const {
    return (wheelFL_.getFrequencyHz() + wheelFR_.getFrequencyHz()) * 0.5f;
}

bool AutoShifter::moving() const {
    return wheelHz() >= minWheelHz_;
}

// ── ① 判断ロジック (純粋判断, 副作用なし。将来の拡張点) ────────────────────

AutoShifter::Dir AutoShifter::evaluate() const {
    int8_t gear = gps_.getGear();
    if (gear < 0)  // ギア不明
        return Dir::None;

    float rpm = engineRpm();
    bool onThrottle = static_cast<float>(apps_.convertedValue()) >= throttleOnPct_;

    // UP: アクセルオン + 回転上 + ギア範囲内 (1..maxGear-1) + 走行中
    if (onThrottle && rpm >= upshiftRpm_ && gear >= 1 && gear < maxGear_ && moving())
        return Dir::Up;
    // DOWN: アクセルオフ + 回転下 + 2速以上 (速度ゲートなし: 1速まで落とし切る)
    if (!onThrottle && rpm <= downshiftRpm_ && gear >= 2)
        return Dir::Down;
    return Dir::None;
}

// ── ③ 出力整形 ──────────────────────────────────────────────────────────

uint32_t AutoShifter::pulseWidthFor(int8_t gear, Dir dir) const {
    if (tx_ == dc_TransmissionType_TRANSMISSION_IST)
        return istPulseMs_;  // N が隣接ギア間に挟まらないので共通幅
    // NORMAL (パターン 1-N-2-3-4-5-6)
    bool toNeutral = !moving() && ((gear == 1 && dir == Dir::Up) || (gear == 2 && dir == Dir::Down));
    return toNeutral ? normalNeutralPulseMs_ : normalDrivePulseMs_;
}

void AutoShifter::startPulse(Dir dir, int8_t gear, unsigned long now) {
    activePulseMs_ = pulseWidthFor(gear, dir);
    writeOutputs(dir == Dir::Up, dir == Dir::Down);
    state_ = State::Pulsing;
    stateEnteredMs_ = now;
    SerialProtocol::sendDebugf("autoshift: %s gear=%d w=%lums", (dir == Dir::Up) ? "UP" : "DOWN", (int)gear,
                               (unsigned long)activePulseMs_);
}

// ── update: ③時間進行 → ④入力追跡 → ②調停 ─────────────────────────────

void AutoShifter::update(bool autoOn) {
    unsigned long now = millis();

    // ③ 出力状態機械の時間進行
    switch (state_) {
        case State::Pulsing:
            if (now - stateEnteredMs_ >= activePulseMs_) {
                writeOutputs(false, false);
                state_ = State::Cooldown;
                stateEnteredMs_ = now;
            }
            break;
        case State::Cooldown:
            if (now - stateEnteredMs_ >= cooldownMs_)
                state_ = State::Idle;
            break;
        case State::Idle:
            break;
    }

    // 走行状況 (副作用なし)。manualWindow = 停車中の低速ギア帯 (不明 -1 / N / 1 / 2):
    // auto 中でもドライバーに渡す既存のハンドオフ帯 (§5)。ギア不明を含めるのは
    // センサノイズ時に手動シフトを殺さないため。
    int8_t gear = gps_.getGear();
    bool stopped = !moving();
    bool manualWindow = stopped && gear <= 2;

    // ④ 入力エッジ追跡 (毎ティック更新)。Pulsing/Cooldown 中の押下は破棄される。
    bool curUp = readUpIn();
    bool curDown = readDownIn();
    bool upEdge = curUp && !prevUpIn_;
    bool downEdge = curDown && !prevDownIn_;
    prevUpIn_ = curUp;
    prevDownIn_ = curDown;

    // CAN オートシフト指令 (autoOn) の ON/OFF 遷移を検出
    bool autoRising = autoOn && !prevAutoOn_;  // 手動→auto
    bool modeChanged = (autoOn != prevAutoOn_);
    prevAutoOn_ = autoOn;

    // CAN が 手動→auto に切り替わったら手動オーバーライドを解除し auto に復帰。
    if (autoRising && manualOverride_) {
        manualOverride_ = false;
        SerialProtocol::sendDebugf("autoshift: manual override cleared (CAN manual->auto)");
    }

    if (modeChanged) {
        SerialProtocol::sendDebugf("autoshift: CAN=%s", autoOn ? "auto" : "manual");
        // 切替直後の「押しっぱ」誤発火を防ぐためエッジを無効化
        upEdge = false;
        downEdge = false;
    }

    if (state_ != State::Idle)
        return;

    // ② 調停: 手動 (CAN manual / 手動オーバーライド / 停車低速帯) か auto か。
    bool manual = !autoOn || manualOverride_ || manualWindow;

    Dir req = Dir::None;
    if (manual) {
        // ドライバーのエッジ (UP 優先)。本機は判断せず整形のみ。
        if (upEdge)
            req = Dir::Up;
        else if (downEdge)
            req = Dir::Down;
    } else if (upEdge || downEdge) {
        // auto 実行中 (走行中 or gear>2) にドライバーが手動シフト → その場で手動へラッチし、
        // このエッジでシフトを実行 (同ティックで切替+シフト)。以降 CAN が 手動→auto に
        // 遷移するまで手動として振る舞う (Pulsing/Cooldown 中の押下はここに来ず破棄される)。
        manualOverride_ = true;
        SerialProtocol::sendDebugf("autoshift: manual override (driver shifted during auto)");
        req = upEdge ? Dir::Up : Dir::Down;
    } else {
        // auto: ① 判断ロジック
        req = evaluate();
    }

    if (req != Dir::None)
        startPulse(req, gear, now);
}

}  // namespace shift
