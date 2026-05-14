#!/usr/bin/env python3
"""Generate a self-contained HTML waveform viewer for a Project-XS trace segment."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, Iterable, List, Tuple


def load_jsonl(path: Path) -> List[Dict[str, Any]]:
    frames: List[Dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            line = line.strip()
            if not line:
                continue
            try:
                frames.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{path}:{line_number}: invalid JSON: {exc}") from exc
    return frames


def signal_key(signal: Dict[str, Any]) -> str:
    scope = signal.get("scope", "")
    name = signal.get("name", "")
    kind = signal.get("kind", "")
    return f"{scope}.{name}#{kind}"


def signal_label(signal: Dict[str, Any]) -> str:
    name_path = signal.get("name_path")
    if isinstance(name_path, list) and name_path:
        return ".".join(str(item) for item in name_path)

    scope = signal.get("scope", "")
    name = signal.get("name", "")
    kind = signal.get("kind", "")
    suffix = ""
    if kind == "PortInput":
        suffix = " <in>"
    elif kind == "PortOutput":
        suffix = " <out>"
    return f"{scope}.{name}{suffix}"


def normalize_string_list(value: Any) -> List[str]:
    if not isinstance(value, list):
        return []
    return [str(item) for item in value if str(item)]


def normalize_options_path(raw_options: Any, path: List[str]) -> List[List[str]]:
    options_path: List[List[str]] = []
    if isinstance(raw_options, list):
        for index, options in enumerate(raw_options):
            current = normalize_string_list(options)
            if not current and index < len(path):
                current = [path[index]]
            options_path.append(current)
    while len(options_path) < len(path):
        options_path.append([path[len(options_path)]])
    return options_path[:len(path)]


def preferred_display_name(path_item: str, options: List[str]) -> str:
    if path_item == "input" and "输入" in options:
        return "输入"
    if path_item == "output" and "输出" in options:
        return "输出"
    return options[0] if options else path_item


def normalize_description_path(raw_descriptions: Any, path: List[str]) -> List[str]:
    descriptions = normalize_string_list(raw_descriptions)
    while len(descriptions) < len(path):
        descriptions.append("")
    return descriptions[:len(path)]


def normalize_number(value: Any) -> float | int | None:
    if value is None or value == "":
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if not number or number < 0:
        return None
    if isinstance(value, int):
        return value
    return number


def normalize_cycle(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def normalize_root_timing(frame: Dict[str, Any], manifest: Dict[str, Any]) -> Dict[str, Any]:
    raw = frame.get("root_timing")
    if isinstance(raw, dict):
        return {
            "kind": str(raw.get("kind") or frame.get("object_kind") or manifest.get("object_kind") or ""),
            "name": str(raw.get("name") or frame.get("object_name") or manifest.get("object_name") or ""),
            "frequency_hz": normalize_number(raw.get("frequency_hz")),
            "cycle": normalize_cycle(raw.get("cycle")),
        }
    return {
        "kind": str(frame.get("object_kind") or manifest.get("object_kind") or ""),
        "name": str(frame.get("object_name") or manifest.get("object_name") or ""),
        "frequency_hz": normalize_number(frame.get("session_frequency_hz")),
        "cycle": normalize_cycle(frame.get("current_tick", frame.get("cycle"))),
    }


def normalize_timing_path(signal: Dict[str, Any], path: List[str]) -> List[Dict[str, Any]]:
    raw_kinds = signal.get("timing_kind_path")
    raw_frequencies = signal.get("timing_frequency_hz_path")
    raw_cycles = signal.get("timing_cycle_path")
    kinds = raw_kinds if isinstance(raw_kinds, list) else []
    frequencies = raw_frequencies if isinstance(raw_frequencies, list) else []
    cycles = raw_cycles if isinstance(raw_cycles, list) else []
    timing: List[Dict[str, Any]] = []
    for index, _ in enumerate(path):
        kind = str(kinds[index]) if index < len(kinds) and kinds[index] else ""
        timing.append({
            "kind": kind,
            "frequency_hz": normalize_number(frequencies[index]) if index < len(frequencies) else None,
            "cycle": normalize_cycle(cycles[index]) if index < len(cycles) else None,
        })
    return timing


def iter_frame_signals(frame: Dict[str, Any]) -> Iterable[Tuple[str, Dict[str, Any]]]:
    if "simulators" in frame:
        for simulator in frame.get("simulators", []):
            simulator_name = simulator.get("name", "")
            for signal in simulator.get("signals", []):
                yield simulator_name, signal
        return

    for signal in frame.get("signals", []):
        yield "", signal


def simulator_metadata_by_name(frames: List[Dict[str, Any]]) -> Dict[str, Dict[str, Any]]:
    metadata: Dict[str, Dict[str, Any]] = {}
    for frame in frames:
        session_frequency = frame.get("session_frequency_hz")
        for simulator in frame.get("simulators", []):
            name = simulator.get("name", "")
            if not name or name in metadata:
                continue
            metadata[name] = {
                "name": name,
                "frequency_hz": simulator.get("frequency_hz"),
                "session_frequency_hz": session_frequency,
                "session_ticks_per_simulator_cycle": simulator.get(
                    "session_ticks_per_simulator_cycle"
                ),
                "simulator_cycles_per_session_tick": simulator.get(
                    "simulator_cycles_per_session_tick"
                ),
            }
    return metadata


def normalize_trace(manifest: Dict[str, Any], frames: List[Dict[str, Any]]) -> Dict[str, Any]:
    signal_order: List[str] = []
    signal_meta: Dict[str, Dict[str, Any]] = {}
    normalized_frames: List[Dict[str, Any]] = []
    simulator_metadata = simulator_metadata_by_name(frames)
    session_frequency_hz = next(
        (frame.get("session_frequency_hz") for frame in frames if "session_frequency_hz" in frame),
        None,
    )
    root_timing = normalize_root_timing(frames[0], manifest) if frames else {
        "kind": str(manifest.get("object_kind", "")),
        "name": str(manifest.get("object_name", "")),
        "frequency_hz": None,
        "cycle": None,
    }

    for frame_index, frame in enumerate(frames):
        values: Dict[str, Dict[str, Any]] = {}
        frame_root_timing = normalize_root_timing(frame, manifest)
        for simulator_name, signal in iter_frame_signals(frame):
            key = signal_key(signal)
            if key not in signal_meta:
                label = signal_label(signal)
                raw_path = signal.get("name_path")
                if isinstance(raw_path, list) and raw_path:
                    path = [str(item) for item in raw_path]
                else:
                    path = label.split(".")
                display_name = path[-1] if path else str(signal.get("name", ""))
                options_path = normalize_options_path(signal.get("name_options_path"), path)
                if path:
                    display_name = preferred_display_name(path[-1], options_path[-1])
                description_path = normalize_description_path(
                    signal.get("description_path"),
                    path,
                )
                timing_path = normalize_timing_path(signal, path)
                const_kind = str(signal.get("kind", ""))
                signal_meta[key] = {
                    "key": key,
                    "label": label,
                    "path": path,
                    "name_options_path": options_path,
                    "description_path": description_path,
                    "timing_path": timing_path,
                    "display_name": display_name,
                    "scope": signal.get("scope", ""),
                    "name": signal.get("name", ""),
                    "kind": const_kind,
                    "type": signal.get("type", ""),
                    "width_bits": signal.get("width_bits", 0),
                    "simulator": simulator_name,
                    "draw_style": "bus" if const_kind == "RuntimeCycle" else "auto",
                }
                signal_order.append(key)

            values[key] = {
                "value": signal.get("value", ""),
                "valid": bool(signal.get("valid", True)),
                "timing_path": normalize_timing_path(signal, signal_meta[key]["path"]),
            }

        normalized_frames.append({
            "index": frame_index,
            "sequence": frame.get("sequence", frame_index),
            "stage": frame.get("stage", ""),
            "cycle": frame.get("cycle", frame.get("current_tick", frame_index)),
            "time_seconds": frame.get("time_seconds", ""),
            "root_timing": frame_root_timing,
            "values": values,
        })

    return {
        "manifest": manifest,
        "root_timing": root_timing,
        "session_frequency_hz": session_frequency_hz,
        "simulators": simulator_metadata,
        "signals": [signal_meta[key] for key in signal_order],
        "frames": normalized_frames,
    }


HTML_TEMPLATE = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Project-XS Waveform</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0f1115;
      --panel: #171a20;
      --panel2: #1f242b;
      --panel3: #252c34;
      --line: #343b46;
      --line2: #242a32;
      --text: #d8dee9;
      --muted: #8b96a5;
      --green: #6bd17a;
      --yellow: #9eb3c7;
      --red: #e06c75;
      --blue: #65a7e8;
      --orange: #d99a5f;
    }
    * { box-sizing: border-box; }
    html {
      width: 100%;
      height: 100%;
      overflow: hidden;
    }
    body {
      margin: 0;
      background: var(--bg);
      color: var(--text);
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: 12px;
      width: 100%;
      height: 100%;
      overflow: hidden;
    }
    .app {
      display: grid;
      grid-template-rows: auto minmax(0, 1fr) 24px;
      width: 100vw;
      height: 100vh;
      height: 100dvh;
      min-width: 0;
      min-height: 0;
      overflow: hidden;
    }
    .toolbar {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 7px 10px;
      background: var(--panel);
      border-bottom: 1px solid var(--line);
      flex-wrap: wrap;
      min-height: 40px;
      overflow: hidden;
      min-width: 0;
    }
    .title { color: var(--green); font-weight: 700; margin-right: 8px; flex: 0 0 auto; }
    .toolbar label {
      display: inline-flex;
      align-items: center;
      gap: 4px;
      white-space: nowrap;
      min-width: 0;
    }
    #summary {
      flex: 1 1 180px;
      min-width: 0;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    button, input, select {
      height: 24px;
      background: #11151a;
      color: var(--text);
      border: 1px solid var(--line);
      border-radius: 4px;
      font: inherit;
      min-width: 0;
    }
    button { padding: 0 8px; cursor: pointer; flex: 0 0 auto; }
    button:hover, select:hover { border-color: var(--blue); }
    button.active { color: var(--green); background: #16221a; }
    input { padding: 0 7px; width: clamp(120px, 22vw, 220px); }
    select { padding: 0 6px; max-width: 100%; }
    .content {
      display: grid;
      grid-template-columns: minmax(150px, 42vw) 5px minmax(0, 1fr);
      min-height: 0;
      min-width: 0;
      position: relative;
      overflow: hidden;
    }
    .content.left-collapsed {
      grid-template-columns: 0 0 minmax(0, 1fr);
    }
    .content.left-collapsed .wave-wrap {
      grid-column: 3;
    }
    .left {
      display: grid;
      grid-template-rows: 30px auto minmax(0, 1fr);
      min-width: 0;
      width: 100%;
      background: var(--panel);
      border-right: 1px solid var(--line);
      position: relative;
      overflow: hidden;
    }
    .content.left-collapsed .left {
      border-right: 0;
      visibility: hidden;
      pointer-events: none;
    }
    .splitter {
      width: 5px;
      background: #11151a;
      cursor: col-resize;
      z-index: 10;
    }
    .content.left-collapsed .splitter {
      display: none;
    }
    .splitter:hover { background: var(--blue); opacity: .35; }
    .left-head {
      display: grid;
      grid-template-columns: minmax(120px, 1fr) 7px 150px;
      align-items: center;
      color: var(--muted);
      border-bottom: 1px solid var(--line);
      background: var(--panel2);
      min-width: 0;
    }
    .head-name {
      display: flex;
      align-items: center;
      gap: 6px;
      padding-left: 6px;
      min-width: 0;
      overflow: hidden;
      white-space: nowrap;
    }
    .head-value { padding-left: 8px; }
    .pane-toggle {
      width: 42px;
      height: 20px;
      padding: 0;
    }
    .left-expander {
      position: absolute;
      left: 8px;
      top: 6px;
      z-index: 20;
      display: none;
    }
    .content.left-collapsed .left-expander {
      display: block;
    }
    .value-splitter {
      width: 7px;
      align-self: stretch;
      border-left: 1px solid var(--line);
      border-right: 1px solid #11151a;
      cursor: col-resize;
      background: #15191f;
    }
    .cycle-list {
      overflow: hidden;
      min-height: 0;
      background: var(--panel);
      box-shadow: inset 0 -1px 0 var(--line);
    }
    .cycle-list:empty {
      box-shadow: none;
    }
    .signal-list { overflow: auto; min-height: 0; }
    .row {
      display: grid;
      grid-template-columns: minmax(120px, 1fr) 7px 150px;
      align-items: center;
      height: 22px;
      border-bottom: 1px solid var(--line2);
      user-select: none;
      min-width: 0;
    }
    .row:nth-child(odd) { background: #141820; }
    .row:hover { background: #24303a; }
    .row.selected { background: #26384b; }
    .row.drop-before { box-shadow: inset 0 2px 0 var(--blue); }
    .row.drop-after { box-shadow: inset 0 -2px 0 var(--blue); }
    .row.group { color: #cfd6df; font-weight: 700; background: #1b2027; }
    .row.hidden-row { display: none; }
    .name-cell {
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      padding: 0 7px;
      cursor: default;
      min-width: 0;
    }
    .value-cell {
      color: var(--yellow);
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      padding: 0 7px;
      min-width: 0;
    }
    .value-cell.selected-value { outline: 1px solid var(--blue); outline-offset: -2px; background: #1d3141; }
    .row-splitter {
      width: 7px;
      align-self: stretch;
      border-left: 1px solid var(--line);
      border-right: 1px solid #11151a;
      background: #15191f;
    }
    .twisty { display: inline-block; width: 14px; color: var(--muted); }
    .indent { display: inline-block; }
    .kind { color: var(--muted); font-weight: 400; }
    .wave-wrap {
      display: grid;
      grid-template-rows: 30px auto minmax(0, 1fr);
      min-width: 0;
      min-height: 0;
      overflow: hidden;
      background: #090b0e;
      position: relative;
    }
    .wave-head {
      border-bottom: 1px solid var(--line);
      background: #090b0e;
    }
    .cycle-wave {
      overflow: hidden;
      min-height: 0;
      background: #090b0e;
      position: relative;
      box-shadow: inset 0 -1px 0 var(--line);
    }
    .cycle-wave:empty {
      box-shadow: none;
    }
    .cycle-wave canvas {
      transform-origin: 0 0;
    }
    .scroll {
      overflow: auto;
      min-width: 0;
      min-height: 0;
      position: relative;
    }
    canvas { display: block; image-rendering: crisp-edges; }
    .cursor-line {
      position: absolute;
      left: 0;
      top: 0;
      height: 100%;
      width: 1px;
      background: var(--red);
      pointer-events: none;
      z-index: 6;
      transform: translateX(-10000px);
    }
    .status {
      display: flex;
      gap: 18px;
      align-items: center;
      padding: 0 10px;
      background: var(--panel);
      border-top: 1px solid var(--line);
      color: var(--muted);
      overflow: hidden;
      white-space: nowrap;
    }
    @media (max-width: 760px) {
      body { font-size: 11px; }
      .toolbar { gap: 5px; padding: 5px 6px; }
      button { padding: 0 6px; }
      input { width: clamp(96px, 28vw, 160px); }
      .content { grid-template-columns: minmax(120px, 46vw) 5px minmax(0, 1fr); }
      .left-head, .row { grid-template-columns: minmax(60px, 1fr) 7px 72px; }
      .status { gap: 10px; padding: 0 6px; }
    }
    .context-menu {
      position: fixed;
      z-index: 100;
      min-width: 170px;
      background: #15191f;
      border: 1px solid var(--line);
      box-shadow: 0 10px 28px rgba(0, 0, 0, .45);
      display: none;
    }
    .context-menu button {
      display: block;
      width: 100%;
      border: 0;
      border-radius: 0;
      text-align: left;
      background: transparent;
      height: 26px;
    }
    .context-menu button:hover { background: #26384b; }
    .menu-item {
      position: relative;
      height: 26px;
      padding: 6px 8px;
      white-space: nowrap;
    }
    .menu-item:hover {
      background: #26384b;
    }
    .submenu {
      position: absolute;
      left: calc(100% - 1px);
      top: 0;
      min-width: 170px;
      max-height: min(60vh, 360px);
      overflow: auto;
      background: #15191f;
      border: 1px solid var(--line);
      box-shadow: 0 10px 28px rgba(0, 0, 0, .45);
      display: none;
    }
    .menu-item:hover .submenu { display: block; }
    .menu-separator {
      height: 1px;
      background: var(--line);
      margin: 4px 0;
    }
  </style>
</head>
<body>
<div class="app">
  <div class="toolbar">
    <span class="title">Project-XS 波形</span>
    <button id="zoomIn">放大</button>
    <button id="zoomOut">缩小</button>
    <button id="fit">适配</button>
    <button id="prevChange">上个变化</button>
    <button id="nextChange">下个变化</button>
    <button id="hideSelected">隐藏选中</button>
    <button id="showAll">全部显示</button>
    <label>搜索 <input id="search" type="search" placeholder="scope 或信号名"></label>
    <span id="summary"></span>
  </div>
  <div class="content" id="content">
    <section class="left" id="leftPane">
      <div class="left-head" id="leftHead"><span class="head-name"><button id="toggleLeftPane" class="pane-toggle" title="收起信号列表">收起</button><span>信号</span></span><span class="value-splitter" id="valueSplitter"></span><span class="head-value">当前值</span></div>
      <div id="cycleList" class="cycle-list"></div>
      <div id="signalList" class="signal-list"></div>
    </section>
    <div class="splitter" id="panelSplitter"></div>
    <button id="leftPaneExpander" class="left-expander" title="展开信号列表">展开左侧</button>
    <section class="wave-wrap">
      <div class="wave-head"></div>
      <div id="cycleWave" class="cycle-wave"><canvas id="cycleCanvas"></canvas><div id="cycleCursorLine" class="cursor-line"></div></div>
      <div id="scroll" class="scroll"><canvas id="wave"></canvas><div id="waveCursorLine" class="cursor-line"></div></div>
    </section>
  </div>
  <div class="status" id="status"></div>
</div>
<div class="context-menu" id="menu">
  <button data-action="hide">隐藏选中</button>
  <button data-action="restoreGroup">恢复本组隐藏信号</button>
  <button data-action="collapse">展开/收起本组</button>
  <div class="menu-separator"></div>
  <div class="menu-item">更换显示名 <span style="float:right">›</span><div class="submenu" id="nameMenu"></div></div>
  <button data-action="resetName">恢复原名</button>
</div>
<div class="context-menu" id="radixMenu">
  <button data-radix="auto">自动</button>
  <button data-radix="bin">二进制</button>
  <button data-radix="oct">八进制</button>
  <button data-radix="dec">十进制</button>
  <button data-radix="hex">十六进制</button>
  <button data-radix="ascii">ASCII</button>
</div>
<script id="trace-data" type="application/json">__TRACE_JSON__</script>
<script>
const TRACE = JSON.parse(document.getElementById('trace-data').textContent);
const frames = TRACE.frames;
const rawSignals = TRACE.signals;
let px = 42;
const lane = 22;
let cursorX = Math.max(0, frames.length - 1) * px;
let panelWidth = 0;
let panelRatio = 0;
let nameWidth = 0;
let valueWidth = 0;
let leftPaneCollapsed = false;
let leftPaneRestoreState = null;
let selected = new Set();
let selectedValues = new Set();
let nameOptionIndices = new Map();
let signalRadix = new Map();
let collapsed = new Set();
let hidden = new Set();
let dragKey = null;
let dropTarget = null;
let dropAfter = false;
let lastSelectedKey = null;

const signalList = document.getElementById('signalList');
const cycleList = document.getElementById('cycleList');
const wave = document.getElementById('wave');
const cycleWave = document.getElementById('cycleWave');
const cycleCanvas = document.getElementById('cycleCanvas');
const scroll = document.getElementById('scroll');
const cycleCursorLine = document.getElementById('cycleCursorLine');
const waveCursorLine = document.getElementById('waveCursorLine');
const statusEl = document.getElementById('status');
const summary = document.getElementById('summary');
const content = document.getElementById('content');
const leftPane = document.getElementById('leftPane');
const toggleLeftPane = document.getElementById('toggleLeftPane');
const leftPaneExpander = document.getElementById('leftPaneExpander');
const menu = document.getElementById('menu');
const nameMenu = document.getElementById('nameMenu');
const radixMenu = document.getElementById('radixMenu');
let layoutFrame = 0;
let renderFrame = 0;

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function contentWidth() {
  return Math.max(0, Math.floor(content.getBoundingClientRect().width));
}

function waveMinWidth() {
  const width = contentWidth();
  if (width < 520) return 96;
  if (width < 760) return 140;
  return 180;
}

function panelBounds() {
  const width = contentWidth();
  if (!width) return {min: 120, max: 240};
  const min = width < 520 ? 110 : 150;
  const max = Math.max(min, width - 5 - waveMinWidth());
  return {min, max};
}

function defaultPanelWidth() {
  const width = contentWidth() || window.innerWidth || 320;
  const targetRatio = width < 760 ? 0.46 : 0.38;
  const bounds = panelBounds();
  return clamp(Math.round(width * targetRatio), bounds.min, bounds.max);
}

function normalizePanelWidth(width) {
  const bounds = panelBounds();
  return clamp(Math.round(width), bounds.min, bounds.max);
}

function normalizeValueColumns() {
  const available = Math.max(1, Math.floor(panelWidth || leftPane.clientWidth || defaultPanelWidth()));
  const compact = available < 220;
  const minName = compact ? 48 : 80;
  const minValue = compact ? 54 : 80;
  const maxValue = Math.max(minValue, available - minName - 7);
  valueWidth = valueWidth || clamp(Math.round(available * 0.3), minValue, Math.min(180, maxValue));
  valueWidth = clamp(valueWidth, minValue, maxValue);
  nameWidth = available - valueWidth - 7;
  if (nameWidth < minName) {
    nameWidth = minName;
    valueWidth = Math.max(minValue, available - nameWidth - 7);
  }
}

function rememberLeftPaneState() {
  leftPaneRestoreState = {
    panelWidth: panelWidth || defaultPanelWidth(),
    panelRatio,
    nameWidth,
    valueWidth,
    signalScrollTop: signalList.scrollTop,
    waveScrollTop: scroll.scrollTop,
    waveScrollLeft: scroll.scrollLeft,
  };
}

function waveContentWidth() {
  return Math.max(1, scroll.clientWidth, frames.length * px + 20);
}
function waveContentHeight(rows) {
  return Math.max(scroll.clientHeight, rows.length * lane + 2);
}

function scheduleWaveRender() {
  if (renderFrame) return;
  renderFrame = requestAnimationFrame(() => {
    renderFrame = 0;
    renderWave();
  });
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, ch => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;',
  }[ch]));
}

function formatNumber(value, digits = 6) {
  if (value === null || value === undefined || value === '') return '';
  const number = Number(value);
  if (!Number.isFinite(number)) return String(value);
  return Number(number.toPrecision(digits)).toString();
}

function formatHz(value) {
  const text = formatNumber(value);
  return text ? `${text}Hz` : '';
}

function kindLabel(kind) {
  const labels = {
    simulation_session: 'session',
    cycle_simulator: 'simulator',
    kernel: 'kernel',
    kernel_component: 'component',
  };
  return labels[kind] || kind || '层级';
}

function timingForNodeAt(node, frameIndex = currentFrameIndex()) {
  if (!node) return null;
  if (node.type === 'signal') {
    const sampleTiming = frames[frameIndex]?.values?.[node.key]?.timing_path;
    const timing = Array.isArray(sampleTiming) ? sampleTiming : node.signal.timing_path;
    const last = timing?.[timing.length - 1];
    return last?.frequency_hz ? last : null;
  }
  return node.timing || null;
}

function parentTimingFor(node, frameIndex = currentFrameIndex()) {
  if (!node) return TRACE.root_timing || null;
  if (!node.parent || node.parent.key === 'group:root') return TRACE.root_timing || null;
  return timingForNodeAt(node.parent, frameIndex) || TRACE.root_timing || null;
}

function isRootObjectNode(node, timing) {
  const rootTiming = TRACE.root_timing || {};
  if (!node || !timing || !node.parent || node.parent.key !== 'group:root') return false;
  if (rootTiming.kind && timing.kind && rootTiming.kind !== timing.kind) return false;
  const rootName = rootTiming.name || TRACE.manifest.object_name || '';
  return rootName && rootName === (node.originalName || node.name);
}

function timingRelationText(node) {
  const timing = timingForNodeAt(node);
  if (!timing?.frequency_hz) return '';
  const parts = [formatHz(timing.frequency_hz)].filter(Boolean);
  const parentTiming = parentTimingFor(node);
  if (parentTiming?.frequency_hz && !isRootObjectNode(node, timing)) {
    const ratio = Number(parentTiming.frequency_hz) / Number(timing.frequency_hz);
    if (Number.isFinite(ratio) && ratio > 0) {
      parts.push(`1本层周期=${formatNumber(ratio)}上层周期`);
    }
  }
  return parts.join(', ');
}

function groupPathFor(sig) {
  const path = [...sig.path];
  path.pop();
  return path.length ? path : ['root'];
}

function optionsFor(node) {
  if (!node) return [''];
  if (node.type === 'signal') {
    if (node.signal.kind === 'RuntimeCycle') {
      const parent = node.parent && node.parent.key !== 'group:root'
        ? baseDisplayNameFor(node.parent)
        : (node.signal.simulator || node.signal.scope || 'simulator');
      return [`${parent}.cycle`];
    }
    const last = node.signal.name_options_path?.[node.signal.name_options_path.length - 1] || [];
    return last.length ? last : [node.signal.display_name || node.signal.name || node.label];
  }
  return node.nameOptions?.length ? node.nameOptions : [node.originalName || node.name];
}

function defaultNameFor(node) {
  return optionsFor(node)[0] || '';
}

function baseDisplayNameFor(node) {
  const options = optionsFor(node);
  const index = nameOptionIndices.get(node.key) || 0;
  return options[Math.min(index, options.length - 1)] || defaultNameFor(node);
}

function displayNameFor(node) {
  const name = baseDisplayNameFor(node);
  if (node.type === 'group') {
    const relation = timingRelationText(node);
    return relation ? `${name} (${relation})` : name;
  }
  return name;
}

function descriptionFor(node) {
  if (!node) return '';
  if (node.type === 'signal') {
    const path = node.signal.description_path || [];
    return [...path].reverse().find(text => text) || node.label;
  }
  return node.description || node.label || defaultNameFor(node);
}

function buildModel() {
  const root = {key: 'group:root', type: 'group', name: 'root', originalName: 'root', nameOptions: ['root'], description: '', label: 'root', depth: 0, children: [], parent: null};
  const groupMap = new Map([[root.key, root]]);
  for (const sig of rawSignals) {
    let parent = root;
    const groups = groupPathFor(sig);
    let accum = '';
    groups.forEach((part, index) => {
      accum = accum ? `${accum}.${part}` : part;
      const key = `group:${accum}`;
      if (!groupMap.has(key)) {
        const nameOptions = sig.name_options_path?.[index] || [part];
        const displayName = (part === 'input' && nameOptions.includes('输入')) ? '输入'
          : (part === 'output' && nameOptions.includes('输出')) ? '输出'
          : part;
        const description = sig.description_path?.[index] || '';
        const timing = sig.timing_path?.[index] || null;
        const node = {key, type: 'group', name: displayName, originalName: part, nameOptions, description, timing, label: accum, depth: index + 1, children: [], parent};
        groupMap.set(key, node);
        parent.children.push(node);
      } else {
        const node = groupMap.get(key);
        if (!node.description && sig.description_path?.[index]) node.description = sig.description_path[index];
        if ((!node.nameOptions || node.nameOptions.length <= 1) && sig.name_options_path?.[index]) node.nameOptions = sig.name_options_path[index];
        if (!node.timing?.frequency_hz && sig.timing_path?.[index]?.frequency_hz) node.timing = sig.timing_path[index];
      }
      parent = groupMap.get(key);
    });
    parent.children.push({key: sig.key, type: 'signal', signal: sig, label: sig.label, depth: parent.depth + 1, parent});
  }
  return root;
}

const root = buildModel();

function formatInt(value, radix) {
  if (value === '' || value === 'z') return value || '';
  if (!/^-?\d+$/.test(String(value))) return value;
  let n = BigInt(value);
  const neg = n < 0n;
  if (neg) n = -n;
  let text = value;
  if (radix === 'bin') text = '0b' + n.toString(2);
  else if (radix === 'oct') text = '0o' + n.toString(8);
  else if (radix === 'hex') text = '0x' + n.toString(16);
  else if (radix === 'dec') text = n.toString(10);
  else if (radix === 'ascii') {
    const code = Number(n);
    text = code >= 32 && code < 127 ? `'${String.fromCharCode(code)}'` : value;
  }
  return neg ? '-' + text : text;
}

function radixFor(sig) { return signalRadix.get(sig.key) || 'auto'; }
function valueAt(sig, index) {
  index = clampFrameIndex(index);
  const item = frames[index]?.values?.[sig.key];
  if (!item) return '';
  if (!item.valid) return 'z';
  const value = item.value;
  const radix = radixFor(sig);
  return radix === 'auto' ? value : formatInt(value, radix);
}
function rawValueAt(sig, index) {
  const item = frames[index]?.values?.[sig.key];
  if (!item) return '';
  return item.valid ? item.value : 'z';
}
function changeIndicesForSignal(sig) {
  const indices = [0];
  let previous = rawValueAt(sig, 0);
  for (let index = 1; index < frames.length; ++index) {
    const current = rawValueAt(sig, index);
    if (current !== previous) {
      indices.push(index);
      previous = current;
    }
  }
  return indices;
}
function collectChangeSignalNodes(node, out = []) {
  if (!node) return out;
  if (node.type === 'signal') {
    out.push(node);
    return out;
  }
  for (const child of node.children || []) collectChangeSignalNodes(child, out);
  return out;
}
function selectedChangeNodes() {
  const nodes = [...selected].map(key => findNode(key)).filter(Boolean);
  if (!nodes.length) return visibleRows().filter(node => node.type === 'signal');
  const out = [];
  for (const node of nodes) collectChangeSignalNodes(node, out);
  return out;
}
function changeIndicesForNodes(nodes) {
  const indices = new Set();
  for (const node of nodes) {
    for (const signalNode of collectChangeSignalNodes(node)) {
      for (const index of changeIndicesForSignal(signalNode.signal)) indices.add(index);
    }
  }
  return [...indices].sort((a, b) => a - b);
}
function snapXForSignal(sig, x) {
  if (!sig) return x;
  const snapDistance = Math.max(5, Math.min(14, px * 0.35));
  let bestX = x;
  let bestDistance = snapDistance + 1;
  for (const index of changeIndicesForSignal(sig)) {
    const candidateX = index * px;
    const distance = Math.abs(candidateX - x);
    if (distance <= snapDistance && distance < bestDistance) {
      bestX = candidateX;
      bestDistance = distance;
    }
  }
  return bestX;
}
function isBit(value) { return value === '0' || value === '1'; }
function isSingleBitSignal(sig) {
  if (Number(sig.width_bits) === 1) return true;
  const type = String(sig.type || '').toLowerCase();
  return type === 'bool' || type === 'boolean' || type === 'bit';
}
function drawsAsBit(sig, value) {
  return sig.draw_style !== 'bus' && isSingleBitSignal(sig) && isBit(value);
}
function isDescendantGroup(node, groupKey) {
  let cur = node.parent;
  while (cur) { if (cur.key === groupKey) return true; cur = cur.parent; }
  return false;
}
function flatten(nodes = root.children, out = []) {
  for (const node of nodes) {
    if (node.key !== 'group:root') out.push(node);
    if (node.type === 'group' && !collapsed.has(node.key)) flatten(node.children, out);
  }
  return out;
}
function matchesQuery(node, query) {
  if (!query) return true;
  return displayNameFor(node).toLowerCase().includes(query) ||
         node.label.toLowerCase().includes(query) ||
         (node.signal?.kind || '').toLowerCase().includes(query);
}
function isRuntimeCycleNode(node) {
  return node?.type === 'signal' && node.signal?.kind === 'RuntimeCycle';
}
function filteredRows() {
  const query = document.getElementById('search').value.trim().toLowerCase();
  return flatten().filter(node => {
    if (node.type === 'signal' && hidden.has(node.key)) return false;
    return matchesQuery(node, query);
  });
}
function cycleRows() {
  return filteredRows().filter(isRuntimeCycleNode);
}
function visibleRows() {
  return filteredRows().filter(node => !isRuntimeCycleNode(node));
}
function applyGridColumns() {
  if (leftPaneCollapsed) return;
  normalizeValueColumns();
  const template = `${nameWidth}px 7px ${valueWidth}px`;
  document.getElementById('leftHead').style.gridTemplateColumns = template;
  Array.from(cycleList.children).forEach(row => row.style.gridTemplateColumns = template);
  Array.from(signalList.children).forEach(row => row.style.gridTemplateColumns = template);
}
function updateContentColumns(width, rememberRatio = true) {
  if (leftPaneCollapsed) {
    content.style.gridTemplateColumns = '0 0 minmax(0, 1fr)';
    scheduleWaveRender();
    return;
  }
  panelWidth = normalizePanelWidth(width);
  const total = contentWidth();
  if (rememberRatio && total > 0) {
    panelRatio = panelWidth / total;
  }
  content.style.gridTemplateColumns = `${panelWidth}px 5px minmax(0, 1fr)`;
  leftPane.style.width = '100%';
  normalizeValueColumns();
  applyGridColumns();
  scheduleWaveRender();
}
function autoLayout() {
  if (leftPaneCollapsed) {
    content.style.gridTemplateColumns = '0 0 minmax(0, 1fr)';
    scheduleWaveRender();
    return;
  }
  const total = contentWidth();
  const target = panelRatio && total > 0 ? Math.round(total * panelRatio) : defaultPanelWidth();
  updateContentColumns(target, false);
}
function scheduleAutoLayout() {
  if (layoutFrame) return;
  layoutFrame = requestAnimationFrame(() => {
    layoutFrame = 0;
    autoLayout();
  });
}
function setLeftPaneCollapsed(collapsedState) {
  if (collapsedState === leftPaneCollapsed) return;
  if (collapsedState) {
    rememberLeftPaneState();
    leftPaneCollapsed = true;
    content.classList.add('left-collapsed');
    content.style.gridTemplateColumns = '0 0 minmax(0, 1fr)';
    document.querySelector('.wave-wrap').style.gridColumn = '3';
    toggleLeftPane.textContent = '展开';
    toggleLeftPane.title = '展开信号列表';
    toggleLeftPane.classList.add('active');
    scheduleWaveRender();
    return;
  }

  const restore = leftPaneRestoreState;
  leftPaneCollapsed = false;
  content.classList.remove('left-collapsed');
  toggleLeftPane.textContent = '收起';
  toggleLeftPane.title = '收起信号列表';
  toggleLeftPane.classList.remove('active');
  if (restore) {
    panelWidth = restore.panelWidth || defaultPanelWidth();
    panelRatio = restore.panelRatio || 0;
    nameWidth = restore.nameWidth || 0;
    valueWidth = restore.valueWidth || 0;
    updateContentColumns(panelWidth, false);
    document.querySelector('.wave-wrap').style.gridColumn = '';
    signalList.scrollTop = restore.signalScrollTop || 0;
    scroll.scrollTop = restore.waveScrollTop || 0;
    scroll.scrollLeft = restore.waveScrollLeft || 0;
  } else {
    document.querySelector('.wave-wrap').style.gridColumn = '';
    autoLayout();
  }
  renderAll();
}
function createListRow(node) {
    const frameIndex = currentFrameIndex();
    const row = document.createElement('div');
    row.className = `row ${node.type}`;
    row.dataset.key = node.key;
    row.draggable = true;
    row.style.gridTemplateColumns = `${nameWidth}px 7px ${valueWidth}px`;
    if (selected.has(node.key)) row.classList.add('selected');
    const name = document.createElement('div');
    name.className = 'name-cell';
    const indent = `<span class="indent" style="width:${Math.max(0, node.depth - 1) * 14}px"></span>`;
    if (node.type === 'group') {
      name.innerHTML = `${indent}<span class="twisty">${collapsed.has(node.key) ? '▸' : '▾'}</span>${escapeHtml(displayNameFor(node))}`;
      name.title = descriptionFor(node);
    } else {
      name.innerHTML = `${indent}<span class="twisty"></span>${escapeHtml(displayNameFor(node))} <span class="kind">${node.signal.kind}</span>`;
      name.title = descriptionFor(node);
    }
    const split = document.createElement('span');
    split.className = 'row-splitter';
    const value = document.createElement('div');
    value.className = 'value-cell';
    if (node.type === 'signal') {
      value.textContent = valueAt(node.signal, frameIndex);
      value.title = `点击切换进制：${radixLabel(radixFor(node.signal))}`;
      if (selectedValues.has(node.key)) value.classList.add('selected-value');
    } else {
      const hiddenCount = countHiddenInGroup(node);
      value.textContent = hiddenCount ? `${hiddenCount} 个隐藏` : '';
    }
    row.append(name, split, value);
    row.addEventListener('click', (event) => onRowClick(event, node));
    row.addEventListener('contextmenu', (event) => openMenu(event, node));
    value.addEventListener('click', (event) => openRadixMenu(event, node));
    row.addEventListener('dragstart', () => { dragKey = node.key; });
    row.addEventListener('dragover', (event) => onDragOver(event, node, row));
    row.addEventListener('dragleave', () => row.classList.remove('drop-before', 'drop-after'));
    row.addEventListener('drop', (event) => onDrop(event, node));
    return row;
}
function renderList() {
  cycleList.textContent = '';
  for (const node of cycleRows()) {
    cycleList.appendChild(createListRow(node));
  }
  signalList.textContent = '';
  for (const node of visibleRows()) {
    signalList.appendChild(createListRow(node));
  }
  applyGridColumns();
}
function countHiddenInGroup(group) {
  let count = 0;
  function walk(node) {
    for (const child of node.children || []) {
      if (child.type === 'signal' && hidden.has(child.key)) count++;
      if (child.type === 'group') walk(child);
    }
  }
  walk(group);
  return count;
}
function onRowClick(event, node) {
  if (event.target.closest('.value-cell')) return;
  if (node.type === 'group' && event.detail === 1 &&
      !event.ctrlKey && !event.metaKey && !event.shiftKey) {
    collapsed.has(node.key) ? collapsed.delete(node.key) : collapsed.add(node.key);
    renderAll();
    return;
  }
  selectNode(event, node);
  renderAll();
}

function selectNode(event, node) {
  const rows = [...cycleRows(), ...visibleRows()];
  const rowKeys = rows.map(row => row.key);
  if (event.shiftKey && lastSelectedKey && rowKeys.includes(lastSelectedKey)) {
    if (!event.ctrlKey && !event.metaKey) {
      selected.clear();
      selectedValues.clear();
    }
    const start = rowKeys.indexOf(lastSelectedKey);
    const end = rowKeys.indexOf(node.key);
    const [lo, hi] = start <= end ? [start, end] : [end, start];
    for (let index = lo; index <= hi; ++index) {
      selected.add(rowKeys[index]);
      if (rows[index].type === 'signal') {
        selectedValues.add(rowKeys[index]);
      }
    }
    return;
  }

  if (event.ctrlKey || event.metaKey) {
    if (selected.has(node.key)) {
      selected.delete(node.key);
      selectedValues.delete(node.key);
    } else {
      selected.add(node.key);
      if (node.type === 'signal') selectedValues.add(node.key);
    }
  } else {
    selected.clear();
    selectedValues.clear();
    selected.add(node.key);
    if (node.type === 'signal') selectedValues.add(node.key);
  }
  lastSelectedKey = node.key;
}

function selectedSignalNodes(fallback) {
  const nodes = [...selected].map(key => findNode(key)).filter(node => node?.type === 'signal');
  if (nodes.length) return nodes;
  if (fallback?.type === 'signal') return [fallback];
  return [];
}

function radixLabel(radix) {
  if (radix === 'bin') return '二进制';
  if (radix === 'oct') return '八进制';
  if (radix === 'dec') return '十进制';
  if (radix === 'hex') return '十六进制';
  if (radix === 'ascii') return 'ASCII';
  return '自动';
}

function openRadixMenu(event, node) {
  if (node.type !== 'signal') return;
  event.preventDefault();
  event.stopPropagation();
  if (!selected.has(node.key)) {
    selectNode(event, node);
  }
  menu.style.display = 'none';
  radixMenu.dataset.node = node.key;
  radixMenu.style.left = `${event.clientX}px`;
  radixMenu.style.top = `${event.clientY}px`;
  Array.from(radixMenu.querySelectorAll('button')).forEach(button => {
    button.classList.toggle('active', button.dataset.radix === radixFor(node.signal));
  });
  radixMenu.style.display = 'block';
  renderAll();
}

radixMenu.addEventListener('click', (event) => {
  const radix = event.target.dataset.radix;
  if (!radix) return;
  const fallback = findNode(radixMenu.dataset.node);
  const nodes = selectedSignalNodes(fallback);
  for (const node of nodes) {
    if (radix === 'auto') signalRadix.delete(node.key);
    else signalRadix.set(node.key, radix);
  }
  radixMenu.style.display = 'none';
  renderAll();
});

function resetNodeName(node) {
  if (!node) return;
  nameOptionIndices.delete(node.key);
}

function applyNameOption(node, index) {
  if (!node) return;
  if (index <= 0) nameOptionIndices.delete(node.key);
  else nameOptionIndices.set(node.key, index);
}

function populateNameMenu(node) {
  nameMenu.textContent = '';
  const options = optionsFor(node);
  options.forEach((option, index) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.dataset.nameIndex = String(index);
    button.classList.toggle('active', displayNameFor(node) === option);
    button.textContent = option;
    nameMenu.appendChild(button);
  });
}

function openMenu(event, node) {
  event.preventDefault();
  radixMenu.style.display = 'none';
  menu.dataset.node = node.key;
  populateNameMenu(node);
  menu.style.left = `${event.clientX}px`;
  menu.style.top = `${event.clientY}px`;
  menu.style.display = 'block';
}
document.addEventListener('click', () => {
  menu.style.display = 'none';
  radixMenu.style.display = 'none';
});
menu.addEventListener('click', (event) => {
  const action = event.target.dataset.action;
  if (!action) return;
  const node = findNode(menu.dataset.node);
  if (action === 'hide') hideSelection(node);
  if (action === 'restoreGroup' && node?.type === 'group') restoreGroup(node);
  if (action === 'collapse' && node?.type === 'group') { collapsed.has(node.key) ? collapsed.delete(node.key) : collapsed.add(node.key); }
  if (action === 'resetName') resetNodeName(node);
  menu.style.display = 'none';
  renderAll();
});
nameMenu.addEventListener('click', (event) => {
  event.stopPropagation();
  const index = Number(event.target.dataset.nameIndex);
  if (!Number.isInteger(index)) return;
  const node = findNode(menu.dataset.node);
  applyNameOption(node, index);
  menu.style.display = 'none';
  renderAll();
});
function findNode(key, node = root) {
  if (node.key === key) return node;
  for (const child of node.children || []) { const found = findNode(key, child); if (found) return found; }
  return null;
}
function hideSelection(fallback) {
  const keys = selected.size ? [...selected] : (fallback ? [fallback.key] : []);
  for (const key of keys) {
    const node = findNode(key);
    if (!node) continue;
    if (node.type === 'signal') hidden.add(node.key);
    else collectSignals(node).forEach(sig => hidden.add(sig.key));
  }
  selected.clear();
  selectedValues.clear();
}
function collectSignals(node, out = []) {
  for (const child of node.children || []) {
    if (child.type === 'signal') out.push(child);
    else collectSignals(child, out);
  }
  return out;
}
function restoreGroup(group) { collectSignals(group).forEach(sig => hidden.delete(sig.key)); }
document.getElementById('hideSelected').addEventListener('click', () => { hideSelection(null); renderAll(); });
document.getElementById('showAll').addEventListener('click', () => { hidden.clear(); renderAll(); });
function onDragOver(event, node, row) {
  if (!dragKey || dragKey === node.key) return;
  const dragNode = findNode(dragKey);
  if (!canMove(dragNode, node)) return;
  event.preventDefault();
  const rect = row.getBoundingClientRect();
  dropAfter = event.clientY > rect.top + rect.height / 2;
  row.classList.toggle('drop-before', !dropAfter);
  row.classList.toggle('drop-after', dropAfter);
  dropTarget = node.key;
}
function canMove(dragNode, targetNode) {
  if (!dragNode || !targetNode || dragNode === targetNode) return false;
  if (dragNode.parent !== targetNode.parent) return false;
  if (dragNode.type === 'group' && isDescendantGroup(targetNode, dragNode.key)) return false;
  return true;
}
function onDrop(event, targetNode) {
  event.preventDefault();
  const dragNode = findNode(dragKey);
  if (!canMove(dragNode, targetNode)) return;
  const siblings = dragNode.parent.children;
  const from = siblings.indexOf(dragNode);
  siblings.splice(from, 1);
  let to = siblings.indexOf(targetNode);
  if (dropAfter) to += 1;
  siblings.splice(to, 0, dragNode);
  dragKey = null; dropTarget = null;
  renderAll();
}
function sizeCanvas(canvas, w, h) {
  const ratio = window.devicePixelRatio || 1;
  canvas.style.width = `${w}px`; canvas.style.height = `${h}px`;
  canvas.width = Math.max(1, Math.floor(w * ratio)); canvas.height = Math.max(1, Math.floor(h * ratio));
  const ctx = canvas.getContext('2d'); ctx.setTransform(ratio, 0, 0, ratio, 0, 0); return ctx;
}
function drawBit(ctx, y, start, end, value) {
  const high = y + 5, low = y + lane - 6, yy = value === '1' ? high : low;
  ctx.strokeStyle = '#6bd17a'; ctx.beginPath(); ctx.moveTo(start, yy); ctx.lineTo(end, yy); ctx.stroke();
}
function drawBus(ctx, y, start, end, value, invalid) {
  ctx.strokeStyle = invalid ? '#6b7280' : '#9eb3c7'; ctx.fillStyle = invalid ? '#6b7280' : '#9eb3c7';
  const top = y + 5, bottom = y + lane - 5; ctx.strokeRect(start + 1, top, Math.max(2, end - start - 2), bottom - top);
  if (end - start > 24) ctx.fillText(value || ' ', start + 5, y + 15);
}
function drawSignal(ctx, sig, rowIndex, selectedRow = false) {
  const y = rowIndex * lane;
  ctx.fillStyle = selectedRow ? '#15283a' : (rowIndex % 2 ? '#0e1114' : '#090b0e'); ctx.fillRect(0, y, waveContentWidth(), lane);
  ctx.strokeStyle = '#242a32'; ctx.beginPath(); ctx.moveTo(0, y + lane - .5); ctx.lineTo(frames.length * px, y + lane - .5); ctx.stroke();
  let segmentStart = 0; let previous = rawValueAt(sig, 0);
  for (let i = 1; i <= frames.length; i++) {
    const current = i < frames.length ? rawValueAt(sig, i) : undefined;
    if (current !== previous) {
      const x0 = segmentStart * px, x1 = i * px;
      const radix = radixFor(sig);
      const formatted = radix === 'auto' ? previous : formatInt(previous, radix);
      if (drawsAsBit(sig, previous)) drawBit(ctx, y, x0, x1, previous); else drawBus(ctx, y, x0, x1, formatted, previous === 'z' || previous === '');
      if (i < frames.length) { ctx.strokeStyle = '#4b5563'; ctx.beginPath(); ctx.moveTo(i * px, y + 4); ctx.lineTo(i * px, y + lane - 4); ctx.stroke(); }
      segmentStart = i; previous = current;
    }
  }
}
function drawWaveRows(canvas, rows, viewportHeight) {
  const width = waveContentWidth();
  const height = Math.max(viewportHeight, rows.length * lane + 2);
  const ctx = sizeCanvas(canvas, width, height);
  ctx.fillStyle = '#090b0e'; ctx.fillRect(0, 0, width, height); ctx.font = '12px ui-monospace, monospace';
  rows.forEach((node, row) => {
    if (node.type === 'signal') drawSignal(ctx, node.signal, row, selected.has(node.key));
    else {
      const y = row * lane;
      ctx.fillStyle = selected.has(node.key) ? '#26384b' : '#11161b';
      ctx.fillRect(0, y, width, lane);
      ctx.strokeStyle = '#242a32';
      ctx.beginPath();
      ctx.moveTo(0, y + lane - .5);
      ctx.lineTo(width, y + lane - .5);
      ctx.stroke();
    }
  });
}
function renderWave() {
  const rows = visibleRows();
  const cycles = cycleRows();
  const cycleHeight = cycles.length * lane;
  const waveHeight = waveContentHeight(rows);
  cycleList.style.height = `${cycleHeight}px`;
  cycleWave.style.height = `${cycleHeight}px`;
  drawWaveRows(cycleCanvas, cycles, cycleHeight);
  drawWaveRows(wave, rows, waveHeight);
  updateCycleWaveScroll();
  updateCursorLine(); updateStatus();
}
function updateCursorLine() {
  const rows = visibleRows();
  waveCursorLine.style.height = `${waveContentHeight(rows)}px`;
  cycleCursorLine.style.height = `${Math.max(cycleWave.clientHeight, cycleRows().length * lane)}px`;
  cycleCursorLine.style.transform = `translateX(${cursorX - scroll.scrollLeft}px)`;
  waveCursorLine.style.transform = `translateX(${cursorX}px)`;
}
function updateCycleWaveScroll() {
  cycleCanvas.style.transform = `translateX(${-scroll.scrollLeft}px)`;
}
function updateStatus() {
  const frameIndex = currentFrameIndex();
  const frame = frames[frameIndex] || {};
  const rootTiming = frame.root_timing || TRACE.root_timing || {};
  const freq = formatHz(rootTiming.frequency_hz);
  const cycle = rootTiming.cycle ?? frame.cycle ?? '';
  statusEl.textContent = `光标=${formatNumber(cursorFrameFloat(), 4)} 最近帧=${frameIndex} 序号=${frame.sequence ?? ''} 阶段=${frame.stage ?? ''} 顶层周期=${cycle} ${freq ? `顶层频率=${freq}` : ''} 时间=${frame.time_seconds ?? ''} 已选=${selected.size} 值选择=${selectedValues.size}`;
}
function renderAll() { renderList(); scheduleWaveRender(); }
function clampFrameIndex(index) {
  return Math.max(0, Math.min(frames.length - 1, index));
}
function maxCursorX() {
  return Math.max(0, (frames.length - 1) * px);
}
function clampCursorX(x) {
  return Math.max(0, Math.min(maxCursorX(), x));
}
function cursorFrameFloat() {
  return px ? cursorX / px : 0;
}
function currentFrameIndex() {
  return clampFrameIndex(Math.round(cursorFrameFloat()));
}
function wavePointFromClient(event) {
  const rect = scroll.getBoundingClientRect();
  return {
    x: event.clientX - rect.left + scroll.scrollLeft,
    y: event.clientY - rect.top + scroll.scrollTop,
  };
}
function cyclePointFromClient(event) {
  const rect = cycleWave.getBoundingClientRect();
  return {
    x: event.clientX - rect.left + scroll.scrollLeft,
    y: event.clientY - rect.top,
  };
}
function setCursorX(x) {
  cursorX = clampCursorX(x);
  renderAll();
}
function setCursorFrame(index) {
  setCursorX(clampFrameIndex(index) * px);
}
function nodeFromWaveClientY(clientY) {
  const rect = scroll.getBoundingClientRect();
  const rowIndex = Math.floor((clientY - rect.top + scroll.scrollTop) / lane);
  return visibleRows()[rowIndex] || null;
}
function revealCursor() {
  const left = scroll.scrollLeft;
  const right = left + scroll.clientWidth;
  if (cursorX < left + 16) scroll.scrollLeft = Math.max(0, cursorX - 16);
  else if (cursorX > right - 16) scroll.scrollLeft = Math.max(0, cursorX - scroll.clientWidth + 16);
}
function startCursorDrag(pointFromEvent, nodeFromEvent = null, snapOnClick = false) {
  return event => {
    if (event.button !== 0) return;
    event.preventDefault();
    const selectInitialNode = nodeFromEvent?.(event);
    if (selectInitialNode) selectNode(event, selectInitialNode);
    const startPoint = pointFromEvent(event);
    const startNode = selectInitialNode;
    let dragged = false;
    const moveCursor = moveEvent => {
      const point = pointFromEvent(moveEvent);
      if (Math.abs(point.x - startPoint.x) > 3 || Math.abs(point.y - startPoint.y) > 3) {
        dragged = true;
      }
      setCursorX(point.x);
    };
    const finish = () => {
      if (!dragged && snapOnClick && startNode?.type === 'signal') {
        setCursorX(snapXForSignal(startNode.signal, startPoint.x));
      }
      document.removeEventListener('mousemove', moveCursor);
      document.removeEventListener('mouseup', finish);
    };
    moveCursor(event);
    document.addEventListener('mousemove', moveCursor);
    document.addEventListener('mouseup', finish);
  };
}
function cycleNodeFromClientY(clientY) {
  const point = {y: clientY - cycleWave.getBoundingClientRect().top};
  return cycleRows()[Math.floor(point.y / lane)] || null;
}
function moveToChange(direction) {
  const changes = changeIndicesForNodes(selectedChangeNodes());
  if (!changes.length) return;
  const current = cursorFrameFloat();
  const epsilon = 1e-6;
  const target = direction > 0
    ? changes.find(index => index > current + epsilon)
    : [...changes].reverse().find(index => index < current - epsilon);
  if (target === undefined) return;
  setCursorFrame(target);
  revealCursor();
}
wave.addEventListener('mousedown', startCursorDrag(wavePointFromClient, event => nodeFromWaveClientY(event.clientY), true));
cycleCanvas.addEventListener('mousedown', startCursorDrag(cyclePointFromClient, event => cycleNodeFromClientY(event.clientY), true));
scroll.addEventListener('scroll', () => {
  updateCycleWaveScroll();
  updateCursorLine();
  if (Math.abs(signalList.scrollTop - scroll.scrollTop) > 1) signalList.scrollTop = scroll.scrollTop;
});
signalList.addEventListener('scroll', () => {
  if (Math.abs(scroll.scrollTop - signalList.scrollTop) > 1) scroll.scrollTop = signalList.scrollTop;
});
signalList.addEventListener('wheel', event => {
  scroll.scrollTop += event.deltaY;
  if (event.shiftKey) scroll.scrollLeft += event.deltaY;
  event.preventDefault();
}, {passive: false});
cycleList.addEventListener('wheel', event => {
  if (event.shiftKey) scroll.scrollLeft += event.deltaY;
  else scroll.scrollTop += event.deltaY;
  event.preventDefault();
}, {passive: false});
scroll.addEventListener('wheel', event => {
  if (event.shiftKey) {
    scroll.scrollLeft += event.deltaY;
    event.preventDefault();
  }
}, {passive: false});
cycleWave.addEventListener('wheel', event => {
  if (event.shiftKey) scroll.scrollLeft += event.deltaY;
  else scroll.scrollTop += event.deltaY;
  event.preventDefault();
}, {passive: false});
document.getElementById('zoomIn').addEventListener('click', () => { px = Math.min(240, Math.round(px * 1.35)); scheduleWaveRender(); });
document.getElementById('zoomOut').addEventListener('click', () => { px = Math.max(8, Math.round(px / 1.35)); scheduleWaveRender(); });
document.getElementById('fit').addEventListener('click', () => { px = Math.max(8, Math.floor((scroll.clientWidth - 20) / Math.max(1, frames.length))); scheduleWaveRender(); });
toggleLeftPane.addEventListener('click', () => setLeftPaneCollapsed(!leftPaneCollapsed));
leftPaneExpander.addEventListener('click', () => setLeftPaneCollapsed(false));
document.getElementById('prevChange').addEventListener('click', () => moveToChange(-1));
document.getElementById('nextChange').addEventListener('click', () => moveToChange(1));
document.getElementById('search').addEventListener('input', renderAll);
function startDragResize(handler) { return event => { event.preventDefault(); const move = e => handler(e); const up = () => { document.removeEventListener('mousemove', move); document.removeEventListener('mouseup', up); }; document.addEventListener('mousemove', move); document.addEventListener('mouseup', up); }; }
document.getElementById('panelSplitter').addEventListener('mousedown', startDragResize(e => {
  if (leftPaneCollapsed) return;
  updateContentColumns(e.clientX - content.getBoundingClientRect().left);
}));
document.getElementById('valueSplitter').addEventListener('mousedown', startDragResize(e => {
  if (leftPaneCollapsed) return;
  const left = leftPane.getBoundingClientRect().left;
  const available = panelWidth || leftPane.clientWidth;
  const compact = available < 220;
  const minName = compact ? 48 : 80;
  const minValue = compact ? 54 : 80;
  nameWidth = clamp(e.clientX - left, minName, Math.max(minName, available - minValue - 7));
  valueWidth = Math.max(minValue, available - nameWidth - 7);
  renderAll();
}));
if ('ResizeObserver' in window) {
  new ResizeObserver(scheduleAutoLayout).observe(content);
  new ResizeObserver(scheduleWaveRender).observe(scroll);
} else {
  window.addEventListener('resize', scheduleAutoLayout);
}
window.addEventListener('resize', scheduleAutoLayout);
const rootTiming = TRACE.root_timing || {};
const rootHz = formatHz(rootTiming.frequency_hz);
const rootLabel = `${kindLabel(rootTiming.kind || TRACE.manifest.object_kind)}:${rootTiming.name || TRACE.manifest.object_name || ''}`;
summary.textContent = `${rootHz ? `顶层=${rootHz}，` : ''}${rawSignals.length} 个信号，${frames.length} 帧，${rootLabel}`;
autoLayout();
renderAll();
</script>
</body>
</html>
"""


def generate(segment_dir: Path, output: Path) -> None:
    manifest_path = segment_dir / "manifest.json"
    with manifest_path.open("r", encoding="utf-8") as source:
        manifest = json.load(source)

    waveform_path = segment_dir / manifest.get("waveform", "waveform.jsonl")
    frames = load_jsonl(waveform_path)
    trace = normalize_trace(manifest, frames)
    html = HTML_TEMPLATE.replace(
        "__TRACE_JSON__",
        json.dumps(trace, ensure_ascii=False, separators=(",", ":")),
    )
    output.write_text(html, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("segment_dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    segment_dir = args.segment_dir
    output = args.output or segment_dir / "waveform.html"
    generate(segment_dir, output)
    if not args.quiet:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
