#pragma once

#include <stdint.h>

// loop() の「N ms ごとに」を表す。due(now) は前回成立から periodMs 経っていれば true を返し、起点を now に進める。
//   Every canTx{CAN_TX_INTERVAL_MS};
//   if (canTx.due(now)) canController.send();
class Every {
   public:
    explicit constexpr Every(uint32_t periodMs) : periodMs_(periodMs) {}

    bool due(uint32_t nowMs) {
        if ((uint32_t)(nowMs - lastMs_) < periodMs_)
            return false;
        lastMs_ = nowMs;
        return true;
    }

    // 起点だけを now に合わせる (最初の成立を periodMs 後にしたいとき)
    void restart(uint32_t nowMs) { lastMs_ = nowMs; }

   private:
    uint32_t periodMs_;
    uint32_t lastMs_ = 0;
};
