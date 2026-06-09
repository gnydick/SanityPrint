# Bug 修复记录

## 1. 基本信息
- Bug ID: `13645`
- 标题: `特殊模型切片进度卡在 25%（Generating infill regions）`
- 创建人: `杨艳虹`
- 指派给: `王文彬`
- 所属产品: `C3DSlicer / SanityPrint`
- 修复日期: `2026-03-05`

## 2. 问题现象
- 使用旧工程文件 `40-15-0.4-LQL-14(2) (1).3mf` 切片时，进度长期停留在 `25%`。
- 界面文案为 `正在生成填充区域`，对应内部阶段 `Generating infill regions`。
- 表象像“卡死”，但进程仍在运行并持续占用 CPU。

## 3. 根因分析
- 卡点位于 `PrintObject::detect_surfaces_type()` 中的 small crack refinement（小裂缝精修）逻辑。
- 该逻辑在模型碎片化严重的层上，会触发大量几何布尔运算（`diff_ex / offset_ex`）。
- 原逻辑对每个 `crack` 都可能遍历大量 `bottom surfaces`，复杂度近似 `O(cracks * bottom_surfaces)`，在病态输入下耗时异常放大，导致 25% 长时间不前进。

## 4. 修复方案
- 在 `detect_surfaces_type()` 中增加病态保护：
  - 计算 `cracks.size() * bottom.size()` 的估算工作量。
  - 当工作量超过阈值 `300000` 时，跳过 small crack refinement，仅保留后续主流程。
- 代码位置：`src/libslic3r/PrintObject.cpp`。
- 本次提交已清理诊断日志，仅保留修复逻辑。

## 5. 改动说明
- 修改文件：`src/libslic3r/PrintObject.cpp`
- 核心改动：
  - 新增常量 `kMaxSmallCrackRefineWork = 300000`
  - 新增条件 `run_small_crack_refine`
  - 将原 `if (lower_layer)` 改为 `if (run_small_crack_refine)`

## 6. 影响评估
- 正向影响：
  - 避免病态模型在 25% 阶段长时间卡住，切片可继续并完成。
- 功能影响：
  - 仅在超高复杂度层触发保护。
  - 可能导致极小裂缝区域的 top/bottom 分类与原精修路径略有差异，但不影响切片主流程和 gcode 生成。
- 性能影响：
  - 在病态输入下显著改善耗时；普通模型基本不受影响。

## 7. 验证结果
- 使用问题工程 `40-15-0.4-LQL-14(2) (1).3mf` 复测：
  - 切片不再卡在 25%。
  - 能继续到后续阶段并成功结束（`PROCESS_EXITED:0`）。
- 编译验证：
  - `cmake --build build_Release --config Release --target SanityPrint_Slicer -- /m` 通过。

## 8. 回归建议
- 回归以下场景：
  - 普通单色模型（确保行为无变化）。
  - 大量薄壁/细碎几何模型（确认不再出现 25% 长卡）。
  - 对 top/bottom 质量敏感的模型（观察是否有可感知差异）。
