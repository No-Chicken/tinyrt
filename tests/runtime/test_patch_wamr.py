"""Reject edited dependency trees while accepting the exact ordered patch set."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("patch_wamr",ROOT/"scripts/patch_wamr.py")
patcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(patcher)


class PreparedSource(unittest.TestCase):
    def test_patch_tree_is_reverified_and_upstream_stays_clean(self):
        source=Path(os.environ["TINYRT_TEST_WAMR_SOURCE"])
        build=Path(os.environ["TINYRT_TEST_BUILD"]).resolve()
        revision="25bd7eb63e828e4bd242cc9b38d260b4b31c6605"
        patches=[ROOT/"patches/wamr"/name for name in (
            "0001-exec-env-cancellation.patch","0002-aot-dbus-writes.patch",
            "0003-aot-mapping-budget.patch","0004-esp-idf-native-stack.patch")]
        with tempfile.TemporaryDirectory(prefix="patch-test-",dir=build) as folder:
            self.assertTrue(Path(folder).resolve().is_relative_to(build))
            prepared=patcher.prepare(source,folder,revision,patches)
            self.assertEqual(patcher.prepare(source,folder,revision,patches),prepared)
            victim=prepared/"core/config.h";original=victim.read_bytes()
            victim.write_bytes(original+b"\n#define UNAPPROVED_CHANGE 1\n")
            with self.assertRaisesRegex(RuntimeError,"differs"):
                patcher.prepare(source,folder,revision,patches)
            victim.write_bytes(original)
            extra=prepared/"unapproved.c";extra.write_text("int unapproved;\n")
            with self.assertRaisesRegex(RuntimeError,"unapproved files"):
                patcher.prepare(source,folder,revision,patches)
            extra.unlink()
            with self.assertRaisesRegex(RuntimeError,"must be clean"):
                patcher.prepare(prepared,folder,revision,patches)
            self.assertEqual(patcher.git(source,"status","--porcelain").strip(),b"")


if __name__=="__main__":unittest.main()
