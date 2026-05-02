import os
import subprocess

Import("env")

# compiledb 実行中はこの pre script を即終了
if "compiledb" in COMMAND_LINE_TARGETS:
    Return()

env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)

project_dir = env.subst("$PROJECT_DIR")
python_exe = env.subst("$PYTHONEXE")
pioenv = env.subst("$PIOENV")
libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")

# ---------------------------------------------------------------------
# 0) Auto-generate compile_commands.json (for clangd)
# ---------------------------------------------------------------------
subprocess.run(
        [
            python_exe,
            "-m",
            "platformio",
            "run",
            "-d",
            project_dir,
            "-e",
            pioenv,
            "-t",
            "compiledb",
        ],
        check=False,
    )

# ---------------------------------------------------------------------
# 1) Generate nanopb C sources from spec/proto/*.proto
# ---------------------------------------------------------------------
proto_src_dir = os.path.normpath(os.path.join(project_dir, "..", "spec", "proto"))
proto_out_dir = os.path.join(project_dir, "src", "proto")
nanopb_dir = os.path.join(libdeps_dir, pioenv, "Nanopb")
nanopb_generator = os.path.join(nanopb_dir, "generator", "nanopb_generator.py")

if os.path.isdir(proto_src_dir) and os.path.exists(nanopb_generator):
    os.makedirs(proto_out_dir, exist_ok=True)
    proto_files = sorted(f for f in os.listdir(proto_src_dir) if f.endswith(".proto"))
    for pf in proto_files:
        subprocess.run(
            [
                python_exe,
                nanopb_generator,
                "-I",
                proto_src_dir,
                "-D",
                proto_out_dir,
                pf,
            ],
            cwd=proto_src_dir,
            check=True,
        )
elif os.path.isdir(proto_src_dir):
    print(
        "[pre_build] nanopb generator not found at {}; "
        "skipping proto generation (lib will be fetched on next run).".format(
            nanopb_generator
        )
    )
