#!/usr/bin/env python3
"""2 つ以上の Intel HEX を 1 つにまとめる (dc-boot + dc-main + dc-sd-reader を 1 回で書くため)。

使い方:
    python3 tools/merge_hex.py dc-firmware/dc-main/.pio/build/teensy41/firmware.hex dc-firmware/dc-sd-reader/.pio/build/teensy41/firmware.hex out.hex

teensy_loader_cli は hex に無いブロックを飛ばすが、先頭 1 KB (アドレス 0) だけは必ず書く (無ければ 0xFF で = A の FCB が消える)。
だから B だけを書くと A が起動しなくなる。A と B を 1 つの hex にして書くか、B → A の順で書くこと。
アドレスが重なったらエラー。
出力は flash 先頭からのオフセット (0x60000000 を引く) で書く: PlatformIO 同梱の teensy_loader_cli (2.2) は拡張アドレスが
ちょうど 0x60000000 のときしか引かないため。オフセットならどの版でも通る。同じ 2.2 は **1 MB 以上のオフセットを parse error にする**
ので、A (512 KB 以内) も B (0x80000〜) も 1 MB 以内に置いてある。
"""
import sys


def parse(path):
    data = {}
    base = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        b = bytes.fromhex(line[1:])
        n, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
        payload = b[4:4 + n]
        if typ == 0x04:
            base = ((payload[0] << 8) | payload[1]) << 16
        elif typ == 0x00:
            for i, v in enumerate(payload):
                a = base + addr + i
                if a in data and data[a] != v:
                    raise SystemExit(f"{path}: overlap at {a:#x}")
                data[a] = v
        elif typ == 0x01:
            break
    return data


FLEXSPI_BASE = 0x60000000


def emit(data, path):
    data = {(a - FLEXSPI_BASE if a >= FLEXSPI_BASE else a): v for a, v in data.items()}
    out = []
    cur_hi = None
    addrs = sorted(data)
    i = 0
    while i < len(addrs):
        a = addrs[i]
        hi = a >> 16
        if hi != cur_hi:
            cur_hi = hi
            rec = bytes([2, 0, 0, 4, hi >> 8, hi & 0xFF])
            out.append(rec)
        chunk = [data[a]]
        j = i + 1
        while j < len(addrs) and addrs[j] == addrs[j - 1] + 1 and len(chunk) < 16 and (addrs[j] >> 16) == hi:
            chunk.append(data[addrs[j]])
            j += 1
        lo = a & 0xFFFF
        rec = bytes([len(chunk), lo >> 8, lo & 0xFF, 0]) + bytes(chunk)
        out.append(rec)
        i = j
    out.append(bytes([0, 0, 0, 1]))
    with open(path, "w") as f:
        for rec in out:
            cs = (-sum(rec)) & 0xFF
            f.write(":" + rec.hex().upper() + f"{cs:02X}\n")


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    merged = {}
    for p in argv[1:-1]:
        d = parse(p)
        for a, v in d.items():
            if a in merged and merged[a] != v:
                raise SystemExit(f"overlap at {a:#x} between inputs")
            merged[a] = v
        lo, hi = min(d), max(d)
        print(f"{p}: {lo:#x}..{hi:#x} ({len(d)} bytes)")
    emit(merged, argv[-1])
    print(f"-> {argv[-1]} ({len(merged)} bytes)")


if __name__ == "__main__":
    main(sys.argv)
