"""Exercise first checkout and dirty-source rejection using a local Git seed."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from build import source

parser = argparse.ArgumentParser()
parser.add_argument("--seed", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix="source-test-", dir=args.output)).resolve()
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.seed, text=True).strip()
spec = {"url": str(args.seed.resolve()), "commit": commit}
checkout = work / "normal"
source(spec, checkout, ["git"])
assert (checkout / "core/config.h").is_file()
with (checkout / "core/config.h").open("a", encoding="utf-8") as output:
    output.write("\n/* local test-only change */\n")
try:
    source(spec, checkout, ["git"])
except RuntimeError as error:
    assert "local modifications" in str(error), error
else:
    raise AssertionError("dirty source accepted")
sparse = work / "sparse"
source(spec, sparse, ["git"], llvm=True)
actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=sparse, text=True).strip()
assert actual == commit
assert not (sparse / "core/config.h").exists(), "sparse patterns not applied"
print("new normal/sparse checkouts and dirty-source rejection passed")
