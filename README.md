# Project-XS

`Project-XS` 现在已经不是最初那个只放辅助脚本和 golden 的小仓库了。它目前承担的是一个更偏“实验支撑层”的角色，主要覆盖三类内容：

1. `Jacobi-PCG` 的 CPU golden 参考实现
2. 面向 HLS/kernel 拆分验证的周期级仿真基础设施
3. 从 `Project-XPlus` 侧抽取出来的 HLS kernel 示例与可视化素材

默认开发分支是 `main`。

## 当前仓库结构

- `main.cpp`
  `CG solver` 的本地参考入口。读取数据集后运行 `Jacobi-PCG golden`，输出收敛信息与残差。
- `src/golden/CgSolverGolden.hpp`
  `Jacobi-PCG` 求解流程、收敛判定和残差计算。
- `src/golden/CsrDataset.hpp`
  CSR 数据集加载、合法性校验、`SpMV` 和 `Jacobi` 对角提取。
- `src/base/` 与 `src/include/base/`
  周期级仿真基础设施，包括 `CycleSimulator`、`Kernel`、`KernelComponent`、`Port`、`PortGroup` 和 `Axi` 抽象。
- `src/golden/`
  当前仍保留的 `CG solver golden` 相关参考实现与数据集加载逻辑。
- `test/abctest/`
  一个最小周期仿真 demo，用来验证 wire/reg 端口时序行为与组件编排方式。
- `script/generate_cg_dataset.py`
  生成 `CG` 验证数据集，同时输出矩阵可视化文件。
- `script/parse_xo.py`
  读取 example 目录下的 spec 和 `xo`，自动生成静态报告页面。
- `example/project_xplus_hls/`
  从 `Project-XPlus` 抽取的 5 个 HLS kernel 示例、说明文件、`xo` 样本和预生成报告。
- `docs/`
  记录 HLS kernel 拆分、周期仿真器设计等过程文档。
- `data/generated/`
  默认数据生成目录，不纳入版本控制。

## 这个仓库现在能做什么

### 1. 运行 CG golden

默认命令：

```bash
make
```

这会：

1. 生成默认数据集 `data/generated/cgsolver/n512`
2. 编译 `build/cgsolver_golden`
3. 运行 `Jacobi-PCG golden`

如需指定规模或求解参数：

```bash
make run SIZE=4096 TAU=1e-10 MAX_ITERS=20000
```

如果只想生成数据：

```bash
make generate SIZE=8192
```

也可以直接运行脚本：

```bash
python3 script/generate_cg_dataset.py \
  --size 8192 \
  --aspect-ratio 1.6 \
  --output-dir data/generated/cgsolver/n8192
```

### 2. 运行周期仿真 demo

```bash
make test abctest
```

或：

```bash
make run-cycle-sim
```

这个 demo 目前不是求解器仿真，而是一个最小三组件例子，用来验证：

1. kernel/component 调度关系
2. wire 输出和寄存器输出的周期差异
3. `CycleSimulator` 的 reset、初始化和逐周期推进机制

### 3. 生成 Project-XPlus HLS 示例报告

```bash
make render-xplus-hls-example
```

或：

```bash
python3 script/parse_xo.py
```

默认会更新：

```text
example/project_xplus_hls/reports/xo_report.json
example/project_xplus_hls/reports/xo_report.html
```

这个报告站在 `XS` 的角度，把示例 kernel 的职责、顶层接口、参数端口、HBM 映射和组件关系整理成静态页面，方便做结构核对。
其中 `.xo` 提供 kernel/arg/port 元数据，`.cfg` 额外提供 kernel 实例名和 `sp -> HBM` 绑定信息；spec 中这些路径默认都按 example 目录解析，也兼容绝对路径。

## CG 数据格式

当前 `CG` 数据集保持为拆分的 CSR 形式，最小输入包括：

- `row_ptr.txt`
- `col_idx.txt`
- `values.txt`
- `b.txt`
- `x0.txt`

其中：

1. `A` 由 `row_ptr / col_idx / values` 三个文件共同表示
2. `b.txt` 是右端项向量
3. `x0.txt` 是默认初始解向量

`Jacobi` 预条件器 `M` 不单独落盘，而是在 C++ 参考实现里由 `diag(A)` 直接构造。

数据生成脚本现在还会额外输出：

- `matrix.svg`
- `matrix.html`

用于快速查看稀疏矩阵的分布形态。

## 数据生成模型

生成器构造的是一个更接近工程场景的稀疏对称正定系统：

1. 基础拓扑是二维近矩形网格
2. 横向和纵向耦合系数带有确定性扰动
3. 局部加入少量长程 contact link，模拟不规则耦合
4. 对角线上叠加正质量项，保证系统保持 `SPD`

## main.cpp 当前行为

`main.cpp` 当前会：

1. 从数据目录读取 CSR 矩阵 `A`、向量 `b` 和初值 `x0`
2. 在内存中构造 `Jacobi` 对角预条件器
3. 运行 `Jacobi-PCG golden`
4. 输出 `n / nnz / iterations / final_rr / residual_l2`

当前不会把 `golden solution` 额外写回文件，它主要作为主机侧参考实现存在，方便后续和别的实现做结果比对。

## 当前定位

## 版本记录

当前仓库根目录新增了一个 `VERSION` 文件，用来记录：

- 上一条基线分支的已定版本
- 当前开发线目标版本
- 该记录开始生效的时间

当前记录为：

- `release_base=1.0.0`
- `development_target=1.1.0`

如果一句话概括现在的 `Project-XS`，更准确的说法是：

`Project-XS` 是 `Project-X / Project-XPlus` 周边的算法参考、仿真验证和 HLS 结构观察仓库，而不再只是一个“稀疏线代辅助脚本仓库”。
