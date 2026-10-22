import os
import subprocess

Import("env")

if "compiledb" in COMMAND_LINE_TARGETS:
    Return()

project_dir = env.subst("$PROJECT_DIR")
python_exe = env.subst("$PYTHONEXE")
pioenv = env.subst("$PIOENV")
libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")

# spec/proto/*.proto → src/proto/*.pb.{c,h} (dc-main/pre_build.py と同じ。RebootCmd の受付に使う)
proto_src_dir = os.path.normpath(os.path.join(project_dir, "..", "..", "spec", "proto"))
proto_out_dir = os.path.join(project_dir, "src", "proto")
nanopb_generator = os.path.join(libdeps_dir, pioenv, "Nanopb", "generator", "nanopb_generator.py")

if os.path.isdir(proto_src_dir) and os.path.exists(nanopb_generator):
    os.makedirs(proto_out_dir, exist_ok=True)
    for pf in sorted(f for f in os.listdir(proto_src_dir) if f.endswith(".proto")):
        subprocess.run([python_exe, nanopb_generator, "-I", proto_src_dir, "-D", proto_out_dir, pf],
                       cwd=proto_src_dir, check=True)
elif os.path.isdir(proto_src_dir):
    print("[pre_build] nanopb generator not found at {}; skipping proto generation".format(nanopb_generator))
