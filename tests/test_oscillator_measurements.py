"""Measurement and evidence-boundary regressions, with no sound model."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("checks", ROOT / "tools/check_oscillators.py")
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)


class MeasurementTests(unittest.TestCase):
    def test_source_phases_cover_targets_and_lag_sign(self):
        targets = json.loads(checks.TARGETS.read_text())
        self.assertEqual(len(checks.source_phases(targets)), len(targets["modulation"]))
        t = np.arange(8000) / 1000
        ramp = 2 ** ((t * 2) % 1)
        lead = 2 ** ((t * 2 + .04) % 1)
        self.assertEqual(checks.trace_lag(ramp, lead, 500), -20)
        self.assertEqual(checks.trace_lag(lead, ramp, 500), 20)

    def test_time_weighted_quantiles_and_observed_cycle_boundaries(self):
        self.assertLess(max(abs(checks.time_quantiles([0, 1], [100, 200], [.05, .95]) - [105, 195])), 1e-10)
        self.assertLess(max(abs(checks.time_quantiles([0, 1, 10], [100, 100, 100], [.05, .95]) - 100)), 1e-10)
        times = np.arange(9.)
        pitch = np.array([200, 100, 200, 100, 200, 100, 200, 100, 200])
        result = checks.cycle_span_from_pitch(times, pitch)
        self.assertEqual(result["complete_periods"], 3)
        self.assertLess(abs(result["span_cents"] - 1200*np.log2(195/105)), 1e-9)
        for t, values in (([0, 0], [1, 2]), ([0, 1], [1, float('nan')])):
            with self.assertRaises(ValueError): checks.time_quantiles(t, values, [.5])
        with self.assertRaises(ValueError): checks.cycle_span_from_pitch([0, 1, 2], [1, 1, 1])

    def test_harmonic_sign_and_dc(self):
        checks.self_check()
        t = np.arange(48000) / 48000
        signal = .3 + .2*np.cos(2*np.pi*375*t) + .1*np.sin(4*np.pi*375*t)
        c = checks.harmonics(signal, 375, 2)
        reverse = checks.harmonics(-signal, 375, 2)
        self.assertLess(max(abs(c + reverse)), 1e-12)
        self.assertGreater(max(abs(c - reverse)), .19)

    def test_reset_ringing_does_not_count_as_extra_cycles(self):
        t = np.arange(96000) / 48000
        phase = (437.25*t) % 1
        diagnostic = 2*phase - 1 + .4*np.sin(2*np.pi*12*phase)
        self.assertLess(abs(checks.frequency(diagnostic) - 437.25), .02)

    def test_whole_period_frequency_mean_is_not_mean_cents(self):
        t = np.arange(8*48000)/48000
        phase = 260*t + 20/(4*np.pi)*np.sin(4*np.pi*t)
        result = checks.slow_modulation(np.sin(2*np.pi*phase), 260)
        self.assertLess(abs(result["cents_of_mean_frequency"]), .02)
        self.assertGreater(result["time_mean_pitch_cents"], -3)
        self.assertLess(result["time_mean_pitch_cents"], -2)
        self.assertLess(abs(result["modulation_hz"] - 2), .01)

    def test_timing_drift_is_reported_only_when_measured(self):
        targets = json.loads(checks.TARGETS.read_text())
        row = next(r for r in targets["modulation"] if r["feedback"])
        one = dict(targets, modulation=[row])
        phases = checks.source_phases(targets)
        fresh = dict(phases[row["source_sha256"]], initial_lag_ms=0, end_lag_ms=-7,
                     candidate_rate_difference_percent=-0.5)
        with tempfile.TemporaryDirectory() as name:
            [historical] = checks.modulation_checks(ROOT / "out/render", Path(name), one, phases)
            [measured] = checks.modulation_checks(ROOT / "out/render", Path(name), one, phases, dict(cases=[fresh]))
        self.assertNotIn("measured_timing", historical)
        self.assertEqual(historical["committed_timing"]["end_lag_ms"], phases[row["source_sha256"]]["end_lag_ms"])
        self.assertIsNone(checks.timing_summary([historical]))
        self.assertEqual(measured["measured_timing"]["end_lag_ms"], -7)
        self.assertIn("-0.500% modulation rate, -7 ms", checks.timing_summary([historical, measured]))

    def test_actual_wrong_sign_and_width_fail_frozen_ratios(self):
        targets = json.loads(checks.TARGETS.read_text())
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            saw = checks.render(ROOT / "out/render", folder, "saw", seconds=2, freq1=375, wave1=3)[48000:]
            wide = checks.render(ROOT / "out/render", folder, "wide", seconds=2, freq1=375, wave1=5)[48000:]
            a = checks.harmonics(saw, 375, 8)
            b = checks.harmonics(wide, 375, 8)
            reverse_target = np.array([complex(*x) for x in targets["audio_shapes"][2]["harmonic_ratios_to_saw"]])
            square_target = np.array([complex(*x) for x in targets["audio_shapes"][4]["harmonic_ratios_to_saw"]])
            tolerance = targets["tolerances"]["complex_ratio_absolute"]
            self.assertGreater(max(abs(a/a - reverse_target)), tolerance)
            self.assertGreater(max(abs(b/a - square_target)), tolerance)

    def test_renderer_rejects_invalid_settings_before_writing(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "invalid.wav"
            for setting in ("seconds=nan", "seconds=0", "seconds=0.00000001", "freq1=inf", "freq1=20001", "wave1=3x", "wave1=9", "level1=-1", "phase1=1", "phase2=-.1", "phase3=nan", "unknown=1"):
                run = subprocess.run([str(ROOT / "out/render"), str(path), setting], capture_output=True)
                self.assertEqual(run.returncode, 2, setting)
                self.assertFalse(path.exists())

    def test_host_phase_override_preserves_default_and_quarter_cycle(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            dry = checks.render(ROOT / "out/render", folder, "dry", seconds=2, freq1=375)
            same = checks.render(ROOT / "out/render", folder, "same", seconds=2, freq1=375, phase1=0)
            shifted = checks.render(ROOT / "out/render", folder, "shifted", seconds=2, freq1=375, phase1=.25)
            self.assertTrue(np.array_equal(dry, same))
            a = checks.harmonics(dry[48000:], 375, 1)[0]
            b = checks.harmonics(shifted[48000:], 375, 1)[0]
            self.assertLess(abs(b/a - 1j), 1e-5)

    def test_float_wav_validation(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "test.wav"
            fmt = struct.pack("<HHIIHH", 3, 2, 48000, 384000, 8, 32)
            def write(samples):
                body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt
                body += b"data" + struct.pack("<I", len(samples)) + samples
                path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
            write(struct.pack("<ffff", .1, .2, .3, .4))
            self.assertEqual(checks.read_float_audio(path).shape, (2, 2))
            write(struct.pack("<ff", float("nan"), 0))
            with self.assertRaises(ValueError): checks.read_float_audio(path)
            path.write_bytes(b"RIFFxxxxWAVEdata" + struct.pack("<I", 123))
            with self.assertRaises(ValueError): checks.read_float_audio(path)

    def test_integrity_rejects_missing_changed_extra_and_unsafe(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); batch = root / "batch"; batch.mkdir()
            (root / "baselines").mkdir()
            recording = batch / "one.wav"; recording.write_bytes(b"integrity test fixture")
            manifest = root / "baselines/batch.json"
            body = dict(batch="batch", count=1, relative_directory="batch",
                        files={"one.wav": checks.sha256(recording)}, shared_files={})
            manifest.write_text(json.dumps(body))
            target = dict(source_manifests=[dict(batch="batch", count=1, sha256=checks.sha256(manifest))])
            self.assertEqual(len(checks.verify_evidence(root, target)), 1)
            original = recording.read_bytes()
            recording.write_bytes(b"changed")
            with self.assertRaises(ValueError): checks.verify_evidence(root, target)
            recording.unlink()
            with self.assertRaises(ValueError): checks.verify_evidence(root, target)
            recording.write_bytes(original); extra = batch / "extra.wav"; extra.write_bytes(original)
            with self.assertRaises(ValueError): checks.verify_evidence(root, target)
            extra.unlink(); manifest.write_text("{}")
            with self.assertRaises(ValueError): checks.verify_evidence(root, target)
            with self.assertRaises(ValueError): checks.safe_path(root, "../escape")


if __name__ == "__main__":
    unittest.main()
