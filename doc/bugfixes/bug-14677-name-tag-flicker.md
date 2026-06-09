# Bug 修复说明

## 1. 基本信息

- Bug ID: `14677`
- 标题: `【布尔】布尔点击重置时，左下角的名称标签会闪屏。建议优化`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-14677.html`
- 创建时间: `2026-01-19 11:36:01`
- 修复日期: `2026-02-27`
- 反馈人: `康美樱`
- 处理人: `钟轩`
- 所属产品/模块: `Sanity Print` / `准备页面`
- 所属项目/执行: `Sanity Print` / `CP7.1 布尔新功能移植`
- Bug 类型: `代码错误`
- 严重程度: `一般`
- 优先级: `低`
- Bug 状态: `激活`（是否确认: `未确认`）
- 分支/提交: `feature/new_bool` / `e40e1cac9`

## 2. 问题现象

- 深色模式下，在布尔工具点击“重置”后，左下角弹出的对象名称标签/模型信息弹窗会出现首帧浅色闪烁（闪一下再恢复深色）。
- 影响: 深色模式下 UI 观感不一致，体验较差。

## 3. 影响范围

- 模块: `Notification / ObjectInfoNotification`（由 `Mesh Boolean` 重置流程触发）
- 关键文件:
  - `src/slic3r/GUI/NotificationManager.cpp`
- 受影响流程:
  - 进入布尔 -> 点击重置 -> 左下角名称标签弹窗渲染

## 4. 复现步骤（修复前）

1. 切换到深色模式。
2. 进入布尔工具。
3. 点击重置按钮。
4. 观察左下角对象名称标签弹窗：可能先闪现一帧浅色样式，再变为深色样式。

禅道原始描述（重现步骤）：

- 【前置条件】无
- 【步骤】进入布尔 -> 点击重置按钮 -> 观察左下角对象名称的标签
- 【结果】左下角标签闪屏
- 【期望】左下角标签不闪屏

## 5. 根因分析

- `ObjectInfoNotification::render(...)` 在绘制时使用成员变量 `m_is_dark` 来决定背景色与文字色（深/浅色分支）。
- 但 `m_is_dark` 的获取是延迟初始化：首次在 `PopNotification::ensure_ui_inited()` 内通过 `GLCanvas3D::get_dark_mode_status()` 读取并缓存。
- 对于新创建的对象信息通知，如果首帧绘制前未先调用 `ensure_ui_inited()`，`m_is_dark` 仍是默认值（走浅色分支），于是首帧按浅色渲染；下一帧初始化完成后按深色渲染，形成“闪一下”的现象。

## 6. 修复方案

- 在 `ObjectInfoNotification::render(...)` 入口处先调用 `ensure_ui_inited()`，保证首帧绘制前就拿到正确的深/浅色模式状态。

## 7. 代码改动摘要

- 文件: `src/slic3r/GUI/NotificationManager.cpp`
  - `NotificationManager::ObjectInfoNotification::render(...)`
    - 增加 `ensure_ui_inited();`，使 `m_is_dark` 在首帧渲染前完成初始化，避免首帧走浅色分支导致闪烁。

## 8. 验证清单

- [ ] 深色模式下：进入布尔并点击重置，左下角名称标签不再闪烁。
- [ ] 浅色模式下：左下角名称标签显示不受影响。
- [ ] 深/浅色切换后再次触发弹窗：首帧颜色与当前模式一致。

## 9. 风险与回滚

- 风险等级: `低`（仅提前初始化通知的 UI 状态，不改变布局与交互）。
- 回滚方式: 回退 `src/slic3r/GUI/NotificationManager.cpp` 中 `ObjectInfoNotification::render(...)` 的 `ensure_ui_inited()` 调用。

## 10. 后续建议

- 若后续新增其他通知类型在首帧也依赖 `m_is_dark`（或 `m_WindowRadius`），建议在对应 `render(...)` 入口统一调用 `ensure_ui_inited()`，避免同类首帧闪烁问题再次出现。
