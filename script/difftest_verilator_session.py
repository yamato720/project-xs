#!/usr/bin/env python3
"""Compare Verilator samples against Project-XS waveform frames."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


def load_json(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        data = json.load(source)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def load_jsonl(path: Path) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            text = line.strip()
            if not text:
                continue
            value = json.loads(text)
            if not isinstance(value, dict):
                raise ValueError(f"{path}:{line_number} must contain a JSON object")
            rows.append(value)
    return rows


def latest_segment(root: Path) -> Path:
    if not root.exists():
        raise FileNotFoundError(f"XS trace root not found: {root}")

    candidates = [
        entry
        for entry in root.iterdir()
        if entry.is_dir() and (entry / "manifest.json").exists()
    ]
    if not candidates:
        raise FileNotFoundError(f"no trace segment found under {root}")
    return max(candidates, key=lambda path: (path / "manifest.json").stat().st_mtime_ns)


def normalize_int(value: Any) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip().lower()
        if text in {"z", "x", ""}:
            raise ValueError(f"non-numeric signal value: {value!r}")
        return int(text, 0)
    raise ValueError(f"unsupported signal value: {value!r}")


def signal_key(signal: Dict[str, Any]) -> str:
    path = signal.get("name_path")
    if isinstance(path, list) and path:
        return ".".join(str(part) for part in path)
    return ".".join(
        part
        for part in [str(signal.get("scope", "")), str(signal.get("name", ""))]
        if part
    )


def flatten_xs_signals(frame: Dict[str, Any], simulator_name: Optional[str]) -> Dict[str, Dict[str, Any]]:
    if simulator_name is None:
        return {
            signal_key(signal): signal
            for signal in frame.get("signals", [])
            if isinstance(signal, dict)
        }

    for simulator in frame.get("simulators", []):
        if simulator.get("name") == simulator_name:
            return {
                signal_key(signal): signal
                for signal in simulator.get("signals", [])
                if isinstance(signal, dict)
            }
    raise KeyError(f"simulator not found in XS frame: {simulator_name}")


def cycle_from_sample(sample: Dict[str, Any]) -> int:
    return normalize_int(sample.get("cycle"))


def sample_stage(sample: Dict[str, Any]) -> str:
    return str(sample.get("stage", ""))


def verilog_samples_by_xs_cycle(
    samples: Iterable[Dict[str, Any]],
    offset: int,
    stage: str,
) -> Dict[int, Dict[str, Any]]:
    indexed: Dict[int, Dict[str, Any]] = {}
    for sample in samples:
        if sample_stage(sample) != stage:
            continue
        xs_cycle = cycle_from_sample(sample) + offset
        if xs_cycle in indexed:
            raise ValueError(f"duplicate Verilog sample for stage {stage} XS cycle {xs_cycle}")
        indexed[xs_cycle] = sample
    return indexed


def frame_cycle(frame: Dict[str, Any]) -> int:
    if "current_tick" in frame:
        return normalize_int(frame["current_tick"])
    root_timing = frame.get("root_timing")
    if isinstance(root_timing, dict) and "cycle" in root_timing:
        return normalize_int(root_timing["cycle"])
    return normalize_int(frame.get("cycle"))


def xs_frames_by_stage(frames: Iterable[Dict[str, Any]], stage: str) -> Dict[int, Dict[str, Any]]:
    indexed: Dict[int, Dict[str, Any]] = {}
    for frame in frames:
        if frame.get("stage") != stage:
            continue
        cycle = frame_cycle(frame)
        if cycle in indexed:
            raise ValueError(f"duplicate XS frame for stage {stage} cycle {cycle}")
        indexed[cycle] = frame
    return indexed


def compare_one(config: Dict[str, Any], config_path: Path) -> Tuple[int, int]:
    config_dir = config_path.parent
    xs_root = (config_dir / config["xs_trace_root"]).resolve()
    xs_segment = latest_segment(xs_root)
    xs_waveform = xs_segment / config.get("xs_waveform", "waveform.jsonl")
    verilog_samples_path = (config_dir / config["verilog_samples"]).resolve()

    xs_stage = str(config.get("xs_stage", "SessionStepEnd"))
    verilog_stage = str(config.get("verilog_stage", "cycle_end"))
    xs_frames = xs_frames_by_stage(load_jsonl(xs_waveform), xs_stage)
    verilator_samples = verilog_samples_by_xs_cycle(
        load_jsonl(verilog_samples_path),
        int(config.get("cycle_offset", 0)),
        verilog_stage,
    )

    simulator_name = config.get("simulator")
    simulator_name = str(simulator_name) if simulator_name is not None else None
    mappings = config.get("signals")
    if not isinstance(mappings, list) or not mappings:
        raise ValueError("config must contain a non-empty signals list")
    if not xs_frames:
        raise ValueError(f"XS waveform has no frames for stage {xs_stage}")
    if not verilator_samples:
        raise ValueError(f"Verilator samples have no rows for stage {verilog_stage}")

    compared = 0
    mismatches: List[str] = []
    missing_xs_cycles = sorted(set(verilator_samples) - set(xs_frames))
    if missing_xs_cycles:
        raise ValueError(f"XS waveform missing cycles: {missing_xs_cycles}")

    for xs_cycle, sample in sorted(verilator_samples.items()):
        xs_signals = flatten_xs_signals(xs_frames[xs_cycle], simulator_name)
        verilog_signals = sample.get("signals", {})
        if not isinstance(verilog_signals, dict):
            raise ValueError(f"Verilog sample at cycle {sample.get('cycle')} has no signals object")

        for mapping in mappings:
            verilog_name = str(mapping["verilog"])
            xs_name = str(mapping["xs"])
            if verilog_name not in verilog_signals:
                raise KeyError(f"Verilog signal not found: {verilog_name}")
            if xs_name not in xs_signals:
                raise KeyError(f"XS signal not found: {xs_name}")

            xs_signal = xs_signals[xs_name]
            if xs_signal.get("valid") is False:
                mismatches.append(
                    f"cycle {xs_cycle}: {xs_name} is invalid/z in XS, "
                    f"Verilog {verilog_name}={verilog_signals[verilog_name]}"
                )
                compared += 1
                continue

            xs_value = normalize_int(xs_signal.get("value"))
            verilog_value = normalize_int(verilog_signals[verilog_name])
            if xs_value != verilog_value:
                mismatches.append(
                    f"cycle {xs_cycle}: {xs_name} XS={xs_value} "
                    f"Verilog {verilog_name}={verilog_value}"
                )
            compared += 1

    if mismatches:
        print(f"[difftest] FAIL {config.get('name', config_path.stem)}")
        print(f"[difftest] xs={xs_waveform}")
        print(f"[difftest] xs_stage={xs_stage}")
        print(f"[difftest] verilator={verilog_samples_path}")
        print(f"[difftest] verilog_stage={verilog_stage}")
        for line in mismatches[:20]:
            print(f"[difftest] {line}")
        if len(mismatches) > 20:
            print(f"[difftest] ... {len(mismatches) - 20} more mismatches")
        return compared, len(mismatches)

    print(
        f"[difftest] PASS {config.get('name', config_path.stem)} "
        f"signals={len(mappings)} cycles={len(verilator_samples)} compared={compared}"
    )
    print(f"[difftest] xs={xs_waveform}")
    print(f"[difftest] xs_stage={xs_stage}")
    print(f"[difftest] verilator={verilog_samples_path}")
    print(f"[difftest] verilog_stage={verilog_stage}")
    return compared, 0


def compare(config: Dict[str, Any], config_path: Path) -> Tuple[int, int]:
    comparisons = config.get("comparisons")
    if comparisons is None:
        return compare_one(config, config_path)
    if not isinstance(comparisons, list) or not comparisons:
        raise ValueError("comparisons must be a non-empty list")

    total_compared = 0
    total_mismatches = 0
    for index, item in enumerate(comparisons):
        if not isinstance(item, dict):
            raise ValueError(f"comparisons[{index}] must be a JSON object")
        merged = {key: value for key, value in config.items() if key != "comparisons"}
        merged.update(item)
        compared, mismatches = compare_one(merged, config_path)
        total_compared += compared
        total_mismatches += mismatches
    return total_compared, total_mismatches


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="difftest mapping JSON")
    args = parser.parse_args()

    compared, mismatches = compare(load_json(args.config), args.config.resolve())
    return 1 if compared == 0 or mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
