"""Integration contract for the core's stdin-driven Wasm runner."""
import json, os, subprocess, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
RUNNER=Path(os.environ.get('TINYRT_RUNNER',str(ROOT/'build/tinyrt-run.exe')))
FIXTURES=Path(os.environ.get('TINYRT_FIXTURE_DIR',str(ROOT/'tests/runtime/fixtures')))
class Runner(unittest.TestCase):
    def run_guest(self, name, commands='', *options):
        self.assertTrue(RUNNER.is_file(), 'tinyrt-run executable is required')
        p=subprocess.run([str(RUNNER),str(FIXTURES/f'{name}.wasm'),*options],input=commands,text=True,capture_output=True,timeout=20)
        records=[json.loads(s) for s in p.stdout.splitlines() if s.startswith('{')]
        return p,records
    def test_events_and_restart_preserve_committed_values(self):
        p,r=self.run_guest('valid','touch 10 20\ntick 1000\nrestart\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['phase'] for x in r],['init','touch','tick','restart','shutdown'])
        self.assertEqual(r[1]['kv'][0],1)
        self.assertEqual(r[3]['kv'][0],r[2]['kv'][0])
        self.assertEqual(r[-1]['heap_bytes'],0)
        self.assertTrue(all(x['frame'][0]['kind']==1 for x in r[:-1]))
    def test_budget_trap_fails_and_releases_runtime(self):
        p,r=self.run_guest('spin','touch 10 20\n')
        self.assertNotEqual(p.returncode,0)
        self.assertNotEqual(r[-2]['status'],0)
        self.assertEqual(r[-2]['kv'],r[0]['kv'])
        self.assertEqual(r[-1]['heap_bytes'],0)
    def test_unknown_import_is_rejected_before_execution(self):
        p,r=self.run_guest('unknown')
        self.assertNotEqual(p.returncode,0)
        self.assertEqual(r[0]['phase'],'create')
        self.assertNotEqual(r[0]['status'],0)
        self.assertEqual(r[-1]['heap_bytes'],0)
    def test_invalid_policy_and_commands_fail(self):
        p,_=self.run_guest('valid','','--pages','17')
        self.assertEqual(p.returncode,2)
        p,_=self.run_guest('valid','tick 4294967296\n')
        self.assertEqual(p.returncode,2)
if __name__=='__main__':unittest.main()
