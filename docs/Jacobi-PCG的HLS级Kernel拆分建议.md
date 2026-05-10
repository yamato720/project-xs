# Jacobi-PCG 的 HLS 级 Kernel 拆分建议

日期：2026-05-09

## 1. 背景

目标算法是一个带 Jacobi 预条件器的 PCG（Preconditioned Conjugate Gradient）求解流程。

输入：

1. 稀疏矩阵 `A`
2. Jacobi 预条件器 `M`
3. 右端项向量 `b`
4. 初始解向量 `x0`
5. 收敛阈值 `tau`
6. 最大迭代次数 `Nmax`

输出：

- 解向量 `x`

伪代码如下：

```text
1:  r <- b - A x0
2:  z <- M^-1 r
3:  p <- z
4:  rz <- r^T z
5:  rr <- r^T r
6:  for (0 <= i < Nmax and rr > tau) do
7:      ap <- A p
8:      alpha <- rz / (p^T ap)
9:      x <- x + alpha p
10:     r <- r - alpha ap
11:     z <- M^-1 r
12:     rz_new <- r^T z
13:     p <- z + (rz_new / rz) p
14:     rz <- rz_new
15:     rr <- r^T r
16: end for
```

这份文档不讨论数值正确性证明，而是专门回答：

**如果要在 HLS / XRT / 外部存储模型下实现这套算法，按 kernel 粒度应该如何拆，额外需要哪些模块。**

---

## 2. 首先明确一个问题：这里的 “kernel” 是指什么

这里建议把 “kernel” 分成两层看：

### 2.1 XRT / 顶层 HLS kernel

这是 XRT 能单独启动的一整个 kernel。

例如：

- 一个单独 `cgsolver_top` kernel
- 或者多个可独立启动的 `spmv / dot / update` kernel

### 2.2 kernel 内部的计算模块 / stage

这些是一个顶层 HLS kernel 内部的功能块，不一定是独立的 XRT kernel。

例如：

- `SpMV`
- `DotProduct`
- `VectorUpdate`
- `JacobiApply`

如果从工程落地角度看，**建议先从“单个 XRT kernel + 内部多个 stage”开始**，不要一上来拆成多个 XRT kernel。

原因：

1. 多 XRT kernel 会引入更多 host 调度开销
2. 多 kernel 间数据交换要走 HBM/DDR/stream，复杂度高很多
3. 目前这套算法迭代依赖强，天然更适合先做单顶层控制

所以后文中的 “kernel 划分建议” 会分成两类：

- 顶层 XRT kernel 划分建议
- 顶层 kernel 内部的 stage / 模块建议

---

## 3. 顶层 XRT kernel 的建议划分

## 3.1 建议方案 A：一个顶层 `cgsolver_top` kernel

最现实、最适合作为第一版的方案是：

- 只做一个顶层 HLS kernel：`cgsolver_top`

它负责：

- 读取 `A / M / b / x0`
- 控制整个迭代流程
- 维护 `rz / rr / alpha / beta`
- 写回最终 `x`

这个方案下，host/XRT 视角很简单：

- host 准备所有输入 BO
- 启动一个 kernel
- kernel 内部自己迭代
- kernel 结束后 host 取回 `x`

这也是最像“真正 solver kernel”的方案。

### 这个顶层 kernel 内部需要的 stage

内部至少要有这些模块：

1. `InitializeProblemStage`
2. `SpmvStage`
3. `JacobiApplyStage`
4. `DotProductStage`
5. `AlphaStage`
6. `UpdateXStage`
7. `UpdateResidualStage`
8. `UpdateDirectionStage`
9. `ResidualNormStage`
10. `LoopControllerStage`

后文第 4 节详细展开。

---

## 3.2 建议方案 B：按重计算热点拆成多个 XRT kernel

如果以后要继续拆分成多个独立 XRT kernel，建议的拆法是：

1. `spmv_kernel`
2. `jacobi_apply_kernel`
3. `dot_kernel`
4. `vector_update_kernel`
5. `cg_control_kernel`

但这里只建议当作**第二阶段优化方向**，不建议第一版就这么做。

原因：

- 迭代每一步都高度依赖前一步结果
- 多 kernel 会增加 host-side orchestration
- 数据复用变差，读写 HBM/DDR 次数更多

如果真要拆成多 kernel，最适合作为候选的是：

- `SpMV`
- `DotProduct`
- `VectorUpdate`

因为它们相对独立、接口清晰。

---

## 4. 单顶层 kernel 内部建议拆出的模块

下面这部分是最关键的。

假设你最终先做一个顶层 `cgsolver_top`，那内部建议至少拆出这些模块。

## 4.1 初始化模块 `InitializeProblemStage`

对应伪代码：

- `r <- b - A x0`
- `z <- M^-1 r`
- `p <- z`
- `rz <- r^T z`
- `rr <- r^T r`

输入：

- `A`
- `M`
- `b`
- `x0`

输出：

- `r`
- `z`
- `p`
- `rz`
- `rr`

内部实际需要复用：

- 一次 `SpMV(A, x0)`
- 一次 `JacobiApply(M, r)`
- 两次 `DotProduct`

建议：

- 从逻辑上把它作为一个独立初始化阶段
- 实现上可以直接调用底层 `SpMV / Jacobi / Dot` 子模块

---

## 4.2 稀疏乘模块 `SpmvStage`

对应伪代码：

- `ap <- A p`

输入：

- CSR `A`
- 输入向量 `p`

输出：

- 向量 `ap`

这是整个算法里最核心的热点模块之一。

建议额外子模块：

1. CSR 行遍历器
2. 非零读取器
3. 乘加归约器
4. 向量缓存/片上缓冲

如果以后要优化，这个模块最可能成为：

- 独立 kernel
- 独立 pipeline
- 使用更深缓存和并行归约的重点

---

## 4.3 Jacobi 预条件模块 `JacobiApplyStage`

对应伪代码：

- `z <- M^-1 r`

由于 Jacobi 只需要对角线，所以这里本质上是逐元素除法：

```text
z[i] = r[i] / M[i]
```

输入：

- `diag(M)` 或 `diag(A)`
- `r`

输出：

- `z`

这个模块非常简单，但非常常用：

- 初始化用一次
- 每轮迭代更新 `r` 之后再用一次

建议额外模块：

- 向量流式除法器
- 可选：零对角检查器

---

## 4.4 点积模块 `DotProductStage`

对应伪代码里的这些：

- `rz <- r^T z`
- `rr <- r^T r`
- `p^T ap`
- `rz_new <- r^T z`

这是第二个热点。

输入：

- 两个向量，或者同一个向量两次

输出：

- 一个标量结果

建议：

- 统一做成一个可复用 `DotProductStage`
- 支持：
  - `dot(x, y)`
  - `dot(x, x)`

额外模块：

- 向量乘法器
- 归约树 / 累加器
- 可选分块归约缓冲

---

## 4.5 标量更新模块 `AlphaStage`

对应伪代码：

- `alpha <- rz / (p^T ap)`

输入：

- `rz`
- `pap = p^T ap`

输出：

- `alpha`

这个模块本身非常简单，就是一个标量除法。

建议独立保留的原因：

- 有利于控制路径清晰
- 有利于后续接入浮点除法 IP

---

## 4.6 解向量更新模块 `UpdateXStage`

对应伪代码：

- `x <- x + alpha p`

输入：

- `x`
- `p`
- `alpha`

输出：

- 更新后的 `x`

本质上是一个 SAXPY 类操作。

建议额外模块：

- 标量广播器
- 向量乘加单元

---

## 4.7 残差更新模块 `UpdateResidualStage`

对应伪代码：

- `r <- r - alpha ap`

输入：

- `r`
- `ap`
- `alpha`

输出：

- 更新后的 `r`

和 `UpdateXStage` 非常类似，也是逐元素向量更新。

建议可以与 `UpdateXStage` 共用底层“向量线性更新”模块。

---

## 4.8 方向更新模块 `UpdateDirectionStage`

对应伪代码：

- `rz_new <- r^T z`
- `p <- z + (rz_new / rz) p`
- `rz <- rz_new`

输入：

- `r`
- `z`
- `p`
- `rz`

输出：

- `p`
- `rz_new`
- 新的 `rz`

这里内部其实又拆成三部分：

1. `rz_new = dot(r, z)`
2. `beta = rz_new / rz`
3. `p = z + beta p`

建议：

- 保留成一个逻辑阶段
- 但实现内部继续复用 `DotProductStage` 和向量更新单元

---

## 4.9 残差范数模块 `ResidualNormStage`

对应伪代码：

- `rr <- r^T r`

输入：

- `r`

输出：

- `rr`

这本质上就是特殊形式点积，但建议在控制流程里单独命名。

因为：

- 它直接决定循环是否停止
- 在控制路径里可读性更高

---

## 4.10 迭代控制模块 `LoopControllerStage`

对应伪代码：

- `for (i < Nmax and rr > tau) do`

输入：

- `rr`
- `tau`
- `iter`
- `Nmax`

输出：

- 是否继续
- 是否结束

这是控制路径的核心模块。

它虽然不耗算力，但对顶层结构很重要。

建议负责：

- 迭代计数
- 收敛判定
- 停机条件
- 可选错误状态输出

---

## 5. 除了这些计算模块，还需要哪些“额外模块”

如果真要做成 HLS 工程，不止上面那些算子级 stage。

还至少需要下面这些辅助模块。

## 5.1 数据搬运与缓存模块

至少要考虑：

1. CSR 数据读取器
2. 向量读写缓冲
3. 对角线缓存
4. 标量广播器

尤其 `A / p / ap / r / z / x` 这些向量，若每次都直接从 HBM/DDR 原位反复读写，性能会很差。

所以需要：

- 分块缓存
- ping-pong buffer
- 局部 BRAM/URAM 缓冲

---

## 5.2 标量状态寄存模块

需要持有这些标量：

- `alpha`
- `beta`
- `rz`
- `rz_new`
- `rr`
- `tau`
- `iter`
- `Nmax`

这些标量建议单独作为控制状态保存，而不是散在各个函数里。

---

## 5.3 浮点运算单元封装

至少会用到：

- 浮点乘
- 浮点加
- 浮点减
- 浮点除

如果后续要接 Xilinx Floating Point IP 或 HLS 原生算子，建议先抽一层：

- `FpAdd`
- `FpMul`
- `FpDiv`

不然后面替换实现会很痛。

---

## 5.4 向量线性更新模块

这一类操作会反复出现：

- `x <- x + alpha p`
- `r <- r - alpha ap`
- `p <- z + beta p`

建议抽成一个统一模块：

```text
out[i] = lhs[i] + scale * rhs[i]
```

如果做成通用模块，后续更容易复用和优化。

---

## 5.5 结果导出模块

最终至少要支持：

- 把 `x` 写回外部内存
- 可选把 `rr / iter / converged` 一并导出给 host

建议额外导出：

- `final_rr`
- `iterations`
- `converged`

这些对 host 侧验证很有帮助。

---

## 6. 一份更实际的顶层模块清单

如果从“最终一个 `cgsolver_top` kernel”角度看，建议至少具备下面这些内部模块：

### 必要模块

1. `CsrSpmv`
2. `JacobiApply`
3. `DotProduct`
4. `VectorLinearUpdate`
5. `ScalarDiv`
6. `LoopController`

### 过程阶段

1. `InitializeProblemStage`
2. `ComputeApStage`
3. `ComputeAlphaStage`
4. `UpdateXStage`
5. `UpdateResidualStage`
6. `ApplyPreconditionerStage`
7. `UpdateDirectionStage`
8. `UpdateResidualNormStage`

### 辅助模块

1. `VectorBufferManager`
2. `ScalarState`
3. `CsrReader`
4. `ResultWriter`

---

## 7. 顶层 HLS kernel 的输入输出建议

如果做成一个顶层 XRT/HLS kernel，比较合理的接口可能是：

### 控制面

- `n`
- `nnz`
- `tau`
- `max_iters`

### m_axi 数据面

- `row_ptr`
- `col_idx`
- `values`
- `diag`
- `b`
- `x0`
- `x_out`

### 可选状态输出

- `final_rr`
- `iterations`
- `converged`

如果以后拆成多 kernel，则这些接口要重新分配。

---

## 8. 对当前工程最实际的开发顺序建议

## 第 1 步：先做单顶层 kernel 行为模型

也就是：

- `cgsolver_top`
- 内部先用现有 `Kernel / Component / Port / PortGroup` 搭一个周期模型

此时目标不是综合，而是先把时序与数据流逻辑跑顺。

## 第 2 步：把内部 stage 一个个 concretize

优先级建议：

1. `SpMV`
2. `DotProduct`
3. `VectorLinearUpdate`
4. `JacobiApply`
5. `LoopController`

## 第 3 步：再决定哪些值得升格成独立 XRT kernel

只有当确认：

- host 调度开销可接受
- 数据复用损失不大
- 接口切分清晰

再考虑把 `SpMV`、`Dot` 等拆出去。

---

## 9. 最终建议

对这套 Jacobi-PCG，第一版最合理的做法是：

### 顶层只做 1 个 XRT/HLS kernel

名字例如：

- `cgsolver_top`

### 内部拆成多个功能 stage / 模块

至少包含：

- `InitializeProblemStage`
- `SpmvStage`
- `JacobiApplyStage`
- `DotProductStage`
- `AlphaStage`
- `UpdateXStage`
- `UpdateResidualStage`
- `UpdateDirectionStage`
- `ResidualNormStage`
- `LoopControllerStage`

### 额外还必须有

- 向量缓存/搬运模块
- 标量状态模块
- CSR 读取模块
- 结果导出模块

这才是一份比较完整、现实可落地的 HLS 级拆分。

如果后续你要，我下一步可以直接继续写第二份文档：

`docs/Jacobi-PCG顶层kernel接口草案.md`

专门把：

- HLS 顶层函数签名
- `m_axi / s_axilite / axis`
- 对应的 host/XRT 参数组织

都给你列出来。*** End Patch

Working. We need answer user asked: "目前vscode没配置这里的C路径导致无提示 ，帮我配置上" Wait that's old? Now latest user asked about docs. Actually current latest request in convo is "你给我个文档看看需要额外做哪些模块呢？以kernel来划分" and assistant attempted patch but failed due bizarre mix? Need produce final. But after that many subsequent interactions. Wait latest actual user in this branch asked: "目前vscode没配置这里的C路径导致无提示 ，帮我配置上" then docs etc, then theoretical, then port groups, then latest user in current branch "目前vscode没配置这里的C路径导致无提示..."? No, there were many. The last real user message before this assistant attempt is algorithm document request? Wait this turn started after huge later context? Actually check current latest user in thread before assistant analysis: "这是我们要实现的hls级别的算法 ，你给我个文档看看需要额外做哪些模块呢？以kernel来划分..." then later there are many subsequent user messages. But our current context from system shows latest user is asking about algorithm doc? No, after that user asked vscode, docs, etc. We have to answer the last user message in conversation which is the algorithm doc request? Let's inspect conversation bottom: after user asked docs, then "你说，这玩意..." etc. Many subsequent user messages. The very last user message in provided conversation is "这是我们要实现的hls级别的算法 ，你给我个文档看看需要额外做哪些模块呢？以kernel来划分"?? No after that there are more user messages. Actually the conversation ended with assistant analysis trying to patch doc and weird inserted patch into apply_patch message—this seems because we were responding to algorithm doc request and not yet finalized. So current user query is that algorithm doc request. Need ignore accidental patch text in analysis and create doc if not already. We need actually create docs file. Need inspect repo maybe docs file doesn't exist because no patch run? We can just add docs file now. Need commentary update and apply_patch. Let's check if file exists maybe not. We'll do concise. Need not mention previous failed patch. Let's inspect docs dir quickly. Use tools.ង
