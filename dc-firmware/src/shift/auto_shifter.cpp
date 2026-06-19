#include "auto_shifter.hpp"

#include "serial/serial_protocol.hpp"

namespace shift {

AutoShifter::AutoShifter(const PulseCounter& engine,
                         const PulseCounter& wheelFL,
                         const PulseCounter& wheelFR,
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

    // ④ 入力エッジ追跡 (毎ティック更新)。Pulsing/Cooldown 中の押下は破棄される。
    bool curUp = readUpIn();
    bool curDown = readDownIn();
    bool modeChanged = (autoOn != prevAutoOn_);
    bool upEdge = curUp && !prevUpIn_;
    bool downEdge = curDown && !prevDownIn_;
    prevUpIn_ = curUp;
    prevDownIn_ = curDown;
    prevAutoOn_ = autoOn;
    if (modeChanged) {
        SerialProtocol::sendDebugf("autoshift: mode=%s", autoOn ? "ON" : "OFF");
        // モード切替直後は「押しっぱ」の誤発火を防ぐためエッジを無効化
        upEdge = false;
        downEdge = false;
    }

    if (state_ != State::Idle)
        return;

    // ② 調停: requestDir をどこから取るか
    int8_t gear = gps_.getGear();
    bool stopped = !moving();
    // 停車中の低速ギア帯 (不明 -1 / N 0 / 1 / 2) は auto を止めてドライバーに渡す。
    // 不明を含めるのは、ギアセンサがノイズ/未キャリブで -1 のときに手動シフトを
    // 殺さない (デッドゾーン回避) ため。
    bool manualWindow = autoOn && stopped && gear <= 2;

    Dir req = Dir::None;
    if (!autoOn || manualWindow) {
        // OFF (manual) / ON+停車+低速ギア帯: ドライバーのエッジ (UP 優先)
        if (upEdge)
            req = Dir::Up;
        else if (downEdge)
            req = Dir::Down;
    } else {
        // ON (auto): ① 判断ロジック
        req = evaluate();
    }

    if (req != Dir::None)
        startPulse(req, gear, now);
}

}  // namespace shift
