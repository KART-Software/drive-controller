#include "app/loop.hpp"

#include "serial/serial_protocol.hpp"
#include "util/log/debug_logger.hpp"

namespace app {

void loop(const LoopDeps& d) {
    d.stats.tick();
    const uint32_t now = d.clock.nowMs();
    // 計測窓の確定 (1 s)。tick 直後に置くので、直前の周 (= tick が計上した周) とその周の SD 所要が
    // 同じ窓に入る (末尾に置くと SD 所要だけ 1 窓先行して組み合わせがずれる)
    if (d.statsWindow.due(now)) {
        d.stats.rollover(d.logger.drops());
    }

#if defined(LAUNCH_CONTROL_ENABLED)
    // Launch FSM tick — 20Hz (pulse counter 周期 100ms と整合)
    // Plausibility 違反 / MOTOR_OFF 時は launchRequested を強制 false にして
    // FSM を Idle に戻す (handler 側の !launchRequested パスで motor_.off() 経由)。
    if (d.launchTick.due(now)) {
        const bool safe = d.plausibility.currentlyValid() && d.control.etcMode() != EtcTarget::Mode::MotorOff;
        d.launch.update(safe && d.control.launchOn(now), now);
    }
#endif

    if (d.telemetry.due(now)) {
        SerialProtocol::sendSensorData(d.hub, d.plausibility.validLatched(), d.plausibility.getErrorHandler(),
                                       d.motor.lastOutput(), d.stats.live(), d.control, d.can.busState(),
                                       d.can.busStats(), now);
    }

    d.logger.service(now);  // ここで loop が止まってもリング (≈ 2 s) が吸収する
    DebugLogger::service();  // ISR が積んだ debug を送る
    d.router.poll();
}

}  // namespace app
