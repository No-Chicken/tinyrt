"""Integration contract for the core's stdin-driven Wasm runner."""
import json, os, subprocess, unittest, zlib
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
    def test_round_commands_preserve_style_in_json(self):
        p,r=self.run_guest('round_valid')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        arc=r[0]['frame'][2]
        self.assertEqual((arc.get('radius'),arc.get('thickness'),arc.get('start_angle'),arc.get('end_angle')),(200,12,0,360))
        text=r[0]['frame'][3]
        self.assertEqual((text.get('font_px'),text.get('align'),text['text']),(48,1,'TEXT'))
    def test_normal_stop_and_restart_commit_without_render(self):
        p,r=self.run_guest('stop_valid','stop\nrestart\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['phase'] for x in r],['init','stop','restart','shutdown'])
        self.assertEqual(r[1]['kv'][0],77)
        self.assertEqual(r[1]['frame'],[])
        self.assertLess(r[1]['heap_bytes'],r[0]['heap_bytes'])
        self.assertEqual(r[-1]['heap_bytes'],0)
        self.assertEqual(r[-1]['commits'],1)
        p,r=self.run_guest('stop_valid','restart\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual(r[1]['kv'][0],77)
    def test_reboot_forces_destroy_but_eof_stops_normally(self):
        p,r=self.run_guest('stop_valid','reboot\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertIsNone(r[1]['kv'][0])
        self.assertEqual(r[-1]['kv'][0],77)
        self.assertEqual(r[-1]['heap_bytes'],0)
    def test_stop_trap_rolls_back_and_destroys(self):
        p,r=self.run_guest('stop_trap','stop\n')
        self.assertNotEqual(p.returncode,0)
        self.assertEqual(r[-2]['phase'],'stop')
        self.assertNotEqual(r[-2]['status'],0)
        self.assertIsNone(r[-2]['kv'][0])
        self.assertEqual(r[-1]['heap_bytes'],0)
    def test_rgb565_owned_pixels_are_summarized(self):
        p,r=self.run_guest('pixel_valid')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual(r[0]['frame'][1]['kind'],7)
        self.assertEqual(r[0]['pixel_bytes'],8)
        self.assertEqual(r[0]['pixel_crc32'],zlib.crc32(bytes.fromhex('00f8e0071f00ffff')))
        self.assertEqual(r[-1]['pixel_bytes'],0)
    def test_skipped_tick_has_no_frame_or_pixels(self):
        p,r=self.run_guest('pixel_then_skip','tick 1\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual(r[0]['pixel_bytes'],8)
        self.assertEqual(r[1]['frame'],[])
        self.assertEqual(r[1]['pixel_bytes'],0)
    def test_clock_interval_is_exposed(self):
        p,r=self.run_guest('clock_valid','tick 1000\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['clock_interval_ms'] for x in r],[1,1000,100])
if __name__=='__main__':unittest.main()
