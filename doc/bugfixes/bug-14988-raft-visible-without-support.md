# Bug 修复记录

## 1. 基本信息
- Bug ID: `14988`
- 标题: `【用户反馈】筏层显示问题`
- 日期: `2026-02-27`
- 提交人: `康美樱`
- 处理人: `贺淼`
- 分支/提交:
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-14988.html`

## 2. 现象
- 在准备页面中，未开启支撑时，筏层选项会置灰不可用。
- 用户只能先开启支撑，才能配置筏层。
- 影响: UI 将“筏层”和“支撑”错误绑定，和预期的独立控制不一致。

## 3. 影响范围
- 模块: `准备页面 / 支撑与筏层参数`
- 产品: `Sanity Print`
- 计划: `CP 7.1.0 Release`
- 关键文件:
  - `src/slic3r/GUI/ConfigManipulation.cpp`

## 4. 修复前复现步骤
1. 进入准备页面。
2. 保持支撑为关闭状态。
3. 尝试配置筏层参数。
4. 结果: 筏层为置灰状态，必须先开启支撑才可用。

## 5. 根因分析
- 在 `ConfigManipulation::toggle_print_fff_options(...)` 中，`have_support_material` 作为一组字段的显隐/可编辑开关。
- `raft_layers` 被放在这组“支撑相关字段”里。
- 当 `enable_support=false` 且 `raft_layers=0` 时，`have_support_material=false`，导致 `raft_layers` 本身也被隐藏/禁用，用户无法从 UI 独立开启筏层。

## 6. 修复策略
- 将 `raft_layers` 从支撑联动的字段组中解耦。
- 支撑专属字段继续由 `have_support_material` 控制。
- 保持 `raft_layers` 可用，使用户在不启用支撑时也能单独开启筏层。

## 7. 代码变更摘要
- 文件: `src/slic3r/GUI/ConfigManipulation.cpp`
  - 在 `toggle_field(el, have_support_material)` 循环字段列表中移除 `"raft_layers"`。
  - 避免筏层入口受支撑状态联动而不可用。

## 8. 验证清单
- [ ] 支撑关闭 + `raft_layers=0` 时，筏层选项仍可见、可编辑。
- [ ] 在支撑关闭状态下可直接启用筏层，并正常展示相关参数。
- [ ] 支撑开启路径下，原有支撑字段显隐逻辑保持不变。
- [ ] `tree` / `organic` 等支撑样式切换逻辑无回归。

## 9. 禅道状态快照（修复时）
- 状态: `激活`
- Bug 类型: `代码错误`
- 严重程度: `严重`
- 优先级: `中`
- 所属模块: `准备页面`
- 指派给: `贺淼`（`2026-02-05 20:37:16`）
- 创建时间: `2026-02-05 20:37:16`
- 所属执行: `CP 6-7.x 外部反馈`

## 10. 回滚与风险
- 回滚方式: 在 `ConfigManipulation.cpp` 中将 `"raft_layers"` 加回 `have_support_material` 联动字段列表。
- 风险等级: 低（单字段 UI 联动调整）。
- 关注点:
  - 历史流程中若依赖“支撑关闭时隐藏筏层”的行为，需确认是否受影响。
  - 准备页面字段联动顺序是否存在边界显示问题。

## 11. 后续建议
- 建议将“支撑参数联动”和“筏层参数联动”拆分为明确的独立逻辑，避免后续再次出现耦合回归。
