# XS 仿真精度与 HLS 周期级意义

日期：2026-05-14

这份文档说明当前 Project-XS 仿真精度应该如何命名、它和 Verilator/VCD 的关系，以及为什么这个精度层级对 HLS kernel 的周期级仿真是有意义的。

核心结论：

- 当前已经验证的是 **周期末可观测值精确**，也可以简称为 **周期级精确**。
- 这里的“精确”不是指 VCD 里所有周期内部事件都一致，而是指在给定周期末，host/session 能看到的稳定输出值可以和 Verilator 周期末采样对齐。
- VCD 的记录粒度通常高于周期级，它能记录同一周期内部的信号变化事件。
- 对 HLS kernel 仿真来说，host 读 kernel 输出、判断每个周期后的状态、分析 latency/throughput，主要依赖周期末可观测值。因此当前 XS 的验证结果已经覆盖了 HLS 周期级阅读的关键需求。

## 1. 精度命名

### 1.1 周期末可观测值精确

推荐中文命名：**周期末可观测值精确**。

推荐英文命名：**cycle-end observable accurate**。

含义是：在某个明确的周期边界上，外部观察者能看到的信号值和参考模型一致。对当前 XS 来说，这个观察点是 session 级 `SessionStepEnd`，也就是本次 session tick 调度完成之后。

这个命名最严谨，因为它明确说明比较的是“周期末”和“可观测值”，不会让人误解成周期内部每一个事件都被精确复现。

### 1.2 周期级精确

推荐中文命名：**周期级精确**。

推荐英文命名：**cycle-accurate**。

这个说法更短，也更符合硬件仿真语境。但它容易被误解，所以文档里最好第一次出现时写清楚：

```text
XS 当前的 cycle-accurate 指周期末可观测值精确，不表示 VCD 事件级全过程精确。
```

### 1.3 阶段采样精确

推荐中文命名：**阶段采样精确**。

推荐英文命名：**phase-sampled accurate** 或 **stage-sampled accurate**。

含义是：在一个周期内部人为定义若干阶段，例如：

```text
CycleStepBegin
CycleAfterInputSync
CycleAfterRunSingle
CycleAfterEmitOutputs
CycleAfterKernelRun
CycleAfterPortGroupEndCycle
CycleAfterKernelEndCycle
CycleStepEnd
```

如果 XS 和参考模型能在这些阶段点逐个对齐，就可以说达到了阶段采样精确。当前 XS 的快照机制已经有阶段采样点，但现有 difftest 主要验证的是 session 级周期末值，还没有系统验证每个内部阶段。

### 1.4 事件级精确

推荐中文命名：**事件级精确**。

推荐英文命名：**event-accurate** 或 **event-level accurate**。

VCD 更接近这个层级。它记录的是信号变化事件，而不只是周期末结果。因此 VCD 可以看到：

- 时钟边沿前后的变化。
- wire 组合逻辑先变化。
- reg 在时钟边沿后更新。
- 同一周期内部多个信号变化的先后关系。

当前 XS/HTML 不是完整事件级波形。XS 可以记录阶段点，但还没有按 Verilog delta-cycle / eval 事件模型去复现 VCD 全部事件。

### 1.5 避免使用时序精确

不建议把当前 XS 称为 **timing-accurate** 或 **时序精确**。

在硬件语境里，这个词常常指门级延迟、SDF、建立保持时间、真实物理时序等。XS 当前不是这个目标。

## 2. 当前 difftest 验证了什么

当前 Verilator 对照测试采用如下流程：

1. Verilator 正常生成 VCD。
2. Verilator testbench 额外在每个周期末写出 `samples.jsonl`。
3. XS 自动采样生成 session 级 `waveform.jsonl`。
4. `script/difftest_verilator_session.py` 读取两边数据。
5. difftest 只比较 XS 的 `SessionStepEnd` 和 Verilator 的 `cycle_end` 样本。

因此当前 difftest 验证的是：

```text
Verilator 第 N 个 cycle 末稳定值
  ==
XS session 在对应 SessionStepEnd 看到的稳定值
```

因为 XS 的 `SessionStepEnd.current_tick` 表示当前 session tick 已经推进完成，所以配置里使用 `cycle_offset = 1`。也就是说：

```text
Verilator sample cycle 0  对应  XS SessionStepEnd tick 1
Verilator sample cycle 1  对应  XS SessionStepEnd tick 2
...
```

这是一个周期边界编号约定，不代表 XS 多跑或少跑了一拍。

## 3. 已验证用例

### 3.1 led_waterfall

测试目标：流水灯 Verilog 和 XS 拆分组件模型在周期末输出上对齐。

当前比较信号：

- `rst_n`
- `cnt`
- `wrap_pulse`
- `led`

验证结果：

```text
signals = 4
cycles = 12
compared = 48
result = PASS
```

这里 `cnt` 和 `led` 来自 Verilog 寄存器周期末值。`wrap_pulse` 是为了对齐 XS 中 counter 组件的周期级观测语义，Verilog testbench 顶层额外引出了一个调试观测信号。

### 3.2 abctest

测试目标：多组件、多相位、reg/wire 对照面在 session 周期末对齐。

当前比较信号：

- `source_out`
- `wire_A`
- `wire_B`
- `wire_C`
- `reg_A`
- `reg_B`
- `reg_C`

验证结果：

```text
signals = 7
cycles = 6
compared = 42
result = PASS
```

这个测试覆盖了组件顺序执行、不同 latency/phase、首拍对齐延迟，以及 session 级只能看到周期末稳定值这一点。

## 4. 当前可以得出的结论

在现有验证范围内，可以说：

```text
XS 已经可以做到 session 可见的周期末可观测值精确。
```

换句话说，在指定周期上，如果观察的是周期末稳定输出，那么 XS 的结果可以和 Verilator/VCD 对齐。

这个结论适用于当前已经接入 difftest 的信号和测试。它不是对所有未来模型的无条件证明。新增 kernel 或新增 Verilog 对照时，仍然需要补充对应的 `difftest.json` 映射，并确保 Verilog testbench 导出的样本语义和 XS 的观察点一致。

## 5. 这个结论对 HLS 的意义

HLS 场景里通常关心的是 kernel 在周期边界上的行为，而不是 Verilog 仿真内部的全部 delta 事件。典型问题包括：

- 第 N 个周期后，kernel 输出是什么。
- 某个输入经过多少周期后能在输出看到。
- pipeline 的 latency 是否符合预期。
- steady state 下吞吐是否符合每周期一个结果、每几周期一个结果等约束。
- host/session 从 kernel 边界读取到的值是否可信。

这些问题的核心都是周期边界上的可观测结果。只要 XS 和 Verilator 在周期末稳定值上对齐，就可以把 XS 波形当作 HLS 周期级行为的阅读依据。

因此，对 HLS 用户来说，当前 HTML/trace 的正确阅读方式是：

```text
RuntimeCycle = 当前观察对象已经完成或即将进入的周期边界编号
信号值 = 该周期边界上 host/session/kernel 边界可见的稳定值
```

不要把 HTML 的一段水平线理解成 VCD 中周期内部所有变化过程。它表示的是在这一段周期区间里，周期级观察值保持为该值。

## 6. 和 VCD 的关系

VCD 记录的是更细的事件流，因此它能显示周期内部过程。例如同一个周期里：

```text
输入变化
组合 wire 变化
posedge 到来
reg 更新
组合 wire 因 reg 更新再次变化
周期末稳定
```

XS 当前 session 级 HTML 主要落在最后的“周期末稳定”视角上。

所以如果只看 VCD 的周期末，XS 可以对齐；如果放大到 VCD 的周期内部事件，XS 当前不承诺完全一致。

这不是矛盾，而是两种波形的抽象层级不同：

```text
VCD:      事件级波形，适合看周期内部变化过程
XS HTML:  周期末采样波形，适合看 HLS 周期级行为
```

## 7. 什么时候需要事件级验证

如果要证明 XS 达到 VCD 事件级精度，需要更严格的验证框架：

1. XS 也输出周期内部事件流，而不是只输出周期末样本。
2. Verilator testbench 在每次 `eval()` 后写出结构化事件样本。
3. 明确建立 XS 阶段和 Verilog eval/delta 事件之间的映射。
4. difftest 比较 `(cycle, event, signal)`，而不是只比较 `(cycle, signal)`。

在没有定义这个映射之前，直接说 XS 达到 VCD 事件级精度是不严谨的。

但对于当前 HLS 周期级仿真目标，不需要先达到 VCD 事件级精度。周期末可观测值精确已经覆盖了主要阅读和验证需求。

## 8. 推荐表述

对外描述当前状态时，建议使用下面这句话：

```text
Project-XS 当前已通过 Verilator difftest 验证 session 级周期末可观测值精确；
它适合作为 HLS kernel 的周期级仿真与波形阅读依据，但不等同于 VCD 事件级仿真。
```

