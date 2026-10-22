#pragma once

#include <stddef.h>
#include <stdint.h>

#include "interfaces/util/log/log_writer.hpp"

// debug メッセージを USB シリアルへ COBS フレームで送る (SerialProtocol::sendDebugv)。
//
// - loop / setup から: その場で送る (先に、ためてあった分を送って順序を保つ)。
// - ISR の中、または割り込み禁止中 (noInterrupts 区間): 固定長のキューに書式化して積むだけにし、
//   loop の service() (または次の loop からの writev()) が送る。USB の送信は割り込みで進むので、
//   その場で送ると送信バッファの空き待ちで固まったり、loop の送信と取り合って壊れたりする。
// - USB がつながっていない間 (ホストがポートを開いていない、!Serial): 同じくキューに積み、つながってから送る。
//   起動時のメッセージを、後から開いた console に届けるため (起動時に接続を待たない)。
// - キュー (96 文字 × 16) が満杯なら新しいほうを捨てて数え (古いもの = 起動時のメッセージが残る)、
//   次に送れたときに捨てた件数を出す (時刻は最後に捨てたメッセージのもの)。
class SerialDebugWriter : public LogWriter {
   public:
    void writev(uint64_t timestampUs, const char* fmt, va_list args) override;
    void service() override;

   private:
    void enqueue(uint64_t timestampUs, const char* fmt, va_list args);
    void send(uint64_t timestampUs, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    // 書き手 (ISR / 割り込み禁止中) は積む間だけ割り込みを止めるので、優先度の違う ISR から同時に
    // 呼ばれても壊れない。読み手は loop だけ (tail を進めるのも loop だけ)
    static constexpr uint32_t kSlots = 16;  // 2 のべき乗
    static constexpr size_t kLen = 96;
    struct Slot {
        uint64_t timestampUs;
        char text[kLen];
    };
    Slot slots_[kSlots];
    volatile uint32_t head_ = 0;
    volatile uint32_t tail_ = 0;
    volatile uint32_t dropped_ = 0;
    volatile uint64_t lastDroppedUs_ = 0;
    uint32_t reportedDropped_ = 0;  // loop だけが触る
};
