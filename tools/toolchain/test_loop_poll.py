"""Check the real compiler output for non-shared cancellation checkpoints."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def uleb(value: int) -> bytes:
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return bytes(out)


def section(kind: int, data: bytes) -> bytes:
    return bytes([kind]) + uleb(len(data)) + data


def guest(with_memory: bool) -> bytes:
    # Separate exports exercise unconditional, conditional, table backedges,
    # and direct recursion. The table uses a parameter to avoid constant folding.
    names = [b"spin_br", b"spin_br_if", b"spin_br_table", b"spin_call"]
    bodies = [
        bytes.fromhex("00 03 40 0c 00 0b 0b"),
        bytes.fromhex("00 03 40 41 01 0d 00 0b 0b"),
        bytes.fromhex("00 03 40 20 00 0e 01 00 00 0b 0b"),
        bytes.fromhex("00 10 03 0b"),
    ]
    types = section(1, bytes.fromhex("02 60 00 00 60 01 7f 00"))
    funcs = section(3, bytes.fromhex("04 00 00 01 00"))
    memory = section(5, bytes.fromhex("01 01 01 01")) if with_memory else b""
    exports = section(7, uleb(4) + b"".join(
        uleb(len(name)) + name + bytes([0, i]) for i, name in enumerate(names)
    ))
    code = section(10, uleb(4) + b"".join(uleb(len(b)) + b for b in bodies))
    return b"\x00asm\x01\x00\x00\x00" + types + funcs + memory + exports + code


def run(argv: list[str]) -> None:
    result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise AssertionError(f"compiler exited {result.returncode}:\n{result.stdout}")


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aot_features(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    assert data[:4] == b"\x00aot", "unexpected AOT magic"
    version = struct.unpack_from("<I", data, 4)[0]
    kind, size = struct.unpack_from("<II", data, 8)
    assert kind == 0 and size >= 48, "missing target-info section"
    return version, struct.unpack_from("<Q", data, 32)[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wamrc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--example", type=Path)
    parser.add_argument("--objdump", type=Path, help="Xtensa objdump for machine-code verification")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lock = json.loads(Path(__file__).with_name("source-lock.json").read_text(encoding="utf-8"))
    flags = lock["target_options"]
    evidence = {"wamrc_sha256": sha(args.wamrc), "flags": flags, "cases": []}
    for with_memory in (True, False):
        stem = "unshared" if with_memory else "no-memory"
        wasm = args.output / f"{stem}.wasm"
        wasm.write_bytes(guest(with_memory))
        ir = args.output / f"{stem}.ll"
        run([str(args.wamrc), *flags, "--format=llvmir-opt", "-o", str(ir), str(wasm)])
        text = ir.read_text(encoding="utf-8")
        functions = re.findall(r'^define\b[^\n]*@"aot_func_internal#[0-9]+"[^\n]*\{\n(.*?)^}',
                               text, re.MULTILINE | re.DOTALL)
        assert len(functions) == 4, f"expected 4 Wasm functions, got {len(functions)}"
        for index, body in enumerate(functions):
            assert "load atomic volatile i32" in body and "acquire, align 4" in body, f"{stem} function {index} lost its atomic poll"
            assert re.search(r"and i32 [^\n]+, 1", body), f"{stem} function {index} misses terminate bit"
        plain_ir = args.output / f"{stem}-without-poll.ll"
        run([str(args.wamrc), *(f for f in flags if f != "--enable-loop-poll"),
             "--format=llvmir-opt", "-o", str(plain_ir), str(wasm)])
        assert not re.search(r"load (?:atomic )?volatile i32", plain_ir.read_text(encoding="utf-8")), "control unexpectedly polls"
        obj = args.output / f"{stem}.o"
        run([str(args.wamrc), *flags, "--format=object", "-o", str(obj), str(wasm)])
        if args.objdump:
            dis = subprocess.run([str(args.objdump), "-dr", str(obj)], check=True,
                                 capture_output=True, encoding="utf-8").stdout
            (args.output / f"{stem}.dis").write_text(dis, encoding="utf-8")
            machine_funcs = re.findall(r"^[0-9a-f]+ <aot_func_internal#[0-9]+>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
                                       dis, re.MULTILINE | re.DOTALL)
            assert len(machine_funcs) == 4, "missing Xtensa function bodies"
            for index, body in enumerate(machine_funcs):
                assert "memw" in body, f"{stem} machine function {index} misses memory ordering"
                assert re.search(r"l32i(?:\.n)?\s+a[0-9]+, a[0-9]+, 20\b", body), f"{stem} machine function {index} misses flag load"
                assert re.search(r"\b(?:beqz|bnez)(?:\.n)?\b", body), f"{stem} machine function {index} misses conditional exit"
        aot = args.output / f"{stem}.aot"
        run([str(args.wamrc), *flags, "-o", str(aot), str(wasm)])
        version, features = aot_features(aot)
        assert version == lock["aot_format_version"], "AOT format mismatch"
        assert features & 4 == 0, "loop polling incorrectly requires guest threading"
        evidence["cases"].append({"name": stem, "wasm_sha256": sha(wasm),
                                  "aot_sha256": sha(aot), "aot_features": features,
                                  "optimized_ir_functions_with_poll": len(functions)})
        if args.objdump:
            evidence["cases"][-1]["machine_code_functions_with_poll"] = len(machine_funcs)
    if args.example:
        products = [args.output / "example-first.aot", args.output / "example-second.aot"]
        for product in products:
            run([str(args.wamrc), *flags, "-o", str(product), str(args.example)])
        assert products[0].read_bytes() == products[1].read_bytes(), "AOT output is nondeterministic"
        evidence["example"] = {"wasm": str(args.example.resolve()),
                               "wasm_sha256": sha(args.example), "aot_sha256": sha(products[0]),
                               "aot_size": products[0].stat().st_size,
                               "identical_compilations": 2}
    (args.output / "verification.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
