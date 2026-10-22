#!/usr/bin/env python3
# /// script
# requires-python = ">=3.8"
# dependencies = ["pyserial>=3.5"]
# ///
"""dc-boot + dc-main + dc-sd-reader を 1 つの hex にして Teensy 4.1 に書く (docs/loop_nonblocking_spec.md §7.11)。Linux / Windows / macOS。

    uv run tools/flash_all.py            # 3 つをビルド → 合成 → 書く (Linux / macOS: teensy_loader_cli、Windows: GUI の Teensy Loader)
    uv run tools/flash_all.py --no-flash # 合成した hex を作るだけ (Teensy Loader の GUI で書くとき)

ブートローダへは、KART のシリアルポートに 134 baud を当てて入れる (Teensy の CDC の決まり。teensy_loader_cli の -s は環境によって
失敗して Teensy をブートローダに残したまま止まるので使わない)。pyserial が無ければ -s にフォールバックする。

dc-sd-reader を書くとき / 初めて書くときに使う。dc-main だけ更新するなら `pio run -d dc-firmware/dc-main -e teensy41 -t upload` でよい
(dc-main の firmware.hex には post_build.py が dc-boot を合成している。dc-sd-reader は残る)。
dc-sd-reader の hex だけを書かないこと (ローダが先頭 1 KB を 0xFF で書いて dc-boot が消え、起動しなくなる)。
dc-main (KART Drive Controller) が動いている状態で実行する (-s のソフトリブート)。dc-sd-reader のときは console の Reboot で戻してから。
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import merge_hex  # noqa: E402


def pio():
    p = shutil.which("pio") or shutil.which("platformio")
    if p:
        return [p]
    home = os.path.expanduser("~/.platformio/penv")
    for cand in (os.path.join(home, "bin", "pio"), os.path.join(home, "Scripts", "pio.exe")):
        if os.path.exists(cand):
            return [cand]
    raise SystemExit("pio が見つかりません")


def loader():
    d = os.path.expanduser("~/.platformio/packages/tool-teensy")
    for name in ("teensy_loader_cli", "teensy_loader_cli.exe"):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


def enter_bootloader():
    """KART のシリアルポート (VID 0x16C0、PID 0x0483 / 0x04D5) に 134 baud を当ててブートローダに入れる。見つからなければ False"""
    try:
        import serial
        import serial.tools.list_ports
    except ImportError:
        return False
    ports = [p for p in serial.tools.list_ports.comports() if p.vid == 0x16C0 and p.pid in (0x0483, 0x04D5)]
    if not ports:
        return False
    try:
        with serial.Serial(ports[0].device, 134, timeout=0.1):
            pass
    except Exception:
        pass
    return True


def main(argv):
    no_flash = "--no-flash" in argv
    fw = os.path.join(ROOT, "dc-firmware")
    for proj in ("dc-main", "dc-sd-reader"):  # dc-boot は dc-main の post_build が一緒にビルド・合成する
        subprocess.run(pio() + ["run", "-d", os.path.join(fw, proj), "-e", "teensy41"], check=True)
    main_hex = os.path.join(fw, "dc-main", ".pio", "build", "teensy41", "firmware.hex")
    sd_hex = os.path.join(fw, "dc-sd-reader", ".pio", "build", "teensy41", "firmware.hex")
    out = os.path.join(fw, "dc-main", ".pio", "build", "teensy41", "dc_all.hex")
    merge_hex.main(["merge_hex", main_hex, sd_hex, out])
    if no_flash:
        print(f"合成した hex: {out} (Teensy Loader で書く)")
        return
    import time

    tl = loader()
    if not tl:
        # Windows の tool-teensy には teensy_loader_cli が無い。PlatformIO の teensy-gui と同じく teensy_post_compile で GUI の Teensy Loader に渡す
        # (console を Disconnect して COM を空けておく。掴まれているとボタン待ちになる)
        tools_dir = os.path.expanduser("~/.platformio/packages/tool-teensy")
        tpc = next((os.path.join(tools_dir, n) for n in ("teensy_post_compile.exe", "teensy_post_compile")
                    if os.path.exists(os.path.join(tools_dir, n))), None)
        if not tpc:
            raise SystemExit(f"teensy_loader_cli も teensy_post_compile も見つかりません。{out} を Teensy Loader (GUI) で書いてください")
        subprocess.run([tpc, "-file=" + os.path.splitext(os.path.basename(out))[0], "-path=" + os.path.dirname(out),
                        "-tools=" + tools_dir, "-board=TEENSY41", "-reboot"], check=True)
        return

    # 134 baud を当てた直後の 1 回目は失敗することがある (ブートローダへの切り替わり待ち) ので 3 回まで試す。
    # 2 回目以降は既にブートローダ (ポートが無い) なので当てずに書く
    for attempt in range(3):
        poked = enter_bootloader()
        if poked:
            time.sleep(3)
        r = subprocess.run([tl, "-mmcu=TEENSY41", "-w", "-v"] + ([] if poked else ["-s"]) + [out])
        if r.returncode == 0:
            return
        time.sleep(3)
    raise SystemExit("書き込みに失敗しました (Teensy のボタンを押してから再実行)")


if __name__ == "__main__":
    main(sys.argv[1:])
