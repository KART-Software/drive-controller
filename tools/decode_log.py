#!/usr/bin/env python3
"""drive-controller の SD ログ (dc_log_*.bin, バイナリ固定長) を CSV へデコードする。

使い方:
    python3 tools/decode_log.py dc_log_20260624_153012.bin            # -> dc_log_..._153012.csv
    python3 tools/decode_log.py dc_log_20260624_153012.bin out.csv

フォーマット (firmware: dc-firmware/src/util/log/sensor_logger.hpp と一致):
  Header (24B, little-endian, packed):
    char magic[8]="KARTLOG1"; u16 version; u16 record_size;
    u32 rtc_epoch; u32 log_hz; u32 boot_ms
  Record (record_size B/レコード, little-endian, packed):
    u32 t_ms; u16 adc[8]; f32 accel[3]; f32 gyro[3];
    f32 wheel[4]; f32 rpm; f32 clutch_rpm; f32 target_tp; f32 clutch;
    u32 errors; i8 gear; u8 mode; u16 _pad
  ADC は生値 (キャリブレーションは別途 config で換算)。IMU は mg / dps (平均後)。
  mode: 0=Calib 1=Normal 2=Restrict 3=MotorOff (EtcTarget::Mode 順, 要確認)。
"""
import csv
import struct
import sys

HEADER_FMT = "<8sHHIII"
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 24
# v2 (128B): t_ms, adc[8], apps1/apps2/ittr/tps1/tps2/bps, accel[3], gyro[3],
#            wheel[4], rpm, clutch_rpm, target_tp, clutch, wheel_count[4],
#            errors, flags, gear, mode, autoshift_state, pad[3]
RECORD_FMT = "<I8H6f3f3f4f4f4IIHbBB3x"
RECORD_SIZE = struct.calcsize(RECORD_FMT)  # 128

# flags ビット (firmware: log_record.hpp LOG_FLAG_*)
FLAG_BITS = [
    ("f_manual", 0), ("f_ittr", 1), ("f_shutdown", 2),
    ("f_valid", 3), ("f_autoshift", 4), ("f_launch", 5),
]

COLUMNS = (
    ["t_ms", "abs_time"]
    + [f"adc{i}" for i in range(8)]
    + ["apps1", "apps2", "ittr", "tps1", "tps2", "bps"]
    + ["ax", "ay", "az", "gx", "gy", "gz"]
    + ["wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr", "rpm", "clutch_rpm"]
    + ["target_tp", "clutch"]
    + ["wc_fl", "wc_fr", "wc_rl", "wc_rr"]
    + ["errors", "flags"]
    + [name for name, _ in FLAG_BITS]
    + ["gear", "mode", "autoshift_state"]
)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    in_path = argv[1]
    out_path = argv[2] if len(argv) > 2 else in_path.rsplit(".", 1)[0] + ".csv"

    with open(in_path, "rb") as f:
        hdr = f.read(HEADER_SIZE)
        magic, ver, rec_size, rtc_epoch, log_hz, boot_ms = struct.unpack(HEADER_FMT, hdr)
        if magic != b"KARTLOG1":
            print(f"bad magic: {magic!r}", file=sys.stderr)
            return 2
        if rec_size != RECORD_SIZE:
            print(
                f"record_size mismatch: file={rec_size} decoder={RECORD_SIZE} "
                f"(version {ver}). デコーダのフォーマットを合わせてください。",
                file=sys.stderr,
            )
            return 3
        print(f"version={ver} record_size={rec_size} rtc_epoch={rtc_epoch} "
              f"log_hz={log_hz} boot_ms={boot_ms}", file=sys.stderr)

        n = 0
        with open(out_path, "w", newline="") as out:
            w = csv.writer(out)
            w.writerow(COLUMNS)
            while True:
                buf = f.read(rec_size)
                if len(buf) < rec_size:
                    break  # 末尾の半端 (電源断時) は捨てる
                v = struct.unpack(RECORD_FMT, buf)
                t_ms = v[0]
                # 絶対時刻 (RTC 設定時のみ): rtc_epoch + (t_ms - boot_ms)/1000
                abs_time = (rtc_epoch + (t_ms - boot_ms) / 1000.0) if rtc_epoch else ""
                adc = v[1:9]
                conv = v[9:15]  # apps1,apps2,ittr,tps1,tps2,bps
                acc = v[15:18]
                gyr = v[18:21]
                wheel = v[21:25]
                rpm, clutch_rpm, target_tp, clutch = v[25:29]
                wc = v[29:33]
                errors, flags, gear, mode, ashift = v[33], v[34], v[35], v[36], v[37]
                flagbits = [(flags >> b) & 1 for _, b in FLAG_BITS]
                w.writerow(
                    [t_ms, abs_time, *adc, *conv, *acc, *gyr, *wheel, rpm, clutch_rpm,
                     target_tp, clutch, *wc, errors, flags, *flagbits, gear, mode, ashift]
                )
                n += 1
        dur = n / log_hz if log_hz else 0
        print(f"{n} records -> {out_path}  (~{dur:.1f}s @ {log_hz}Hz)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
