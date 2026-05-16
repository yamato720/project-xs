# Project-XPlus Build Hardware Report

这个 example 从 `Project-XS` 直接读取 `Project-XPlus/build/hw`，用已有硬件 build 产物生成 report。
它不会复制 `.xo`、`.xclbin`、HLS 报告、schedule rpt 或 routed timing rpt。
spec 里不需要显式列出 kernel 名；`parse_xo.py` 会扫描 build 目录顶层 `.xo`，再从 XO 内部的 `kernel.xml` 读取真实 kernel 名。

默认引用：

```text
../../../Project-XPlus/cfg/connectivity_u55c.cfg
../../../Project-XPlus/build/hw
```

从 `Project-XS` 目录执行：

```bash
make render-xplus-build-report
```

等价于：

```bash
python3 script/parse_xo.py example/project_xplus_build_report
```

输出：

```text
example/project_xplus_build_report/reports/xplus_hw_build_report.json
example/project_xplus_build_report/reports/xplus_hw_build_report.html
```

## 可选字段

当前最小 spec 只写 `cfg`、`artifacts.build_dir` 和 `output`。如果只想从 XPlus 已有 build 目录生成硬件 report，这已经够用。
下面这些字段也被 `script/parse_xo.py` 支持，可以按需要逐步加。

### 顶层字段

- `title`
  报告 HTML 标题。
- `subtitle`
  报告页顶部副标题，用来说明这个 report 的来源或用途。
- `source_note`
  报告页顶部的补充说明，适合记录“这个 report 来自哪个 build / 不复制产物 / kernel 自动发现”等信息。
- `cfg`
  connectivity cfg 路径。用于解析 `nk=` kernel 实例、`sp=` HBM 绑定和 `stream_connect=` 流连接。
  不写时脚本会在 example 目录里找 `connectivity.cfg`、`<example>.cfg` 或唯一的 `*.cfg`。
- `output.json`
  生成的 JSON report 路径，默认是 `reports/xo_report.json`。
- `output.html`
  生成的 HTML report 路径，默认是 `reports/xo_report.html`。
- `sequence`
  手工指定报告里的 kernel 执行顺序。也会影响 kernel 卡片排序。
- `sequence_descriptions`
  给 `sequence` 里的每个 kernel 补一句阶段说明。
- `dependencies`
  手工描述跨 kernel 或 kernel-host 的依赖关系，显示在报告的“阶段依赖”表里。
- `host_nodes`
  手工描述 host 侧控制节点，例如 alpha/beta 计算、收敛判断、启动顺序等。XO 和 CFG 不会自动表达这些 host orchestration。

### `artifacts` 字段

- `build_dir`
  真实 Vitis build 目录。给了它以后，脚本会自动发现顶层 `*.xo`、`*.xclbin`、`_x_temp` 下的 csynth、schedule、routed timing 和 resource report。
- `auto_discover`
  是否启用旧 example 布局的 fallback 自动发现。为 `true` 时，脚本会尝试从 example 目录下的 `vivado-log/` 找 `csynth`、`sched` 和 `link`。
  直接指向 XPlus build 时通常设为 `false`，避免混用 example 自带产物。
- `xclbin`
  显式指定 `.xclbin`。不写时，如果 `build_dir` 下有顶层 `.xclbin`，脚本会自动取第一个。
- `csynth_dir`
  显式指定存放 `*_csynth.xml` 的目录。适合不是标准 Vitis build 目录的手工收集报告。
- `schedule_dir`
  显式指定存放 `*.verbose.sched.rpt` 的目录。适合 schedule 报告不在 `_x_temp/<kernel>/.../.autopilot/db/` 下时使用。
- `link_dir`
  旧布局字段，用来在目录里找 routed timing summary。
- `link_timing_report`
  显式指定 routed timing summary。优先级高于从 `build_dir` 自动发现。
- `resource_report`
  显式指定 `kernel_util_routed.json` / `kernel_util_placed.json` / `kernel_util_synthed.json`。不写时会从 `build_dir` 自动发现；如果没有实现阶段资源报告，则回退到 HLS `csynth.xml` 资源估计。
- `power_report`
  显式指定 Vivado/Vitis routed power report，例如 `impl_1_hw_bb_locked_power_routed.rpt` 或 XPlus `make vivado-power-report` 生成的 `power.rpt`。
  不写时会从 `build_dir` 自动发现，并解析总片上功耗、FPGA/HBM 功耗、动态/静态功耗、结温和 confidence level。
  报告里功率项单位是 W；`junction_temp` / `max_ambient` 是 C，`effective_tja` 是 C/W，`confidence` 是 Vivado 对活动数据完整性的评级。
- `vivado_log_dir`
  旧布局字段。设置后可以从它下面的 `csynth/`、`sched/`、`link/` 推导对应报告目录。

### `kernels` 字段

`kernels` 不写时，脚本会扫描 `artifacts.build_dir` 顶层的 `*.xo`，并从 XO 的 `kernel.xml` 读取 kernel 名。
需要筛选、排序、覆盖路径或补充人工说明时，再加 `kernels`。

每个 kernel entry 支持：

- `name`
  kernel 名。配合 `build_dir` 使用时，脚本会找 `<name>.xo`，或读取 XO 内部 metadata 匹配该名字。
- `xo`
  显式指定 `.xo` 路径。适合 XO 不在 `build_dir` 顶层时使用。
- `csynth`
  显式指定该 kernel 的 `*_csynth.xml`。
- `schedule_top`
  显式指定该 kernel 顶层 `*.verbose.sched.rpt`。
- `schedule_children`
  显式指定该 kernel 的子 pipeline schedule rpt，可以是字符串或字符串数组。
- `schedule_dir`
  显式指定该 kernel 的 schedule rpt 目录。
- `role`
  人工写的 kernel 角色定位，显示在 kernel 卡片里。
- `summary`
  人工写的功能摘要数组。适合补充 XO 看不到的算法语义。
- `reads`
  人工写的逻辑读集合。
- `writes`
  人工写的逻辑写集合。
- `hbm_mapping`
  预留的人工作图字段，当前脚本会放进 JSON，但 HTML 主要使用 CFG 里的 `sp=` 绑定展示 HBM。
- `xs_mapping`
  描述这个 kernel 在 XS 抽象里的建议映射。HTML 读取 `kernel`、`memory`、`ports`。
- `xs_sim_mapping`
  描述贴合 XS 模拟器的建议映射。HTML 读取 `kernel_class`、`control_if`、`data_if`、`state_ports`。
- `components`
  手工拆出的内部 component 草图。每项常用 `name`、`role`、`reads`、`writes`、`xs_mapping`、`sim_ports`。
- `component_edges`
  手工写 component 之间的依赖边。每项常用 `upstream`、`downstream`、`description`。

### 示例片段

如果只是想固定显示顺序并给关键 kernel 加说明，可以这样扩展：

```json
{
  "sequence": [
    "spmv_csr_kernel",
    "init_pcg_kernel",
    "dot_kernel",
    "update_xrz_kernel",
    "update_p_kernel"
  ],
  "sequence_descriptions": {
    "spmv_csr_kernel": "CSR SpMV",
    "dot_kernel": "pAp reduction"
  },
  "kernels": [
    {
      "name": "spmv_csr_kernel",
      "role": "稀疏矩阵向量乘",
      "summary": [
        "从 CSR row_ptr / col_idx / values 读取矩阵。",
        "读取输入向量 x，写回 y。"
      ],
      "reads": ["row_ptr", "col_idx", "values", "x"],
      "writes": ["y"],
      "xs_mapping": {
        "kernel": "Kernel",
        "memory": ["CSR matrix", "vector buffer"],
        "ports": ["AXI read row_ptr/col_idx/values/x", "AXI write y"]
      }
    }
  ]
}
```

更完整的写法可以参考：

```text
example/project_xplus_hls/project_xplus_hls_spec.json
example/hls_pipeline_demo/spec.json
script/parse_xo.py
```
