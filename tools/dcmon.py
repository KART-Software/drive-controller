#!/usr/bin/env python3
# /// script
# requires-python = ">=3.8"
# dependencies = ["protobuf>=5", "pyserial>=3.5"]
# ///
"""drive-controller の簡易モニタ / コマンド送信 (console の代わりに端末から。Linux / Windows / macOS)。

使い方 (uv があれば依存は自動で入る。無ければ `pip install protobuf pyserial` して python3 で):
    uv run tools/dcmon.py                      # debug 行と、1 s ごとの sys (tick / loop / SD / drops)。Ctrl-C で終了
    uv run tools/dcmon.py -t 5 --can --control # 5 s。can / control の中身も出す
    uv run tools/dcmon.py --watch-can          # CAN の fault / TEC / REC が変わるたびに表示 (バスオフ実験用)
    uv run tools/dcmon.py --send reboot mode=1 # コマンドを送って応答を見る (値は整数か小数。mode=1 = SD リーダーへ)
    uv run tools/dcmon.py --send set_control_override etc_mode=2
    uv run tools/dcmon.py --list               # ポート一覧

ポートは自動 (Teensy の VID 0x16C0、PID 0x0483 = KART Drive Controller / 0x04D5 = KART SD Reader)。複数あれば -p で指定。
ワイヤフォーマットは dc-firmware/src/serial と同じ: COBS( protobuf ‖ crc16_le ) 0x00。
proto の descriptor は dc-console の buf で spec/proto から作る (dc-console で pnpm install 済みが前提)。--desc で binpb を直接渡してもよい。
"""
import argparse
from typing import Optional
import os
import random
import shutil
import subprocess
import sys
import time

import serial
import serial.tools.list_ports
from google.protobuf import descriptor_pb2, descriptor_pool, message_factory, text_format

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
KART_VID = 0x16C0
KART_PIDS = {0x0483: "KART Drive Controller", 0x04D5: "KART SD Reader"}


# ---- wire: CRC16-CCITT-FALSE + COBS ----
def _crc_table():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        t.append(c)
    return t


CRC_TABLE = _crc_table()


def crc16(data: bytes) -> int:
    c = 0xFFFF
    for b in data:
        c = ((c << 8) ^ CRC_TABLE[((c >> 8) ^ b) & 0xFF]) & 0xFFFF
    return c


def cobs_decode(b: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(b):
        k = b[i]
        i += 1
        if k == 0 or i + k - 1 > len(b):
            return b""
        out += b[i:i + k - 1]
        i += k - 1
        if k < 0xFF and i < len(b):
            out.append(0)
    return bytes(out)


def cobs_encode(b: bytes) -> bytes:
    out = bytearray([0])
    idx = 0
    code = 1
    for x in b:
        if x == 0:
            out[idx] = code
            idx = len(out)
            out.append(0)
            code = 1
        else:
            out.append(x)
            code += 1
            if code == 0xFF:
                out[idx] = code
                idx = len(out)
                out.append(0)
                code = 1
    out[idx] = code
    return bytes(out)


def frame(payload: bytes) -> bytes:
    c = crc16(payload)
    return cobs_encode(payload + bytes([c & 0xFF, c >> 8])) + b"\x00"


# ---- proto ----
def load_messages(desc_path: Optional[str]):
    if desc_path:
        blob = open(desc_path, "rb").read()
    else:
        pnpm = shutil.which("pnpm")
        if not pnpm:
            raise SystemExit("pnpm が見つかりません。dc-console で pnpm install するか、--desc で descriptor (binpb) を渡してください")
        r = subprocess.run([pnpm, "exec", "buf", "build", os.path.join(ROOT, "spec", "proto"), "-o", "-#format=binpb"],
                           cwd=os.path.join(ROOT, "dc-console"), capture_output=True)
        if r.returncode != 0:
            raise SystemExit("buf build に失敗:\n" + r.stderr.decode(errors="replace"))
        blob = r.stdout
    fds = descriptor_pb2.FileDescriptorSet.FromString(blob)
    pool = descriptor_pool.DescriptorPool()
    for f in fds.file:
        pool.Add(f)
    return (message_factory.GetMessageClass(pool.FindMessageTypeByName("dc.DeviceToHost")),
            message_factory.GetMessageClass(pool.FindMessageTypeByName("dc.HostToDevice")))


# ---- ports ----
def kart_ports():
    return [p for p in serial.tools.list_ports.comports() if p.vid == KART_VID and p.pid in KART_PIDS]


def pick_port(explicit: Optional[str]) -> str:
    if explicit:
        return explicit
    ports = kart_ports()
    if not ports:
        raise SystemExit("KART デバイスが見つかりません (--list で確認)")
    if len(ports) > 1:
        raise SystemExit("KART デバイスが複数あります。-p で指定してください:\n" + "\n".join(f"  {p.device}  {KART_PIDS[p.pid]}" for p in ports))
    return ports[0].device


def parse_value(v: str):
    try:
        return int(v, 0)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="シリアルポート (既定: 自動)")
    ap.add_argument("-t", "--duration", type=float, default=None, help="秒。既定は Ctrl-C まで (--send のときは 3 s)")
    ap.add_argument("--can", action="store_true", help="1 s ごとに can (CanStatus) も出す")
    ap.add_argument("--control", action="store_true", help="1 s ごとに control (ControlStatus) も出す")
    ap.add_argument("--watch-can", action="store_true", help="CAN の fault / TEC / REC が変わるたびに出す")
    ap.add_argument("--no-sys", action="store_true", help="1 s ごとの sys を出さない")
    ap.add_argument("--send", nargs="+", metavar="CMD [k=v ...]", help="コマンドを送る (Command の oneof 名と、そのフィールド)")
    ap.add_argument("--desc", help="proto の FileDescriptorSet (binpb)。無ければ dc-console の buf で生成")
    ap.add_argument("--list", action="store_true", help="ポート一覧を出して終わる")
    args = ap.parse_args()

    if args.list:
        for p in serial.tools.list_ports.comports():
            mark = KART_PIDS.get(p.pid, "") if p.vid == KART_VID else ""
            print(f"{p.device:14s} {p.description:40s} {mark}")
        return

    D2H, H2D = load_messages(args.desc)
    port = pick_port(args.port)
    duration = args.duration if args.duration is not None else (3.0 if args.send else None)
    ser = serial.Serial(port, 115200, timeout=0.01)
    ser.dtr = True
    print(f"port {port}" + (f", {duration:g} s" if duration else ", Ctrl-C で終了"), flush=True)

    sent_at = None
    cmd_id = None
    if args.send:
        m = H2D()
        cmd_id = random.randint(1, 0x7FFFFFFF)
        m.command.id = cmd_id
        sub = getattr(m.command, args.send[0])
        sub.SetInParent()
        for kv in args.send[1:]:
            k, v = kv.split("=", 1)
            setattr(sub, k, parse_value(v))
        ser.write(frame(m.SerializeToString()))
        sent_at = time.time()
        print(f">> {args.send[0]} {' '.join(args.send[1:])}", flush=True)

    raw = bytearray()
    t0 = time.time()
    last_sys = t0
    n_tel = 0
    last = None
    last_can = None
    try:
        while duration is None or time.time() - t0 < duration:
            raw += ser.read(4096)
            while (j := raw.find(b"\x00")) >= 0:
                f = bytes(raw[:j])
                del raw[:j + 1]
                if not f:
                    continue
                d = cobs_decode(f)
                if len(d) < 3 or crc16(d[:-2]) != int.from_bytes(d[-2:], "little"):
                    continue
                m = D2H()
                m.ParseFromString(d[:-2])
                k = m.WhichOneof("payload")
                if k == "debug":
                    print(f"  {m.debug.timestamp_us / 1e6:9.3f}s {m.debug.msg}", flush=True)
                elif k == "response":
                    dt = f" ({time.time() - sent_at:.2f} s)" if sent_at else ""
                    extra = ""
                    if m.response.WhichOneof("data"):
                        extra = " " + text_format.MessageToString(getattr(m.response, m.response.WhichOneof("data")), as_one_line=True)
                    print(f"<< response id={m.response.id} ok={m.response.ok}{dt}{extra}", flush=True)
                elif k == "sensor":
                    last = m.sensor
                    n_tel += 1
                    if args.watch_can:
                        key = (last.can.fault, last.can.tx_errors, last.can.rx_errors)
                        if key != last_can:
                            last_can = key
                            print(f"  {time.time() - t0:7.2f}s can {text_format.MessageToString(last.can, as_one_line=True)}", flush=True)
            now = time.time()
            if now - last_sys >= 1.0 and last is not None and not args.no_sys:
                s = last.sys
                line = (f"telemetry {n_tel / (now - last_sys):.0f} Hz | tick {s.tick_max_us} us | loop mean {s.loop_mean_us} max {s.loop_max_us}"
                        f" boot {s.loop_max_us_boot} | sd max {s.sd_max_us} us | drops {s.log_drops}")
                if args.can:
                    line += " | can " + (text_format.MessageToString(last.can, as_one_line=True) or "-")
                if args.control:
                    line += " | control " + (text_format.MessageToString(last.control, as_one_line=True) or "-")
                print(line, flush=True)
                last_sys = now
                n_tel = 0
    except KeyboardInterrupt:
        pass
    except serial.SerialException as e:  # デバイスが消えた (reboot で再列挙された) など
        print(f"port closed: {e}", file=sys.stderr)
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
