# 语义模块导读

本目录按分析职责组织实现。对外入口为 `rx::analyze`，头文件位于
`include/rx/semantic/semantic.hpp`。AST 的实现和头文件分别位于
`src/ast`、`include/rx/ast`。

## 分析器

| 文件 | 职责 |
| --- | --- |
| `run.cpp` | 分析入口、执行顺序、结果汇总 |
| `collection.cpp` | 顶层声明收集、符号身份与受保护名字 |
| `declarations.cpp` | 声明解析调度、impl 目标与关联项登记 |
| `structures.cpp` | 字段与派生声明、布局环检查、内建 Trait 支持 |
| `functions.cpp` | 函数签名、入口检查、参数与 self 绑定、函数体检查 |
| `type_resolution.cpp` | 类型语法解析、类型路径与 Self 解析 |
| `constants.cpp` | 常量类型、依赖求值与数组长度 |
| `coercion.cpp` | 类型兼容、共同类型、权限与隐式调整 |
| `expressions.cpp` | 表达式分派、字面量、路径与事实登记 |
| `expression_values.cpp` | 数组、结构体初值、字段与下标访问 |
| `expression_control.cpp` | 块、let、分支、循环、跳转与返回 |
| `expression_operators.cpp` | 借用、一元与二元运算、赋值与转换 |
| `calls.cpp` | 普通调用、内建关联路径与调用签名 |
| `method_calls.cpp` | 接收者调整、内建及用户方法调用 |
| `type.cpp` / `operation.cpp` | 语义类型管理与运算解析规则 |
| `result.cpp` / `dump.cpp` | 结果发布、失败结果及稳定文本输出 |

`detail/state.hpp` 保存分析期间的内部记录；`detail/analyzer.hpp`
声明跨实现文件共享的分析器、状态和成员函数。它们属于实现内部，
后续阶段通过公开的语义结果与安全门访问数据。

执行次序保持为：收集顶层声明、解析声明、准备可调用函数索引、
常量校验、布局与派生校验、入口校验、函数体检查、汇总并发布结果。
声明解析内部先处理 impl，随后解析常量类型、关联函数签名、结构体
字段以及顶层函数签名。

## 安全保证（safety）

原 `SemanticIndex` 统一更名为 `SafeGate`，公开头文件为
`include/rx/semantic/safety/safe_gate.hpp`。

| 文件 | 职责 |
| --- | --- |
| `safety/safe_gate.cpp` | 快照持有、索引构造与只读查询 |
| `safety/links_gate.cpp` | 语义记录之间的引用和类型对象检查 |
| `safety/tree_gate.cpp` | AST 身份、源码范围与记录覆盖检查 |
| `safety/declarations_gate.cpp` | 符号、字段、签名与声明一致性检查 |
| `safety/shapes_gate.cpp` | AST 类别与表达式、操作、调用事实核验 |
| `safety/checks.hpp` / `checks.cpp` | 检查门共享的类型与操作核验工具 |

此次重构保留原校验内容和触发时机：发布成功结果时执行校验，
消费者构造 `SafeGate` 时也执行校验。安全门共享只读语义快照，
AST 的生命周期与地址稳定性由调用方保证。

## 验证

使用 CMake 构建并运行 CTest。`scripts/test_frontend.py` 检查外部
lexer、parser、semantic 清单；`scripts/compare_frontend.py` 接受
`--baseline` 和 `--candidate`，逐项比较退出码、诊断以及 AST 和语义
文本输出，可用于后续行为保持型重构。

本次拆分完成后的验证结果：

- CMake 构建成功，C++17 配置保持不变。
- CTest：14/14 通过，包括安全门不变量与 IR 对接测试。
- 外部清单：lexer 53/53、parser 442/442、semantic 236/236 通过。
- 与拆分前编译器比较 731 个清单用例，退出码、标准输出和诊断全部
  一致；crate 解析包含 AST 文本，semantic 阶段包含 AST 与语义文本。

重构前已经存在的学习注释随对应实现迁移，并保留为未提交改动。
