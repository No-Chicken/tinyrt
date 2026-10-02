"""Integration contract for the core's stdin-driven Wasm runner."""
import json, os, subprocess, sys, unittest, zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
RUNNER=Path(os.environ.get('TINYRT_RUNNER',str(ROOT/'build/tinyrt-run.exe')))
FIXTURES=Path(os.environ.get('TINYRT_FIXTURE_DIR',str(ROOT/'tests/runtime/fixtures')))
class Runner(unittest.TestCase):
    def event_guest(self, name, input_mask=None):
        sys.path.insert(0,str(ROOT/'tests/runtime'))
        from make_fixtures import c,call
        from make_round_fixtures import guest
        folder=RUNNER.parent/'runner-fixtures';folder.mkdir(exist_ok=True)
        path=folder/f'{name}.wasm'
        imports=[('draw_clear',1)]
        init=c(0)
        if input_mask is not None:
            imports.append(('input_events',1));init=c(input_mask)+call(1)+b'\x1a'+c(0)
        # Count delivered events and expose the last kind in the rendered color.
        event=c(0)+c(0)+b'\x2d\0\0'+c(1)+b'\x6a\x3a\0\0'+c(1)+b'\x20\0\x3a\0\0'+c(0)
        render=c(0)+b'\x2d\0\0'+c(256)+b'\x6c'+c(1)+b'\x2d\0\0\x6a'+call(0)+b'\x1a'+c(0)
        path.write_bytes(guest(imports,render,init=init,event=event,payload=b'\0\0'))
        return path
    def run_guest(self, name, commands='', *options):
        self.assertTrue(RUNNER.is_file(), 'tinyrt-run executable is required')
        guest=name if isinstance(name,Path) else FIXTURES/f'{name}.wasm'
        p=subprocess.run([str(RUNNER),str(guest),*options],input=commands,text=True,capture_output=True,timeout=20)
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
    def test_advance_honors_changed_guest_clock_without_capture_callbacks(self):
        # This guest replaces its interval with CLOCK arg (the virtual timestamp).
        p,r=self.run_guest('clock_valid','advance 7\ncapture\nadvance 17\ncapture\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['now_ms'] for x in r if x['phase']=='tick'],[1,2,4,8,16])
        self.assertEqual([x['now_ms'] for x in r if x['phase']=='capture'],[7,17])
        self.assertEqual([x['clock_interval_ms'] for x in r if x['phase']=='tick'],[1,2,4,8,16])
        self.assertEqual(len([x for x in r if x['phase'] not in ('capture','shutdown')]),6)
    def test_advance_keeps_pixels_when_latest_callback_skips(self):
        p,r=self.run_guest('pixel_then_skip','advance 100\ncapture\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        tick=next(x for x in r if x['phase']=='tick')
        capture=next(x for x in r if x['phase']=='capture')
        self.assertEqual(tick['frame'],[])
        self.assertEqual(capture['frame'],r[0]['frame'])
        self.assertEqual(capture['pixels_hex'],'00f8e0071f00ffff')
        self.assertEqual(capture['pixel_crc32'],r[0]['pixel_crc32'])
    def test_virtual_input_delivers_key_press_and_release_at_script_time(self):
        # Observe both payload fields through the real guest, not just event kind.
        sys.path.insert(0,str(ROOT/'tests/runtime'))
        from make_fixtures import c,call
        from make_round_fixtures import guest
        folder=RUNNER.parent/'runner-fixtures';folder.mkdir(exist_ok=True)
        path=folder/'key-state.wasm'
        render=c(0)+b'\x2d\0\0'+c(256)+b'\x6c'+c(1)+b'\x2d\0\0\x6a'+call(0)+b'\x1a'+c(0)
        event=c(0)+b'\x20\1\x3a\0\0'+c(1)+b'\x20\2\x3a\0\0'+c(0)
        path.write_bytes(guest([('draw_clear',1),('input_events',1)],render,
                              init=c(120)+call(1)+b'\x1a'+c(0),event=event))
        p,r=self.run_guest(path,'advance 5\nkey 1 1\ncapture\nadvance 9\nkey 1 0\ncapture\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([(x['now_ms'],x['frame'][0]['rgb']) for x in r if x['phase']=='key'],[(5,257),(9,256)])
        self.assertEqual([x['now_ms'] for x in r if x['phase']=='capture'],[5,9])
        self.assertFalse(any(x['phase']=='tick' for x in r))
    def test_advance_requires_monotonic_bounded_time_and_running_guest(self):
        for commands in ('advance 60001\n','advance 7\nadvance 6\n','stop\nadvance 10\n'):
            with self.subTest(commands=commands):
                p,_=self.run_guest('valid',commands)
                self.assertEqual(p.returncode,2)
        p,r=self.run_guest('clock_valid','time 4\nadvance 5\ncapture\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['now_ms'] for x in r if x['phase']=='tick'],[5])
    def test_draw_only_advance_skips_clock_but_explicit_tick_stays_strict(self):
        guest=self.event_guest('draw-only')
        p,r=self.run_guest(guest,'advance 330\ncapture\ntime 500\nadvance 830\ncapture\n','--permissions','1')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['phase'] for x in r],['init','capture','capture','shutdown'])
        self.assertEqual([x['now_ms'] for x in r if x['phase']=='capture'],[330,830])
        self.assertTrue(all(x['frame'][0]['rgb']==0 for x in r if x['phase']=='capture'))
        p,r=self.run_guest(guest,'tick 100\n','--permissions','1')
        self.assertNotEqual(p.returncode,0)
        self.assertEqual(r[-2]['phase'],'tick')
        self.assertNotEqual(r[-2]['status'],0)
    def test_scripted_input_filters_permissions_and_legacy_subscription(self):
        guest=self.event_guest('release-only')
        commands='pointer 3 20 30\npointer 4 21 31\npointer 5 21 31\nkey 1 1\npointer 1 21 31\ncapture\n'
        p,r=self.run_guest(guest,commands,'--permissions','3')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['phase'] for x in r],['init','pointer','capture','shutdown'])
        self.assertEqual(r[1]['frame'][0]['rgb'],257)
        p,r=self.run_guest(guest,commands,'--permissions','1')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['phase'] for x in r],['init','capture','shutdown'])
        self.assertEqual(r[1]['frame'][0]['rgb'],0)
        p,r=self.run_guest(guest,'touch 21 31\n','--permissions','1')
        self.assertNotEqual(p.returncode,0)  # Explicit diagnostic still checks INPUT.
        self.assertEqual(r[-2]['phase'],'touch')
        self.assertNotEqual(r[-2]['status'],0)
    def test_scripted_input_delivers_only_subscribed_lifecycle_and_key(self):
        guest=self.event_guest('key-only',64)
        p,r=self.run_guest(guest,'pointer 3 20 30\npointer 4 21 31\npointer 5 21 31\nkey 1 1\nkey 1 0\npointer 1 21 31\n','--permissions','3')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        # RELEASE is the legacy event and is delivered even with a key-only mask.
        self.assertEqual([x['frame'][0]['rgb'] for x in r if x['phase'] in ('key','pointer')],[262,518,769])
        guest=self.event_guest('lifecycle-only',56)
        p,r=self.run_guest(guest,'pointer 3 20 30\npointer 4 21 31\npointer 5 21 31\nkey 1 1\npointer 1 21 31\n','--permissions','3')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual([x['frame'][0]['rgb'] for x in r if x['phase']=='pointer'],[259,516,773,1025])
if __name__=='__main__':unittest.main()
