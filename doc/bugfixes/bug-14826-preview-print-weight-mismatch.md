# Bug 修复记录（14826）

## 基本信息
- Bug ID：`14826`
- 标题：`附件Gcode，在预览页面显示的耗材重量与打印过程中显示的耗材重量不一致`
- 创建人：`冷金辉`
- 指派给：`王文彬`
- 修复分支：`release-260330`
- 修复日期：`2026-03-03`

## 问题现象
- 预览页面显示的耗材重量与打印过程中/导出 G-code 尾部统计不一致。
- 典型表现为：总量可能对齐，但分喷嘴数据不一致；或不同页面使用的总重量口径不一致。

## 根因分析
- 统计口径不统一：部分路径使用 `total_volumes_per_extruder`，部分路径再额外叠加 `flush_per_filament`，导致重复或遗漏。
- G-code 后处理阶段会重写 `; filament used ...` 注释，若口径不一致会覆盖前面已计算的结果。
- `flush_multiplier` 与换料体积矩阵在异常场景下缺少默认值/边界保护，存在崩溃风险。

## 修复方案
- 统一统计口径：以 `total_volumes_per_extruder` 作为最终来源。
- 在 `GCodeProcessor::calculateVolume()` 中，将 legacy 路径下计算出的冲刷体积同步并入 `total_volumes_per_extruder`。
- 移除上层重复叠加 `flush_per_filament` 的逻辑，避免双计。
- 强化稳健性：
  - `flush_multiplier` 默认初始化为 `1.0f`。
  - `reset()` 时清理 `tool_change_path/tool_change_volumes_map/flush_multiplier/creality_flush_time`。
  - `calculateVolume()` 增加索引边界与非法值保护。
- 后处理 `run_post_process()` 按统一口径重写 `filament used [mm/cm3/g]` 与 `filament cost`。

## 代码改动摘要
- `src/libslic3r/GCode/GCodeProcessor.hpp`
  - `flush_multiplier` 默认值初始化为 `1.0f`。
- `src/libslic3r/GCode/GCodeProcessor.cpp`
  - `GCodeProcessorResult::reset()` 增加关键字段复位。
  - `calculateVolume()` 增加边界保护，并将冲刷体积并入 `total_volumes_per_extruder`。
  - `run_post_process()` 统一基于 `total_volumes_per_extruder` 重写耗材统计。
- `src/libslic3r/GCode.cpp`
  - 去除对 `flush_per_filament` 的二次叠加，统一按 `total_volumes_per_extruder` 汇总。
- `src/slic3r/GUI/Plater.cpp`
  - 去除加载 G-code 场景下对 `flush_per_filament` 的重复累加。
- `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
  - 发送打印耗材长度改为直接使用 `total_volumes_per_extruder`，移除 `total + flush` 双加。

## 影响范围
- G-code 导出尾部统计：
  - `; filament used [mm/cm3/g]`
  - `; filament cost`
  - `; total filament cost`
- 预览页与仅 G-code 模式重量/长度统计。
- 发送打印面板的耗材长度展示。

## 验证结果
- 编译验证：
  - `cmake --build build_Release --config Release --target SanityPrint_Slicer` 通过。
  - `cmake --install build_Release --config Release` 通过。
- 结果验证：
  - 新生成 G-code 的分喷嘴 `filament used [g]` 与预览界面“总计”一致。
  - `total filament cost` 与页面成本一致。

## 风险与说明
- 该修复聚焦“统计口径统一”，不改变 G-code 实际路径生成逻辑。
- 仍建议后续补充回归用例（legacy/new 多色、含/不含擦拭塔、仅 G-code 模式）。
