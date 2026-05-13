# Validate 骨架使用说明

## 当前状态

XS 现在已经补上了一套最小 `validate()` 骨架，目标不是一次性把所有规则都写满，
而是先把“诊断收集路径”打通。

当前已经具备这些接口：

- `PortGroup::collect_diagnostics(...)`
- `PortGroup::validate()`
- `PortGroup::validate_or_throw()`

- `KernelComponent::collect_diagnostics(...)`
- `KernelComponent::validate()`
- `KernelComponent::validate_or_throw()`

- `Kernel::collect_diagnostics(...)`
- `Kernel::validate()`
- `Kernel::validate_or_throw()`

- `CycleSimulator::collect_diagnostics(...)`
- `CycleSimulator::validate()`
- `CycleSimulator::validate_or_throw()`

- `SimulationSession::collect_diagnostics(...)`
- `SimulationSession::validate()`
- `SimulationSession::validate_or_throw()`

## 当前行为

当前版本先做的是“递归收集框架”，不是“完整规则库”。

也就是说：

- `collect_diagnostics(...)`
  会按层向下递归遍历子对象
- 但默认规则目前还比较少
- 主要价值是先把接口和调用路径稳定下来

## 怎么用

### 1. 收集诊断但不抛错

```cpp
std::vector<project_xs::sim::error::Diagnostic> diagnostics;
session.collect_diagnostics(diagnostics);
```

适合：

- IDE / GUI 展示
- 批量打印
- 生成报告

### 2. 直接拿诊断列表

```cpp
auto diagnostics = simulator->validate();
```

适合：

- 局部检查某个 simulator / kernel / component

### 3. 发现 Error 就直接抛

```cpp
session.validate_or_throw();
```

适合：

- 在 `run()` 前做一次硬校验
- 在导出前做一次强校验

## 推荐使用位置

### 当前推荐

在下面两个时机调用最有意义：

1. `session.run()` 前
2. 导入生成完成后、真正使用前

例如：

```cpp
session.reset();
session.initialize_zero();
session.validate_or_throw();
session.run();
```

## 后续演进方向

当前骨架已经具备，后续可以逐步把规则补进去，例如：

- 必需端口是否连接
- 关键组件是否存在
- 某些标准模块拓扑是否完整
- 某些名字布局是否满足规范

也就是说，下一步不是再发明接口，而是往现有 `collect_diagnostics(...)` 里逐层填规则。
