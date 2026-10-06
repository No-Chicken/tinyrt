"""Build a pinned Windows Xtensa wamrc from upstream WAMR and LLVM sources.

Run in a Visual Studio x64 developer environment. CMake, Ninja, Git and Python
must be installed; this script never downloads a prebuilt wamrc.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect_licenses(wamr: Path, llvm: Path, output: Path) -> list[dict]:
    """Preserve upstream license files and original source copyright headers.

    Cover the source directories conservatively, including material that a
    particular build may omit. This retains separate notices in LLVM Support
    sources (for example xxhash, Unicode conversion, and BSD regular expressions).
    """
    output.mkdir(exist_ok=True)
    files = []
    notices = bytearray()
    header = re.compile(rb"\A(?:\xef\xbb\xbf)?(?:\s*(?:/\*.*?\*/|//[^\r\n]*(?:\r?\n|$)))+", re.S)
    roots = [("WAMR", wamr, [wamr / "core", wamr / "wamr-compiler"]),
             ("LLVM", llvm, [llvm / "llvm/include", llvm / "llvm/lib"])]
    for label, source_root, directories in roots:
        top_license = source_root / ("LICENSE" if label == "WAMR" else "llvm/LICENSE.TXT")
        top_copy = output / f"{label}-LICENSE.txt"
        top_copy.write_bytes(top_license.read_bytes())
        files.append(top_copy)
        candidates = {top_license}
        for directory in directories:
            candidates.update(path for path in directory.rglob("*") if path.is_file())
        for path in sorted(candidates, key=lambda item: item.as_posix()):
            relative = path.relative_to(source_root).as_posix()
            original = path.read_bytes()
            if path.name.upper().startswith(("LICENSE", "COPYING", "COPYRIGHT", "NOTICE")):
                target = output / label / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(original)
                files.append(target)
            elif path.suffix.lower() in {".c", ".cpp", ".cc", ".h", ".hpp", ".inc", ".def", ".s", ".td"}:
                match = header.match(original)
                if match and re.search(rb"copyright|licen[cs]e|SPDX", match.group(), re.I):
                    notices.extend(f"\n===== {label}/{relative} =====\n".encode("utf-8"))
                    notices.extend(match.group())
                    notices.extend(b"\n")
    combined = output / "upstream-source-notices.txt"
    combined.write_bytes(notices)
    files.append(combined)
    return [{"path": path.relative_to(output).as_posix(), "sha256": digest(path)}
            for path in sorted(files)]


def invoke(argv: list[str], *, cwd: Path | None = None, log: Path | None = None,
           capture: bool = False, env: dict | None = None) -> str:
    if log:
        with log.open("w", encoding="utf-8") as output:
            output.write(json.dumps(argv) + "\n")
            output.flush()
            result = subprocess.run(argv, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"command failed ({result.returncode}); see {log}")
        return ""
    result = subprocess.run(argv, cwd=cwd, env=env, check=True, stdout=subprocess.PIPE if capture else None,
                            encoding="utf-8", errors="replace")
    return result.stdout.strip() if capture else ""


def source(spec: dict, path: Path, git: list[str], *, llvm: bool = False) -> None:
    created = not (path / ".git").exists()
    if created:
        if path.exists() and any(path.iterdir()):
            raise RuntimeError(f"refusing to replace nonempty source directory: {path}")
        invoke([*git, "clone", "--depth", "1", "--filter=blob:none", "--no-checkout", spec["url"], str(path)])
    remote = invoke([*git, "remote", "get-url", "origin"], cwd=path, capture=True)
    if remote != spec["url"]:
        raise RuntimeError(f"unexpected source remote for {path}: {remote}")
    if not created and invoke([*git, "diff", "HEAD", "--name-only"], cwd=path, capture=True):
        raise RuntimeError(f"source has local modifications; choose a clean work directory: {path}")
    invoke([*git, "fetch", "--depth", "1", "origin", spec["commit"]], cwd=path)
    if llvm:
        invoke([*git, "sparse-checkout", "init", "--no-cone"], cwd=path)
        patterns = ["/cmake/", "/third-party/", "/llvm/", "!/llvm/test/", "!/llvm/docs/",
                    "!/llvm/unittests/", "!/llvm/examples/", "/LICENSE.TXT"]
        invoke([*git, "sparse-checkout", "set", "--no-cone", *patterns], cwd=path)
    invoke([*git, "checkout", "--detach", spec["commit"]], cwd=path)
    actual = invoke([*git, "rev-parse", "HEAD"], cwd=path, capture=True)
    if actual != spec["commit"]:
        raise RuntimeError(f"source pin mismatch: {actual}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--proxy", help="optional Git HTTPS proxy; no global Git changes")
    parser.add_argument("--resume", action="store_true", help="reuse already verified, patched sources")
    parser.add_argument("--source-lock", type=Path, default=ROOT / "source-lock.json")
    parser.add_argument("--riscv", action="store_true", help="build a separate RISC-V compiler using verified sources")
    args = parser.parse_args()
    if os.name != "nt" or not shutil.which("cl"):
        parser.error("run in a Visual Studio x64 developer environment on Windows")
    if not 1 <= args.jobs <= 64:
        parser.error("--jobs must be between 1 and 64")
    for tool in ("cmake", "ninja", "git"):
        if not shutil.which(tool):
            parser.error(f"{tool} is missing from PATH")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    lock_path = args.source_lock.resolve()
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    git = ["git", "-c", "core.longpaths=true",
           "-c", "http.sslBackend=openssl"]
    if args.proxy:
        git += ["-c", "http.proxy=" + args.proxy]
    wamr, llvm = work / "wamr-upstream", work / "llvm-project"
    llvm_build, wamrc_build = work / "llvm-build", work / "wamrc-build"
    if args.riscv:
        wamrc_build = work / "wamrc-build-riscv"
    patches = [(lock_path.parent / name).resolve() for name in lock["patches"]]
    if any(not patch.is_file() for patch in patches):
        raise RuntimeError("a locked source patch is missing")
    if not args.resume:
        source(lock["llvm"], llvm, git, llvm=True)
        source(lock["wamr"], wamr, git)
        for patch in patches:
            invoke([*git, "apply", "--check", str(patch)], cwd=wamr)
            invoke([*git, "apply", str(patch)], cwd=wamr)
    else:
        for path, spec in ((wamr, lock["wamr"]), (llvm, lock["llvm"])):
            if invoke([*git, "rev-parse", "HEAD"], cwd=path, capture=True) != spec["commit"]:
                raise RuntimeError(f"cannot resume mismatched source revision: {path}")
        if invoke([*git, "diff", "HEAD", "--name-only"], cwd=llvm, capture=True):
            raise RuntimeError("LLVM source contains unrecorded modifications")
    if invoke([*git, "ls-files", "--others", "--exclude-standard"], cwd=llvm, capture=True):
        raise RuntimeError("LLVM source has unrecorded files")
    # Compare the complete source tree against HEAD plus the locked patches.
    # A separate index avoids touching the user's Git staging area.
    expected_index = work / "wamr-expected.index"
    index_env = dict(os.environ, GIT_INDEX_FILE=str(expected_index))
    try:
        invoke([*git, "read-tree", "HEAD"], cwd=wamr, env=index_env)
        for patch in patches:
            invoke([*git, "apply", "--cached", str(patch)], cwd=wamr, env=index_env)
        invoke([*git, "diff", "--exit-code"], cwd=wamr, env=index_env)
        if invoke([*git, "ls-files", "--others", "--exclude-standard"], cwd=wamr, capture=True):
            raise RuntimeError("WAMR source has unrecorded files")
    finally:
        expected_index.unlink(missing_ok=True)
    common = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_FLAGS=/utf-8",
              "-DCMAKE_CXX_FLAGS=/utf-8"]
    llvm_command = ["cmake", "-S", str(llvm / "llvm"), "-B", str(llvm_build), *common,
        "-DLLVM_ENABLE_DIA_SDK=OFF", "-DLLVM_TARGETS_TO_BUILD=" + ("X86;RISCV" if args.riscv else "X86"),
        "-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=Xtensa", "-DLLVM_INCLUDE_TESTS=OFF",
        "-DLLVM_INCLUDE_EXAMPLES=OFF", "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_DOCS=OFF",
        "-DLLVM_ENABLE_TERMINFO=OFF", "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF",
        "-DLLVM_ENABLE_LIBXML2=OFF", "-DLLVM_ENABLE_ASSERTIONS=OFF", "-DLLVM_ENABLE_BINDINGS=OFF",
        "-DLLVM_OPTIMIZED_TABLEGEN=ON", "-DLLVM_INCLUDE_UTILS=OFF", "-DLLVM_INCLUDE_TOOLS=ON",
        "-DLLVM_BUILD_TOOLS=OFF", "-DPython3_EXECUTABLE=" + sys.executable]
    invoke(llvm_command, log=work / "configure-llvm.log")
    invoke(["cmake", "--build", str(llvm_build), "--parallel", str(args.jobs)],
           log=work / "build-llvm.log")
    wamrc_command = ["cmake", "-S", str(wamr / "wamr-compiler"), "-B", str(wamrc_build), *common,
        "-DWAMR_BUILD_WITH_CUSTOM_LLVM=1", "-DLLVM_DIR=" + str(llvm_build / "lib/cmake/llvm"),
        "-DWAMR_DISABLE_HW_BOUND_CHECK=1", "-DWAMR_BUILD_PLATFORM=windows", "-DWAMR_BUILD_TARGET=X86_64",
        "-DWAMR_BUILD_LIBC_WASI=0", "-DWAMR_BUILD_LIB_PTHREAD=0", "-DWAMR_BUILD_LIB_WASI_THREADS=0",
        "-DWAMR_BUILD_LOOP_POLL=1"]
    invoke(wamrc_command, log=work / "configure-wamrc.log")
    invoke(["cmake", "--build", str(wamrc_build), "--parallel", str(args.jobs)],
           log=work / "build-wamrc.log")
    binary = wamrc_build / "wamrc.exe"
    compiler = subprocess.run(["cl"], capture_output=True, encoding="utf-8", errors="replace")
    manifest = {"schema": 1, "source_lock_sha256": digest(lock_path),
        "build_script_sha256": digest(Path(__file__)),
        "sources": {"wamr": lock["wamr"], "llvm": lock["llvm"]},
        "patches": [{"name": name, "sha256": digest(path)}
                    for name, path in zip(lock["patches"], patches)],
        "compiler": {"path": str(binary), "sha256": digest(binary),
                     "version": invoke([str(binary), "--version"], capture=True)},
        "host": {"cc": shutil.which("cl"), "cc_version": compiler.stdout + compiler.stderr,
                 "cmake": invoke(["cmake", "--version"], capture=True),
                 "ninja": invoke(["ninja", "--version"], capture=True), "python": sys.version},
        "configure_commands": [llvm_command, wamrc_command],
        "target_options": lock["target_options"],
        "target_options_sha256": hashlib.sha256(
            json.dumps(lock["target_options"], separators=(",", ":")).encode()).hexdigest()}
    # Retain upstream license text and separate source notices unchanged.
    licenses = wamrc_build / "licenses"
    licenses.mkdir(exist_ok=True)
    manifest["licenses"] = collect_licenses(wamr, llvm, licenses)
    (work / ("provenance-riscv.json" if args.riscv else "provenance.json")).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
