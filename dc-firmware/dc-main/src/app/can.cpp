#include "app/can.hpp"

#include "can/can_data.hpp"

namespace app {

void onCanRx(const CanRxDeps& d, const CanFrame& frame) {
    const uint32_t now = d.clock.nowMs();
    d.can.receive(frame, now);
    if (needsImmediateHandling(frame)) {
        const CanRxData& rx = d.can.rxData();
        d.shifter.onPaddles(rx.shiftUp, rx.shiftDown, d.clock.nowUs());  // パルスの開始時刻 = 出力を立てた時刻
    }
}

}  // namespace app
