# HLS Pipeline Demo Hardware Report

这个 example 使用 `Project-X` 根目录已经生成的 `hls_pipeline_demo` 真实硬件产物，
通过 spec 中的 `artifacts.build_dir` 自动发现 XO、HLS 报告和 Vivado routed timing，
生成 `Project-XS` 视角的硬件报告。

它不会复制 `.xo`、`.xclbin`、`csynth.xml`、schedule rpt 或 routed timing rpt。
spec 只显式写 `cfg` 与 build 根目录，路径细节由 `parse_xo.py` 检索。

## 输入产物

默认引用这些根目录路径：

```text
cfg/hls_pipeline_demo.cfg
build/hls_pipeline_demo/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/
  hls_pipeline_demo.xclbin
  krnl_hls_pipeline_demo.xo
  krnl_hls_pipeline_source.xo
  krnl_hls_pipeline_compute.xo
  krnl_hls_pipeline_sink.xo
  _x_temp/.../solution/syn/report/*_csynth.xml
  _x_temp/.../solution/.autopilot/db/*.verbose.sched.rpt
  _x_temp/link/vivado/vpl/prj/prj.runs/impl_1/hw_bb_locked_timing_summary_routed.rpt
```

## 生成报告

从仓库根目录执行：

```bash
python3 Project-XS/script/parse_xo.py Project-XS/example/hls_pipeline_demo
```

输出：

```text
Project-XS/example/hls_pipeline_demo/reports/hls_pipeline_demo_hw_report.json
Project-XS/example/hls_pipeline_demo/reports/hls_pipeline_demo_hw_report.html
```

也可以从 `Project-XS` 目录执行：

```bash
python3 script/parse_xo.py example/hls_pipeline_demo
```
