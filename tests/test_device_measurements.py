"""Capture and shared-phase measurement tests; no oscillator model."""
import sys
from pathlib import Path
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_device_oscillator import coefficients, common_frequency


class DeviceMeasurements(unittest.TestCase):
    def test_global_phase_and_common_frequency(self):
        hz = 321.234567
        t = np.arange(8*48000) / 48000
        y = .2 + .3*np.cos(2*np.pi*hz*t+.7) + .1*np.sin(4*np.pi*hz*t+.2)
        rows = [dict(start_frame=a, end_frame=a+48000) for a in (123, 3*48000+491, 6*48000+719)]
        measured = common_frequency(y, rows)
        self.assertLess(abs(measured-hz), 1e-7)
        for row in rows:
            c = coefficients(y, row, measured, 2)
            self.assertLess(abs(c[0]-.15*np.exp(.7j)), 1e-7)
            self.assertLess(abs(c[1]+.05j*np.exp(.2j)), 1e-7)
        with self.assertRaises(ValueError):common_frequency(y, rows[:1])


class CaptureValidation(unittest.TestCase):
    def test_signed_pcm_windows_hash_and_restore_checks(self):
        import hashlib
        import json
        import tempfile
        import wave
        from check_device_oscillator import read_capture
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name)
            path=folder/'audio.wav'
            raw=b''.join(v.to_bytes(3,'little',signed=True) for v in (0,8388607,-8388608,-1))
            with wave.open(str(path),'wb') as stream:
                stream.setparams((2,3,48000,0,'NONE','not compressed'));stream.writeframes(raw)
            state=dict(decoded=dict(frequency_hz=321.))
            report=dict(mode='pitch',status='captured',restored=True,audio_status=[],frames=2,
                        wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                        segments=[dict(start_frame=0,end_frame=2,before=state,after=state)])
            def save(): (folder/'capture.json').write_text(json.dumps(report))
            save();loaded,audio=read_capture(folder)
            np.testing.assert_array_equal(audio,[[0,8388607/8388608],[-1,-1/8388608]])
            self.assertEqual(loaded['analog'],'not recorded')
            state['decoded']['analog']=False;save()
            self.assertEqual(read_capture(folder)[0]['analog'],'off')
            state['decoded']['analog']=True;save()
            with self.assertRaises(ValueError):read_capture(folder)
            del state['decoded']['analog'];save()
            report['restored']=False;save()
            with self.assertRaises(ValueError):read_capture(folder)
            report['restored']=True;report['segments'][0]['end_frame']=3;save()
            with self.assertRaises(ValueError):read_capture(folder)
            report['segments'][0]['end_frame']=2;save()
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises(ValueError):read_capture(folder)
            report['wav_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();save()
            with self.assertRaises(ValueError):read_capture(folder)


if __name__ == '__main__':
    unittest.main()
