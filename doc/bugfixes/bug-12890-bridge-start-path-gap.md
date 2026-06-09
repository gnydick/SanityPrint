# Bug 修复说明

## 1. 基本信息
- Bug ID: `12890`
- 标题: `【用户反馈】打印桥接时的起始路径规划有问题，导致出现一条间隙`
- 创建人: `江圣龙`
- 指派给: `王文彬`
- 所属产品: `C3DSlicer / SanityPrint`
- 修复日期: `2026-03-05`

## 2. 问题现象
- 同一模型对比 Bambu 与 C3DSlicer 生成结果时，C3DSlicer 在“悬空墙(Overhang wall)”与“桥接(Bridge)”交界处出现可见缝隙。
- Bambu 对应位置无缝或重叠连接。
- 该问题主要体现在桥接起始路径与边界的相位关系上。

## 3. 根因分析
- 代码路径: `src/libslic3r/Fill/Fill.cpp` -> `src/libslic3r/Fill/FillRectilinear.cpp`。
- C3DSlicer 在 external bridge 且 density>99% 时，存在如下逻辑:
  - `params.density = bridge_density`
  - `params.dont_adjust = true`
- 当 `params.dont_adjust = true` 后，full infill/bridge 不再走 spacing adjust 分支，而进入 `align_to_grid` 相位路径，导致桥接首线相对边界发生偏移。
- 该偏移在特定几何下会形成“桥接与悬空墙之间的缝隙”。

## 4. 方案对比

### 方案A: 解耦优化（推荐型）
- 说明:
  - 保留 `bridge_density`。
  - 仅对 full bridge（约100%）禁止 `dont_adjust` 触发，或按条件精细控制。
- 效果:
  - 可定点修复缝隙，同时对部分低密度桥接保留旧行为。
- 影响:
  - 逻辑复杂度更高，需要更多回归样例覆盖边界条件。

### 方案B: 最小方案（本次采用）
- 说明:
  - 删除 external bridge 分支中的 `params.dont_adjust = true;`。
  - 保留 `params.density = bridge_density`。
- 效果:
  - 桥接 full infill 可继续参与 spacing adjust，避免被强制切到 `align_to_grid` 相位路径。
  - 与 Bambu 行为更一致，问题模型缝隙风险明显降低。
- 影响:
  - 改动最小，行为更接近 Bambu。
  - 可能导致部分模型桥接线分布与旧版本略有差异（通常为轻微变化）。

## 5. 选择本方案的原因
- 用户期望与 Bambu 结果一致，且对同模型对比最直观。
- 方案B改动面最小、可控性高，便于快速修复和验证。
- 删除 `params.dont_adjust = true;` 不会导致用户参数失效:
  - `bridge_density` 仍然生效。
  - 变化仅在内部路径规划策略（spacing adjust vs align_to_grid）上。

## 6. 实际代码改动
- 文件: `src/libslic3r/Fill/Fill.cpp`
- 变更点:
  - 在 external bridge 分支中删除:
    - `params.dont_adjust = true;`
- 保留:
  - `params.density = layerm->region().config().bridge_density.get_abs_value(1.0);`

## 7. 影响评估
- 参数影响:
  - `bridge_density`、`bridge_speed`、`bridge_flow` 等用户参数仍可正常生效。
  - 无参数被删除或失效。
- 打印路径影响:
  - 主要影响桥接起始线与边界贴合方式。
  - 目标是减少“悬空墙与桥接之间的可见缝隙”。
- 风险等级: `低-中`
  - 低: 改动单点且逻辑清晰。
  - 中: 桥接细节路径在个别模型上可能与旧版本略有差异。

## 8. 验证建议
- 回归以下模型场景:
  1. 典型桥接+悬空墙接壤模型（问题复现模型）。
  2. 长桥、短桥、不同桥接角度模型。
  3. `bridge_density` = 100%、80%、60% 的对比切片。
- 关注指标:
  - 是否消除/减少起始缝隙。
  - 耗材、时间、桥接面质量是否无异常回退。

## 9. 回滚方案
- 若出现不可接受的回归，可回滚 `src/libslic3r/Fill/Fill.cpp` 对 `params.dont_adjust` 的本次删除。
