"""Fetch the exact dependency into a NEW checkout; never reset existing work."""
import json
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
lock = json.loads((root/"dependencies.lock.json").read_text(encoding="utf-8"))["wamr"]
target = root/lock["directory"]
if target.exists():
    raise SystemExit("Destination already exists; pass its path as WAMR_ROOT_DIR after verifying it")
target.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(["git","clone","--no-checkout",lock["repository"],str(target)],check=True)
subprocess.run(["git","-C",str(target),"checkout","--detach",lock["revision"]],check=True)
print(target)
