# Bug 修复记录

## 1. 基本信息
- Bug ID: `14588`
- 标题: `[fluidd] 发送提示页面中发送进度条与需求设计不符`
- 日期: `2026-03-09`
- 所属产品: `Sanity Print`
- 所属模块: `切片预览`
- 所属计划: `CP 7.1.0 Release`
- Bug 类型: `代码错误`
- 严重程度: `一般`
- 优先级: `中`
- 当前状态: `激活`
- 指派给: `王昭`
- 创建人/创建时间: `檀献祖 / 2026-01-12 19:15:16`

## 2. 现象描述
- Fluidd 发送提示卡片中的上传进度条样式与需求设计不一致。
- 现场表现为进度条可见性差/显示位置异常，和设计稿中的细长条进度提示不一致。

## 3. 影响范围
- 模块: `通知卡片（上传进度提示）`
- 关键文件:
  - `src/slic3r/GUI/NotificationManager.cpp`
- 影响流程:
  - 文件上传时左下角（或侧边）上传通知卡片的进度显示

## 4. 复现路径（修复前）
1. 在 Fluidd 发送流程中触发上传。
2. 观察上传通知卡片进度区域。
3. 进度条样式/位置与设计不符，视觉反馈不稳定。

## 5. 根因分析
- `PrintHostUploadNotification::render_bar(...)` 复用父类 `ProgressBarNotification::render_bar(...)` 时，传入的 `win_size_x/win_size_y/win_pos_x/win_pos_y` 与当前 ImGui 实际窗口坐标系存在偏差风险。
- 在不同布局/缩放下，进度线可能出现错位或观感异常，导致“与设计不符”。
- 原父类颜色计算采用 `ImVec4 + IM_COL32` 手工转换，样式统一性和可读性不佳。

## 6. 修复策略
- 在 `PrintHostUploadNotification::render_bar(...)` 中，改为使用当前窗口的真实几何信息（`ImGui::GetWindowPos/Size`）推导父类绘制参数，再调用父类进度渲染。
- 在 `ProgressBarNotification::render_bar(...)` 中统一使用 `ImColor`（含淡出透明度）绘制背景线和前景线，提升样式一致性与可维护性。

## 7. 代码改动摘要
- 文件: `src/slic3r/GUI/NotificationManager.cpp`
  - `ProgressBarNotification::render_bar(...)`
    - 新增 `alpha` 统一透明度计算。
    - 使用 `ImColor bg_color/fg_color` 替代原 `IM_COL32` 手工拼色。
    - 保留原线形进度逻辑，改进配色稳定性。
  - `PrintHostUploadNotification::render_bar(...)`
    - `PB_PROGRESS` 分支中改为基于 `ImGui::GetWindowPos/Size` 的窗口实际坐标计算。
    - 使用父窗口尺寸/位置参数调用 `ProgressBarNotification::render_bar(...)`，避免坐标不一致导致的显示异常。

## 8. 验证清单
- [ ] Fluidd 上传通知卡片中，进度条在上传过程中稳定可见。
- [ ] 进度条随百分比增长连续变化，无跳变或错位。
- [ ] 暗色/亮色主题下进度条对比度符合预期。
- [ ] 上传完成、失败、取消状态切换时，进度区域显示正常。
- [ ] 不影响其他复用 `ProgressBarNotification` 的通知类型。

## 9. 回滚 / 风险
- 回滚点: `src/slic3r/GUI/NotificationManager.cpp` 中上述两处改动。
- 风险等级: `低`
- 关注点:
  - 其他通知类型对 `ProgressBarNotification::render_bar(...)` 的样式依赖。
  - 不同 DPI / 缩放下的进度线位置一致性。


