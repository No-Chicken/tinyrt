"""Emit a tiny unshared-memory safety probe and compile it with the locked flags."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from test_loop_poll import section, uleb

parser = argparse.ArgumentParser()
parser.add_argument("--wamrc", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
cases = [
    ("safe", "00 41 2a 0b", "returns 42"),
    ("spin", "00 03 40 0c 00 0b 41 00 0b", "requires host cancellation"),
    ("oob_load", "00 41 ff ff 03 28 02 00 0b", "out-of-bounds memory access trap"),
    ("oob_store", "00 41 ff ff 03 41 01 36 02 00 41 00 0b", "out-of-bounds memory access trap"),
    ("grow", "00 41 02 40 00 0b", "returns -1; memory remains 1 page"),
    ("div_zero", "00 41 01 41 00 6d 0b", "integer divide by zero trap"),
    ("recurse", "00 10 06 0b", "native stack bounds trap or cancellation"),
]
exports = [(name.encode(), index) for index, (name, _, _) in enumerate(cases)]
bodies = [bytes.fromhex(body) for _, body, _ in cases]
wasm = b"\x00asm\x01\x00\x00\x00"
wasm += section(1, bytes.fromhex("01 60 00 01 7f"))
wasm += section(3, uleb(len(cases)) + bytes(len(cases)))
wasm += section(5, bytes.fromhex("01 01 01 02"))
wasm += section(7, uleb(len(exports)) + b"".join(
    uleb(len(name)) + name + bytes([0]) + uleb(index) for name, index in exports))
wasm += section(10, uleb(len(bodies)) + b"".join(uleb(len(body)) + body for body in bodies))
wasm_path = args.output / "safety-probe.wasm"
aot_path = args.output / "safety-probe.aot"
wasm_path.write_bytes(wasm)
lock = json.loads(Path(__file__).with_name("source-lock.json").read_text(encoding="utf-8"))
subprocess.run([str(args.wamrc), *lock["target_options"], "-o", str(aot_path), str(wasm_path)], check=True)
manifest = {"functions": [{"name": name, "signature": "() -> i32", "expected": expected}
                          for name, _, expected in cases],
            "wasm_sha256": hashlib.sha256(wasm).hexdigest(),
            "aot_sha256": hashlib.sha256(aot_path.read_bytes()).hexdigest(),
            "wamrc_sha256": hashlib.sha256(args.wamrc.read_bytes()).hexdigest(),
            "flags": lock["target_options"]}
(args.output / "safety-probe.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(json.dumps(manifest, indent=2))
