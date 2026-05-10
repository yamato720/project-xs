#!/usr/bin/env python3

from __future__ import annotations

import html
import json
import sys
import xml.etree.ElementTree as et
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SPEC = ROOT / "example" / "project_xplus_hls" / "project_xplus_hls_spec.json"


def read_spec(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_path(base: Path, raw: str) -> Path:
    path = Path(raw)
    if path.is_absolute():
        return path
    return (base / path).resolve()


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


def build_report(spec: dict, spec_path: Path) -> dict:
    base_dir = spec_path.parent

    kernels = []
    for kernel_spec in spec["kernels"]:
        xo_path = resolve_path(base_dir, kernel_spec["xo"])
        xo_info = parse_xo(xo_path)
        kernels.append(
            {
                "name": kernel_spec["name"],
                "xo": xo_info,
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
    kernels.sort(key=lambda item: (order.get(item["name"], 1 << 30), item["name"]))

    return {
        "title": spec.get("title", "HLS Example"),
        "subtitle": spec.get("subtitle", ""),
        "spec_path": str(spec_path),
        "source_note": spec.get("source_note", ""),
        "sequence": spec.get("sequence", []),
        "sequence_descriptions": spec.get("sequence_descriptions", {}),
        "dependencies": spec.get("dependencies", []),
        "host_nodes": spec.get("host_nodes", []),
        "kernels": kernels,
    }


def render_html_page(data: dict) -> str:
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

    dependency_rows = []
    for dep in data["dependencies"]:
        dependency_rows.append(
            "<tr>"
            f"<td>{html.escape(dep['upstream'])}</td>"
            f"<td>{html.escape(dep['downstream'])}</td>"
            f"<td>{html.escape(dep['description'])}</td>"
            "</tr>"
        )

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

    cards = []
    for kernel in data["kernels"]:
        ann = kernel["annotations"]
        xo = kernel["xo"]

        summary_html = "".join(f"<li>{html.escape(item)}</li>" for item in ann.get("summary", []))
        reads_html = "".join(f"<span class='pill alt'>{html.escape(item)}</span>" for item in ann.get("reads", []))
        writes_html = "".join(f"<span class='pill warn'>{html.escape(item)}</span>" for item in ann.get("writes", []))

        xo_args_rows = []
        hbm_map = {item["arg"]: item["target"] for item in ann.get("hbm_mapping", [])}
        for arg in xo["args"]:
            xo_args_rows.append(
                "<tr>"
                f"<td>{html.escape(arg['name'])}</td>"
                f"<td>{html.escape(arg['type'])}</td>"
                f"<td>{html.escape(arg['port'])}</td>"
                f"<td>{html.escape(arg['address_qualifier'])}</td>"
                f"<td>{html.escape(arg['offset'])}</td>"
                f"<td>{html.escape(hbm_map.get(arg['name'], '-'))}</td>"
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

        xs_mapping = ann.get("xs_mapping", {})
        xs_sim_mapping = ann.get("xs_sim_mapping", {})

        cards.append(
            f"""
            <div class="col-12 col-xl-6">
              <div class="report-card card h-100"><div class="card-body">
                <div class="section-title">{html.escape(kernel['name'])}</div>
                <div class="section-note">XO: {html.escape(xo['xo_path'])}</div>
                <div class="sub-title">JSON 注释：角色定位</div>
                <div class="muted">{html.escape(ann.get('role', ''))}</div>
                <div class="sub-title">JSON 注释：功能摘要</div>
                <ul>{summary_html}</ul>
                <div class="sub-title">JSON 注释：逻辑读集合</div>
                <div class="pill-wrap">{reads_html or '<span class="muted">无</span>'}</div>
                <div class="sub-title">JSON 注释：逻辑写集合</div>
                <div class="pill-wrap">{writes_html or '<span class="muted">无</span>'}</div>

                <div class="sub-title">XO 自动解析：Kernel 元数据</div>
                <div class="kv-grid">
                  <div class="kv-row"><div class="kv-key">kernel_name</div><div class="kv-value">{html.escape(xo['kernel_name'])}</div></div>
                  <div class="kv-row"><div class="kv-key">language</div><div class="kv-value">{html.escape(xo['language'])}</div></div>
                  <div class="kv-row"><div class="kv-key">vlnv</div><div class="kv-value">{html.escape(xo['vlnv'])}</div></div>
                  <div class="kv-row"><div class="kv-key">control_protocol</div><div class="kv-value">{html.escape(xo['hw_control_protocol'])}</div></div>
                </div>

                <div class="sub-title">XO 自动解析：Arguments</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>arg</th><th>type</th><th>port</th><th>aq</th><th>offset</th><th>HBM(JSON)</th></tr></thead>
                    <tbody>{''.join(xo_args_rows)}</tbody>
                  </table>
                </div>

                <div class="sub-title">XO 自动解析：Ports</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>port</th><th>mode</th><th>width</th><th>type</th><th>range</th></tr></thead>
                    <tbody>{''.join(xo_ports_rows)}</tbody>
                  </table>
                </div>

                <div class="sub-title">JSON 注释：映射到 XS 抽象</div>
                <div class="muted">建议抽象层级：{html.escape(xs_mapping.get('kernel', ''))}</div>
                <div class="muted">建议 memory 关注点：{html.escape(', '.join(xs_mapping.get('memory', [])) if xs_mapping.get('memory') else '无')}</div>
                <ul>{''.join(f'<li>{html.escape(item)}</li>' for item in xs_mapping.get('ports', []))}</ul>

                <div class="sub-title">JSON 注释：贴合 XS 模拟器的映射</div>
                <div class="muted">建议 Kernel 类：{html.escape(xs_sim_mapping.get('kernel_class', ''))}</div>
                <div class="muted">建议 Control 接口：{html.escape(xs_sim_mapping.get('control_if', ''))}</div>
                <div class="sub-title">建议 Data Interface</div>
                <ul>{''.join(f'<li>{html.escape(item)}</li>' for item in xs_sim_mapping.get('data_if', [])) or '<li>无</li>'}</ul>
                <div class="sub-title">建议 State Ports</div>
                <ul>{''.join(f'<li>{html.escape(item)}</li>' for item in xs_sim_mapping.get('state_ports', [])) or '<li>无</li>'}</ul>

                <div class="sub-title">JSON 注释：内部 Component 草图</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>component</th><th>role</th><th>reads</th><th>writes</th><th>XS mapping</th><th>Sim ports</th></tr></thead>
                    <tbody>{''.join(component_rows) or '<tr><td colspan="6">无</td></tr>'}</tbody>
                  </table>
                </div>

                <div class="sub-title">JSON 注释：内部 Component 依赖</div>
                <div class="table-wrap">
                  <table class="table table-sm align-middle">
                    <thead><tr><th>upstream</th><th>downstream</th><th>说明</th></tr></thead>
                    <tbody>{''.join(component_edge_rows) or '<tr><td colspan="3">无</td></tr>'}</tbody>
                  </table>
                </div>
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
    }}
    .table-wrap table {{
      min-width: 100%;
      white-space: nowrap;
    }}
  </style>
</head>
<body>
  <div class="page">
    <div class="mb-4">
      <div class="report-title">{html.escape(data['title'])}</div>
      <div class="report-subtitle">{html.escape(data['subtitle'])}</div>
      <div class="report-subtitle">spec: {html.escape(data['spec_path'])}</div>
      <div class="report-subtitle">{html.escape(data['source_note'])}</div>
    </div>

    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">执行顺序</div>
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

    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Host 控制节点</div>
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

    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">阶段依赖</div>
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

    <div class="row g-4">
      {''.join(cards)}
    </div>
  </div>
</body>
</html>
"""


def main() -> int:
    spec_path = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_SPEC
    spec = read_spec(spec_path)
    report = build_report(spec, spec_path)

    output = spec.get("output", {})
    json_out = resolve_path(spec_path.parent, output.get("json", "reports/project_xplus_hls_example.json"))
    html_out = resolve_path(spec_path.parent, output.get("html", "reports/project_xplus_hls_example.html"))
    json_out.parent.mkdir(parents=True, exist_ok=True)
    html_out.parent.mkdir(parents=True, exist_ok=True)

    json_out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    html_out.write_text(render_html_page(report), encoding="utf-8")
    print(f"json: {json_out}")
    print(f"html: {html_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
