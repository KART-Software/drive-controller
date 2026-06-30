#pragma once

#include <stddef.h>

// バイナリログの出力先インターフェース (テキスト用 LogWriter のバイナリ版)。
// SD カードや将来の他媒体 (USB バルク等) を抽象化する。SensorLogger が本 writer へ
// ヘッダ/レコードを書き出す。SerialDebugWriter ↔ LogWriter と同じ関係を
// SdBinaryWriter ↔ BinaryWriter が担う。
class BinaryWriter {
   public:
    virtual ~BinaryWriter() = default;
    virtual bool begin() = 0;                            // 初期化/オープン。使用不可なら false
    virtual void write(const void* data, size_t len) = 0;
    virtual void flush() = 0;                            // 媒体へ確実に反映 (sync)
    virtual bool active() const = 0;
};
