#pragma once

#include <optional>

#include <LittleFS.h>
#include <pb.h>

#include "util/flash_write_gate.hpp"

#define BEGIN_FS_LIMIT_TIMES 5

class Flash {
   public:
    bool initialize();
    // 書き込み (writeProto / remove / format) の前後で ETC を止めて再開する (§6.8)。null なら待たない
    void setWriteGate(FlashWriteGate* gate) { gate_ = gate; }
    // 複数の書き込みを 1 回の停止でまとめるときに外側で囲む (入れ子可。最も外側でだけ待って再開する)
    bool beginWrite();
    void endWrite();
    bool writeProto(const char* fileName, const pb_msgdesc_t* fields, const void* msg);
    bool readProto(const char* fileName, const pb_msgdesc_t* fields, void* msg);
    // 値で返す版。ファイルが無い / デコード失敗なら nullopt
    template <typename T>
    std::optional<T> readProto(const char* fileName, const pb_msgdesc_t* fields) {
        T msg = {};
        if (!readProto(fileName, fields, &msg))
            return std::nullopt;
        return msg;
    }
    void remove(const char* fileName);
    bool format();  // FS 全消去 (呼び出し側で保持したいデータを事前に RAM へ退避すること)
    size_t usedSize() { return fs.usedSize(); }
    size_t totalSize() { return fs.totalSize(); }

   private:
    LittleFS_Program fs;
    FlashWriteGate* gate_ = nullptr;
    static constexpr size_t kBufSize = 512;
    uint8_t writeDepth_ = 0;
    static constexpr uint32_t kGateTimeoutMs = 1000;
};
