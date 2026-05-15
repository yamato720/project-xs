#!/usr/bin/env python3

from __future__ import annotations

import argparse
import html
import json
import re
import xml.etree.ElementTree as et
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXAMPLE_DIR = ROOT / "example" / "project_xplus_hls"
RESOURCE_KEYS = ["LUT", "LUTAsMem", "REG", "BRAM", "URAM", "DSP"]


def read_spec(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_path(base: Path, raw: str) -> Path:
    path = Path(raw)
    if path.is_absolute():
        return path
    return (base / path).resolve()


def format_display_path(path: Path, base: Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path.resolve())


def discover_spec(example_dir: Path) -> Path:
    preferred_names = [f"{example_dir.name}_spec.json", "spec.json"]
    for name in preferred_names:
        candidate = example_dir / name
        if candidate.is_file():
            return candidate.resolve()

    candidates = sorted(path.resolve() for path in example_dir.glob("*_spec.json") if path.is_file())
    if len(candidates) == 1:
        return candidates[0]

    if not candidates:
        json_candidates = sorted(path.resolve() for path in example_dir.glob("*.json") if path.is_file())
        if len(json_candidates) == 1:
            return json_candidates[0]
        raise FileNotFoundError(
            "spec json not found in "
            f"{example_dir}; expected {example_dir.name}_spec.json, spec.json, or a single *_spec.json"
        )

    names = ", ".join(path.name for path in candidates)
    raise RuntimeError(f"multiple spec json files found in {example_dir}: {names}")


def discover_cfg(example_dir: Path) -> Path | None:
    preferred = ["connectivity.cfg", f"{example_dir.name}.cfg"]
    for name in preferred:
        candidate = example_dir / name
        if candidate.is_file():
            return candidate.resolve()

    connectivity_candidates = sorted(path.resolve() for path in example_dir.glob("connectivity*.cfg") if path.is_file())
    if len(connectivity_candidates) == 1:
        return connectivity_candidates[0]

    cfg_candidates = sorted(path.resolve() for path in example_dir.glob("*.cfg") if path.is_file())
    if len(cfg_candidates) == 1:
        return cfg_candidates[0]

    return None


def resolve_optional_path(base: Path, raw: str | None) -> Path | None:
    if not raw:
        return None
    return resolve_path(base, raw)


def resolve_optional_path_list(base: Path, raw: object) -> list[Path]:
    if not raw:
        return []
    if isinstance(raw, str):
        return [resolve_path(base, raw)]
    if isinstance(raw, list):
        return [resolve_path(base, str(item)) for item in raw if str(item)]
    raise RuntimeError(f"path list field must be a string or list of strings: {raw!r}")


def resolve_input_path(raw_path: str) -> tuple[Path, Path]:
    input_path = Path(raw_path).resolve()
    if input_path.is_dir():
        return input_path, discover_spec(input_path)
    if input_path.is_file():
        if input_path.suffix != ".json":
            raise RuntimeError(f"input file must be a spec json: {input_path}")
        return input_path.parent, input_path
    raise FileNotFoundError(f"path not found: {input_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Parse XO files under an example directory and render a static report."
    )
    parser.add_argument(
        "input_path",
        nargs="?",
        default=str(DEFAULT_EXAMPLE_DIR),
        help=f"example directory or spec json path (default: {DEFAULT_EXAMPLE_DIR})",
    )
    return parser.parse_args()


def parse_xo(xo_path: Path) -> dict:
    if not xo_path.exists():
        raise FileNotFoundError(f"xo not found: {xo_path}")

    with zipfile.ZipFile(xo_path) as archive:
        kernel_xml_name = next((name for name in archive.namelist() if name.endswith("/kernel.xml")), None)
        xo_xml_name = next((name for name in archive.namelist() if name.endswith("xo.xml")), None)
        if kernel_xml_name is None:
            raise RuntimeError(f"kernel.xml not found in {xo_path}")

        kernel_root = et.fromstring(archive.read(kernel_xml_name))
        kernel = kernel_root.find("kernel")
        if kernel is None:
            raise RuntimeError(f"invalid kernel.xml in {xo_path}")

        ports = []
        for port in kernel.findall("./ports/port"):
            ports.append(
                {
                    "name": port.attrib.get("name", ""),
                    "mode": port.attrib.get("mode", ""),
                    "data_width": port.attrib.get("dataWidth", ""),
                    "port_type": port.attrib.get("portType", ""),
                    "base": port.attrib.get("base", ""),
                    "range": port.attrib.get("range", ""),
                }
            )

        args = []
        for arg in kernel.findall("./args/arg"):
            args.append(
                {
                    "name": arg.attrib.get("name", ""),
                    "id": arg.attrib.get("id", ""),
                    "port": arg.attrib.get("port", ""),
                    "address_qualifier": arg.attrib.get("addressQualifier", ""),
                    "size": arg.attrib.get("size", ""),
                    "offset": arg.attrib.get("offset", ""),
                    "type": arg.attrib.get("type", ""),
                }
            )

        xo_meta = {}
        if xo_xml_name is not None:
            xo_root = et.fromstring(archive.read(xo_xml_name))
            kernels = xo_root.findall("./Kernels/Kernel")
            if kernels:
                xo_meta = {
                    "dir": kernels[0].attrib.get("Dir", ""),
                    "supported_flows": [flow.text for flow in kernels[0].findall("./SupportedFlows/SupportedFlow") if flow.text],
                }

        return {
            "xo_path": str(xo_path),
            "kernel_name": kernel.attrib.get("name", ""),
            "language": kernel.attrib.get("language", ""),
            "vlnv": kernel.attrib.get("vlnv", ""),
            "hw_control_protocol": kernel.attrib.get("hwControlProtocol", ""),
            "interrupt": kernel.attrib.get("interrupt", ""),
            "ports": ports,
            "args": args,
            "xo_meta": xo_meta,
        }


def parse_connectivity_cfg(cfg_path: Path | None) -> dict:
    if cfg_path is None:
        return {
            "cfg_path": "",
            "kernel_instances": [],
            "sp_bindings": [],
            "stream_connections": [],
            "instances_by_kernel": {},
            "bindings_by_kernel": {},
        }

    kernel_instances = []
    sp_bindings = []
    stream_connections = []
    current_section = ""

    for raw_line in cfg_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current_section = line[1:-1].strip().lower()
            continue
        if current_section and current_section != "connectivity":
            continue

        if line.startswith("nk="):
            payload = line[3:].strip()
            parts = payload.split(":", 2)
            kernel_name = parts[0] if parts else ""
            instance_count = parts[1] if len(parts) > 1 else ""
            instance_expr = parts[2] if len(parts) > 2 else ""
            instances = [item for item in instance_expr.split(".") if item]
            kernel_instances.append(
                {
                    "kernel_name": kernel_name,
                    "count": instance_count,
                    "instances": instances,
                    "raw": payload,
                }
            )
            continue

        if line.startswith("sp="):
            payload = line[3:].strip()
            endpoint, target = payload.rsplit(":", 1)
            instance, arg = endpoint.split(".", 1)
            sp_bindings.append(
                {
                    "instance": instance,
                    "arg": arg,
                    "target": target,
                    "raw": payload,
                }
            )
            continue

        if line.startswith("stream_connect="):
            payload = line[len("stream_connect="):].strip()
            source_endpoint, sink_endpoint = payload.split(":", 1)
            source_instance, source_port = source_endpoint.split(".", 1)
            sink_instance, sink_port = sink_endpoint.split(".", 1)
            stream_connections.append(
                {
                    "source_instance": source_instance,
                    "source_port": source_port,
                    "sink_instance": sink_instance,
                    "sink_port": sink_port,
                    "raw": payload,
                }
            )

    instances_by_kernel = {}
    instance_to_kernel = {}
    for entry in kernel_instances:
        instances_by_kernel.setdefault(entry["kernel_name"], []).extend(entry["instances"])
        for instance in entry["instances"]:
            instance_to_kernel[instance] = entry["kernel_name"]

    bindings_by_kernel = {}
    for binding in sp_bindings:
        kernel_name = instance_to_kernel.get(binding["instance"], "")
        binding["kernel_name"] = kernel_name
        bindings_by_kernel.setdefault(kernel_name, []).append(binding)

    for connection in stream_connections:
        connection["source_kernel"] = instance_to_kernel.get(connection["source_instance"], "")
        connection["sink_kernel"] = instance_to_kernel.get(connection["sink_instance"], "")

    return {
        "cfg_path": str(cfg_path),
        "kernel_instances": kernel_instances,
        "sp_bindings": sp_bindings,
        "stream_connections": stream_connections,
        "instances_by_kernel": instances_by_kernel,
        "bindings_by_kernel": bindings_by_kernel,
    }


def normalize_report_value(raw: str | None) -> str:
    if raw is None:
        return ""
    text = raw.strip()
    if not text or text.lower() == "undef":
        return ""
    return text


def parse_ns_to_mhz(period_ns: str) -> str:
    try:
        value = float(period_ns)
    except (TypeError, ValueError):
        return ""
    if value <= 0.0:
        return ""
    return f"{1000.0 / value:.3f}"


def parse_int_resource(value: object) -> int | None:
    if value is None:
        return None
    text = str(value).strip().replace(",", "")
    if not text or text in {"-", "?"}:
        return None
    if text.startswith("~"):
        text = text[1:]
    try:
        return int(float(text))
    except ValueError:
        return None


def format_resource_value(value: int | None) -> str:
    return "" if value is None else str(value)


def resource_percent(used: int | None, available: int | None) -> str:
    if used is None or available is None or available <= 0:
        return ""
    return f"{used * 100.0 / available:.2f}"


def implementation_resource_cells(actual: dict, supply: dict | None = None) -> dict:
    supply = supply or {}
    cells = {}
    for key in RESOURCE_KEYS:
        used = parse_int_resource(actual.get(key))
        available = parse_int_resource(supply.get(key))
        cells[key] = {
            "used": format_resource_value(used),
            "available": format_resource_value(available),
            "util_pct": resource_percent(used, available),
        }
    return cells


def hls_resource_cells(resources: dict, available_resources: dict | None = None) -> dict:
    available_resources = available_resources or {}
    key_map = {
        "LUT": "LUT",
        "LUTAsMem": "LUTAsMem",
        "REG": "FF",
        "BRAM": "BRAM_18K",
        "URAM": "URAM",
        "DSP": "DSP",
    }
    cells = {}
    for key, hls_key in key_map.items():
        used = parse_int_resource(resources.get(hls_key))
        available = parse_int_resource(available_resources.get(hls_key))
        cells[key] = {
            "used": format_resource_value(used),
            "available": format_resource_value(available),
            "util_pct": resource_percent(used, available),
        }
    return cells


def discover_csynth_report(base_dir: Path, kernel_name: str) -> Path | None:
    candidate = base_dir / f"{kernel_name}_csynth.xml"
    if candidate.is_file():
        return candidate.resolve()
    return None


def discover_build_xos(build_dir: Path | None) -> list[Path]:
    if build_dir is None or not build_dir.is_dir():
        return []
    # v++ leaves additional export.xo files under _x_temp; the top-level XOs are
    # the linked kernel artifacts that match the build target.
    return sorted(path.resolve() for path in build_dir.glob("*.xo") if path.is_file())


def discover_build_xo(build_dir: Path | None, kernel_name: str) -> Path | None:
    if build_dir is None or not build_dir.is_dir() or not kernel_name:
        return None
    candidate = build_dir / f"{kernel_name}.xo"
    if candidate.is_file():
        return candidate.resolve()

    for path in discover_build_xos(build_dir):
        try:
            if parse_xo(path).get("kernel_name") == kernel_name:
                return path
        except Exception:
            continue
    return None


def discover_build_csynth_report(build_dir: Path | None, kernel_name: str) -> Path | None:
    if build_dir is None or not build_dir.is_dir() or not kernel_name:
        return None

    preferred = (
        build_dir
        / "_x_temp"
        / kernel_name
        / kernel_name
        / kernel_name
        / "solution"
        / "syn"
        / "report"
        / f"{kernel_name}_csynth.xml"
    )
    if preferred.is_file():
        return preferred.resolve()

    matches = sorted(
        path.resolve()
        for path in build_dir.rglob(f"{kernel_name}_csynth.xml")
        if path.is_file() and "_x_temp/link/" not in path.as_posix()
    )
    if matches:
        return matches[0]
    return None


def discover_link_timing_report(base_dir: Path) -> Path | None:
    candidate = base_dir / "impl_1_hw_bb_locked_timing_summary_routed.rpt"
    if candidate.is_file():
        return candidate.resolve()

    matches = sorted(base_dir.glob("*timing_summary*.rpt")) if base_dir.is_dir() else []
    if matches:
        return matches[0].resolve()
    return None


def discover_build_link_timing_report(build_dir: Path | None) -> Path | None:
    if build_dir is None or not build_dir.is_dir():
        return None

    preferred = [
        build_dir / "_x_temp" / "reports" / "link" / "imp" / "impl_1_hw_bb_locked_timing_summary_routed.rpt",
        build_dir
        / "_x_temp"
        / "link"
        / "vivado"
        / "vpl"
        / "prj"
        / "prj.runs"
        / "impl_1"
        / "hw_bb_locked_timing_summary_routed.rpt",
    ]
    for candidate in preferred:
        if candidate.is_file():
            return candidate.resolve()

    routed = sorted(path.resolve() for path in build_dir.rglob("*hw_bb_locked_timing_summary_routed.rpt") if path.is_file())
    if routed:
        return routed[0]
    timing = sorted(path.resolve() for path in build_dir.rglob("*timing_summary*.rpt") if path.is_file())
    if timing:
        return timing[0]
    return None


def discover_build_resource_report(build_dir: Path | None) -> Path | None:
    if build_dir is None or not build_dir.is_dir():
        return None

    preferred_names = [
        "kernel_util_routed.json",
        "kernel_util_placed.json",
        "kernel_util_synthed.json",
    ]
    preferred_dirs = [
        build_dir / "_x_temp" / "link" / "vivado" / "vpl" / "prj" / "prj.runs" / "impl_1",
        build_dir / "_x_temp" / "reports" / "link" / "imp",
    ]
    for directory in preferred_dirs:
        for name in preferred_names:
            candidate = directory / name
            if candidate.is_file():
                return candidate.resolve()

    for name in preferred_names:
        matches = sorted(path.resolve() for path in build_dir.rglob(name) if path.is_file())
        if matches:
            return matches[0]
    return None


def discover_schedule_reports(
    base_dir: Path,
    kernel_name: str,
    *,
    include_all_pipeline_children: bool = False,
) -> tuple[Path | None, list[Path]]:
    top_report = None
    child_reports = []
    if not base_dir.is_dir():
        return top_report, child_reports

    direct = base_dir / f"{kernel_name}.verbose.sched.rpt"
    if direct.is_file():
        top_report = direct.resolve()

    child_pattern = "*.verbose.sched.rpt" if include_all_pipeline_children else f"{kernel_name}_Pipeline_*.verbose.sched.rpt"
    child_reports = sorted(
        path.resolve()
        for path in base_dir.glob(child_pattern)
        if path.is_file()
        and "_Pipeline_" in path.name
    )
    return top_report, child_reports


def discover_build_schedule_reports(build_dir: Path | None, kernel_name: str) -> tuple[Path | None, list[Path]]:
    if build_dir is None or not build_dir.is_dir() or not kernel_name:
        return None, []

    db_dir = (
        build_dir
        / "_x_temp"
        / kernel_name
        / kernel_name
        / kernel_name
        / "solution"
        / ".autopilot"
        / "db"
    )
    if db_dir.is_dir():
        return discover_schedule_reports(db_dir, kernel_name, include_all_pipeline_children=True)

    matches = sorted(
        path.resolve()
        for path in build_dir.rglob(f"{kernel_name}.verbose.sched.rpt")
        if path.is_file() and "_x_temp/link/" not in path.as_posix()
    )
    if not matches:
        return None, []
    return discover_schedule_reports(matches[0].parent, kernel_name, include_all_pipeline_children=True)


def resolve_artifact_config(spec: dict, example_dir: Path) -> dict:
    artifacts = spec.get("artifacts", {})
    auto_discover = artifacts.get("auto_discover", True)
    build_dir = resolve_optional_path(example_dir, artifacts.get("build_dir"))
    vivado_log_dir = resolve_optional_path(example_dir, artifacts.get("vivado_log_dir"))
    csynth_dir = resolve_optional_path(example_dir, artifacts.get("csynth_dir"))
    schedule_dir = resolve_optional_path(example_dir, artifacts.get("schedule_dir"))
    link_dir = resolve_optional_path(example_dir, artifacts.get("link_dir"))
    link_timing_report = resolve_optional_path(example_dir, artifacts.get("link_timing_report"))
    resource_report = resolve_optional_path(example_dir, artifacts.get("resource_report"))
    xclbin = resolve_optional_path(example_dir, artifacts.get("xclbin"))

    if xclbin is None and build_dir is not None:
        xclbin_matches = sorted(path.resolve() for path in build_dir.glob("*.xclbin") if path.is_file())
        if xclbin_matches:
            xclbin = xclbin_matches[0]

    if link_timing_report is None and build_dir is not None:
        link_timing_report = discover_build_link_timing_report(build_dir)
    if resource_report is None and build_dir is not None:
        resource_report = discover_build_resource_report(build_dir)

    # Backward-compatible fallback to historical auto-discovery under vivado-log/.
    if auto_discover:
        fallback_vivado_dir = example_dir / "vivado-log"
        if vivado_log_dir is None and fallback_vivado_dir.exists():
            vivado_log_dir = fallback_vivado_dir.resolve()
        if csynth_dir is None and vivado_log_dir is not None:
            candidate = vivado_log_dir / "csynth"
            if candidate.exists():
                csynth_dir = candidate.resolve()
        if schedule_dir is None and vivado_log_dir is not None:
            candidate = vivado_log_dir / "sched"
            if candidate.exists():
                schedule_dir = candidate.resolve()
        if link_dir is None and vivado_log_dir is not None:
            candidate = vivado_log_dir / "link"
            if candidate.exists():
                link_dir = candidate.resolve()
        if link_timing_report is None and link_dir is not None:
            link_timing_report = discover_link_timing_report(link_dir)

    return {
        "auto_discover": auto_discover,
        "build_dir": build_dir,
        "xclbin": xclbin,
        "vivado_log_dir": vivado_log_dir,
        "csynth_dir": csynth_dir,
        "schedule_dir": schedule_dir,
        "link_dir": link_dir,
        "link_timing_report": link_timing_report,
        "resource_report": resource_report,
    }


def parse_sched_report_top(report_path: Path | None, example_dir: Path) -> dict:
    if report_path is None or not report_path.is_file():
        return {
            "report_path": "",
            "available": False,
            "requested_path": format_display_path(report_path, example_dir) if report_path else "",
            "how_to_get": "期望文件名通常为 <kernel>.verbose.sched.rpt，常见于 HLS /.autopilot/db/ 调度报告目录中",
            "is_top_model": "",
            "pipeline_count": "",
            "dataflow_pipeline_count": "",
            "fsm_states": "",
        }

    text = report_path.read_text(encoding="utf-8", errors="ignore")

    def match_one(pattern: str) -> str:
        match = re.search(pattern, text, flags=re.MULTILINE)
        return match.group(1).strip() if match else ""

    top_loop_rows = []
    table_match = re.search(
        r"\*\s+Loop:\s*\n(?P<body>.*?)(?:\n=+|\n\+ Verbose Summary:)",
        text,
        flags=re.DOTALL,
    )
    if table_match:
        for raw_line in table_match.group("body").splitlines():
            line = raw_line.strip()
            if not line.startswith("|-") and not line.startswith("| +"):
                continue
            cells = [cell.strip() for cell in raw_line.strip().strip("|").split("|")]
            if len(cells) < 8:
                continue
            loop_name = cells[0].lstrip("-+").strip()
            if not loop_name:
                continue
            top_loop_rows.append(
                {
                    "loop_name": loop_name,
                    "latency_min": cells[1],
                    "latency_max": cells[2],
                    "iteration_latency": cells[3],
                    "ii_achieved": cells[4],
                    "ii_target": cells[5],
                    "trip_count": cells[6],
                    "pipelined": cells[7],
                }
            )

    return {
        "report_path": format_display_path(report_path, example_dir),
        "available": True,
        "requested_path": format_display_path(report_path, example_dir),
        "how_to_get": "",
        "is_top_model": match_one(r"^IsTopModel:\s+(.+)$"),
        "pipeline_count": match_one(r"^\* Pipeline\s*:\s*(.+)$"),
        "dataflow_pipeline_count": match_one(r"^\* Dataflow Pipeline:\s*(.+)$"),
        "fsm_states": match_one(r"^\* Number of FSM states\s*:\s*(.+)$"),
        "top_loop_rows": top_loop_rows,
    }


def parse_sched_pipeline_report(report_path: Path, example_dir: Path) -> dict:
    text = report_path.read_text(encoding="utf-8", errors="ignore")

    def match_one(pattern: str) -> str:
        match = re.search(pattern, text, flags=re.MULTILINE)
        return match.group(1).strip() if match else ""

    pipeline_name = report_path.name.replace(".verbose.sched.rpt", "")
    loop_rows = []
    table_match = re.search(
        r"\|\s*Loop Name.*?\n(?P<body>.*?)(?:\n=+|\n\+ Verbose Summary:)",
        text,
        flags=re.DOTALL,
    )
    if table_match:
        for raw_line in table_match.group("body").splitlines():
            line = raw_line.strip()
            if not line.startswith("|-"):
                continue
            cells = [cell.strip() for cell in raw_line.strip().strip("|").split("|")]
            if len(cells) < 8:
                continue
            loop_rows.append(
                {
                    "loop_name": cells[0].lstrip("-").strip(),
                    "latency_min": cells[1],
                    "latency_max": cells[2],
                    "iteration_latency": cells[3],
                    "ii_achieved": cells[4],
                    "ii_target": cells[5],
                    "trip_count": cells[6],
                    "pipelined": cells[7],
                }
            )

    individual_pipeline = re.search(
        r"Pipeline-0:\s+initiation interval \(II\)\s*=\s*([^,]+),\s*depth\s*=\s*(.+)$",
        text,
        flags=re.MULTILINE,
    )
    individual_ii = individual_pipeline.group(1).strip() if individual_pipeline else ""
    individual_depth = individual_pipeline.group(2).strip() if individual_pipeline else ""

    return {
        "report_path": format_display_path(report_path, example_dir),
        "pipeline_name": pipeline_name,
        "fsm_states": match_one(r"^\* Number of FSM states\s*:\s*(.+)$"),
        "pipeline_count": match_one(r"^\* Pipeline\s*:\s*(.+)$"),
        "dataflow_pipeline_count": match_one(r"^\* Dataflow Pipeline:\s*(.+)$"),
        "individual_ii": individual_ii,
        "individual_depth": individual_depth,
        "loop_rows": loop_rows,
    }


def parse_pipeline_reports(top_report: Path | None, child_reports: list[Path], example_dir: Path) -> dict:
    top = parse_sched_report_top(top_report, example_dir)
    children = [parse_sched_pipeline_report(path, example_dir) for path in child_reports]

    overview_rows = []
    child_loop_names = set()
    for child in children:
        if child["loop_rows"]:
            for loop in child["loop_rows"]:
                child_loop_names.add(loop["loop_name"])
                overview_rows.append(
                    {
                        "kernel_pipeline": child["pipeline_name"],
                        "loop_name": loop["loop_name"],
                        "ii_achieved": loop["ii_achieved"],
                        "ii_target": loop["ii_target"],
                        "iteration_latency": loop["iteration_latency"],
                        "trip_count": loop["trip_count"],
                        "pipelined": loop["pipelined"],
                        "pipeline_depth": child["individual_depth"],
                        "report_path": child["report_path"],
                    }
                )
        else:
            overview_rows.append(
                {
                    "kernel_pipeline": child["pipeline_name"],
                    "loop_name": "",
                    "ii_achieved": child["individual_ii"],
                    "ii_target": "",
                    "iteration_latency": "",
                    "trip_count": "",
                    "pipelined": "yes" if child["pipeline_count"] == "1" else "",
                    "pipeline_depth": child["individual_depth"],
                    "report_path": child["report_path"],
                }
            )

    for loop in top.get("top_loop_rows", []):
        if loop["loop_name"] in child_loop_names:
            continue
        overview_rows.append(
            {
                "kernel_pipeline": "top-level-loop",
                "loop_name": loop["loop_name"],
                "ii_achieved": loop["ii_achieved"],
                "ii_target": loop["ii_target"],
                "iteration_latency": loop["iteration_latency"],
                "trip_count": loop["trip_count"],
                "pipelined": loop["pipelined"],
                "pipeline_depth": "",
                "report_path": top.get("report_path", ""),
            }
        )

    return {
        "top": top,
        "children": children,
        "overview_rows": overview_rows,
    }


def pipeline_count_to_yes_no(value: str) -> str:
    if value == "0":
        return "no"
    if value:
        return "yes"
    return ""


def flatten_loop_latency(node: et.Element, prefix: str = "") -> list[dict]:
    metric_tags = {
        "Slack",
        "TripCount",
        "Latency",
        "AbsoluteTimeLatency",
        "IterationLatency",
        "PipelineII",
        "PipelineDepth",
    }
    path = f"{prefix}/{node.tag}" if prefix else node.tag
    metrics = {tag: normalize_report_value(node.findtext(tag)) for tag in metric_tags}

    rows = []
    if any(metrics.values()):
        rows.append(
            {
                "name": node.tag,
                "path": path,
                "slack_ns": metrics["Slack"],
                "trip_count": metrics["TripCount"],
                "latency_cycles": metrics["Latency"],
                "latency_abs": metrics["AbsoluteTimeLatency"],
                "iteration_latency": metrics["IterationLatency"],
                "pipeline_ii": metrics["PipelineII"],
                "pipeline_depth": metrics["PipelineDepth"],
            }
        )

    for child in list(node):
        if child.tag in metric_tags or child.tag == "InstanceList":
            continue
        rows.extend(flatten_loop_latency(child, path))
    return rows


def parse_csynth_report(csynth_path: Path | None, example_dir: Path) -> dict:
    if csynth_path is None or not csynth_path.is_file():
        return {
            "report_path": "",
            "available": False,
            "requested_path": format_display_path(csynth_path, example_dir) if csynth_path else "",
            "how_to_get": "期望文件名通常为 <kernel>_csynth.xml，常见于 Vitis HLS / v++ kernel compile 生成目录下的 solution/syn/report/ 中",
            "kernel_name": "",
            "target_clock_ns": "",
            "target_clock_mhz": "",
            "clock_uncertainty_ns": "",
            "estimated_clock_ns": "",
            "estimated_clock_mhz": "",
            "pipeline_type": "",
            "overall_latency": {},
            "resources": {},
            "available_resources": {},
            "loop_latency": [],
        }

    root = et.fromstring(csynth_path.read_bytes())
    target_clock_ns = normalize_report_value(root.findtext("./UserAssignments/TargetClockPeriod"))
    estimated_clock_ns = normalize_report_value(
        root.findtext("./PerformanceEstimates/SummaryOfTimingAnalysis/EstimatedClockPeriod")
    )
    resources_root = root.find("./AreaEstimates/Resources")
    resources = {}
    if resources_root is not None:
        for tag in ["BRAM_18K", "DSP", "FF", "LUT", "URAM"]:
            resources[tag] = normalize_report_value(resources_root.findtext(tag))
    available_resources_root = root.find("./AreaEstimates/AvailableResources")
    available_resources = {}
    if available_resources_root is not None:
        for tag in ["BRAM_18K", "DSP", "FF", "LUT", "URAM"]:
            available_resources[tag] = normalize_report_value(available_resources_root.findtext(tag))

    overall_latency_root = root.find("./PerformanceEstimates/SummaryOfOverallLatency")
    overall_latency = {}
    if overall_latency_root is not None:
        overall_latency = {
            "unit": normalize_report_value(overall_latency_root.findtext("unit")),
            "best_cycles": normalize_report_value(overall_latency_root.findtext("Best-caseLatency")),
            "avg_cycles": normalize_report_value(overall_latency_root.findtext("Average-caseLatency")),
            "worst_cycles": normalize_report_value(overall_latency_root.findtext("Worst-caseLatency")),
            "best_time": normalize_report_value(overall_latency_root.findtext("Best-caseRealTimeLatency")),
            "avg_time": normalize_report_value(overall_latency_root.findtext("Average-caseRealTimeLatency")),
            "worst_time": normalize_report_value(overall_latency_root.findtext("Worst-caseRealTimeLatency")),
            "interval_min": normalize_report_value(overall_latency_root.findtext("Interval-min")),
            "interval_max": normalize_report_value(overall_latency_root.findtext("Interval-max")),
        }

    loop_latency = []
    loop_summary_root = root.find("./PerformanceEstimates/SummaryOfLoopLatency")
    if loop_summary_root is not None:
        for child in list(loop_summary_root):
            loop_latency.extend(flatten_loop_latency(child))

    return {
        "report_path": format_display_path(csynth_path, example_dir),
        "available": True,
        "requested_path": format_display_path(csynth_path, example_dir),
        "how_to_get": "",
        "kernel_name": normalize_report_value(root.findtext("./UserAssignments/TopModelName")),
        "target_clock_ns": target_clock_ns,
        "target_clock_mhz": parse_ns_to_mhz(target_clock_ns),
        "clock_uncertainty_ns": normalize_report_value(root.findtext("./UserAssignments/ClockUncertainty")),
        "estimated_clock_ns": estimated_clock_ns,
        "estimated_clock_mhz": parse_ns_to_mhz(estimated_clock_ns),
        "pipeline_type": normalize_report_value(root.findtext("./PerformanceEstimates/PipelineType")),
        "overall_latency": overall_latency,
        "resources": resources,
        "available_resources": available_resources,
        "loop_latency": loop_latency,
    }


def find_next_numeric_row(lines: list[str], start: int, width: int) -> list[str]:
    numeric_token = re.compile(r"^-?\d+(?:\.\d+)?$")
    for line in lines[start:]:
        tokens = line.strip().split()
        if len(tokens) != width:
            continue
        if all(numeric_token.match(token) for token in tokens):
            return tokens
    return []


def parse_link_timing_report(report_path: Path | None, example_dir: Path) -> dict:
    if report_path is None or not report_path.is_file():
        return {
            "report_path": "",
            "available": False,
            "requested_path": format_display_path(report_path, example_dir) if report_path else "",
            "how_to_get": "期望文件名通常为 impl_1_*timing_summary*_routed.rpt 或同类 routed timing summary，常见于 v++ link / Vivado implementation 的 reports/link/imp/ 或 impl report 目录中",
            "constraints_met": False,
            "design_summary": {},
            "clock_summary": [],
            "kernel_clock_summary": [],
        }

    lines = report_path.read_text(encoding="utf-8", errors="ignore").splitlines()
    design_summary = {}
    design_idx = next((index for index, line in enumerate(lines) if "| Design Timing Summary" in line), -1)
    if design_idx >= 0:
        summary_tokens = find_next_numeric_row(lines, design_idx, 12)
        if summary_tokens:
            fields = [
                "wns_ns",
                "tns_ns",
                "tns_failing_endpoints",
                "tns_total_endpoints",
                "whs_ns",
                "ths_ns",
                "ths_failing_endpoints",
                "ths_total_endpoints",
                "wpws_ns",
                "tpws_ns",
                "tpws_failing_endpoints",
                "tpws_total_endpoints",
            ]
            design_summary = dict(zip(fields, summary_tokens))

    clock_summary = []
    clock_idx = next((index for index, line in enumerate(lines) if "| Clock Summary" in line), -1)
    if clock_idx >= 0:
        pattern = re.compile(
            r"^(?P<name>\S.*\S|\S)\s+(?P<waveform>\{[^}]+\})\s+"
            r"(?P<period>-?\d+(?:\.\d+)?)\s+(?P<frequency>-?\d+(?:\.\d+)?)\s*$"
        )
        for line in lines[clock_idx:]:
            if "| Intra Clock Table" in line:
                break
            match = pattern.match(line.strip())
            if not match:
                continue
            clock_summary.append(
                {
                    "name": match.group("name").strip(),
                    "waveform_ns": match.group("waveform"),
                    "period_ns": match.group("period"),
                    "frequency_mhz": match.group("frequency"),
                }
            )

    intra_metrics_by_clock = {}
    intra_idx = next((index for index, line in enumerate(lines) if "| Intra Clock Table" in line), -1)
    if intra_idx >= 0:
        for line in lines[intra_idx:]:
            if "| Inter Clock Table" in line:
                break
            tokens = line.strip().split()
            if len(tokens) < 13:
                continue
            clock_name = tokens[0]
            if not clock_name.startswith("clk_kernel_"):
                continue
            intra_metrics_by_clock[clock_name] = {
                "wns_ns": tokens[1],
                "tns_ns": tokens[2],
                "tns_failing_endpoints": tokens[3],
                "tns_total_endpoints": tokens[4],
                "whs_ns": tokens[5],
                "ths_ns": tokens[6],
                "ths_failing_endpoints": tokens[7],
                "ths_total_endpoints": tokens[8],
                "wpws_ns": tokens[9],
                "tpws_ns": tokens[10],
                "tpws_failing_endpoints": tokens[11],
                "tpws_total_endpoints": tokens[12],
            }

    kernel_clock_summary = []
    for clock in clock_summary:
        if not clock["name"].startswith("clk_kernel_"):
            continue
        row = dict(clock)
        row.update(intra_metrics_by_clock.get(clock["name"], {}))
        kernel_clock_summary.append(row)

    known_kernel_clocks = {clock["name"] for clock in kernel_clock_summary}
    for clock_name, metrics in intra_metrics_by_clock.items():
        if clock_name in known_kernel_clocks:
            continue
        row = {"name": clock_name, "waveform_ns": "", "period_ns": "", "frequency_mhz": ""}
        row.update(metrics)
        kernel_clock_summary.append(row)

    return {
        "report_path": format_display_path(report_path, example_dir),
        "available": True,
        "requested_path": format_display_path(report_path, example_dir),
        "how_to_get": "",
        "constraints_met": any("All user specified timing constraints are met." in line for line in lines),
        "design_summary": design_summary,
        "clock_summary": clock_summary,
        "kernel_clock_summary": kernel_clock_summary,
    }


def empty_resource_summary(report_path: Path | None, example_dir: Path) -> dict:
    return {
        "source": "",
        "stage": "",
        "available": False,
        "report_path": "",
        "requested_path": format_display_path(report_path, example_dir) if report_path else "",
        "how_to_get": "优先使用 kernel_util_routed.json / kernel_util_placed.json / kernel_util_synthed.json；若实现报告尚未生成，则回退到每个 kernel 的 csynth.xml 资源估计。",
        "total": {},
        "kernels": [],
    }


def parse_implementation_resource_report(report_path: Path | None, example_dir: Path) -> dict:
    if report_path is None or not report_path.is_file():
        return empty_resource_summary(report_path, example_dir)

    try:
        raw = json.loads(report_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return empty_resource_summary(report_path, example_dir)

    budget = raw.get("user_budget", {})
    total_actual = budget.get("actual_resources", {})
    total_supply = budget.get("supply_resources", {})
    kernel_rows = []
    for kernel in raw.get("kernels", []):
        actual = {key: 0 for key in RESOURCE_KEYS}
        compute_units = kernel.get("compute_units", [])
        for compute_unit in compute_units:
            resources = compute_unit.get("actual_resources", {})
            for key in RESOURCE_KEYS:
                value = parse_int_resource(resources.get(key))
                if value is not None:
                    actual[key] += value
        kernel_rows.append(
            {
                "name": str(kernel.get("name", "")),
                "compute_unit_count": str(kernel.get("compute_unit_count", len(compute_units))),
                "cells": implementation_resource_cells(actual, total_supply),
            }
        )

    return {
        "source": "implementation",
        "stage": str(raw.get("design_state", "")),
        "available": True,
        "report_path": format_display_path(report_path, example_dir),
        "requested_path": format_display_path(report_path, example_dir),
        "how_to_get": "",
        "total": implementation_resource_cells(total_actual, total_supply),
        "kernels": kernel_rows,
    }


def build_hls_resource_summary(kernels: list[dict]) -> dict:
    total_values = {key: 0 for key in RESOURCE_KEYS}
    any_value = False
    kernel_rows = []
    for kernel in kernels:
        hls = kernel.get("hls", {})
        if not hls.get("available"):
            continue
        cells = hls_resource_cells(hls.get("resources", {}), hls.get("available_resources", {}))
        has_cell = False
        for key, cell in cells.items():
            value = parse_int_resource(cell.get("used"))
            if value is not None:
                total_values[key] += value
                any_value = True
                has_cell = True
        if has_cell:
            kernel_rows.append(
                {
                    "name": kernel.get("name", ""),
                    "compute_unit_count": "",
                    "cells": cells,
                }
            )

    if not any_value:
        return empty_resource_summary(None, ROOT)

    total_cells = {
        key: {
            "used": format_resource_value(value),
            "available": "",
            "util_pct": "",
        }
        for key, value in total_values.items()
    }
    return {
        "source": "hls_estimate",
        "stage": "csynth",
        "available": True,
        "report_path": "",
        "requested_path": "",
        "how_to_get": "",
        "total": total_cells,
        "kernels": kernel_rows,
    }


def build_report(spec: dict, spec_path: Path, example_dir: Path) -> dict:
    cfg_raw = spec.get("cfg", "")
    cfg_path = resolve_path(example_dir, cfg_raw) if cfg_raw else discover_cfg(example_dir)
    connectivity = parse_connectivity_cfg(cfg_path)
    artifacts = resolve_artifact_config(spec, example_dir)
    link_timing_path = artifacts["link_timing_report"]
    link_timing = parse_link_timing_report(link_timing_path, example_dir)
    resource_summary = parse_implementation_resource_report(artifacts["resource_report"], example_dir)

    kernel_specs = list(spec.get("kernels", []))
    if not kernel_specs and artifacts["build_dir"] is not None:
        for xo_path in discover_build_xos(artifacts["build_dir"]):
            kernel_specs.append({"xo": format_display_path(xo_path, example_dir)})

    kernels = []
    pipeline_overview = []
    for index, kernel_spec in enumerate(kernel_specs):
        xo_raw = kernel_spec.get("xo")
        xo_path = resolve_path(example_dir, xo_raw) if xo_raw else None
        if xo_path is None and kernel_spec.get("name"):
            xo_path = discover_build_xo(artifacts["build_dir"], kernel_spec["name"])
        if xo_path is None:
            raise RuntimeError(f"kernel entry #{index} must specify xo or name with artifacts.build_dir")
        xo_info = parse_xo(xo_path)
        kernel_name = kernel_spec.get("name") or xo_info["kernel_name"] or xo_path.stem
        cfg_kernel_name = xo_info["kernel_name"] or kernel_name
        csynth_override = resolve_optional_path(example_dir, kernel_spec.get("csynth"))
        csynth_path = csynth_override
        if csynth_path is None:
            csynth_path = discover_build_csynth_report(artifacts["build_dir"], cfg_kernel_name)
        if csynth_path is None and artifacts["csynth_dir"] is not None:
            csynth_path = discover_csynth_report(artifacts["csynth_dir"], cfg_kernel_name)
        csynth = parse_csynth_report(csynth_path, example_dir)
        sched_top_path = resolve_optional_path(example_dir, kernel_spec.get("schedule_top"))
        sched_child_paths = resolve_optional_path_list(example_dir, kernel_spec.get("schedule_children"))
        schedule_dir = resolve_optional_path(example_dir, kernel_spec.get("schedule_dir"))
        if sched_top_path is None and not sched_child_paths:
            discovered_top, discovered_children = discover_build_schedule_reports(
                artifacts["build_dir"], cfg_kernel_name
            )
            sched_top_path = discovered_top
            sched_child_paths = discovered_children
        if schedule_dir is None:
            schedule_dir = artifacts["schedule_dir"]
        if schedule_dir is not None:
            discovered_top, discovered_children = discover_schedule_reports(schedule_dir, cfg_kernel_name)
            if sched_top_path is None:
                sched_top_path = discovered_top
            if not sched_child_paths:
                sched_child_paths = discovered_children
        pipeline = parse_pipeline_reports(sched_top_path, sched_child_paths, example_dir)
        for row in pipeline["overview_rows"]:
            pipeline_overview.append({"kernel_name": kernel_name, **row})
        cfg_bindings = connectivity["bindings_by_kernel"].get(cfg_kernel_name, [])
        cfg_bindings_by_arg = {}
        for binding in cfg_bindings:
            cfg_bindings_by_arg.setdefault(binding["arg"], []).append(
                {
                    "instance": binding["instance"],
                    "target": binding["target"],
                }
            )
        kernels.append(
            {
                "name": kernel_name,
                "input_index": index,
                "xo": xo_info,
                "hls": csynth,
                "pipeline": pipeline,
                "cfg": {
                    "cfg_path": format_display_path(cfg_path, example_dir) if cfg_path else "",
                    "instances": connectivity["instances_by_kernel"].get(cfg_kernel_name, []),
                    "sp_bindings": cfg_bindings,
                    "bindings_by_arg": cfg_bindings_by_arg,
                },
                "annotations": {
                    "role": kernel_spec.get("role", ""),
                    "summary": kernel_spec.get("summary", []),
                    "reads": kernel_spec.get("reads", []),
                    "writes": kernel_spec.get("writes", []),
                    "hbm_mapping": kernel_spec.get("hbm_mapping", []),
                    "xs_mapping": kernel_spec.get("xs_mapping", {}),
                    "xs_sim_mapping": kernel_spec.get("xs_sim_mapping", {}),
                    "components": kernel_spec.get("components", []),
                    "component_edges": kernel_spec.get("component_edges", []),
                },
            }
        )

    order = {name: index for index, name in enumerate(spec.get("sequence", []))}
    if order:
        kernels.sort(key=lambda item: (order.get(item["name"], 1 << 30), item["input_index"], item["name"]))

    if not resource_summary.get("available"):
        resource_summary = build_hls_resource_summary(kernels)

    connectivity_rows = []
    for entry in connectivity["kernel_instances"]:
        kernel_name = entry["kernel_name"]
        bindings = connectivity["bindings_by_kernel"].get(kernel_name, [])
        connectivity_rows.append(
            {
                "kernel_name": kernel_name,
                "count": entry["count"],
                "instances": entry["instances"],
                "binding_count": len(bindings),
            }
        )

    return {
        "title": spec.get("title", "HLS Example"),
        "subtitle": spec.get("subtitle", ""),
        "spec_path": str(spec_path),
        "example_dir": str(example_dir),
        "source_note": spec.get("source_note", ""),
        "sequence": spec.get("sequence", []),
        "sequence_descriptions": spec.get("sequence_descriptions", {}),
        "dependencies": spec.get("dependencies", []),
        "host_nodes": spec.get("host_nodes", []),
        "connectivity": {
            "cfg_path": format_display_path(cfg_path, example_dir) if cfg_path else "",
            "kernels": connectivity_rows,
            "stream_connections": connectivity["stream_connections"],
        },
        "vivado": {
            "auto_discover": artifacts["auto_discover"],
            "build_dir": format_display_path(artifacts["build_dir"], example_dir)
            if artifacts["build_dir"] is not None
            else "",
            "xclbin": format_display_path(artifacts["xclbin"], example_dir)
            if artifacts["xclbin"] is not None
            else "",
            "vivado_log_dir": format_display_path(artifacts["vivado_log_dir"], example_dir)
            if artifacts["vivado_log_dir"] is not None
            else "",
            "csynth_dir": format_display_path(artifacts["csynth_dir"], example_dir)
            if artifacts["csynth_dir"] is not None
            else "",
            "schedule_dir": format_display_path(artifacts["schedule_dir"], example_dir)
            if artifacts["schedule_dir"] is not None
            else "",
            "link_dir": format_display_path(artifacts["link_dir"], example_dir)
            if artifacts["link_dir"] is not None
            else "",
            "link_timing": link_timing,
            "resources": resource_summary,
            "pipeline_overview": pipeline_overview,
        },
        "kernels": kernels,
    }


def render_html_page(data: dict) -> str:
    def render_resource_cell(cell: dict) -> str:
        used = cell.get("used", "") or "-"
        available = cell.get("available", "")
        pct = cell.get("util_pct", "")
        if available and pct:
            return f"{html.escape(used)} / {html.escape(available)} ({html.escape(pct)}%)"
        if available:
            return f"{html.escape(used)} / {html.escape(available)}"
        return html.escape(used)

    sequence_rows = []
    for index, kernel_name in enumerate(data["sequence"], start=1):
        desc = data["sequence_descriptions"].get(kernel_name, "")
        sequence_rows.append(
            "<tr>"
            f"<td>{index}</td>"
            f"<td>{html.escape(kernel_name)}</td>"
            f"<td>{html.escape(desc)}</td>"
            "</tr>"
        )

    has_sequence = bool(data["sequence"])
    dependency_rows = []
    for dep in data["dependencies"]:
        dependency_rows.append(
            "<tr>"
            f"<td>{html.escape(dep['upstream'])}</td>"
            f"<td>{html.escape(dep['downstream'])}</td>"
            f"<td>{html.escape(dep['description'])}</td>"
            "</tr>"
        )

    has_dependencies = bool(data["dependencies"])
    host_rows = []
    for node in data["host_nodes"]:
        host_rows.append(
            "<tr>"
            f"<td>{html.escape(node['name'])}</td>"
            f"<td>{html.escape(node['role'])}</td>"
            f"<td>{html.escape(', '.join(node.get('reads', [])))}</td>"
            f"<td>{html.escape(', '.join(node.get('writes', [])))}</td>"
            f"<td>{html.escape(', '.join(node.get('drives', [])))}</td>"
            f"<td>{html.escape(', '.join(node.get('sim_mapping', [])))}</td>"
            "</tr>"
        )

    has_host_nodes = bool(data["host_nodes"])
    connectivity_rows = []
    for row in data.get("connectivity", {}).get("kernels", []):
        connectivity_rows.append(
            "<tr>"
            f"<td>{html.escape(row['kernel_name'])}</td>"
            f"<td>{html.escape(row['count'])}</td>"
            f"<td>{html.escape(', '.join(row.get('instances', [])) or '-')}</td>"
            f"<td>{row['binding_count']}</td>"
            "</tr>"
        )

    stream_connection_rows = []
    for row in data.get("connectivity", {}).get("stream_connections", []):
        source = f"{row.get('source_instance', '')}.{row.get('source_port', '')}"
        sink = f"{row.get('sink_instance', '')}.{row.get('sink_port', '')}"
        stream_connection_rows.append(
            "<tr>"
            f"<td>{html.escape(row.get('source_kernel', '') or '-')}</td>"
            f"<td>{html.escape(source)}</td>"
            f"<td>{html.escape(row.get('sink_kernel', '') or '-')}</td>"
            f"<td>{html.escape(sink)}</td>"
            "</tr>"
        )

    resources = data.get("vivado", {}).get("resources", {})
    resource_total_cells = resources.get("total", {})
    resource_total_row = (
        "<tr>"
        "<td>total</td>"
        + "".join(f"<td>{render_resource_cell(resource_total_cells.get(key, {}))}</td>" for key in RESOURCE_KEYS)
        + "</tr>"
    )
    resource_kernel_rows = []
    for row in resources.get("kernels", []):
        cells = row.get("cells", {})
        resource_kernel_rows.append(
            "<tr>"
            f"<td>{html.escape(row.get('name', ''))}</td>"
            f"<td>{html.escape(row.get('compute_unit_count', '') or '-')}</td>"
            + "".join(f"<td>{render_resource_cell(cells.get(key, {}))}</td>" for key in RESOURCE_KEYS)
            + "</tr>"
        )
    resource_source_label = {
        "implementation": "实现阶段资源",
        "hls_estimate": "HLS 资源估计",
    }.get(resources.get("source", ""), resources.get("source", "") or "未提供")
    resource_overview_section = f"""
    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">设计资源总览</div>
          <div class="section-note">优先使用 Vitis/Vivado 已生成的 kernel_util 报告；如果 bitstream/link/implementation 尚未走到该阶段，则回退到 HLS csynth.xml 的 kernel 资源估计。</div>
          <div class="section-note">source: {html.escape(resource_source_label)}{f'，stage: {html.escape(resources.get("stage", ""))}' if resources.get("stage") else ''}</div>
          {f'<div class="section-note">report: {html.escape(resources.get("report_path", ""))}</div>' if resources.get("report_path") else ''}
          {f'<div class="section-note">当前未提供。{html.escape(resources.get("how_to_get", ""))}</div>' if not resources.get("available") else ''}
          <div class="table-wrap mb-3">
            <table class="table table-sm align-middle">
              <thead><tr><th>scope</th>{''.join(f'<th>{key}</th>' for key in RESOURCE_KEYS)}</tr></thead>
              <tbody>{resource_total_row if resources.get("available") else '<tr><td colspan="7">无</td></tr>'}</tbody>
            </table>
          </div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>kernel</th><th>CUs</th>{''.join(f'<th>{key}</th>' for key in RESOURCE_KEYS)}</tr></thead>
              <tbody>{''.join(resource_kernel_rows) or '<tr><td colspan="8">无</td></tr>'}</tbody>
            </table>
          </div>
        </div></div>
      </div>
    </div>
"""

    link_timing = data.get("vivado", {}).get("link_timing", {})
    design_timing = link_timing.get("design_summary", {})
    link_timing_rows = []
    for clock in link_timing.get("kernel_clock_summary", []):
        link_timing_rows.append(
            "<tr>"
            f"<td>{html.escape(clock.get('name', ''))}</td>"
            f"<td>{html.escape(clock.get('period_ns', ''))}</td>"
            f"<td>{html.escape(clock.get('frequency_mhz', ''))}</td>"
            f"<td>{html.escape(clock.get('wns_ns', '-'))}</td>"
            f"<td>{html.escape(clock.get('whs_ns', '-'))}</td>"
            f"<td>{html.escape(clock.get('wpws_ns', '-'))}</td>"
            "</tr>"
        )

    pipeline_overview_rows = []
    for row in data.get("vivado", {}).get("pipeline_overview", []):
        pipeline_overview_rows.append(
            "<tr>"
            f"<td>{html.escape(row.get('kernel_name', ''))}</td>"
            f"<td>{html.escape(row.get('kernel_pipeline', ''))}</td>"
            f"<td>{html.escape(row.get('loop_name', '') or '-')}</td>"
            f"<td>{html.escape(row.get('pipelined', '') or '-')}</td>"
            f"<td>{html.escape(row.get('ii_achieved', '') or '-')}</td>"
            f"<td>{html.escape(row.get('ii_target', '') or '-')}</td>"
            f"<td>{html.escape(row.get('pipeline_depth', '') or '-')}</td>"
            f"<td>{html.escape(row.get('iteration_latency', '') or '-')}</td>"
            "</tr>"
        )

    pipeline_overview_section = f"""
    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Pipeline 总览（来自 HLS schedule rpt）</div>
          <div class="section-note">这部分把 kernel 顶层是否 pipeline，以及各子 pipeline / loop 的实际 II 情况拆开看。</div>
          <div class="section-note">schedule_dir: {html.escape(data.get('vivado', {}).get('schedule_dir', '') or '未指定')}</div>
          <div class="section-note">`loop`：HLS 报告里的循环名。它通常能对应源码里的某个 loop label 或某个工具生成的内部循环；在 XS 视角里，它不自动等价于一个 `KernelComponent`，但常常可以作为你后续手工拆 component 的线索。</div>
          <div class="section-note">`pipelined`：该 loop 在 HLS 报告里是否被认定为 pipeline。`yes` 表示形成了 pipeline；`no` 表示这条 loop 没形成 pipeline，或者只在更外层 / 更内层以别的方式体现。</div>
          <div class="section-note">`II achieved`：实际综合达到的 initiation interval。数字表示隔多少拍才能再发起一轮新迭代；`1` 表示每拍都能启动一轮，`5` 表示每 5 拍才能启动一轮，`-` 表示报告没有给出可用值。</div>
          <div class="section-note">`II target`：目标 initiation interval。通常来自源码里的 pipeline pragma 或工具目标设置；把它和 `II achieved` 对比，就能看出目标是否达成。</div>
          <div class="section-note">`depth`：pipeline 深度，表示一条迭代被切分成多少个串行流水阶段；稳态时，不同迭代可以同时分布在这些阶段上。它不是总 latency，也不是并行 issue 宽度。</div>
          <div class="section-note">`iter latency`：iteration latency。数字表示单次迭代穿过该 loop/pipeline 时在报告里的迭代级延迟；它描述的是“单迭代需要多长”，不直接等于整个 kernel 的总 latency。</div>
          <div class="section-note">CPU 流水线类比：`II achieved` 更像发射间隔或必须插入的 bubble 间隔；`depth` 更像流水线级数；`iter latency` 更像一条指令/一次迭代从进入到结果出来需要的拍数。注意 `depth` 不是“某一级同时能执行多少条”。</div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>kernel</th><th>pipeline report</th><th>loop</th><th>pipelined</th><th>II achieved</th><th>II target</th><th>depth</th><th>iter latency</th></tr></thead>
              <tbody>{''.join(pipeline_overview_rows) or '<tr><td colspan="8">当前未提供。期望文件名通常为 <kernel>.verbose.sched.rpt 与 <kernel>_Pipeline_*.verbose.sched.rpt，常见于 HLS /.autopilot/db/ 调度报告目录中</td></tr>'}</tbody>
            </table>
          </div>
        </div></div>
      </div>
    </div>
"""

    link_timing_section = f"""
    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">实现时序（来自 routed timing summary）</div>
          <div class="section-note">这部分来自 bitstream 对应的 routed timing summary，用来表达 implementation 阶段时序是否收敛，以及 kernel 时钟域的 WNS/WHS。</div>
          <div class="section-note">requested: {html.escape(link_timing.get('requested_path', '') or '未指定')}</div>
          {'' if link_timing.get('available') else f'<div class="section-note">当前未提供。{html.escape(link_timing.get("how_to_get", ""))}</div>'}
          {f'''
          <div class="kv-grid mb-3">
            <div class="kv-row"><div class="kv-key">report</div><div class="kv-value">{html.escape(link_timing.get('report_path', '') or '未发现')}</div></div>
            <div class="kv-row"><div class="kv-key">constraints_met</div><div class="kv-value">{'yes' if link_timing.get('constraints_met') else 'no'}</div></div>
            <div class="kv-row"><div class="kv-key">design_wns</div><div class="kv-value">{html.escape(design_timing.get('wns_ns', '') or '-')}</div></div>
            <div class="kv-row"><div class="kv-key">design_tns</div><div class="kv-value">{html.escape(design_timing.get('tns_ns', '') or '-')}</div></div>
            <div class="kv-row"><div class="kv-key">design_whs</div><div class="kv-value">{html.escape(design_timing.get('whs_ns', '') or '-')}</div></div>
            <div class="kv-row"><div class="kv-key">design_ths</div><div class="kv-value">{html.escape(design_timing.get('ths_ns', '') or '-')}</div></div>
          </div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>clock</th><th>period(ns)</th><th>freq(MHz)</th><th>WNS(ns)</th><th>WHS(ns)</th><th>WPWS(ns)</th></tr></thead>
              <tbody>{''.join(link_timing_rows) or '<tr><td colspan="6">无</td></tr>'}</tbody>
            </table>
          </div>
          ''' if link_timing.get('available') else ''}
        </div></div>
      </div>
    </div>
"""

    cards = []
    for kernel in data["kernels"]:
        ann = kernel["annotations"]
        xo = kernel["xo"]
        hls = kernel.get("hls", {})
        pipeline = kernel.get("pipeline", {})
        cfg = kernel.get("cfg", {})

        summary_html = "".join(f"<li>{html.escape(item)}</li>" for item in ann.get("summary", []))
        reads_html = "".join(f"<span class='pill alt'>{html.escape(item)}</span>" for item in ann.get("reads", []))
        writes_html = "".join(f"<span class='pill warn'>{html.escape(item)}</span>" for item in ann.get("writes", []))
        has_role = bool(ann.get("role"))
        has_summary = bool(ann.get("summary"))
        has_reads = bool(ann.get("reads"))
        has_writes = bool(ann.get("writes"))
        has_xs_mapping = bool(ann.get("xs_mapping"))
        has_xs_sim_mapping = bool(ann.get("xs_sim_mapping"))
        has_components = bool(ann.get("components"))
        has_component_edges = bool(ann.get("component_edges"))

        xo_args_rows = []
        cfg_hbm_map = cfg.get("bindings_by_arg", {})
        for arg in xo["args"]:
            cfg_targets = ", ".join(
                f"{item['instance']} -> {item['target']}" for item in cfg_hbm_map.get(arg["name"], [])
            )
            xo_args_rows.append(
                "<tr>"
                f"<td>{html.escape(arg['name'])}</td>"
                f"<td>{html.escape(arg['type'])}</td>"
                f"<td>{html.escape(arg['port'])}</td>"
                f"<td>{html.escape(arg['address_qualifier'])}</td>"
                f"<td>{html.escape(arg['offset'])}</td>"
                f"<td>{html.escape(cfg_targets or '-')}</td>"
                "</tr>"
            )

        xo_ports_rows = []
        for port in xo["ports"]:
            xo_ports_rows.append(
                "<tr>"
                f"<td>{html.escape(port['name'])}</td>"
                f"<td>{html.escape(port['mode'])}</td>"
                f"<td>{html.escape(port['data_width'])}</td>"
                f"<td>{html.escape(port['port_type'])}</td>"
                f"<td>{html.escape(port['range'])}</td>"
                "</tr>"
            )

        component_rows = []
        for component in ann.get("components", []):
            component_rows.append(
                "<tr>"
                f"<td>{html.escape(component['name'])}</td>"
                f"<td>{html.escape(component['role'])}</td>"
                f"<td>{html.escape(', '.join(component.get('reads', [])))}</td>"
                f"<td>{html.escape(', '.join(component.get('writes', [])))}</td>"
                f"<td>{html.escape(component.get('xs_mapping', ''))}</td>"
                f"<td>{html.escape(', '.join(component.get('sim_ports', [])))}</td>"
                "</tr>"
            )

        component_edge_rows = []
        for edge in ann.get("component_edges", []):
            component_edge_rows.append(
                "<tr>"
                f"<td>{html.escape(edge['upstream'])}</td>"
                f"<td>{html.escape(edge['downstream'])}</td>"
                f"<td>{html.escape(edge['description'])}</td>"
                "</tr>"
            )

        cfg_binding_rows = []
        for binding in cfg.get("sp_bindings", []):
            cfg_binding_rows.append(
                "<tr>"
                f"<td>{html.escape(binding['instance'])}</td>"
                f"<td>{html.escape(binding['arg'])}</td>"
                f"<td>{html.escape(binding['target'])}</td>"
                "</tr>"
            )

        hls_overall_latency = hls.get("overall_latency", {})
        hls_resources = hls.get("resources", {})
        hls_loop_rows = []
        for loop in hls.get("loop_latency", []):
            hls_loop_rows.append(
                "<tr>"
                f"<td>{html.escape(loop.get('path', ''))}</td>"
                f"<td>{html.escape(loop.get('latency_cycles', '-'))}</td>"
                f"<td>{html.escape(loop.get('latency_abs', '-'))}</td>"
                f"<td>{html.escape(loop.get('iteration_latency', '-'))}</td>"
                f"<td>{html.escape(loop.get('trip_count', '-'))}</td>"
                f"<td>{html.escape(loop.get('slack_ns', '-'))}</td>"
                "</tr>"
            )

        pipeline_top = pipeline.get("top", {})
        pipeline_child_rows = []
        child_loop_names = {
            child_loop.get("loop_name", "")
            for child in pipeline.get("children", [])
            for child_loop in child.get("loop_rows", [])
        }
        for loop in pipeline_top.get("top_loop_rows", []):
            if loop["loop_name"] in child_loop_names:
                continue
            pipeline_child_rows.append(
                "<tr>"
                "<td>top-level-loop</td>"
                f"<td>{html.escape(loop.get('loop_name', '') or '-')}</td>"
                f"<td>{html.escape(loop.get('pipelined', '') or '-')}</td>"
                f"<td>{html.escape(loop.get('ii_achieved', '') or '-')}</td>"
                f"<td>{html.escape(loop.get('ii_target', '') or '-')}</td>"
                "<td>-</td>"
                f"<td>{html.escape(loop.get('iteration_latency', '') or '-')}</td>"
                "</tr>"
            )
        for child in pipeline.get("children", []):
            if child["loop_rows"]:
                for loop in child["loop_rows"]:
                    pipeline_child_rows.append(
                        "<tr>"
                        f"<td>{html.escape(child.get('pipeline_name', ''))}</td>"
                        f"<td>{html.escape(loop.get('loop_name', '') or '-')}</td>"
                        f"<td>{html.escape(loop.get('pipelined', '') or '-')}</td>"
                        f"<td>{html.escape(loop.get('ii_achieved', '') or '-')}</td>"
                        f"<td>{html.escape(loop.get('ii_target', '') or '-')}</td>"
                        f"<td>{html.escape(child.get('individual_depth', '') or '-')}</td>"
                        f"<td>{html.escape(loop.get('iteration_latency', '') or '-')}</td>"
                        "</tr>"
                    )
            else:
                pipeline_child_rows.append(
                    "<tr>"
                    f"<td>{html.escape(child.get('pipeline_name', ''))}</td>"
                    "<td>-</td>"
                    f"<td>{html.escape('yes' if child.get('pipeline_count') == '1' else '-')}</td>"
                    f"<td>{html.escape(child.get('individual_ii', '') or '-')}</td>"
                    "<td>-</td>"
                    f"<td>{html.escape(child.get('individual_depth', '') or '-')}</td>"
                    "<td>-</td>"
                    "</tr>"
                )

        pipeline_section = f"""
                <div class="sub-title">Pipeline 细节（来自 HLS schedule rpt）</div>
                <div class="section-note">这里的 top-level 字段只描述 kernel 顶层摘要；下面的 pipeline / loop 表才是内部 pipeline 优化是否真正形成、II 是否达标的直接依据。</div>
                <div class="kv-grid">
                  <div class="kv-row"><div class="kv-key">top_schedule</div><div class="kv-value">{html.escape(pipeline_top.get('report_path', '') or '未提供')}</div></div>
                  <div class="kv-row"><div class="kv-key">top_is_top_model</div><div class="kv-value">{html.escape(pipeline_top.get('is_top_model', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">top_pipelined</div><div class="kv-value">{html.escape(pipeline_count_to_yes_no(pipeline_top.get('pipeline_count', '')) or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">top_dataflow</div><div class="kv-value">{html.escape(pipeline_count_to_yes_no(pipeline_top.get('dataflow_pipeline_count', '')) or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">top_fsm_states</div><div class="kv-value">{html.escape(pipeline_top.get('fsm_states', '') or '-')}</div></div>
                </div>
                {f'<div class="section-note">当前未提供。{html.escape(pipeline_top.get("how_to_get", ""))}</div>' if not pipeline_top.get("available") and not pipeline_child_rows else ''}
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>pipeline report</th><th>loop</th><th>pipelined</th><th>II achieved</th><th>II target</th><th>depth</th><th>iter latency</th></tr></thead>
                    <tbody>{''.join(pipeline_child_rows) or '<tr><td colspan="7">无</td></tr>'}</tbody>
                  </table>
                </div>
"""

        xs_mapping = ann.get("xs_mapping", {})
        xs_sim_mapping = ann.get("xs_sim_mapping", {})
        cfg_instances = ", ".join(cfg.get("instances", []))
        cfg_path = cfg.get("cfg_path", "")
        hls_report_path = hls.get("report_path", "")
        hls_sections = ""
        if hls.get("available"):
            hls_sections = f"""
                <div class="sub-title">综合时序与延迟（来自 csynth.xml）</div>
                <div class="section-note">requested: {html.escape(hls.get('requested_path', '') or '未指定')}</div>
                <div class="section-note">这里的 `pipeline_type` 是 csynth 对 top-level kernel 的摘要；内部 loop / 子 pipeline 是否真的 pipeline，以及 II 是否达标，请以上面的 `Pipeline 细节` 为准。</div>
                <div class="kv-grid">
                  <div class="kv-row"><div class="kv-key">target_clock</div><div class="kv-value">{html.escape((hls.get('target_clock_ns', '') + ' ns') if hls.get('target_clock_ns') else '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">target_freq</div><div class="kv-value">{html.escape((hls.get('target_clock_mhz', '') + ' MHz') if hls.get('target_clock_mhz') else '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">estimated_clock</div><div class="kv-value">{html.escape((hls.get('estimated_clock_ns', '') + ' ns') if hls.get('estimated_clock_ns') else '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">estimated_freq</div><div class="kv-value">{html.escape((hls.get('estimated_clock_mhz', '') + ' MHz') if hls.get('estimated_clock_mhz') else '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">clock_uncertainty</div><div class="kv-value">{html.escape((hls.get('clock_uncertainty_ns', '') + ' ns') if hls.get('clock_uncertainty_ns') else '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">pipeline_type</div><div class="kv-value">{html.escape(hls.get('pipeline_type', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">latency(best)</div><div class="kv-value">{html.escape(hls_overall_latency.get('best_cycles', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">latency(worst)</div><div class="kv-value">{html.escape(hls_overall_latency.get('worst_cycles', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">latency(best time)</div><div class="kv-value">{html.escape(hls_overall_latency.get('best_time', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">latency(worst time)</div><div class="kv-value">{html.escape(hls_overall_latency.get('worst_time', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">interval(min)</div><div class="kv-value">{html.escape(hls_overall_latency.get('interval_min', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">interval(max)</div><div class="kv-value">{html.escape(hls_overall_latency.get('interval_max', '') or '-')}</div></div>
                </div>

                <div class="sub-title">资源估计（来自 csynth.xml）</div>
                <div class="kv-grid">
                  <div class="kv-row"><div class="kv-key">BRAM_18K</div><div class="kv-value">{html.escape(hls_resources.get('BRAM_18K', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">DSP</div><div class="kv-value">{html.escape(hls_resources.get('DSP', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">FF</div><div class="kv-value">{html.escape(hls_resources.get('FF', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">LUT</div><div class="kv-value">{html.escape(hls_resources.get('LUT', '') or '-')}</div></div>
                  <div class="kv-row"><div class="kv-key">URAM</div><div class="kv-value">{html.escape(hls_resources.get('URAM', '') or '-')}</div></div>
                </div>

                <div class="sub-title">Loop 延迟（来自 csynth.xml）</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>loop</th><th>latency(cycles)</th><th>latency(abs)</th><th>iter latency</th><th>trip count</th><th>slack(ns)</th></tr></thead>
                    <tbody>{''.join(hls_loop_rows) or '<tr><td colspan="6">无</td></tr>'}</tbody>
                  </table>
                </div>
"""
        else:
            hls_sections = f"""
                <div class="sub-title">综合时序与延迟（来自 csynth.xml）</div>
                <div class="section-note">requested: {html.escape(hls.get('requested_path', '') or '未指定')}</div>
                <div class="section-note">当前未提供。{html.escape(hls.get('how_to_get', ''))}</div>
"""

        cards.append(
            f"""
            <div class="col-12 col-xl-6">
              <div class="report-card card h-100"><div class="card-body">
                <div class="section-title">{html.escape(kernel['name'])}</div>
                <div class="section-note">XO: {html.escape(xo['xo_path'])}</div>
                <div class="section-note">CFG: {html.escape(cfg_path or '未发现')}</div>
                {f'<div class="section-note">CSYNTH: {html.escape(hls_report_path)}</div>' if hls_report_path else ''}
                {f'<div class="sub-title">角色定位（来自 spec JSON）</div><div class="muted">{html.escape(ann.get("role", ""))}</div>' if has_role else ''}
                {f'<div class="sub-title">功能摘要（来自 spec JSON）</div><ul>{summary_html}</ul>' if has_summary else ''}
                {f'<div class="sub-title">逻辑读集合（来自 spec JSON）</div><div class="pill-wrap">{reads_html}</div>' if has_reads else ''}
                {f'<div class="sub-title">逻辑写集合（来自 spec JSON）</div><div class="pill-wrap">{writes_html}</div>' if has_writes else ''}

                <div class="sub-title">Kernel 元数据（来自 XO）</div>
                <div class="kv-grid">
                  <div class="kv-row"><div class="kv-key">kernel_name</div><div class="kv-value">{html.escape(xo['kernel_name'])}</div></div>
                  <div class="kv-row"><div class="kv-key">language</div><div class="kv-value">{html.escape(xo['language'])}</div></div>
                  <div class="kv-row"><div class="kv-key">vlnv</div><div class="kv-value">{html.escape(xo['vlnv'])}</div></div>
                  <div class="kv-row"><div class="kv-key">control_protocol</div><div class="kv-value">{html.escape(xo['hw_control_protocol'])}</div></div>
                  <div class="kv-row"><div class="kv-key">cfg_instances</div><div class="kv-value">{html.escape(cfg_instances or '-')}</div></div>
                </div>

                {pipeline_section}

                {hls_sections}

                <div class="sub-title">Arguments（来自 XO + CFG + spec JSON）</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>arg</th><th>type</th><th>port</th><th>aq</th><th>offset</th><th>HBM(CFG)</th></tr></thead>
                    <tbody>{''.join(xo_args_rows)}</tbody>
                  </table>
                </div>

                <div class="sub-title">Ports（来自 XO）</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>port</th><th>mode</th><th>width</th><th>type</th><th>range</th></tr></thead>
                    <tbody>{''.join(xo_ports_rows)}</tbody>
                  </table>
                </div>

                <div class="sub-title">实例与端口绑定（来自 CFG）</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>instance</th><th>arg</th><th>target</th></tr></thead>
                    <tbody>{''.join(cfg_binding_rows) or '<tr><td colspan="3">无</td></tr>'}</tbody>
                  </table>
                </div>

                {f'<div class="sub-title">映射到 XS 抽象（来自 spec JSON）</div><div class="muted">建议抽象层级：{html.escape(xs_mapping.get("kernel", ""))}</div><div class="muted">建议 memory 关注点：{html.escape(", ".join(xs_mapping.get("memory", [])) if xs_mapping.get("memory") else "无")}</div><ul>{"".join(f"<li>{html.escape(item)}</li>" for item in xs_mapping.get("ports", [])) or "<li>无</li>"}</ul>' if has_xs_mapping else ''}
                {f'<div class="sub-title">贴合 XS 模拟器的映射（来自 spec JSON）</div><div class="muted">建议 Kernel 类：{html.escape(xs_sim_mapping.get("kernel_class", ""))}</div><div class="muted">建议 Control 接口：{html.escape(xs_sim_mapping.get("control_if", ""))}</div><div class="sub-title">建议 Data Interface</div><ul>{"".join(f"<li>{html.escape(item)}</li>" for item in xs_sim_mapping.get("data_if", [])) or "<li>无</li>"}</ul><div class="sub-title">建议 State Ports</div><ul>{"".join(f"<li>{html.escape(item)}</li>" for item in xs_sim_mapping.get("state_ports", [])) or "<li>无</li>"}</ul>' if has_xs_sim_mapping else ''}
                {f'<div class="sub-title">内部 Component 草图（来自 spec JSON）</div><div class="table-wrap"><table class="table table-sm align-middle"><thead><tr><th>component</th><th>role</th><th>reads</th><th>writes</th><th>XS mapping</th><th>Sim ports</th></tr></thead><tbody>{"".join(component_rows)}</tbody></table></div>' if has_components else ''}
                {f'<div class="sub-title">内部 Component 依赖（来自 spec JSON）</div><div class="table-wrap"><table class="table table-sm align-middle"><thead><tr><th>upstream</th><th>downstream</th><th>说明</th></tr></thead><tbody>{"".join(component_edge_rows)}</tbody></table></div>' if has_component_edges else ''}
              </div></div>
            </div>
            """
        )

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{html.escape(data['title'])}</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
  <style>
    :root {{
      --bg: #f6f4ee;
      --panel: #fffdf8;
      --line: #ddd7cc;
      --ink: #1f2937;
      --muted: #6b7280;
    }}
    body {{
      background: linear-gradient(180deg, #f6f4ee 0%, #efebe2 100%);
      color: var(--ink);
      font-family: "Helvetica Neue", Arial, sans-serif;
    }}
    .page {{
      max-width: 1680px;
      margin: 0 auto;
      padding: 24px;
    }}
    .report-card {{
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 18px;
      box-shadow: 0 8px 24px rgba(15, 23, 42, 0.05);
    }}
    .report-card .card-body {{
      padding: 20px 22px;
    }}
    .report-title {{
      font-size: 2rem;
      font-weight: 700;
      letter-spacing: -0.02em;
    }}
    .report-subtitle {{
      color: var(--muted);
      word-break: break-all;
    }}
    .section-title {{
      font-size: 1.05rem;
      font-weight: 700;
      margin-bottom: 8px;
    }}
    .section-note {{
      color: var(--muted);
      font-size: 0.88rem;
      margin-bottom: 12px;
      word-break: break-all;
    }}
    .sub-title {{
      font-weight: 700;
      margin-top: 14px;
      margin-bottom: 8px;
    }}
    .pill-wrap {{
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }}
    .pill {{
      display: inline-block;
      padding: 4px 10px;
      border-radius: 999px;
      background: #e0f2fe;
      color: #0c4a6e;
      font-size: 0.85rem;
      font-weight: 700;
    }}
    .pill.alt {{
      background: #dcfce7;
      color: #166534;
    }}
    .pill.warn {{
      background: #fee2e2;
      color: #991b1b;
    }}
    .muted {{
      color: var(--muted);
    }}
    .kv-grid {{
      display: grid;
      gap: 10px;
    }}
    .kv-row {{
      display: grid;
      grid-template-columns: 180px 1fr;
      gap: 14px;
      align-items: baseline;
      font-size: 0.95rem;
    }}
    .kv-key {{
      color: var(--muted);
    }}
    .kv-value {{
      font-weight: 700;
      word-break: break-word;
    }}
    .table-wrap {{
      overflow-x: auto;
      scrollbar-color: #b9ad96 #efe8dc;
    }}
    .table-wrap table {{
      min-width: 100%;
      white-space: nowrap;
    }}
    .table-wrap::-webkit-scrollbar {{
      height: 10px;
      width: 10px;
    }}
    .table-wrap::-webkit-scrollbar-track {{
      background: #efe8dc;
      border-radius: 999px;
    }}
    .table-wrap::-webkit-scrollbar-thumb {{
      background: #b9ad96;
      border-radius: 999px;
      border: 2px solid #efe8dc;
    }}
    .table-wrap::-webkit-scrollbar-thumb:hover {{
      background: #9d8f77;
    }}
  </style>
</head>
<body>
  <div class="page">
    <div class="mb-4">
      <div class="report-title">{html.escape(data['title'])}</div>
      {f'<div class="report-subtitle">{html.escape(data["subtitle"])}</div>' if data.get("subtitle") else ''}
      <div class="report-subtitle">example: {html.escape(data['example_dir'])}</div>
      <div class="report-subtitle">spec: {html.escape(data['spec_path'])}</div>
      <div class="report-subtitle">cfg: {html.escape(data.get('connectivity', {}).get('cfg_path', '') or '未发现')}</div>
      {f'<div class="report-subtitle">vivado-log: {html.escape(data.get("vivado", {}).get("vivado_log_dir", ""))}</div>' if data.get("vivado", {}).get("vivado_log_dir") else ''}
      {f'<div class="report-subtitle">{html.escape(data["source_note"])}</div>' if data.get("source_note") else ''}
    </div>

    {resource_overview_section}

    {f'''
    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">执行顺序（来自 spec JSON）</div>
          <div class="section-note">这部分完全来自 JSON spec，不从 xo 推导。</div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>#</th><th>kernel</th><th>说明</th></tr></thead>
              <tbody>{''.join(sequence_rows)}</tbody>
            </table>
          </div>
        </div></div>
      </div>
    </div>
    ''' if has_sequence else ''}

    {f'''
    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Host 控制节点（来自 spec JSON）</div>
          <div class="section-note">这部分完全来自 JSON spec，用来描述 xo 之外的 host orchestration。</div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>node</th><th>role</th><th>reads</th><th>writes</th><th>drives</th><th>Sim mapping</th></tr></thead>
              <tbody>{''.join(host_rows)}</tbody>
            </table>
          </div>
        </div></div>
      </div>
    </div>
    ''' if has_host_nodes else ''}

    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Connectivity 配置（来自 CFG）</div>
          <div class="section-note">这部分来自 cfg，用来表达 kernel 实例数量以及端口到 HBM 的实际绑定。</div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>kernel</th><th>count</th><th>instances</th><th>sp bindings</th></tr></thead>
              <tbody>{''.join(connectivity_rows) or '<tr><td colspan="4">无</td></tr>'}</tbody>
            </table>
          </div>
          {f'''
          <div class="sub-title">AXI4-Stream 连接</div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>source kernel</th><th>source endpoint</th><th>sink kernel</th><th>sink endpoint</th></tr></thead>
              <tbody>{''.join(stream_connection_rows)}</tbody>
            </table>
          </div>
          ''' if stream_connection_rows else ''}
        </div></div>
      </div>
    </div>

    {link_timing_section}

    {pipeline_overview_section}

    {f'''
    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">阶段依赖（来自 spec JSON）</div>
          <div class="section-note">这部分完全来自 JSON spec，用来表达跨 kernel 的数据与控制依赖。</div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead><tr><th>upstream</th><th>downstream</th><th>说明</th></tr></thead>
              <tbody>{''.join(dependency_rows)}</tbody>
            </table>
          </div>
        </div></div>
      </div>
    </div>
    ''' if has_dependencies else ''}

    <div class="row g-4">
      {''.join(cards)}
    </div>
  </div>
</body>
</html>
"""


def main() -> int:
    args = parse_args()
    example_dir, spec_path = resolve_input_path(args.input_path)
    spec = read_spec(spec_path)
    report = build_report(spec, spec_path, example_dir)

    output = spec.get("output", {})
    json_out = resolve_path(example_dir, output.get("json", "reports/xo_report.json"))
    html_out = resolve_path(example_dir, output.get("html", "reports/xo_report.html"))
    json_out.parent.mkdir(parents=True, exist_ok=True)
    html_out.parent.mkdir(parents=True, exist_ok=True)

    json_out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    html_out.write_text(render_html_page(report), encoding="utf-8")
    print(f"input: {example_dir}")
    print(f"spec: {spec_path}")
    print(f"json: {json_out}")
    print(f"html: {html_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
