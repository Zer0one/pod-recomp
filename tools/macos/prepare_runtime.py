#!/usr/bin/env python3
"""Stage the pinned pcrecomp runtime with reviewable local portability fixes."""
import argparse
import filecmp
import shutil
import subprocess
import tempfile
from pathlib import Path

PIN = "a255839d0b78688aa98e677f05d0f0f74710d8aa"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("toolkit", type=Path)
parser.add_argument("destination", type=Path)
args = parser.parse_args()
revision = subprocess.check_output(
    ["git", "-C", str(args.toolkit), "rev-parse", "HEAD"], text=True).strip()
if revision != PIN:
    parser.error(f"POD's tested toolkit is {PIN}; found {revision}")
destination = args.destination.resolve()
# Only remove our named staging tree, never the original toolkit.
if destination.name != "pcrecomp-runtime" or destination == args.toolkit.resolve():
    parser.error("destination must be a separate pcrecomp-runtime staging directory")
patch = Path(__file__).parent / "patches/win32hle.patch"
destination.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="pod-runtime-", dir=destination.parent) as tmp:
    temporary = Path(tmp)
    shutil.copytree(args.toolkit / "runtime", temporary / "runtime")
    subprocess.run(["patch", "-p1", "--batch", "-i", str(patch.resolve())],
                   cwd=temporary, check=True)
    # Preserve timestamps for unchanged files, so a small host fix does not
    # recompile several million unchanged lines of generated guest C.
    for source in temporary.rglob("*"):
        target = destination / source.relative_to(temporary)
        if source.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif not target.exists() or not filecmp.cmp(source, target, shallow=False):
            shutil.copy2(source, target)
