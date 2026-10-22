#pragma once

#include <Arduino.h>

#include <optional>

#include "can/can_data.hpp"
#include "sensor/sensors.hpp"

// ドライバー指令 (コックピットスイッチ) の唯一の入口 (docs/control_input_spec.md)。
// 入力元は CAN (data_logger → 0x740 Control / 0x741 Shift) と、ベンチ用の console override。
// 消費側 (main のモード反映・アーミング③、AutoShifter、ExperimentRunner、ログ、telemetry) は
// このクラスだけを見て、入力が CAN から来たのか override なのかを知らない。
//
// 安全規則はすべてこのクラスにある (CanRxData は受信した値と時刻を持つだけ):
//   ETC モードの決定順
//     1. console override が有効で ETC モードを指定していればその値
//     2. 0x740 を起動後に一度も受信していなければ MotorOff (ETC 停止で待つ)
//     3. CAN の値。UNSPECIFIED (0) や未知値のフレームはモードを変えない (ハートビート扱い)
//   0x740 が CAN_CONTROL_TIMEOUT_MS 途絶 → モードは Normal (MotorOff はラッチして保持)、
//     launch / オートシフト / セル = false
//   0x741 が CAN_SHIFT_TIMEOUT_MS 途絶 → 両パドル false
//
// update() は tickISR から 1 ms ごと。時刻で判定する getter (途絶・経過時間) は呼ぶ側が時刻を渡す。
// 受信時刻 (CanRxData) は CAN 受信割り込みがその場の時刻で刻むので、渡された時刻より新しいことがある
// (loop が時刻を読んだ後に受信した場合など)。その差は符号付きで扱い、新しいものは経過 0 とみなす。
// setOverride / clearOverride は loop (コマンド) から呼ばれ、中で割り込みを禁止して書き換える。
class ControlInput {
   public:
    enum class Source : uint8_t { WaitingForCan, Can, Override };

    // console からの上書き (RAM のみ、再起動で解除)。未指定の項目は、上書き中なら今の上書き値を保ち、
    // そうでなければ CAN 側の値を使う
    struct Override {
        bool hasEtcMode;
        EtcTarget::Mode etcMode;
        bool hasAutoShift;
        bool autoShift;
    };

    explicit ControlInput(const CanRxData& rx) : rx_(rx) {}

    // tickISR から毎回。新しい 0x740 の取り込み、途絶時のフォールバック、
    // override の自動解除 (override 中に 0x740 を受信し始めたら解除) を行う
    void update(uint32_t nowMs);

    // 0x740 を受信している間は受け付けない (車載時にドライバーのスイッチを上書きしないため)
    bool setOverride(const Override& o, uint32_t nowMs);
    void clearOverride();

    EtcTarget::Mode etcMode() const;
    bool autoShiftOn(uint32_t nowMs) const;
    bool launchOn(uint32_t nowMs) const { return controlLinkAlive(nowMs) && rx_.launchActive; }  // launch は凍結中
    bool shiftUpPressed(uint32_t nowMs) const { return shiftLinkAlive(nowMs) && rx_.shiftUp; }
    bool shiftDownPressed(uint32_t nowMs) const { return shiftLinkAlive(nowMs) && rx_.shiftDown; }
    // セルスイッチ (0x740 byte3)。未受信・途絶時は false
    bool starterPressed(uint32_t nowMs) const { return controlLinkAlive(nowMs) && rx_.starter; }

    // CAN 側の値 (override を適用する前。telemetry 表示用)
    std::optional<EtcTarget::Mode> canEtcMode() const;  // 未受信は nullopt
    bool canAutoShift(uint32_t nowMs) const { return controlLinkAlive(nowMs) && rx_.autoShiftActive; }

    Source source() const;
    bool controlLinkAlive(uint32_t nowMs) const;    // 0x740 を CAN_CONTROL_TIMEOUT_MS 以内に受信している
    bool shiftLinkAlive(uint32_t nowMs) const;      // 0x741 を CAN_SHIFT_TIMEOUT_MS 以内に受信している
    uint32_t controlRxAgeMs(uint32_t nowMs) const;  // 最後に 0x740 を受信してからの経過 (未受信は 0)

   private:
    bool setOverrideLocked(const Override& o, uint32_t nowMs);

    const CanRxData& rx_;
    uint32_t seenControlFrames_ = 0;
    EtcTarget::Mode canMode_ = EtcTarget::Mode::Normal;  // 取り込み済みの CAN のモード (フォールバック適用後)
    volatile bool overrideActive_ = false;
    Override override_ = {
        .hasEtcMode = false, .etcMode = EtcTarget::Mode::MotorOff, .hasAutoShift = false, .autoShift = false};
};
