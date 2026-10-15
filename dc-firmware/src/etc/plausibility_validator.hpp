#pragma once

#include <Arduino.h>
#include "error_handler.hpp"
#include "sensor/sensors.hpp"

#define SENSOR_IMPLAUSIBLE_THRESHOLD_TIME 100
#define APPS_TPS_TARGET_IMPLAUSIBLE_THRESHOLD_TIME 1000
#define BPS_TPS_IMPLAUSIBLE_THRESHOLD_TIME 1000
#define SENSOR_SAME_POSITION_THRESHOLD 10.0
#define PLAUSIBLE_DURATION 500

namespace etc {

class PlausibilityValidator {
   public:
    PlausibilityValidator(const Apps& apps1,
                          const Apps& apps2,
                          const Ittr& ittr,
                          const Tps& tps1,
                          const Tps& tps2,
                          const EtcTarget& target,
                          const Bps& bps);
    // now は sysClock の ms (tickISR から渡す)。isValid / isCurrentlyValid は評価してラッチを更新するので
    // tickISR からのみ呼ぶ。loop は副作用なしの validLatched() / currentlyValid() を読む
    void initialize(uint32_t now);
    bool isCurrentlyValid(uint32_t now);
    bool isValid(uint32_t now);
    void setCheckFlags(bool apps,
                       bool tps,
                       bool apps1,
                       bool apps2,
                       bool tps1,
                       bool tps2,
                       bool target,
                       bool bps,
                       bool bpsTps);
    ErrorHandler& getErrorHandler() { return errorHandler; }
    // 副作用なしの const 読み取り (ログ生成等の純粋関数用)。
    // errorBits = ETC エラービットマスク、currentlyValid = 直近に評価した瞬時 plausibility。
    uint16_t errorBits() const { return errorHandler.bits(); }
    bool currentlyValid() const { return lastCurrentlyValid_; }
    // ① ラッチ状態 (一度でも違反すると false。isValid() と違い評価しない = 副作用なし)
    bool validLatched() const { return isValidAllTime; }
    // 同定実験用: target-vs-TPS チェックを一時停止する。config 由来の targetCheckFlag とは独立なので、
    // 実験中に set_config / revert / set_plausibility_flags が来ても停止は維持され、終了時に古い値で
    // フラグを上書きすることもない。再開時は「最後に正常だった時刻」を今にリセットする (停止中の乖離で
    // 即ラッチしないため)。停止中は判定もエラービットの記録も行わない。
    void suspendTargetCheck(bool suspend, uint32_t now);
    bool targetCheckSuspended() const { return targetCheckSuspended_; }
   private:
    // チェック有効フラグ。書き込みは setCheckFlags() だけ (呼び出し元は Configurator: config の適用と
    // set_plausibility_flags コマンド)。同定実験の一時停止は suspendTargetCheck() で、フラグには触れない
    bool appsCheckFlag = false, tpsCheckFlag = false, apps1CheckFlag = false, apps2CheckFlag = false,
         tps1CheckFlag = false, tps2CheckFlag = false, targetCheckFlag = false, bpsCheckFlag = false,
         bpsTpsCheckFlag = false;
    ErrorHandler errorHandler = ErrorHandler();
    const Apps &apps1, &apps2;
    const Ittr& ittr;
    const Tps &tps1, &tps2;
    const EtcTarget& target;
    const Bps& bps;
    bool isValidAllTime;
    bool targetCheckSuspended_ = false;
    bool lastCurrentlyValid_ = true;  // isCurrentlyValid() の最新結果 (const 参照用キャッシュ)
    unsigned long lastTpsPlausibleTime, lastAppsPlausibleTime, lastTps1CircuitValidTime, lastTps2CircuitValidTime,
        lastApps1CircuitValidTime, lastApps2CircuitValidTime, lastAppsTpsTargetValidTime, lastBpsCircuitValidTime,
        lastBpsTpsPlausibleTime;

    void initParameters(uint32_t now);
    // 各チェック。now = 評価中の時刻 (isCurrentlyValid の引数)
    bool isAppsPlausible(uint32_t now);
    bool isTpsPlausible(uint32_t now);
    bool isApps1CircuitValid(uint32_t now);
    bool isApps2CircuitValid(uint32_t now);
    bool isTps1CircuitValid(uint32_t now);
    bool isTps2CircuitValid(uint32_t now);
    bool isAppsTpsTargetValid(uint32_t now);
    bool isBpsCircuitValid(uint32_t now);
    bool isBpsTpsPlausible(uint32_t now);
};

}  // namespace etc

