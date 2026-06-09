# Bug 修复记录

## 1. 基本信息
- Bug ID: `15039`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15039.html`
- 标题: `CRT invalid parameter 崩溃堆栈不完整 — 增强堆栈采集`
- 日期: `2026-02-28`
- 分支/提交: `release-260330`

## 2. 问题现象
- 用户长时间运行程序后偶发崩溃，Breakpad 生成的 dump 文件中堆栈仅有 2 帧，无法看到任何业务代码调用，无法定位根因
- 崩溃类型为 CRT invalid parameter（异常码 `0xc000000d`），Assertion 信息：`Invalid parameter passed to library function (null)`

## 3. 影响范围
- 模块: `崩溃捕获 / Breakpad 集成`
- 关键文件:
  - `src/SanityPrint.cpp`
- 受影响流程:
  - CRT invalid parameter 类崩溃的堆栈采集（不影响其他类型崩溃的正常捕获）

## 4. 复现步骤（修复前）
1. 长时间运行程序（用户案例为约 17.5 小时）。
2. 某个操作触发 CRT 函数收到 NULL 参数（如 printf、sprintf、wcslen 等）。
3. 程序崩溃，生成的 dump 中堆栈仅有 Breakpad 自身的 HandleInvalidParameter 帧，无业务代码帧。

## 5. 根因分析
- CRT invalid parameter 崩溃不走 SEH 异常分发路径，而是通过 `_invalid_parameter_handler` 函数回调触发
- Breakpad 在回调中使用 `RtlCaptureContext` 抓取上下文后生成 minidump，但此上下文是 Breakpad 内部帧的上下文，后续 minidump_stackwalk 解析时因缺乏可靠的栈帧链接信息而回溯失败
- 导致 dump 中只有 Breakpad 回调自身的 1-2 帧，业务代码调用链完全丢失
- 注意：绝大多数崩溃（访问违例、除零等 SEH 异常）的堆栈采集完全正常，此问题仅影响 CRT invalid parameter 这一种特定崩溃类型

## 6. 修复策略
- 在 Breakpad 的 `ExceptionHandler` 创建之后，注册一个自定义的 `_invalid_parameter_handler`，在 Breakpad 介入之前先行采集完整堆栈
- 自定义 handler 内部使用 `CaptureStackBackTrace`（Win32 API，基于 PE 文件 `.pdata` 段 unwind info，x64 上可靠性 90-95%）在业务代码帧尚完整时采集全部调用帧
- 将每一帧的绝对地址、所属模块名、RVA（相对虚拟地址）写入 temp 目录下的日志文件
- 采集完成后链式调用 Breakpad 原始 handler，dump 照常生成，现有崩溃处理流程不受任何影响
- handler 内部全部使用 Win32 API（CreateFileW、WriteFile、wsprintfA 等），避免 CRT 重入问题
- 包含 `InterlockedCompareExchange` 重入保护，防止 handler 内部再次触发 invalid parameter 导致死循环

## 7. 代码改动摘要
- 文件: `src/SanityPrint.cpp`
- 关键修改点:
  - 添加头文件: `#include <stdlib.h>`（确保 `_set_invalid_parameter_handler` 声明可用）
  - 新增函数: `CrealityInvalidParameterHandler`（约 70 行，含重入保护、堆栈采集、模块解析、文件写入、链式调用）
  - 新增注册: 在 `ExceptionHandler` 创建后调用 `_set_invalid_parameter_handler(CrealityInvalidParameterHandler)`
- 修改行数: 共约 108 行（1 行头文件 + 104 行函数定义及注释 + 3 行注册调用及注释）
- 编译隔离: 全部在 `#if defined(WIN32) && defined(USE_BREAKPAD)` 内，非 Windows 或未启用 Breakpad 时不编译

## 8. 验证清单
- [ ] 正常使用各项功能，无新增崩溃或异常。
- [ ] 人为触发 CRT invalid parameter 崩溃时，temp 目录下生成 `sanityprint_invalid_param_stack.log` 文件，包含完整堆栈帧。
- [ ] 上述崩溃同时仍正常生成 Breakpad dump 文件，现有崩溃上报流程不受影响。
- [ ] 正常退出程序时，temp 目录下不会生成该日志文件（仅崩溃时才产生）。
- [ ] 日志中的模块名+RVA 配合 PDB 可正确解析出函数名和行号。

## 9. 风险与回滚
- 回滚方式: 删除 `CrealityInvalidParameterHandler` 函数定义、移除 `_set_invalid_parameter_handler` 注册调用、移除 `#include <stdlib.h>`。
- 风险等级: `低` — 仅在 CRT invalid parameter 崩溃路径上增加日志采集，正常运行时完全不执行；所有失败场景（文件创建失败、栈采集失败等）均静默跳过并回退到原有 Breakpad 处理流程。

## 10. 后续建议
- 收集到完整堆栈后，定位并修复触发 CRT invalid parameter 的业务代码根因。
- 日志中的地址解析方式：`模块名 + RVA` 配合对应版本 PDB，使用 `dumpbin /symbols`、Visual Studio 或 WinDbg 的 `ln` 命令即可解析。
