# Bug 修复记录

## 1. 基本信息
- Bug ID: `15214`
- Bug 标题: `在设备列表页一直按F5时，会触发Webview2的弹窗，该弹窗应该控制在软件启动时才弹`
- 记录日期: `2026-03-17`
- 提单人: `冷金辉`
- 处理人: `贺淼`
- 分支/提交:

## 2. 问题现象
- 在设备列表页持续按 `F5` 刷新时，会弹出 WebView2 相关修复提示框。
- 该类弹窗按预期应只在软件启动阶段或首次运行时异常处理阶段出现，不应在后续页面刷新中反复弹出。
- 影响:
  - 设备页使用过程中被频繁打断
  - 用户容易误以为 WebView2 持续异常
  - 页面刷新行为和运行时修复提示耦合过深，体验较差

## 3. 影响范围
- 模块: `WebView2 运行时初始化 / 设备页 WebView 错误处理`
- 关键文件:
  - `src/slic3r/GUI/GUI_App.hpp`
  - `src/slic3r/GUI/GUI_App.cpp`
  - `src/slic3r/GUI/print_manage/App/PrinterMgrView.cpp`
- 涉及流程:
  - 设备列表页 / 设备管理页 WebView 展示流程
  - Windows 下 WebView2 运行时异常修复提示流程

## 4. 修复前复现步骤
1. 打开设备列表页。
2. 持续按 `F5` 刷新页面。
3. 当刷新过程中进入 WebView 连接错误分支时，会触发 WebView2 修复相关提示。
4. 结果:
   - 同一次启动中，后续刷新仍可能再次弹出修复提示；
   - 与“该提示只应在软件启动时出现一次”的预期不符。

## 5. 原因分析
- ZenTao 页面只描述了现象，没有给出代码层原因。
- 基于当前源码分析，问题主要在两个层面：
  - `GUI_App::init_webview_runtime()` 只负责启动阶段检查 WebView2 Runtime 是否存在；
  - `PrinterMgrView::OnError()` 在设备页运行过程中，也包含一条 `wxWEBVIEW_NAV_ERR_CONNECTION` 的运行时修复提示路径。
- 原实现中，设备页侧只使用实例级变量 `m_bHasError` 做保护。
- 这意味着“是否弹出修复提示”的控制范围只在当前 `PrinterMgrView` 实例内，而不是整个应用会话级别。
- 当页面反复刷新、重载或重新进入错误分支时，同一次应用启动中仍可能再次触发修复提示。

## 6. 修复方案
- 保留 `F5` 刷新能力，不对快捷键做拦截。
- 只调整 `PrinterMgrView` 自身逻辑，不扩散到其他 WebView 页面。
- 处理规则:
  - `PrinterMgrView` 在本实例内一旦成功加载过一次，就将该实例标记为“已成功”；
  - 后续该实例即使再收到连接类错误，也不再弹出 WebView2 修复提示；
  - 在首次成功加载之前，仍保留原有的修复提示与会话去重逻辑。
- 同时保留原有的 `webview_single_process` 降级与重启逻辑，不改变该分支原行为。

## 7. 代码修改说明
- 文件: `src/slic3r/GUI/GUI_App.hpp`
  - 保持现有 `m_webview_runtime_repair_prompted` 与 `mark_webview_runtime_repair_prompted()` 逻辑不变，继续只负责会话级去重
- 文件: `src/slic3r/GUI/print_manage/App/PrinterMgrView.hpp`
  - 新增实例级标记 `m_webview_loaded_successfully`
- 文件: `src/slic3r/GUI/print_manage/App/PrinterMgrView.cpp`
  - 在 `OnLoaded()` 中将 `m_webview_loaded_successfully` 置为 `true`
  - 调整 `OnError()`:
    - 若当前实例已成功加载过，则仅记录日志，不再弹修复提示
    - 若当前实例尚未成功加载过，则仍沿用原有修复提示逻辑

## 8. 验证建议
- [ ] 冷启动后，在设备页 WebView 首次成功加载前触发原始异常场景，修复提示仍可按预期弹出
- [ ] 设备页 WebView 成功加载一次后，在同一页面内连续按 `F5`，不再重复弹出修复提示
- [ ] 即使后续继续出现 `wxWEBVIEW_NAV_ERR_CONNECTION` 等错误，也只记录日志，不再弹出修复提示
- [ ] `F5` 页面刷新能力保持不变
- [ ] 若先进入 `webview_single_process` 降级分支，原有重启逻辑保持不变
- [ ] 关闭设备页并重新创建新实例后，重新按上述规则判断

## 9. 调查记录
- ZenTao 页面关键信息:
  - 状态: `激活`
  - 严重程度: `严重`
  - 优先级: `高`
  - 影响版本: `SanityPrint_7.1.0.4276_Beta`
- ZenTao 复现描述:
  - `在设备列表页一直按F5时，会触发Webview2的弹窗，该弹窗应该控制在软件启动时才弹`
- ZenTao 页面中显示的解决方案关联到了 `#15226`，但标题内容与本问题不一致，不能直接视为 `15214` 的真实修复说明。

## 10. 风险与回滚
- 回滚方式:
  - 删除 `PrinterMgrView` 中的 `m_webview_loaded_successfully`
  - 删除 `PrinterMgrView::OnLoaded()` 中的成功标记
  - 将 `PrinterMgrView::OnError()` 中的修复提示判断恢复为原逻辑
- 风险等级: `低`
- 需关注的副作用:
  - 同一个 `PrinterMgrView` 实例一旦成功加载过，后续新的真实运行时异常也会被静默抑制
  - 这类后续异常需要依赖日志或其他非阻塞提示辅助排查

## 11. 后续建议
- 可考虑在首次弹窗后，后续重复命中时增加非模态提示或日志上报，避免完全静默。
- 如果后续需要更精细控制，可继续区分“启动阶段异常”和“页面运行阶段异常”，分别做会话级去重。
