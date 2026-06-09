# Bug 修复记录

## 1. 基本信息
- Bug ID: `15149`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15149.html`
- 标题: `高分屏点击进入首页时，首页图标自动变小与模型名称只显示一半`
- 日期: `2026-03-09`
- 所属产品: `Sanity Print`
- 所属模块: `准备页面`
- 所属计划: `CP 7.1.0 Release`
- Bug 类型: `代码错误`
- 严重程度: `一般`
- 优先级: `中`
- 当前状态: `激活`
- 是否确认: `未确认`
- 指派给: `钟轩（2026-03-04 13:48:45）`
- 截止日期: `2026-03-13`
- 所属执行: `CP7.1.0 260330`

## 2. 问题现象
- 高分屏环境下点击进入首页后，左上角首页图标出现缩小。
- 顶部模型名称显示区域异常，文本被截断为仅显示一半。

## 3. 禅道重现信息
- `[步骤]` 高分屏点击进入首页时，首页图标自动变小与模型名称只显示一半。
- `[结果]` 首页图标和模型名称显示异常。
- `[期望]` 显示正常。

## 4. 影响范围
- 模块: `Topbar 顶部工具栏`
- 关键文件:
  - `src/slic3r/GUI/BBLTopbar.cpp`
- 受影响流程:
  - 首页入口图标显示
  - 顶部模型名称显示
  - 窗口缩放/分辨率缩放下的标题栏布局

## 5. 根因分析
- Logo 位图在部分路径中未统一使用窗口上下文进行缩放，导致高 DPI 场景下进入首页后图标尺寸不稳定。
- 模型名称在 `SetTitle(...)` 中直接按固定宽度截断，且未统一走窗口变化后的刷新路径，导致部分场景显示不完整。
- 标题控件与工具栏项存在双通道更新（Label/ToolBarItem），增加了显示不一致风险。

## 6. 修复策略
- 统一首页 Logo 与 Hover Logo 的位图缩放上下文（`this`），并在 `Rescale` 中同步更新。
- 标题显示统一走 `UpdateFileNameDisplay()`，由同一函数完成文本截断与 tooltip 更新。
- 保持标题区固定宽度（`TOPBAR_TITLE_WIDTH`），避免影响中间导航与右侧窗口按钮布局。
- 在 `SetTitle`、`Rescale`、`OnWindowResize` 场景统一触发标题刷新，保证显示一致。

## 7. 代码改动摘要
- 文件: `src/slic3r/GUI/BBLTopbar.cpp`
  - 初始化 Logo 时使用 `create_scaled_bitmap("logo", this, 20)` 与对应 hover 位图。
  - `Rescale(...)` 中补充 `ID_LOGO` 的 hover 位图更新。
  - `SetTitle(...)` 简化为更新 `m_displayName` 后统一调用 `UpdateFileNameDisplay()`。
  - `UpdateFileNameDisplay()`（无参）恢复为调用 `UpdateFileNameDisplay(m_displayName)`。
  - `UpdateFileNameDisplay(const wxString&)` 采用固定标题宽度并统一设置标题文本与 tooltip。
  - `TruncateTextToWidth(...)` 通过 `wxClientDC` 实测文本宽度并执行省略。
  - `OnWindowResize(...)` 中调用 `UpdateFileNameDisplay()` 保证窗口变更时同步刷新标题。

## 8. 验证清单
- [ ] Windows 缩放 125%/150%/175%/200% 下进入首页，Logo 尺寸一致且不突变。
- [ ] 首页切换后模型名称完整显示；空间不足时以省略号正确截断。
- [ ] 拖拽窗口尺寸（放大/缩小）过程中，中间导航按钮与右侧窗口按钮位置稳定。
- [ ] 多次切换首页/准备/预览标签后，顶部栏图标与标题显示无回归。
- [ ] 亮色/暗色模式下 Topbar 显示与交互正常。

## 9. 风险与回滚
- 风险等级: `低`（局部图标缩放与标题刷新路径统一）。
- 主要风险:
  - 固定标题宽度下，超长文件名展示长度受限（通过 tooltip 查看全名）。
- 回滚方案:
  - 回滚 `BBLTopbar.cpp` 中本次 Logo 缩放上下文与标题统一刷新路径修改。

## 10. 后续建议
- 增加 Topbar 回归用例：覆盖 DPI 切换、窗口拖拽缩放、跨显示器移动。
- 如后续确需增强标题可见长度，建议在不改变导航居中的前提下单独评估标题宽度策略。
