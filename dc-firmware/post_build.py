import os
import subprocess
import sys

Import("env")

# firmware.hex に dc-boot (起動の選択をする薄いファーム、flash の先頭) を合成する (docs/loop_nonblocking_spec.md §7.11)。
# teensy_loader_cli は先頭 1 KB (アドレス 0) を必ず書く (hex に無ければ 0xFF) ので、dc-firmware (0x60010000〜) だけの hex を書くと
# 先頭の dc-boot が消えて起動しなくなる。合成しておけば `pio run -t upload` がそのまま使える (dc-sd-reader は別。python3 tools/flash_all.py)。
project_dir = env.subst("$PROJECT_DIR")
boot_dir = os.path.normpath(os.path.join(project_dir, "..", "dc-boot"))
tools_dir = os.path.normpath(os.path.join(project_dir, "..", "tools"))


def merge_boot(source, target, env):
    hex_path = str(target[0])
    subprocess.run([env.subst("$PYTHONEXE"), "-m", "platformio", "run", "-d", boot_dir, "-e", "teensy41"], check=True)
    boot_hex = os.path.join(boot_dir, ".pio", "build", "teensy41", "firmware.hex")
    sys.path.insert(0, tools_dir)
    import merge_hex  # noqa: E402

    main_img = merge_hex.parse(hex_path)
    boot_img = merge_hex.parse(boot_hex)
    for a in boot_img:
        if a in main_img:
            raise SystemExit("[post_build] dc-boot と dc-firmware が重なっています ({:#x})".format(a))
    merge_hex.emit({**boot_img, **main_img}, hex_path)
    print("[post_build] firmware.hex に dc-boot を合成 (boot {} B + main {} B)".format(len(boot_img), len(main_img)))


def check_ldscripts(source, target, env):
    """3 つのリンカスクリプト (コアの imxrt1062_t41.ld の FLASH 行だけ変えたコピー) がコアの更新で乖離していないか。違えば警告"""
    core_ld = os.path.join(env.PioPlatform().get_package_dir("framework-arduinoteensy"), "cores", "teensy4", "imxrt1062_t41.ld")
    if not os.path.exists(core_ld):
        return

    def flash_line_removed(path):
        return [l for l in open(path, encoding="utf-8").read().splitlines() if not l.strip().startswith("FLASH (rx):")]

    core = flash_line_removed(core_ld)
    for rel in ("../dc-boot/imxrt1062_t41_boot.ld", "imxrt1062_t41_main.ld", "../dc-sd-reader/imxrt1062_t41_512k.ld"):
        path = os.path.normpath(os.path.join(project_dir, rel))
        if os.path.exists(path) and flash_line_removed(path) != core:
            print("[post_build] 警告: {} がコアの imxrt1062_t41.ld と FLASH 行以外で違います (コアが更新された?)。差分を取り込むこと".format(rel))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", merge_boot)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", check_ldscripts)
