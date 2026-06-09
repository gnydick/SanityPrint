# Bug 修复记录

## 1. 基本信息
- Bug ID: `15053`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15053.html`
- 标题: `预览页面缺少冲刷的显示，导致预估总量对应不上`
- 首次处理日期: `2026-03-06`
- 文档更新日期: `2026-03-10`
- 所属产品: `Sanity Print`
- 所属模块: `切片预览`
- Bug 类型: `代码错误`
- 严重程度: `一般`
- 当前状态: `激活`
- 是否确认: `已确认`
- 指派给: `钟轩（2026-03-09 10:04:39）`
- 截止日期: `2026-02-26（已延期）`
- 所属执行: `CP7.1.0 260330`
- 禅道补充要求（2026-03-09 10:04:39）: `加上时间和百分比，在 空驶 上面`

## 2. 问题现象
- 预览页左侧 `Line Type` 列表中缺少独立的 `Flushed` 展示，导致“分项和总量”不易对齐核对。
- 禅道激活后补充要求：`Flushed` 需显示时间与百分比，并按指定顺序与 `Travel` 展示。
- 用户侧感知问题是：冲刷统计口径不透明、与总量对应关系不清晰。

## 3. 禅道重现信息
- `[前置条件]` 进入预览页，存在冲刷耗材（如多色/换料场景）。
- `[步骤]` 查看预览页面左侧“线型/耗材”相关显示项。
- `[结果]` 缺少冲刷项，或冲刷项信息不完整（无时间/百分比）。
- `[期望]` 预览页显示冲刷项，并包含时间、百分比、长度、重量，顺序符合产品要求。

## 4. 影响范围
- 模块: `GCode 预览线型图例（Line Type Legend）`
- 关键文件:
  - `src/slic3r/GUI/GCodeViewer.cpp`
- 受影响流程:
  - 预览页线型统计展示
  - 冲刷耗材核对流程

## 5. 根因分析
- 早期实现中，`Line Type` 仅展示 `Travel` 相关时间/占比，冲刷统计未独立成行。
- 冲刷总量数据已存在（`total_flushed_filament_m/g`），但 UI 渲染层未完整输出。
- 补充需求加入后，需要同时满足两点：
  - 冲刷展示 `Time / Percent / Length / Weight`。
  - 冲刷与空驶的上下顺序符合产品侧预期。
- 该区域行渲染存在“代码追加顺序与视觉顺序不完全一致”的特性，导致顺序调整时容易出现“看起来相反”。

## 6. 修复策略
- 在线型图例中新增并保留独立 `Flushed` 行。
- 为 `Flushed` 行补充时间和百分比：
  - 时间: `flush_time = total_time - sum(role_times) - sum(move_times)`，并做 `max(0, value)` 保护。
  - 百分比: 优先显示时间占比；若时间占比不可用，则回退显示耗材占比。
- 保留冲刷长度、重量显示，并绑定 `erWipeTower` 可见性开关。
- 调整 `Travel / Flushed` 追加顺序，使最终界面顺序与产品要求一致（当前目标为 `Travel` 在上、`Flushed` 在下）。

## 7. 代码改动摘要
- 文件: `src/slic3r/GUI/GCodeViewer.cpp`
  - `showColorHeader(...)`
    - 增加 `Flushed` 行数据准备：`flush_time_str`、`flush_time_percent_str`、`flush_percent_str`、`flush_length_str`、`flush_weight_str`。
    - 百分比显示逻辑：`flush_time_percent_str.empty() ? flush_percent_str : flush_time_percent_str`。
  - `showColorTable(...)`
    - 新增 `Flushed` 行渲染，包含 `Time / Percent / Length / Weight`。
    - 通过 `append_item(...)` 与 `m_extrusions.role_visibility_flags` 控制 `erWipeTower` 显隐。
    - 调整 `Travel` 与 `Flushed` 的追加顺序，修正界面显示顺序。
  - 成员字段
    - 新增/保留：`flush_label`、`flush_time_str`、`flush_percent_str`、`flush_time_percent_str`、`flush_length_str`、`flush_weight_str`、`has_flush_data`。

## 8. 验证清单
- [ ] 含冲刷的模型在预览页可看到 `Flushed` 行。
- [ ] `Flushed` 行显示 `Time / Percent / Length / Weight` 四类信息。
- [ ] `Flushed` 的 `Percent` 列优先显示时间占比，时间不可用时回退耗材占比。
- [ ] `Flushed` 行显示的长度/重量与统计总量口径一致。
- [ ] 列表顺序符合产品要求（当前口径：`Travel` 在 `Flushed` 上方）。
- [ ] 点击 `Flushed` 行可正确切换冲刷显示，不影响其他线型。
- [ ] 无冲刷数据时不显示 `Flushed` 行（或显示为 0 的产品预期需确认）。

## 9. 风险与回滚
- 风险等级: `低`（局部 UI 展示、顺序和统计文案调整）。
- 主要风险:
  - `Flushed` 与 `Travel/Wipe/Tower` 的口径理解差异，需与产品文案保持一致。
  - 边界场景（冲刷极小、总时长极小）下百分比显示可能出现 `<0.1%`，需确认是否符合预期。
  - 若后续再次调整顺序，需同时验证“代码顺序”和“视觉顺序”一致性。
- 回滚方案:
  - 回滚 `GCodeViewer.cpp` 中 `Flushed` 行时间/百分比渲染与顺序调整相关改动。

## 10. 后续建议
- 增加预览统计回归用例，覆盖“多色+冲刷”场景。
- 在产品说明中明确 `Travel / Wipe / Flushed / Tower` 的定义、显示顺序和统计口径。
- 对 `Line Type` 列表增加 UI 自动化快照，避免顺序类问题反复回归。
