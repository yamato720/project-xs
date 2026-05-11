# Project-XPlus HLS Example

这份 example 用来把 `Project-XPlus` 里的 5 个 HLS kernel 作为示例引入到 `Project-XS`，
并生成一个静态 HTML 可视化页面，帮助从 `XS` 视角观察：

1. kernel 的职责
2. 顶层接口
3. memory bundle 分配
4. loop label 与 pipeline 结构
5. HLS 综合 latency / resource / estimated clock
6. bitstream 级 routed timing summary

## 文件

```text
example/project_xplus_hls/
  kernels/
    spmv_csr_kernel.cpp
    init_pcg_kernel.cpp
    dot_kernel.cpp
    update_xrz_kernel.cpp
    update_p_kernel.cpp
  xo/
    *.xo
  vivado-log/                # 可选
    csynth/                  # 可选
      *_csynth.xml
    link/                    # 可选
      *timing_summary*_routed.rpt
  project_xplus_hls_spec.json
  reports/
    xo_report.json
    xo_report.html
  README.md
```

## 生成可视化

在 `Project-XS` 根目录执行：

```bash
python3 script/parse_xo.py
```

或显式指定 example 目录：

```bash
python3 script/parse_xo.py example/project_xplus_hls
```

脚本会自动：

1. 把输入参数当成 example 目录处理，默认目录是 `example/project_xplus_hls`
2. 在该目录下自动识别 spec，优先查找 `<目录名>_spec.json`
3. 将 spec 中的 `xo` 和 `cfg` 路径按 example 目录解析；若写绝对路径也兼容
4. 自动尝试发现目录下的 `*.cfg`
5. 将报告输出到该目录下的 `reports/`
6. 若 spec 未关闭自动发现且存在 `vivado-log/`，自动补充 HLS/Vivado 时序信息；不存在则跳过

默认输出：

```text
example/project_xplus_hls/reports/xo_report.json
example/project_xplus_hls/reports/xo_report.html
```

## spec 路径规则

- `xo` 建议写成相对 example 目录的路径，比如 `xo/spmv_csr_kernel.xo`
- `cfg` 建议写成相对 example 目录的路径，比如 `connectivity_u55c.cfg`
- `output.json` 和 `output.html` 也按 example 目录解析
- `artifacts.csynth_dir` / `artifacts.link_timing_report` / `kernels[].csynth` 也按 example 目录解析
- 以上字段都兼容绝对路径

## artifacts 显式字段

推荐用 `artifacts` 显式声明报告来源：

```json
{
  "artifacts": {
    "auto_discover": false,
    "csynth_dir": "vivado-log/csynth"
    ,"schedule_dir": "vivado-log/sched"
  }
}
```

字段说明：

- `auto_discover`
  - 默认 `true`
  - 设为 `false` 后，不会再因为目录里恰好存在 `vivado-log/` 而自动补充报告
- `csynth_dir`
  - 指向放置 `<kernel>_csynth.xml` 的目录
- `schedule_dir`
  - 指向放置 `<kernel>.verbose.sched.rpt` 与 `<kernel>_Pipeline_*.verbose.sched.rpt` 的目录
- `link_timing_report`
  - 可选，指向某份 routed timing summary 的 `.rpt`
- `kernels[].csynth`
  - 可选，单 kernel 覆盖默认 `<csynth_dir>/<kernel>_csynth.xml` 规则

当前完整示例见：

- [project_xplus_hls_spec.json](/home/pyx/ProjectFS/Project-X/Project-XS/example/project_xplus_hls/project_xplus_hls_spec.json)

最小可运行示例：

```json
{
  "title": "Minimal XO Parse Example",
  "cfg": "connectivity_u55c.cfg",
  "output": {
    "json": "reports/minimal_xo_report.json",
    "html": "reports/minimal_xo_report.html"
  },
  "kernels": [
    {
      "xo": "xo/spmv_csr_kernel.xo"
    }
  ]
}
```

## cfg 会补充什么

除了 `.xo` 里的 kernel/arg/port 元数据，脚本现在还会从 `cfg` 提取：

1. `nk=` 定义的 kernel 实例名和实例数量
2. `sp=` 定义的 `instance.arg -> HBM[...]` 绑定
3. 按 kernel 聚合后的端口绑定表

这些信息会直接展示在 HTML 中。

## vivado-log 会补充什么

如果 spec 显式指定，或者未关闭自动发现且 example 目录下可选地存在：

- `vivado-log/csynth/<kernel>_csynth.xml`
- `vivado-log/link/*timing_summary*_routed.rpt`

脚本会自动补充：

1. 每个 kernel 的 target / estimated clock
2. 每个 kernel 的 overall latency、interval、resource estimate
3. 可从 `csynth.xml` 结构化读出的 loop latency / iteration latency
4. routed design 的全局 WNS / TNS / WHS / THS
5. `clk_kernel_*` 时钟域的 period / frequency / intra-clock WNS/WHS
6. top-level kernel 是否 pipeline，以及各子 pipeline / loop 的 achieved II / target II / depth

注意：

- 这些是 HLS / Vivado 静态报告，不是 host 运行时 `kernel_timing_ms`
- `spmv_csr_kernel` 这类数据相关 kernel 可能出现 `undef/?`，这是原始 HLS 报告本身就无法给出封闭上界，不是 `XS` 解析失败
- 如果没有这些文件，报告仍然可生成，只是不会显示对应 section
- 缺失时，页面会提示常见文件名与常见生成位置：
  - `csynth` 常见文件名是 `<kernel>_csynth.xml`，通常位于 HLS `solution/syn/report/`
  - `schedule` 常见文件名是 `<kernel>.verbose.sched.rpt` 与 `<kernel>_Pipeline_*.verbose.sched.rpt`，通常位于 HLS `/.autopilot/db/`
  - routed timing 常见文件名是 `impl_1_*timing_summary*_routed.rpt`，通常位于 implementation 的 `reports/link/imp/` 或类似 report 目录

## 最小化审查样例

如果你想在不复制 `xo/cfg` 文件的前提下做一个“只有 spec”的最小目录，可参考：

- [project_xplus_hls_min/spec.json](/home/pyx/ProjectFS/Project-X/Project-XS/example/project_xplus_hls_min/spec.json)

这个目录与主 example 同级，只放一个 spec：

- 显式关闭自动发现
- 用相对路径引用主 example 里的 5 个 `xo`
- 用相对路径引用主 example 里的 `cfg`
- 输出默认写到它自己目录下的 `reports/`
