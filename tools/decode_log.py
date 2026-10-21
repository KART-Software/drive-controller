#!/usr/bin/env python3
"""drive-controller の SD ログ (dc_NNNN[_日時]/NNNN.bin、バイナリ固定長) を CSV にする。

使い方:
    python3 tools/decode_log.py dc_0003_20261020_153012/          # ディレクトリ → dc_0003_20261020_153012.csv
    python3 tools/decode_log.py dc_0003_20261020_153012/ out.csv
    python3 tools/decode_log.py dc_0003/0001.bin                   # 1 ファイルだけ → 0001.csv

ディレクトリを渡すと、中の NNNN.bin を file_index の順につないで 1 つの CSV にする (1 回の起動分)。

フォーマット (firmware: dc-firmware/src/util/log/log_record.hpp、docs/loop_nonblocking_spec.md §7.2 が一次ソース。
開発中は version = 0 のまま):
  Header (32 B, little-endian, packed):
    char magic[8]="KARTLOG1"; u16 version; u16 record_size; u16 log_hz; u16 dir_number; u16 file_index;
    u16 reserved; u32 rtc_epoch (0=RTC 未設定); u64 rtc_t_us (rtc_epoch を読んだ瞬間の t_us)
  Record (152 B):
    u64 t_us; u16 adc[8]; f32 apps1 apps2 ittr tps1 tps2 bps clutch; f32 accel[3]; f32 gyro[3];
    f32 wheel_hz[4]; f32 engine_hz; f32 clutch_hz; u32 wheel_count[4]; f32 target_tp; f32 duty;
    u32 errors; u16 flags; i8 gear; u8 mode; u8 autoshift_state; u8 exp_type exp_index exp_phase;
    u32 loop_max_us; u32 sd_max_us; u16 tick_max_us; u16 log_drops; u8 can_fault can_tec can_rec reserved
  t_us は起動からの µs (PIT Lifetime Timer、割り込みが止まっても進む)。絶対時刻 = rtc_epoch + (t_us - rtc_t_us) / 1e6。
  ADC は生値 (キャリブレーションは別途 config で換算)。IMU は mg / dps (平均後)。回転はパルスの Hz (rpm は歯数で換算)。
  mode: EtcTarget::Mode。exp_type: 0=none 1=dwell 2=release 3=step。exp_phase: 0=idle 1=settling 2=holding 3=coasting。
  loop_max_us / sd_max_us / tick_max_us / log_drops は直前の 1 s 窓と現在の窓のここまでの大きい方 (§5)。
  can_fault: 0=active 1=passive 2=bus-off。can_tec / can_rec はその tick のエラーカウンタ。

終端: ファイルは 100 MB を事前確保しているので、書かれた分より後ろはゼロ (exFAT の有効な長さ)。全バイトがゼロのレコード、
ファイルの半端、t_us の後退 (減る) で止める。t_us の飛び (欠け) は止めずに stderr へ出す (リングの溢れ、flash 保存の約 150 ms、
ログの停止と再開で正常に起きる)。
"""
import csv
import os
import struct
import sys

MAGIC = b"KARTLOG1"
VERSION = 0
HEADER_FMT = "<8sHHHHHHIQ"
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 32
RECORD_FMT = "<Q8H7f3f3f4fff4IffIHbBBBBBIIHHBBBB"
RECORD_SIZE = struct.calcsize(RECORD_FMT)  # 152
GAP_WARN_US = 1_500  # これより飛んだら欠けとして報告する (止めない)

# flags ビット (firmware: log_record.hpp LOG_FLAG_*)
FLAG_BITS = [
    ("f_manual", 0), ("f_ittr", 1), ("f_shutdown", 2),
    ("f_valid", 3), ("f_autoshift", 4), ("f_launch", 5),
    ("f_starter_relay", 6), ("f_starter_switch", 7),
]

COLUMNS = (
    ["t_us", "t_s", "abs_time"]
    + [f"adc{i}" for i in range(8)]
    + ["apps1", "apps2", "ittr", "tps1", "tps2", "bps", "clutch"]
    + ["ax", "ay", "az", "gx", "gy", "gz"]
    + ["wheel_fl_hz", "wheel_fr_hz", "wheel_rl_hz", "wheel_rr_hz", "engine_hz", "clutch_hz"]
    + ["wc_fl", "wc_fr", "wc_rl", "wc_rr"]
    + ["target_tp", "duty"]
    + ["errors", "flags"]
    + [name for name, _ in FLAG_BITS]
    + ["gear", "mode", "autoshift_state"]
    + ["exp_type", "exp_index", "exp_phase"]
    + ["loop_max_us", "sd_max_us", "tick_max_us", "log_drops"]
    + ["can_fault", "can_tec", "can_rec"]
)


def log_files(path):
    """ディレクトリなら中の NNNN.bin を名前順に、ファイルならそれだけ"""
    if os.path.isdir(path):
        names = sorted(n for n in os.listdir(path) if n.lower().endswith(".bin"))
        return [os.path.join(path, n) for n in names]
    return [path]


def default_out(path):
    base = os.path.basename(os.path.normpath(path))
    if not os.path.isdir(path):
        base = base.rsplit(".", 1)[0]
    return base + ".csv"


def read_header(f, name):
    hdr = f.read(HEADER_SIZE)
    if len(hdr) < HEADER_SIZE:
        return None  # 空 (作っただけで sync 前に電源が切れた) か半端
    magic, ver, rec_size, log_hz, dir_number, file_index, _res, rtc_epoch, rtc_t_us = struct.unpack(HEADER_FMT, hdr)
    if magic != MAGIC:
        raise ValueError(f"{name}: bad magic {magic!r}")
    if ver != VERSION or rec_size != RECORD_SIZE:
        raise ValueError(f"{name}: version={ver} record_size={rec_size} (デコーダは version {VERSION} / {RECORD_SIZE} B)")
    return {"log_hz": log_hz, "dir_number": dir_number, "file_index": file_index,
            "rtc_epoch": rtc_epoch, "rtc_t_us": rtc_t_us}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    in_path = argv[1]
    out_path = argv[2] if len(argv) > 2 else default_out(in_path)
    files = log_files(in_path)
    if not files:
        print(f"no .bin files in {in_path}", file=sys.stderr)
        return 2

    zero_record = b"\0" * RECORD_SIZE
    total = 0
    gaps = 0
    prev_t = None
    log_hz = 0
    with open(out_path, "w", newline="") as out:
        w = csv.writer(out)
        w.writerow(COLUMNS)
        for name in files:
            with open(name, "rb") as f:
                h = read_header(f, name)
                if h is None:
                    print(f"{name}: empty (no header), skipped", file=sys.stderr)
                    continue
                log_hz = h["log_hz"]
                rtc_epoch, rtc_t_us = h["rtc_epoch"], h["rtc_t_us"]
                print(f"{name}: dir {h['dir_number']} file {h['file_index']} rtc_epoch={rtc_epoch}", file=sys.stderr)
                n = 0
                stop = None
                while True:
                    buf = f.read(RECORD_SIZE)
                    if len(buf) < RECORD_SIZE:
                        stop = "end of file"
                        break
                    if buf == zero_record:
                        stop = "zero record (end of valid data)"
                        break
                    v = struct.unpack(RECORD_FMT, buf)
                    t_us = v[0]
                    if prev_t is not None:
                        if t_us <= prev_t:
                            stop = f"t_us went backwards {prev_t} -> {t_us}"
                            break
                        if t_us - prev_t > GAP_WARN_US:
                            gaps += 1
                            print(f"  gap: {(t_us - prev_t) / 1e6:.3f} s at t={prev_t / 1e6:.3f} s", file=sys.stderr)
                    prev_t = t_us
                    abs_time = (rtc_epoch + (t_us - rtc_t_us) / 1e6) if rtc_epoch else ""
                    adc = v[1:9]
                    conv = v[9:16]       # apps1 apps2 ittr tps1 tps2 bps clutch
                    acc = v[16:19]
                    gyr = v[19:22]
                    wheel = v[22:26]
                    engine_hz, clutch_hz = v[26], v[27]
                    wc = v[28:32]
                    target_tp, duty = v[32], v[33]
                    errors, flags, gear, mode, ashift = v[34], v[35], v[36], v[37], v[38]
                    exp_type, exp_index, exp_phase = v[39], v[40], v[41]
                    sysv = v[42:46]
                    canv = v[46:49]  # can_fault can_tec can_rec (v[49] は reserved)
                    flagbits = [(flags >> b) & 1 for _, b in FLAG_BITS]
                    w.writerow(
                        [t_us, t_us / 1e6, abs_time, *adc, *conv, *acc, *gyr, *wheel, engine_hz, clutch_hz,
                         *wc, target_tp, duty, errors, flags, *flagbits, gear, mode, ashift,
                         exp_type, exp_index, exp_phase, *sysv, *canv]
                    )
                    n += 1
                total += n
                print(f"  {n} records ({stop})", file=sys.stderr)
    dur = total / log_hz if log_hz else 0
    print(f"{total} records, {gaps} gaps -> {out_path}  (~{dur:.1f}s @ {log_hz}Hz)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
