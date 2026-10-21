"""Measure retained oscillator1 device captures against C and frozen observations."""
import argparse
import json
from pathlib import Path
import wave

import numpy as np

from check_oscillators import (TARGETS, carrier_crossings, frequency, harmonics,
                               render, sha256, time_quantiles, verify_evidence)

RATE = 48000


def read_capture(folder):
    report = json.loads((folder / 'capture.json').read_text())
    if report['status'] != 'captured' or not report.get('restored') or report['audio_status']:
        raise ValueError('A successful capture and restoration are required')
    if report['mode'] not in {'waves', 'pitch', 'levels', 'lfo', 'notes'}:
        raise ValueError('Unsupported capture mode')
    path = folder / 'audio.wav'
    if sha256(path) != report['wav_sha256']:
        raise ValueError('Changed capture audio')
    with wave.open(str(path), 'rb') as stream:
        frames = stream.getnframes()
        if stream.getframerate() != RATE or stream.getnchannels() != 2 or stream.getsampwidth() != 3:
            raise ValueError('Expected 48kHz stereo PCM24')
        data = stream.readframes(frames)
    if frames != report['frames'] or len(data) != frames * 6:
        raise ValueError('Incomplete capture')
    b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    samples = (((b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16) ^ 0x800000) - 0x800000).reshape(-1, 2) / 2**23
    for row in report['segments']:
        if not 0 <= row['start_frame'] < row['end_frame'] <= frames:
            raise ValueError('Window outside captured audio')
        if 'trace' not in row and row['before']['decoded'] != row['after']['decoded']:
            raise ValueError('Parameters changed inside a static window')
    return report, samples


def coefficients(samples, row, hz, count):
    start, end = row['start_frame'], row['end_frame']
    local = harmonics(samples[start:end], hz, count)
    return local * np.exp(-2j * np.pi * hz * start / RATE * np.arange(1, count + 1))


def common_frequency(samples, rows):
    """One frequency from repeated saws; retain the recording's shared phase origin."""
    if len(rows) < 2:
        raise ValueError('Need repeated saw windows')
    first = rows[0]
    hz = frequency(samples[first['start_frame']:first['end_frame']])
    times = np.array([(r['start_frame'] + r['end_frame']) / (2 * RATE) for r in rows])
    if np.ptp(times) == 0:
        raise ValueError('Saw windows need distinct times')
    for _ in range(3):
        phase = np.unwrap([np.angle(coefficients(samples, row, hz, min(16, int(19200 / hz)))[0]) for row in rows])
        hz += np.polyfit(times - times[0], phase, 1)[0] / (2 * np.pi)
    return float(hz)


def waveform_checks(report, audio, renderer, out, targets):
    results = []
    rows = [r for r in report['segments'] if r['name'].startswith('wave')]
    for requested in sorted({int(r['name'].split('-note')[1].split('-')[0]) for r in rows}):
        group = [r for r in rows if int(r['name'].split('-note')[1].split('-')[0]) == requested]
        note = group[0]['before']['note_q16']
        saws = [r for r in group if r['before']['decoded']['waveform'] == 3]
        if {r['before']['decoded']['waveform'] for r in group} != {0, 1, 3, 4, 5, 6}:
            raise ValueError('Incomplete oscillator1 waveform set')
        if any(np.ptp(audio[r['start_frame']:r['end_frame'], 0]) <= 1e-10 for r in saws):
            results.extend(dict(name=r['name'], nominal_note=requested, native_note=r['before']['note_q16']/65536,
                                status='no AC signal; native note path did not trigger', passed_frozen_ratio=False) for r in group)
            continue
        native = group[0]['before']['decoded']['frequency_hz']
        if any(r['before']['decoded']['frequency_hz'] != native for r in group):
            raise ValueError('Waveform group changed native pitch')
        hz = common_frequency(audio[:, 0], saws)
        fit_count = min(16, int(19200 / hz))
        count = min(8, fit_count)
        baseline = coefficients(audio[:, 0], saws[0], hz, fit_count)[:count]
        candidate = {}
        for shape in (0, 1, 3, 4, 5, 6):
            y = render(renderer, out, f'c-requested{requested}-wave{shape}', seconds=2, freq1=native, wave1=shape)
            candidate[shape] = harmonics(y[48000:], native, fit_count)[:count]
        for row in group:
            shape = row['before']['decoded']['waveform']
            observed = coefficients(audio[:, 0], row, hz, fit_count)[:count] / baseline
            c_ratio = candidate[shape] / candidate[3]
            reference = np.array([complex(*x) for x in targets['audio_shapes'][shape]['harmonic_ratios_to_saw'][:count]])
            to_c = abs(observed - c_ratio)
            to_ref = abs(observed - reference)
            results.append(dict(name=row['name'], nominal_note=requested, requested_midi_note=row.get('requested_midi_note', requested), native_note=note / 65536, shape=shape, common_hz=hz,
                                native_hz=native, pitch_error_cents=float(1200 * np.log2(hz / native)),
                                observed_ratio=[[z.real, z.imag] for z in observed],
                                c_ratio=[[z.real, z.imag] for z in c_ratio],
                                reference_ratio=[[z.real, z.imag] for z in reference],
                                error_to_c=to_c.tolist(), error_to_reference=to_ref.tolist(),
                                passed_frozen_ratio=bool(max(to_ref) <= targets['tolerances']['complex_ratio_absolute'])))
    return results


def pitch_checks(report, audio):
    results = []
    for row in report['segments']:
        if row['name'].startswith('muted'):
            continue
        y = audio[row['start_frame']:row['end_frame'], 0]
        nominal = int(row['name'].split('-note')[1]) if '-note' in row['name'] else None
        native_note = row['before']['note_q16']/65536
        if (row['name'].startswith('pitch-note') and native_note != nominal) or np.ptp(y) <= 1e-10:
            results.append(dict(name=row['name'], requested_midi_note=row.get('requested_midi_note', nominal),
                                native_note=row['before']['note_q16']/65536, status='native note not triggered or no AC signal', rms=float(np.sqrt(np.mean(y*y))), measured_hz=None, error_cents=None))
            continue
        hz = frequency(y)
        expected = row['before']['decoded']['frequency_hz']
        results.append(dict(name=row['name'], requested_midi_note=row.get('requested_midi_note', nominal),
                            native_note=row['before']['note_q16']/65536, native_hz=expected, measured_hz=hz,
                            error_cents=float(1200 * np.log2(hz / expected)),
                            rms=float(np.sqrt(np.mean(y*y)))))
    return results


def level_checks(report, audio):
    rows = [r for r in report['segments'] if r['name'].startswith('level')]
    hz = frequency(audio[rows[0]['start_frame']:rows[0]['end_frame'], 0])
    baseline = abs(coefficients(audio[:, 0], rows[0], hz, 16)[0])
    result = []
    for row in rows:
        expected = row['before']['decoded']['level']
        actual = abs(coefficients(audio[:, 0], row, hz, 16)[0]) / baseline
        result.append(dict(name=row['name'], expected=expected, measured=float(actual),
                           error=float(actual - expected),
                           error_db=float(20*np.log10(actual / expected)) if expected and actual else None))
    return result


def lfo_checks(report, audio):
    result = []
    for row in report['segments']:
        if 'trace' not in row:
            continue
        trace = row['trace']
        y = audio[row['start_frame']:row['end_frame'], 0]
        native_hz = np.array([r['decoded']['frequency_hz'] for r in trace])
        native_level = np.array([r['decoded']['level'] for r in trace])
        native_shapes = sorted({r['decoded']['waveform'] for r in trace})
        measured = dict(name=row['name'], native_hz_range=[float(native_hz.min()), float(native_hz.max())],
                        native_level_range=[float(native_level.min()), float(native_level.max())],
                        native_shapes=native_shapes, points=len(trace),
                        probe_window_frames_max=max(r['probe_frames'] for r in trace))
        if np.ptp(native_hz) > 1:
            times = carrier_crossings(y)
            hz = 1 / np.diff(times)
            measured['audio_frequency_quantiles'] = time_quantiles((times[:-1]+times[1:])/2, hz, [.01,.5,.99]).tolist()
            measured['native_frequency_quantiles'] = np.quantile(native_hz, [.01, .5, .99]).tolist()
        if np.ptp(native_level) > .01:
            base = float(np.median(native_hz))
            size = round(32 * RATE / base)
            amplitudes = [abs(harmonics(y[i:i+size], base, 16)[0]) for i in range(0, len(y)-size, size//2)]
            lo, hi = np.quantile(amplitudes, [.1, .9])
            measured['audio_level_ratio'] = float(hi/lo)
            measured['native_level_ratio'] = float(native_level.max()/native_level.min())
            measured['level_ratio_error_db'] = float(20*np.log10(hi/lo / measured['native_level_ratio']))
        if len(native_shapes) > 1:
            base = float(np.median(native_hz))
            fingerprints = []
            for i in range(0, len(y)-4800, 2400):
                h = harmonics(y[i:i+4800], base, 8)
                fingerprints.append(float(abs(h[1]/h[0])))
            measured['audio_second_harmonic_ratio_quantiles'] = np.quantile(fingerprints, [.1, .5, .9]).tolist()
        result.append(measured)
    return result


def continuity_checks(report, audio):
    rows = [r for r in report['segments']
            if r['name'].startswith(('repeat-note', 'unmuted-held'))]
    if len({r['before']['decoded']['frequency_hz'] for r in rows}) != 1:
        raise ValueError('Continuity windows changed native pitch')
    hz = common_frequency(audio[:, 0], rows)
    h1 = np.array([coefficients(audio[:, 0], r, hz, 16)[0] for r in rows])
    return dict(common_hz=hz, names=[r['name'] for r in rows],
                phase_difference_degrees=np.degrees(np.angle(h1 / h1[0])).tolist(),
                amplitude_ratio=abs(h1 / h1[0]).tolist())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--renderer', type=Path, default=Path('out/render'))
    parser.add_argument('--reference-dir', type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    targets = json.loads(TARGETS.read_text())
    files = verify_evidence(args.reference_dir, targets) if args.reference_dir else {}
    report, audio = read_capture(args.capture)
    result = dict(mode=report['mode'], capture_sha256=sha256(args.capture/'capture.json'),
                  wav_sha256=report['wav_sha256'], target_sha256=sha256(TARGETS),
                  renderer_sha256=sha256(args.renderer),
                  c_source_sha256=sha256(Path('dsp/oscillator.c')), decoder_source_sha256=sha256(Path('machines/digitakt/controls.c')),
                  verified_reference_recordings=sum(r['count'] for r in targets['source_manifests']) if files else 0,
                  verified_reference_unique_hashes=len(files),
                  firmware_sha256=report['firmware_sha256'], observation='Digitakt USB stereo after native processing',
                  method=('one common frequency from repeated saws; no independent waveform phase or gain fit'
                          if report['mode'] == 'waves' else 'endpoint measurements against actual C-decoded native parameters'),
                  muted=[], peak_channels=abs(audio).max(axis=0).tolist(),
                  rms_channels=np.sqrt(np.mean(audio*audio, axis=0)).tolist())
    for row in report['segments']:
        if row['name'].startswith('muted'):
            y = audio[row['start_frame']:row['end_frame']]
            result['muted'].append(dict(name=row['name'], peak=float(abs(y).max()),
                                       ac_peak=float(abs(y - y.mean(axis=0)).max())))
    if report['mode'] == 'waves':
        result['waveforms'] = waveform_checks(report, audio, args.renderer.resolve(), args.out, targets)
    elif report['mode'] == 'lfo':
        result['lfo'] = lfo_checks(report, audio)
    elif report['mode'] == 'notes':
        result['continuity'] = continuity_checks(report, audio)
    elif report['mode'] == 'levels':
        result['levels'] = level_checks(report, audio)
    else:
        result['pitch'] = pitch_checks(report, audio)
    (args.out/'analysis.json').write_text(json.dumps(result, indent=2)+'\n')
    print('Measured', report['mode'], 'into', args.out/'analysis.json')
    if 'waveforms' in result and not all(r['passed_frozen_ratio'] for r in result['waveforms']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
