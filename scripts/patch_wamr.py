"""Prepare a verified local WAMR checkout without changing the upstream source."""
import argparse
import hashlib
import os
import subprocess
import tempfile
from pathlib import Path


def git(folder, *args, env=None):
    return subprocess.check_output(["git", "-C", str(folder), "-c", "core.autocrlf=false", *args], env=env)


def prepare(source, output, revision, patches):
    source, output = (Path(p).resolve() for p in (source, output))
    patches = [Path(p).resolve() for p in patches]
    if git(source, "rev-parse", "HEAD").decode().strip() != revision:
        raise RuntimeError("WAMR source revision mismatch")
    if git(source, "status", "--porcelain", "--untracked-files=normal").strip():
        raise RuntimeError("WAMR upstream source must be clean")
    patch_bytes = [patch.read_bytes() for patch in patches]
    # A content-addressed directory never overwrites a previous prepared tree.
    identity = b"".join(len(data).to_bytes(8,"big")+data for data in patch_bytes)
    target = output / hashlib.sha256(identity).hexdigest()
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--shared", "--no-checkout", str(source), str(target)], check=True, stdout=subprocess.DEVNULL)
        git(target, "checkout", "--detach", revision)
        for patch in patches:
            git(target, "apply", "--index", str(patch))
    if git(target, "rev-parse", "HEAD").decode().strip() != revision:
        raise RuntimeError("Prepared WAMR revision mismatch")
    if git(target, "ls-files", "--others", "--exclude-standard").strip():
        raise RuntimeError("Prepared WAMR contains unapproved files")
    # Derive the expected tree independently from HEAD and the approved patches;
    # never trust a mutable marker stored beside the compiled sources.
    with tempfile.TemporaryDirectory(prefix="verify-", dir=output) as folder:
        assert Path(folder).resolve().is_relative_to(output)
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(folder)/"index"))
        git(target, "read-tree", revision, env=env)
        for patch in patches:
            git(target, "apply", "--cached", str(patch), env=env)
        expected = git(target, "write-tree", env=env).decode().strip()
        if git(target, "diff", expected, "--"):
            raise RuntimeError("Prepared WAMR differs from the approved patch set")
    return target


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--source", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--revision", required=True)
    p.add_argument("--patch", action="append", required=True)
    args = p.parse_args()
    print(prepare(args.source, args.output, args.revision, args.patch).as_posix())
