"""Measure the actual C renderer against frozen observations and Fourier identities.

This module never generates oscillator audio or rewrites expectations.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import struct
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ROOT / "tests/reference/oscillator-targets-v1.json"


def read_pcm(path):
    with wave.open(str(path), "rb") as stream:
        assert stream.getframerate() == 48000 and stream.getnchannels() == 1
        assert stream.getsampwidth() == 4
        return np.frombuffer(stream.readframes(stream.getnframes()), dtype="<i4").astype(float) / 2**31


def render(renderer, folder, name, **settings):
    path = folder / (name + ".wav")
    subprocess.run([str(renderer), str(path), *[f"{key}={value}" for key, value in settings.items()]], check=True)
    y = read_pcm(path)
    assert np.isfinite(y).all() and np.max(abs(y)) <= 10**(-6/20), name
    return y


def harmonics(y, frequency, count):
    phase = 2 * np.pi * frequency * np.arange(len(y)) / 48000
    columns = [np.ones(len(y))]
    for h in range(1, count + 1):
        columns.extend([np.cos(h * phase), np.sin(h * phase)])
    coeff = np.linalg.lstsq(np.array(columns).T, y, rcond=None)[0]
    return (coeff[1::2] - 1j * coeff[2::2]) / 2


def frequency(y):
    centered = y - y.mean()
    assert np.ptp(centered) > 1e-10, "Silent/DC signal has no pitch"
    indices = np.flatnonzero((centered[:-1] <= 0) & (centered[1:] > 0))
    assert len(indices) >= 3, "Insufficient cycles"
    positions = indices - centered[indices] / (centered[indices + 1] - centered[indices])
    measured = float((len(positions) - 1) * 48000 / (positions[-1] - positions[0]))
    if measured > 100:
        spectrum = abs(np.fft.rfft(centered * np.hanning(len(centered))))
        spectrum[0] = 0
        k = int(spectrum.argmax())
        assert 0 < k < len(spectrum) - 1, "No resolved fundamental"
        a, b, c = np.log(np.maximum(spectrum[k-1:k+2], 1e-30))
        spectral = (k + .5*(a-c)/(a-2*b+c)) * 48000 / len(y)
        # Reset ringing can add crossings without adding oscillator cycles.
        if abs(measured / spectral - 1) > .01:
            return float(spectral)
    return measured


def frequency_checks(renderer, folder, targets):
    rows = []
    for i, row in enumerate(targets["lo_control_off"] + targets["tuning"]):
        hz = row["frequency_hz"]
        y = render(renderer, folder, f"frequency-{i}", seconds=max(6, 4/hz + 2), freq1=hz)[48000:]
        measured = frequency(y)
        cents = float(1200 * np.log2(measured / hz))
        rows.append(dict(input_hz=hz, measured_hz=measured, cents_error=cents, passed=abs(cents) <= 1))
    return rows


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def safe_path(root, name):
    relative = Path(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("Unsafe evidence path: " + name)
    result = (root / relative).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError("Evidence path escapes root: " + name)
    return result


def verify_evidence(root, targets):
    """Check the committed manifest anchors before using any private recording."""
    files = {}
    for anchor in targets["source_manifests"]:
        manifest = safe_path(root / "baselines", anchor["batch"] + ".json")
        if sha256(manifest) != anchor["sha256"]:
            raise ValueError("Changed integrity baseline: " + manifest.name)
        data = json.loads(manifest.read_text())
        batch = safe_path(root, data["relative_directory"])
        if data["batch"] != anchor["batch"] or data["count"] != anchor["count"]:
            raise ValueError("Wrong evidence inventory")
        expected = {name for name in data["files"] if name.endswith(".wav")}
        if expected != {p.name for p in batch.glob("*.wav")} or len(expected) != anchor["count"]:
            raise ValueError("Changed recording inventory: " + batch.name)
        for base, entries in ((batch, data["files"]), (root, data["shared_files"])):
            for name, checksum in entries.items():
                path = safe_path(base, name)
                if sha256(path) != checksum:
                    raise ValueError("Changed evidence: " + str(path))
                if name.endswith(".wav"):
                    files[checksum] = path
    return files


def read_float_audio(path):
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("Expected a RIFF WAV")
    fmt = samples = None
    at = 12
    while at + 8 <= len(data):
        tag, size = struct.unpack_from("<4sI", data, at)
        at += 8
        if at + size > len(data):
            raise ValueError("Truncated WAV chunk")
        if tag == b"fmt ":
            fmt = data[at:at+size]
        elif tag == b"data":
            samples = data[at:at+size]
        at += size + size % 2
    if fmt is None or len(fmt) < 16 or samples is None:
        raise ValueError("Missing WAV format/data")
    code, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", fmt)
    if code == 65534 and len(fmt) >= 40:
        code = struct.unpack_from("<H", fmt, 24)[0]
    if code != 3 or bits != 32 or rate != 48000 or channels not in (2, 6) or len(samples) % (4*channels):
        raise ValueError("Unexpected evidence format")
    y = np.frombuffer(samples, dtype="<f4").reshape(-1, channels).astype(float)
    if not np.isfinite(y).all():
        raise ValueError("Nonfinite evidence samples")
    return y


def local_checks(renderer, folder, targets, files):
    result = []
    for note in (60, 72, 84, 96, 108):
        source = []
        for shape in targets["audio_shapes"]:
            record = next(r for r in shape["validation"] if r["note"] == note)
            source.append(read_float_audio(files[record["source_sha256"]])[48000:96000, 0])
        hz = frequency(source[3])
        native_hz = round(hz * 65536) / 65536
        fit_count = min(16, int(19200/hz))
        count = min(8, fit_count)
        raw = [harmonics(y, hz, fit_count)[:count] for y in source]
        candidate = [harmonics(render(renderer, folder, f"local-shape{s}-n{note}", seconds=2,
                                      freq1=native_hz, wave1=s)[48000:96000], native_hz, fit_count)[:count] for s in range(7)]
        for shape in range(7):
            errors = abs(candidate[shape]/candidate[3] - raw[shape]/raw[3])
            error = float(max(errors))
            result.append(dict(note=note, shape=shape, max_complex_error=error,
                               raw_frequency_hz=hz, candidate_frequency_hz=native_hz,
                               harmonic_errors=errors.tolist(),
                               passed=error <= targets["tolerances"]["complex_ratio_absolute"]))
    return result


def carrier_crossings(y):
    smooth = np.convolve(y-y.mean(), np.ones(16)/16, mode="valid")
    at = np.flatnonzero((smooth[:-1] <= 0) & (smooth[1:] > 0))
    times = (at - smooth[at]/(smooth[at+1]-smooth[at]))/48000
    if len(times) < 32 or np.any(np.diff(times) <= 0):
        raise ValueError("Insufficient carrier cycles")
    return times


def slow_modulation(y, baseline):
    """The frozen crossing/whole-period estimator; no centering of pitch."""
    times = carrier_crossings(y)
    centers = (times[:-1]+times[1:])/2
    grid = np.arange(centers[0], centers[-1], .001)
    cents = 1200*np.log2(np.interp(grid, centers, 1/np.diff(times))/baseline)
    z = cents.copy(); z -= z.mean(); n = len(z)
    corr = np.fft.irfft(abs(np.fft.rfft(z, n=2*n))**2)[:n]
    energy = np.r_[0, np.cumsum(z*z)]; lag = np.arange(n)
    corr /= np.sqrt(np.maximum(energy[n-lag]*(energy[n]-energy[lag]), 1e-40))
    peaks = np.flatnonzero((corr[1:-1] > corr[:-2]) & (corr[1:-1] > corr[2:]))+1
    peaks = peaks[(peaks > 100) & (peaks < 5000)]
    if not len(peaks):
        raise ValueError("No resolved modulation period")
    k = peaks[corr[peaks] > .995*corr[peaks].max()][0]
    a, b, c = corr[k-1:k+2]
    modulation_hz = 1000/(k + .5*(a-c)/(a-2*b+c))
    periods = int((times[-1]-times[0])*modulation_hz)
    if periods < 2:
        raise ValueError("Need two complete modulation periods")
    start = times[0]; end = start + periods/modulation_hz
    mean_hz = (np.interp(end, times, np.arange(len(times))) - np.interp(start, times, np.arange(len(times))))/(end-start)
    t = np.linspace(start, end, round((end-start)*1000)+1)
    hz = np.interp(t, centers, 1/np.diff(times))
    return dict(pitch_span_cents=float(np.quantile(cents, .95)-np.quantile(cents, .05)),
                cents_of_mean_frequency=float(1200*np.log2(mean_hz/baseline)),
                time_mean_pitch_cents=float(np.trapezoid(1200*np.log2(hz/baseline), t)/(end-start)),
                modulation_hz=float(modulation_hz), repetition_correlation=float(b),
                complete_periods=periods)


def modulation_checks(renderer, folder, targets):
    rows = []
    baseline = round(targets["extraction"]["modulation_carrier_baseline_hz"]*65536)/65536
    for row in targets["modulation"]:
        feedback = row["feedback"]
        shape = targets["extraction"]["modulation_shape_to_global"][row["shape"]]
        source = targets["extraction"]["modulation_source_unmodulated_hz"]["control_on" if feedback else "control_off"]
        y = render(renderer, folder, f"mod-shape{shape}-feedback{int(feedback)}-amount{row['amount']}",
                   seconds=20 if feedback else 42, freq1=baseline, freq3=source, wave1=3, wave3=shape,
                   depth=row["calibrated_depth_octaves"], mod=1, control=int(feedback))[48000:-24000]
        measured = slow_modulation(y, baseline)
        errors = {key: measured[key] - row[key] for key in ("pitch_span_cents", "cents_of_mean_frequency", "time_mean_pitch_cents")}
        passed = abs(errors["pitch_span_cents"]) <= targets["tolerances"]["modulation_span_cents"]
        passed &= abs(errors["cents_of_mean_frequency"]) <= targets["tolerances"]["modulation_cents_of_mean_frequency"]
        passed &= abs(errors["time_mean_pitch_cents"]) <= targets["tolerances"]["modulation_time_mean_pitch_cents"]
        rows.append(dict(shape=shape, amount=row["amount"], feedback=feedback,
                         measured=measured, errors=errors, passed=bool(passed)))
    return rows


def refinement_checks(current, previous, targets):
    if current["target_sha256"] != previous["target_sha256"] or current["source_sha256"] != previous["source_sha256"]:
        raise ValueError("Refinement must use the same targets and C source")
    if current["factor"] != 2*previous["factor"]:
        raise ValueError("Refinement must double the internal rate")
    if len(current["modulation"]) != 48 or len(previous["modulation"]) != 48:
        raise ValueError("Refinement requires every frozen slow-modulation case")
    if not all(row["passed"] for report in (current, previous) for section in ("static", "alias", "frequencies", "modulation") for row in report[section]):
        raise ValueError("Refinement inputs must independently pass their targets")
    key = lambda row: (row["shape"], row["amount"], row["feedback"])
    before = {key(row): row for row in previous["modulation"]}
    if len(before) != 48 or set(before) != {key(row) for row in current["modulation"]}:
        raise ValueError("Refinement case settings differ")
    limits = dict(pitch_span_cents=targets["tolerances"]["modulation_span_cents"]/10,
                  cents_of_mean_frequency=targets["tolerances"]["modulation_cents_of_mean_frequency"]/10,
                  time_mean_pitch_cents=targets["tolerances"]["modulation_time_mean_pitch_cents"]/10)
    differences = {name: max(abs(row["measured"][name] - before[key(row)]["measured"][name]) for row in current["modulation"]) for name in limits}
    return dict(maximum_differences=differences, limits=limits,
                passed=all(differences[name] <= limits[name] for name in limits),
                scope="Steady slow-modulation summaries; complete-period means retain offsets. Reported filter delays differ; no samplewise waveform or audio-rate-FM convergence claim.")


def static_checks(renderer, folder, targets, factor):
    result = []
    for note in (60, 72, 84, 96, 108):
        hz = round(440 * 2**((note - 69) / 12) * 65536) / 65536
        fit_count = min(16, int(19200 / hz))
        count = min(8, fit_count)
        measured = []
        for shape in range(7):
            y = render(renderer, folder, f"shape{shape}-n{note}", seconds=2, freq1=hz, wave1=shape)
            measured.append(harmonics(y[48000:96000], hz, fit_count)[:count])
        for shape, coeff in enumerate(measured):
            expected = np.array([complex(*x) for x in targets["audio_shapes"][shape]["harmonic_ratios_to_saw"][:count]])
            error = float(np.max(abs(coeff / measured[3] - expected)))
            result.append(dict(note=note, shape=shape, max_complex_error=error,
                               passed=error <= targets["tolerances"]["complex_ratio_absolute"]))
        expected_fundamental = targets["choices"]["saw_peak"] * abs(response(hz, factor)) / (16 * np.pi)
        assert abs(abs(measured[3][0]) / expected_fundamental - 1) < .001
    return result


def fourier(shape, h, choices):
    """Static coefficient identities only; no sampled reference synthesizer."""
    saw = -1j * choices["saw_peak"] / (np.pi * h)
    triangle = (4 * choices["triangle_component_peak"] / (np.pi*h)**2 if h % 2 else 0)
    triangle += choices["triangle_saw_residue"] * saw
    if shape == 0:
        return triangle
    if shape == 1:
        a, b = choices["triangle_saw_coefficients"]
        return a * triangle + b * saw
    if shape == 2:
        return -saw
    if shape == 3:
        return saw
    return 2 * math.sin(np.pi * h * choices["pulse_high_fractions"][shape - 4]) / (np.pi*h)


def response(hz, factor):
    # Independent closed-form filter definition, not the candidate's coefficient array.
    gain = np.sinc(hz / (48000 * factor))**2
    k = np.arange(1, 48, 2)
    taps = np.sin(np.pi*k/2)/(np.pi*k) * (.42 + .5*np.cos(np.pi*k/48) + .08*np.cos(2*np.pi*k/48))
    rate_factor = factor
    while rate_factor > 1:
        gain *= .5 + 2 * np.sum(taps * np.cos(2*np.pi*hz*k/(48000*rate_factor)))
        rate_factor //= 2
    delay = 47 * (1 - 1 / factor)
    return gain * np.exp(-2j*np.pi*hz*delay/48000)


def alias_checks(renderer, folder, targets, factor):
    results = []
    for hz in (375, 997, 1500, 3000, 3001, 6000, 7001, 9000, 11003, 12000, 15997, 16000, 19001, 20000):
        for shape in range(7):
            y = render(renderer, folder, f"alias-shape{shape}-hz{hz}", seconds=2, freq1=hz, wave1=shape)[48000:96000]
            coeff = np.fft.rfft(y) / len(y)
            expected = np.zeros_like(coeff)
            for h in range(1, 20000 // hz + 1):
                expected[h*hz] = fourier(shape, h, targets["choices"]) * response(h*hz, factor) / 16
            # Include harmonic-bin errors too: coherent folded aliases can land there.
            power = np.sum(abs(coeff[20:20001] - expected[20:20001])**2)
            residual = float(10*np.log10(max(float(power / abs(expected[hz])**2), 1e-30)))
            transition_power = float(np.sum(abs(coeff[20001:])**2))
            results.append(dict(frequency_hz=hz, shape=shape, residual_db=residual,
                                dc=float(y.mean()), transition_band_power=transition_power,
                                passed=bool(residual <= -60 and abs(y.mean()) < 1e-6)))
    return results


def self_check():
    t = np.arange(48000) / 48000
    c = harmonics(.3 + .2*np.cos(2*np.pi*321.5*t) + .1*np.sin(4*np.pi*321.5*t), 321.5, 2)
    assert np.max(abs(c - [.1, -.05j])) < 1e-12
    assert abs(response(0, 8) - 1) < 1e-5


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--renderer", type=Path, default=ROOT / "out/render")
    parser.add_argument("--out", type=Path, default=ROOT / "out/oscillators")
    parser.add_argument("--factor", type=int, choices=(1, 2, 4, 8, 16), default=8)
    parser.add_argument("--reference-dir", type=Path)
    parser.add_argument("--modulation", action="store_true", help="Also run all frozen slow-modulation cases")
    parser.add_argument("--compare", type=Path, help="Compare slow-modulation summaries with a half-rate report")
    args = parser.parse_args()
    self_check()
    targets = json.loads(TARGETS.read_text())
    try:
        sources = verify_evidence(args.reference_dir, targets) if args.reference_dir is not None else None
    except (OSError, ValueError) as error:
        raise SystemExit("REFERENCE evidence check failed: " + str(error)) from error
    args.out.mkdir(parents=True, exist_ok=True)
    static = static_checks(args.renderer.resolve(), args.out, targets, args.factor)
    alias = alias_checks(args.renderer.resolve(), args.out, targets, args.factor)
    frequencies = frequency_checks(args.renderer.resolve(), args.out, targets)
    local = local_checks(args.renderer.resolve(), args.out, targets, sources) if sources is not None else []
    modulation = modulation_checks(args.renderer.resolve(), args.out, targets) if args.modulation else []
    report = dict(target_sha256=hashlib.sha256(TARGETS.read_bytes()).hexdigest(),
                  renderer_sha256=hashlib.sha256(args.renderer.read_bytes()).hexdigest(),
                  source_sha256=sha256(ROOT / "dsp/oscillator.c"),
                  output_delay_samples=47*(1-1/args.factor),
                  factor=args.factor, static=static, alias=alias, frequencies=frequencies, local=local, modulation=modulation,
                  alias_method="Coherent 1-second spectrum; 20 Hz–20 kHz residual against static Fourier identities with documented polynomial-kernel/filter response, including harmonic-bin errors; 20–24 kHz transition band reported separately",
                  scope="Static shapes, resolved-frequency inputs and optional slow-modulation summaries; dynamic FM alias/refinement, device cost and firmware integration remain separate")
    if args.compare:
        report["refinement"] = refinement_checks(report, json.loads(args.compare.read_text()), targets)
    (args.out / "validation.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    failures = [row for row in static + alias + frequencies + local + modulation if not row["passed"]]
    if args.compare and not report["refinement"]["passed"]:
        failures.append(report["refinement"])
    for row in failures:
        print("FAIL:", row)
    print(f"{'FAIL' if failures else 'PASS'}: {len(static)} frozen signed-shape, {len(alias)} static spectral, {len(frequencies)} frequency, {len(local)} local recording and {len(modulation)} slow-modulation cases")
    raise SystemExit(bool(failures))


if __name__ == "__main__":
    main()
