# Chisel风格分层报错草案

## 背景

参考 Chisel 的经验，硬件生成/建模框架不应该把所有错误都塞进同一层处理。
更合理的做法是把错误按阶段拆开，让每一层只负责它最擅长发现的问题。

Chisel 的典型错误层次可以粗略理解为：

1. 宿主语言编译层  
   例如 Scala 语法/类型错误。

2. elaboration 层  
   运行生成器、构建电路对象图时发现的问题。

3. 后端编译层  
   例如 CIRCT / firtool 在导出 Verilog 前后发现的问题。

XS 当前也可以按同样思路演进。

## 目标

让 XS 的错误更早暴露、更容易定位、更方便以后继续往“尽早发现问题”的方向收紧。

## 分层建议

### 1. 编译期静态层

只负责“完全静态可知”的问题。

适合放这里的约束：

- 模板参数不合法
- 固定 shape 不合法
- 某些禁止的模板组合
- 固定接口表中的静态重复项

适合的实现：

- `static_assert`
- `constexpr`
- `consteval`
- 模板参数

### 2. Elaboration 层

对应“对象构造、注册、连线、复制”阶段。

适合放这里的约束：

- `StateSet` / `StateArrayRegistry` 注册重名
- `StateArray` 元素名/别名冲突
- 端口方向不匹配
- 端口类型/位宽不匹配
- 组件 / kernel / portgroup 按名字查找失败
- snapshot/restore 后布局不一致

适合的实现：

- 构造函数中检查
- `register_*` 中检查
- `connect()` 中检查
- 统一错误模块抛出带阶段信息的异常

### 3. Validate 层

对应“真正 run() 或导出后端前”的全局合法性检查。

适合放这里的约束：

- 关键端口是否已连接
- 拓扑是否完整
- 是否缺少必需组件
- 依赖顺序是否满足设计要求
- 某类标准模块是否满足协议约束

适合的实现：

- `validate()`
- `collect_diagnostics()`
- 支持一次性收集多个错误而不是只抛第一个

### 4. Backend 层

对应未来如果做 Verilog/SystemVerilog 导出时的后端约束。

适合放这里的约束：

- 不支持的抽象无法落成 RTL
- 命名不合法
- 保留字冲突
- 导出后端自己的结构限制

## 当前 XS 的落点

### 已有

- 编译期静态层：少量
- Elaboration 层：已有不少，主要集中在注册、查找、连接、复制
- Validate 层：尚未系统化
- Backend 层：尚未建立

### 当前最值得推进

优先补齐 Validate 层，而不是盲目追求“全部编译期报错”。

原因：

- 当前很多名字、连接、shape 都是运行时数据
- 用运行时检查更符合当前框架的灵活性
- 但需要把错误格式统一、阶段明确

## 统一错误格式建议

统一输出格式：

`[stage][kind][scope] detail`

例如：

- `[elaboration][duplicate-name][StateSet] duplicate state name: rs1_addr`
- `[elaboration][type-mismatch][Port connect] rs1_rdata -> rd_wdata`
- `[validate][not-found][Kernel sim_a] required component missing: write_stage`
- `[backend][unsupported][Verilog] dynamic port creation is not supported`

## 实施顺序建议

1. 先建立统一错误模块
2. 先把 Elaboration 层全部收进统一错误模块
3. 再补 Validate 层入口
4. 最后才考虑哪些约束值得进一步前移到编译期

## 结论

参考 Chisel，XS 更合理的方向不是“自己做一套编译器”，
而是“把错误分阶段，并给每个阶段统一错误模型”。

对当前工程来说，最现实、收益最高的路线是：

- 保留编译期静态检查作为补充
- 强化 Elaboration 层统一错误
- 逐步建立 Validate 层
