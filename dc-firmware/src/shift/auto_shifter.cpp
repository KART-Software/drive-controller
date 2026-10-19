#include "auto_shifter.hpp"

#include "util/log/debug_logger.hpp"

namespace shift {

AutoShifter::AutoShifter(const RotationSensor& engine,
                         const RotationSensor& wheelFL,
                         const RotationSensor& wheelFR,
                         const GearPositionSensor& gps,
                         const Apps& apps)
    : engine_(engine), wheelFL_(wheelFL), wheelFR_(wheelFR), gps_(gps), apps_(apps) {}

void AutoShifter::begin() {
    pinMode(AUTO_SHIFT_UP_OUT_PIN, OUTPUT);
    pinMode(AUTO_SHIFT_DOWN_OUT_PIN, OUTPUT);
    writeOutputs(false, false);
    prevUpIn_ = false;  // パドル入力は CAN (0x741)。未受信の間は押されていない扱い
    prevDownIn_ = false;
    prevAutoOn_ = false;
    manualOverride_ = false;
    state_ = State::Idle;
}

void AutoShifter::setConfig(const dc_AutoShiftConfig& cfg, dc_TransmissionType tx) {
    upshiftRpm_ = cfg.upshift_rpm;
    downshiftRpm_ = cfg.downshift_rpm;
    minWheelHz_ = cfg.min_wheel_hz;
    minIntervalMs_ = max(cfg.cooldown_ms, (uint32_t)SHIFT_MIN_INTERVAL_MS);
    istPulseMs_ = cfg.ist_pulse_ms;
    normalDrivePulseMs_ = cfg.normal_drive_pulse_ms;
    normalNeutralPulseMs_ = cfg.normal_neutral_pulse_ms;
    throttleOnPct_ = cfg.throttle_on_pct;
    tx_ = tx;
    maxGear_ = (tx == dc_TransmissionType_TRANSMISSION_IST) ? 4 : 6;
}

// ── ④ I/O ──────────────────────────────────────────────────────────────

void AutoShifter::writeOutputs(bool up, bool down) {
    digitalWrite(AUTO_SHIFT_UP_OUT_PIN, up ? AUTO_SHIFT_OUT_ACTIVE : AUTO_SHIFT_OUT_INACTIVE);
    digitalWrite(AUTO_SHIFT_DOWN_OUT_PIN, down ? AUTO_SHIFT_OUT_ACTIVE : AUTO_SHIFT_OUT_INACTIVE);
}

float AutoShifter::engineRpm() const {
    return engine_.rpm();
}

float AutoShifter::wheelHz() const {
    return (wheelFL_.hz() + wheelFR_.hz()) * 0.5f;
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

void AutoShifter::startPulse(Dir dir, int8_t gear, uint32_t nowUs) {
    activePulseMs_ = pulseWidthFor(gear, dir);
    writeOutputs(dir == Dir::Up, dir == Dir::Down);
    state_ = State::Pulsing;
    pulseStartUs_ = nowUs;
    DebugLogger::log("autoshift: %s gear=%d w=%lums", (dir == Dir::Up) ? "UP" : "DOWN", (int)gear,
              (unsigned long)activePulseMs_);
}

// ── ④ 入力 (ドライバーのパドル) ─────────────────────────────────────────────

void AutoShifter::onPaddles(bool upPressed, bool downPressed, uint32_t nowUs) {
    trackPaddles(upPressed, downPressed, nowUs);
}

void AutoShifter::trackPaddles(bool up, bool down, uint32_t nowUs) {
    const bool upEdge = up && !prevUpIn_;
    const bool downEdge = down && !prevDownIn_;
    prevUpIn_ = up;
    prevDownIn_ = down;
    if (upEdge || downEdge)
        onDriverEdge(upEdge, downEdge, nowUs);
}

// ② 調停 (ドライバー操作)。パルス中・最短間隔内の押下は捨てる (ためて後で出すとドライバーの意図と
// 違うタイミングでシフトするため)
void AutoShifter::onDriverEdge(bool upEdge, bool downEdge, uint32_t nowUs) {
    if (state_ != State::Idle || suspended_)
        return;
    // 走行状況 (副作用なし)。manualWindow = 停車中の低速ギア帯 (不明 -1 / N / 1 / 2):
    // auto 中でもドライバーに渡す既存のハンドオフ帯 (§5)。ギア不明を含めるのは
    // センサノイズ時に手動シフトを殺さないため。
    const int8_t gear = gps_.getGear();
    const bool manualWindow = !moving() && gear <= 2;
    const bool manual = !autoOn_ || manualOverride_ || manualWindow;
    if (!manual) {
        // auto 実行中 (走行中 or gear>2) にドライバーが手動シフト → その場で手動へラッチし、
        // このエッジでシフトを実行。以降 CAN が 手動→auto に遷移するまで手動として振る舞う。
        manualOverride_ = true;
        DebugLogger::log("autoshift: manual override (driver shifted during auto)");
    }
    // ドライバーのエッジ (UP 優先)。本機は判断せず整形のみ。
    startPulse(upEdge ? Dir::Up : Dir::Down, gear, nowUs);
}

// ── tick: ③時間進行 → ④入力追跡 → ①自動判断 ─────────────────────────────────

void AutoShifter::tick(const Inputs& in, uint32_t nowUs) {
    // ③ 出力状態機械の時間進行。最短間隔はパルスの開始から数える。
    // 開始時刻は CAN 受信割り込みがその場で刻むことがあり、nowUs (tick の先頭で読んだ時刻) より新しいこともありうる
    // (今は tick と同じ優先度なので起きないが、優先度に頼らない)。差は符号付きで見て、新しければ経過 0 とする。
    // この比較は Pulsing / Cooldown の間だけ毎 ms 行うので、差が 2^31 µs (35.8 分) に届くことはない
    const int32_t sinceStartUs = (int32_t)(nowUs - pulseStartUs_);
    switch (state_) {
        case State::Pulsing:
            if (sinceStartUs >= (int32_t)(activePulseMs_ * 1000)) {
                writeOutputs(false, false);
                state_ = State::Cooldown;
            }
            break;
        case State::Cooldown:
            if (sinceStartUs >= (int32_t)(minIntervalMs_ * 1000))
                state_ = State::Idle;
            break;
        case State::Idle:
            break;
    }

    // オートシフト指令 (autoOn) の ON/OFF 遷移
    const bool autoOn = in.autoOn;
    const bool autoRising = autoOn && !prevAutoOn_;  // 手動→auto
    const bool modeChanged = (autoOn != prevAutoOn_);
    prevAutoOn_ = autoOn;
    autoOn_ = autoOn;
    // CAN が 手動→auto に切り替わったら手動オーバーライドを解除し auto に復帰。
    if (autoRising && manualOverride_) {
        manualOverride_ = false;
        DebugLogger::log("autoshift: manual override cleared (CAN manual->auto)");
    }
    if (modeChanged) {
        DebugLogger::log("autoshift: CAN=%s", autoOn ? "auto" : "manual");
        // 切替直後の「押しっぱ」誤発火を防ぐため、今の押下状態をエッジなしで取り込む
        prevUpIn_ = in.upPressed;
        prevDownIn_ = in.downPressed;
    } else {
        // 押下は通常 onPaddles で処理済み。ここで追うのは 0x741 の途絶で押下が false に戻る場合
        trackPaddles(in.upPressed, in.downPressed, nowUs);
    }

    // ① 自動判断 (auto で、手動に落ちておらず、停車低速帯でもないとき)
    if (state_ != State::Idle || suspended_ || !autoOn || manualOverride_)
        return;
    const int8_t gear = gps_.getGear();
    if (!moving() && gear <= 2)
        return;
    const Dir req = evaluate();
    if (req != Dir::None)
        startPulse(req, gear, nowUs);
}

}  // namespace shift
