"""Exercise the shipped WAV reader and the same feature extractor used by firmware."""
import csv
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

TOOL = Path(sys.argv.pop(1)).resolve()

class Features(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.wav = Path(tmp.name) / "input.wav"
        self.csv = Path(tmp.name) / "out.csv"

    def write(self, samples, rate=16000):
        with wave.open(str(self.wav), "wb") as w:
            w.setparams((1, 2, rate, 0, "NONE", ""))
            w.writeframes(struct.pack(f"<{len(samples)}h", *samples))

    def run_tool(self):
        return subprocess.run([TOOL, self.wav, self.csv], capture_output=True)

    def frames(self, signal, count=120):
        self.write([int(signal(i)) for i in range(count * 160)])
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        with self.csv.open() as stream:
            return [{k: int(v) for k, v in r.items()} for r in csv.DictReader(stream)]

    def test_silence_and_floor(self):
        frames = self.frames(lambda i: 0)
        self.assertEqual([f['seq'] for f in frames], list(range(120)))
        self.assertEqual([f['t_ms'] for f in frames], list(range(10, 1201, 10)))
        self.assertTrue(all(f['rms_db_q8'] == -25600 and f['pitch_hz_q4'] == 0 for f in frames))
        self.assertTrue(all(f['flags'] & 8 for f in frames[:100]))
        self.assertEqual(frames[-1]['flags'], 0)

    def test_tone_pitch_band_and_repeatability(self):
        f = self.frames(lambda i: 3276.8 * math.sin(i * math.tau / 16))[-1]
        self.assertAlmostEqual(f['rms_db_q8'] / 256, -23, delta=0.2)
        self.assertGreater(f['band1_q15'], .9 * 32768)
        self.assertAlmostEqual(f['pitch_hz_q4'] / 16, 1000, delta=5)
        self.assertGreater(f['pitch_conf_q15'], .9 * 32768)
        self.assertAlmostEqual(f['rms_db_q8'], f['floor_db_q8'], delta=8)
        first = self.csv.read_bytes()
        self.assertEqual(self.run_tool().returncode, 0)
        self.assertEqual(first, self.csv.read_bytes())

    def test_decimation_rejects_high_frequency_alias(self):
        frames = self.frames(lambda i: 8000 * math.sin(i * math.tau * 7500 / 16000), 18)
        self.assertTrue(all(f['pitch_hz_q4'] == 0 and f['pitch_conf_q15'] == 0 for f in frames[5:]))

    def test_pitch_at_lower_limit(self):
        for hz in (80, 83.5):
            frames = self.frames(lambda i: 8000 * math.sin(i * math.tau * hz / 16000), 18)
            for frame in frames[5:]:
                self.assertAlmostEqual(frame['pitch_hz_q4'] / 16, hz, delta=1)
                self.assertGreater(frame['pitch_conf_q15'], .9 * 32768)

    def test_saw_noise_clipping_and_cross_frame_zcr(self):
        saw = self.frames(lambda i: ((i * 220 / 16000) % 1 - .5) * 8192)[-1]
        self.assertAlmostEqual(saw['pitch_hz_q4'] / 16, 220, delta=2)
        rng = random.Random(77)
        noise = self.frames(lambda i: rng.uniform(-8000, 8000), 60)[5:]
        # A single periodogram of white noise has flatness near exp(-EulerGamma).
        flat = sum(f['flatness_q15'] for f in noise) / (32768 * len(noise))
        self.assertTrue(.45 < flat < .70, flat)
        self.assertTrue(all(f['pitch_conf_q15'] < .3 * 32768 for f in noise))
        # An impulse has equal power at all FFT bins (a flat reference spectrum).
        impulse = self.frames(lambda i: 24000 if i == 600 else 0, 4)[-1]
        self.assertGreater(impulse['flatness_q15'], .7 * 32768)
        edges = self.frames(lambda i: 32767 if i < 160 else -32768, 2)
        self.assertEqual([f['zcr'] for f in edges], [0, 1])
        self.assertTrue(all(f['flags'] & 2 for f in edges))

    def test_recorded_host_flags_control_floor_history(self):
        metadata = {'start_ms': 43000, 'settling': [[0, 1600]],
                    'playback': [[1600, 1920]], 'overrun': [[2080, 2240]]}
        self.wav.with_suffix('.json').write_text(json.dumps(metadata), encoding='utf-8')
        fx = self.frames(lambda i: 1000 * math.sin(i * math.tau / 16), 15)
        self.assertEqual([f['t_ms'] for f in fx], list(range(43010, 43151, 10)))
        self.assertTrue(all(f['floor_db_q8'] == -25600 for f in fx[:12]))
        self.assertEqual(fx[12]['floor_db_q8'], fx[12]['rms_db_q8'])
        self.assertEqual([f['seq'] for f in fx if f['flags'] & 1], [10, 11])
        self.assertEqual([f['seq'] for f in fx if f['flags'] & 4], [13])
        metadata['settling'] = [[160, 0]]
        self.wav.with_suffix('.json').write_text(json.dumps(metadata), encoding='utf-8')
        self.assertNotEqual(self.run_tool().returncode, 0)

    def test_invalid_wav(self):
        self.write([0]*160, 8000)
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.write([0]*160)
        self.wav.write_bytes(self.wav.read_bytes()[:-2])
        self.assertNotEqual(self.run_tool().returncode, 0)

if __name__ == "__main__":
    unittest.main()
