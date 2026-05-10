# Project-XPlus HLS Example

这份 example 用来把 `Project-XPlus` 里的 5 个 HLS kernel 作为示例引入到 `Project-XS`，
并生成一个静态 HTML 可视化页面，帮助从 `XS` 视角观察：

1. kernel 的职责
2. 顶层接口
3. memory bundle 分配
4. loop label 与 pipeline 结构

## 文件

```text
example/project_xplus_hls/
  kernels/
    spmv_csr_kernel.cpp
    init_pcg_kernel.cpp
    dot_kernel.cpp
    update_xrz_kernel.cpp
    update_p_kernel.cpp
  README.md
```

## 生成可视化

在 `Project-XS` 根目录执行：

```bash
python3 script/render_xplus_hls_example.py
```

默认输出：

```text
example/project_xplus_hls/reports/project_xplus_hls_example.json
example/project_xplus_hls/reports/project_xplus_hls_example.html
```
