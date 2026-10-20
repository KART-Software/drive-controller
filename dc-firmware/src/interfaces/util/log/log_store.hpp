#pragma once

#include <stddef.h>
#include <stdint.h>

// ログの媒体とファイルの操作 (docs/loop_nonblocking_spec.md §7.8)。SensorLogger が使う。レコードの形は知らない。
// すべて loop から呼ぶ (止まってよい)。false を返したら、その起動では使えない (SensorLogger が止める)。
class LogStore {
   public:
    virtual ~LogStore() = default;

    // 媒体の初期化とマウント (必要ならフォーマット) と、起動ごとのディレクトリの作成
    virtual bool begin() = 0;
    // 次のファイルを作って bytes を確保する。開いているファイルがあれば sync して閉じる。
    // 満杯なら一番古いファイルを消してやり直す
    virtual bool openNextFile(uint64_t bytes) = 0;
    virtual bool write(const void* data, size_t len) = 0;
    virtual bool sync() = 0;       // 媒体へ反映する (電源断で失う分を限る)
    virtual void closeFile() = 0;  // sync して閉じる (開いていなければ何もしない)
    virtual bool format() = 0;     // 媒体全体を消す (中身は失われる)。終わったら begin() からやり直す

    virtual uint16_t dirNumber() const = 0;  // 今のディレクトリの番号 (ヘッダ用)
    virtual uint16_t fileIndex() const = 0;  // 今のファイルの連番 (ヘッダ用)
};
