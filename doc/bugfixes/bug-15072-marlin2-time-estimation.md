# Bug 修复记录（15072）

## 基本信息
- Bug ID：`15072`
- 禅道链接：`https://zentao.creality.com/zentao/bug-view-15072.html`
- 标题（禅道）：`模型时间原为2h，将稀疏填充密度改为10%后，预估时间变成了10h，而且大部分时间都是空驶`
- 影响模块：`GCode 时间预估 / M204 加速度解析`
- 所属产品/模块（禅道）：`Sanity Print / 切片预览`
- 严重程度/优先级（禅道）：`严重 / 高`
- 当前状态/是否确认（禅道）：`已解决 / 已确认`
- 指派给（禅道）：`利浩贤`
- 创建时间/创建人（禅道）：`2026-02-27 16:58:18 / 李佳沁`
- 关联计划（禅道）：`CP 7.1.0 Release`
- 关联分支：`release-260330`
- 关联提交：`e1a3b3490`（`fix:[#15072 ] 修复Marlin2 GCode风格下的时间预估Bug。`）
- 修复日期：`2026-03-09`

## 问题现象
- 模型切片后的预估时间原本约 `2h`；将 `稀疏填充密度` 改为 `10%` 后，预估时间异常变为 `10h`，且绝大部分时间被统计为空驶（Travel）。
- 影响范围：主要影响旧机器/旧固件对应的 `Marlin2 GCode 风格`；`Klipper GCode 风格`不受影响（禅道备注）。

## 复现步骤
1. 选择 `Marlin2 GCode 风格`（旧机器）对应的打印机/配置进行切片。
2. 保持其他参数不变，先切片一次，记录预估时间约 `2h`。
3. 将 `稀疏填充密度` 修改为 `10%`，再次切片。
4. 观察预估时间异常变为约 `10h`，并且时间构成中 Travel 占比异常偏大（空驶时间异常）。

## 根因分析
- `src/libslic3r/GCode/GCodeProcessor.cpp` 中 `GCodeProcessor::process_M204(...)` 需要同时兼容两类 `M204` 变体：
  - Legacy：`M204 S<accel> [T<retract_accel>]`
  - Upstream Marlin：`M204 [P<print_accel>] [T<travel_accel>] [R<retract_accel>]`
- 修复前 `M204` 解析存在变量复用导致的未定义/不稳定行为（禅道备注与代码一致）：
  - 解析过程中复用同一个临时变量，导致解析 `M204 T...` 时可能误用上一次解析到的 `P` 值（或旧值/随机值）；
  - 进而出现“读到 `T` 却拿 `P`（或未定义值）去生效”的情况，Travel/Print 加速度链条不稳定，最终导致时间预估出现异常飙升（表现为空驶时间异常变大）。

## 修复方案
- 重构 `process_M204(...)` 的解析逻辑，明确区分并正确处理两类格式，并采用“增量式更新”避免互相覆盖：
  - 若存在 `S`：按 legacy 格式处理（`S` 同时用于 acceleration/deceleration/travel；可选 `T` 作为 retract acceleration），并更新 `m_max_accel` 与 junction deviation。
  - 否则：按 Upstream Marlin 格式分别解析 `P/R/T`，仅在参数存在时更新对应加速度；当 `P` 与 `T` 同时存在且有效时，用 `min(P, T)` 更新 `m_max_accel` 并重新计算 junction deviation。
- 使用独立变量与 `has_*` 标记，避免变量复用带来的歧义与潜在旧值污染。

## 代码改动摘要
- `src/libslic3r/GCode/GCodeProcessor.cpp`
  - 重写 `GCodeProcessor::process_M204(...)` 的参数解析与赋值流程。
  - 修正 Upstream Marlin 格式下 `T`（travel acceleration）应写入 travel acceleration 的逻辑。
  - 修正 `m_max_accel`/`m_requested_accel_to_decel` 更新条件与取值来源，避免受到不相关参数影响。

## 验证清单
- [ ] 按“复现步骤”在 `Marlin2 GCode 风格`下验证：修改稀疏填充密度后预估时间不再异常飙升。
- [ ] 导入包含 `M204 P/T/R` 的 GCode：时间预估不出现异常大/小值或明显跳变。
- [ ] Legacy `M204 S...`（含/不含 `T`）场景：时间预估与既有版本一致或符合预期。
- [ ] 覆盖 `M204` 参数组合边界：仅有 `P`、仅有 `T`、仅有 `R`、`P+T`、`P+R`、`T+R`、`P+T+R`。
- [ ] `Klipper GCode 风格`下时间预估口径不受影响（回归验证）。

## 风险与回退
- 风险：
  - 行为变更集中在 `M204` 解析，可能改变部分使用 Upstream Marlin 格式 GCode 的时间预估结果（属于预期修正，但需关注与产品口径/实际打印的偏差）。
  - 若存在非标准固件/后处理脚本对 `M204` 参数语义有差异，可能引发边界兼容问题。
- 回退：
  - 回退提交 `e1a3b3490`（或回退 `src/libslic3r/GCode/GCodeProcessor.cpp` 中 `process_M204(...)` 的改动）。

## 备注
- 禅道历史备注摘录：
  - `2026-02-27 17:08:22, 由 利浩贤 解决（已解决）`：
    - 仅影响旧机器（Marlin2 GCode 风格），Klipper 风格不受影响；根因是 `M204` 指令解析有误导致未定义行为；修复为将 `P/T/R/S` 分开存并增量式更新，避免解析 `T` 时误用 `P/旧值/随机值`，使估时加速度链条稳定。
  - `2026-03-05 09:40:45, 由 郭锋兴 添加备注`：客诉链接（飞书记录）已附在禅道备注中。
