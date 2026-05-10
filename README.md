# Project-XS

`Project-XS` 用来放稀疏线性代数相关的辅助脚本、测试数据和 golden 参考实现。

## 目录说明

- `script/generate_cg_dataset.py`
  生成用于 CG solver 验证的稀疏 SPD 数据集。
- `src/CgSolverGolden.hpp`
  只保留 Jacobi 预条件 CG 的 golden 求解逻辑。
- `src/CsrDataset.hpp`
  独立的数据集单元，负责读取、校验、SpMV 和提取 Jacobi 对角线，便于后续复用。
- `main.cpp`
  示例入口。当前先在进程内计算 golden 解，后续你可以在这里接入其他实现并直接对比。
- `data/generated/`
  默认的数据生成目录，不纳入版本控制。

## 数据格式

当前数据集保持为拆分的 CSR 形式，并且只输出求解所需的最小输入：

- `row_ptr.txt`
- `col_idx.txt`
- `values.txt`
- `b.txt`
- `x0.txt`

其中：

- `A` 由 `row_ptr / col_idx / values` 三个文件共同表示
- `b.txt` 是右端项向量
- `x0.txt` 是默认初始解向量

`Jacobi` 预条件器 `M` 不单独落盘，而是在 C++ golden 入口里由 `diag(A)` 直接构造。

## 数据生成模型

生成器构造的是一个更接近工程场景的稀疏对称正定系统：

- 基础拓扑是二维近矩形网格
- 横向和纵向耦合系数各自带有确定性扰动
- 局部加入少量长程 contact link，模拟不规则耦合
- 对角线上再叠加正质量项，保证 CG 可用

## 用法

默认生成 512 维数据集：

```bash
python3 script/generate_cg_dataset.py
```

指定输出目录：

```bash
python3 script/generate_cg_dataset.py \
  --size 8192 \
  --output-dir data/generated/cgsolver/n8192
```

一键编译并运行：

```bash
make
```

如需指定规模或迭代参数：

```bash
make run SIZE=4096 TAU=1e-10 MAX_ITERS=20000
```

## 当前行为

`main.cpp` 当前会：

1. 读取 CSR 矩阵 `A`、向量 `b` 和初值 `x0`
2. 在内存里构造 Jacobi 对角预条件器 `M`
3. 按 Jacobi-PCG 伪代码求解得到 `golden_solution`
4. 将 golden 结果保留在当前进程内存中，等待你后续把其他实现接进来做对比

当前不会把 golden 解额外写回文件。
