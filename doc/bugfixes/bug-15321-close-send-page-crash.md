# Bug 修复记录

## 1. 基本信息
- Bug ID: `15321`
- 标题: `关闭发送页时，崩溃`
- 日期: `2026-03-17`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15321.html`
- 所属产品: `Sanity Print`
- 所属模块: `设备管理`
- Bug 类型: `代码错误`
- 严重程度: `致命`
- 优先级: `高`
- 状态: `激活`
- 创建人: `冷金辉`
- 指派给: `贺淼`
- 影响版本: `SanityPrint_7.1.0.4318_Beta`
- 所属执行: `CP7.1.0 260330`

## 2. 问题现象
- 在发送打印文件过程中，如果用户关闭发送页，程序会发生崩溃。
- 从现场 dump 看，崩溃点落在 `CxSentToPrinterDialog::handle_send_gcode(...)` 的异步回调中，访问 `m_browser->IsBusy()` 时触发访问违规。
- 这类问题会在关闭窗口与上传回调并发发生时出现，属于典型的窗口生命周期与异步任务时序冲突。

## 3. 影响范围
- 模块: `发送打印页 / WebView 交互 / 上传进度回调`
- 关键文件:
  - `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
  - `src/slic3r/GUI/print_manage/App/SendToPrinter.hpp`
- 受影响流程:
  - `send_gcode` 上传进度回调
  - `send_gcode` 上传状态回调
  - `send_gcode` 上传完成回调
  - `send_3mf` 对应的上传/完成回调
  - 关闭发送页时的异步 `Close(false)` 投递路径

## 4. 复现方式（修复前）
1. 打开发送打印页。
2. 开始发送 `gcode` 或 `3mf` 文件到设备。
3. 在上传仍有后台回调返回时关闭发送页。
4. 后台上传线程继续通过 `wxTheApp->CallAfter(...)` 投递 UI lambda。
5. lambda 执行时，对话框或其中的 `m_browser` 可能已经析构。
6. 访问 `m_browser->IsBusy()` / `run_script(...)` 时触发崩溃。

## 5. 根因分析
- `handle_send_gcode(...)` / `handle_send_3mf(...)` 中的上传回调使用了 `wxTheApp->CallAfter(...)`。
- 这些 lambda 捕获了 `this`，并在执行时直接访问 `m_browser`。
- `wxTheApp` 级别的投递队列独立于对话框生命周期存在:
  - 即使发送页已经关闭并进入析构，队列中的 lambda 仍可能继续执行。
  - 此时 `this` 或 `m_browser` 已变成悬空对象。
- 崩溃截图中的异常码为访问违规，说明这不是 C++ 异常，而是对象生命周期失效后的非法内存访问。
- 原来的 `try/catch (...)` 只能吞掉 C++ 异常，无法阻止这类访问违规，因此对问题没有实际保护作用。

## 6. 修复策略
- 将发送页相关的异步 UI 投递从 `wxTheApp->CallAfter(...)` 改为对话框自身的 `CallAfter(...)`。
- 增加统一封装:
  - `post_script(const wxString& script)`
  - `post_close()`
- 在真正执行脚本前增加对象有效性检查:
  - `m_browser != nullptr`
  - `!m_browser->IsBeingDeleted()`
- 在 `run_script(...)` 内部增加同样的防御式保护，避免其他调用路径误入已销毁 WebView。
- 上传完成回调虽然仍需在 UI 线程执行，但改为投递到对话框自身，并在访问 `m_browser` 前做有效性判断。

## 7. 代码改动摘要
- 文件: `src/slic3r/GUI/print_manage/App/SendToPrinter.hpp`
  - 新增 `post_script(...)` 与 `post_close()` 私有方法声明。

- 文件: `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
  - 为 `run_script(...)` 增加 `m_browser` 空指针和删除中状态保护。
  - 新增 `post_script(...)`，统一处理发送页脚本投递。
  - 新增 `post_close()`，统一处理关闭动作投递。
  - 将 `send_gcode` / `send_3mf` 上传进度与上传状态中的 `wxTheApp->CallAfter(...)` 替换为 `post_script(...)` / `post_close()`。
  - 将上传完成回调中的 `wxTheApp->CallAfter(...)` 改为对话框自身 `CallAfter(...)`。
  - 在上传完成回调执行 `m_browser->IsBusy()` 前增加 `m_browser` 生命周期判断。

## 8. 修复后的行为
- 如果发送页仍然存活，上传回调照常更新进度、状态和完成通知。
- 如果发送页已经关闭或正在销毁:
  - 对话框自身的挂起任务不会再越过窗口生命周期继续访问已析构对象。
  - 即使完成回调已经排队，执行前也会先检查 `m_browser` 是否仍有效。
- 结果是“关闭发送页时崩溃”被收敛为安全跳过，不再访问悬空 WebView。

## 9. 验证清单
- [ ] 打开发送页，发送 `gcode`，上传过程中关闭页面，程序不崩溃。
- [ ] 打开发送页，发送 `3mf`，上传过程中关闭页面，程序不崩溃。
- [ ] 上传完成较晚返回时，不再因为 `notify_send_complete` 的回调访问失效 `m_browser` 而崩溃。
- [ ] 正常不关闭页面时，上传进度、上传状态、发送完成通知仍能正常刷新到前端页面。
- [ ] 多次打开/关闭发送页后重复测试，无新增崩溃。

## 10. 风险与回滚
- 风险等级: `低`
- 原因:
  - 修改仅局限于发送页异步 UI 投递方式与生命周期保护。
  - 不涉及上传协议、业务参数、前端协议字段、设备通信逻辑。
- 需要关注的副作用:
  - 页面关闭瞬间，末尾少量进度/完成脚本可能被安全跳过，这是符合预期的。
  - 若外部逻辑依赖“关闭后仍强制执行页面脚本”，该行为会被抑制，但这本身就是本 bug 的修复目标。
- 回滚方式:
  - 回退 `SendToPrinter.cpp/.hpp` 中 `post_script(...)`、`post_close()` 和相关调用替换即可。

## 11. 结论
- 本问题的本质不是单纯空指针，而是 `wxTheApp->CallAfter(...)` 投递的异步任务跨越了发送页对话框的生命周期。
- 修复后的策略是让 UI 任务跟随对话框生命周期，并在执行前检查 `m_browser` 是否仍然有效。
- 该方案能直接覆盖本次 dump 所对应的崩溃现场，也同时修复了 `send_gcode` 与 `send_3mf` 两条发送路径中的同类风险。
