# Bug 修复记录

## 1. 基本信息
- Bug ID: `15445`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15445.html`
- 标题: `切片用两种耗材，gcode里default_filament_colour有三种耗材`
- 文档日期: `2026-04-02`
- 所属产品: `Sanity Print`
- 所属模块: `其它`
- 所属计划: `CP 7.1.1（0430）`
- 所属执行: `CP7.1.0 260330`
- Bug 类型: `代码错误`
- 严重程度: `严重`
- 当前状态: `激活`
- 创建人: `李佳沁（2026-03-20 16:04:55）`
- 指派给: `王昭（2026-03-20 16:04:56）`
- 截止日期: `2026-03-31（截图显示已延期 2 天）`
- 历史记录:
  - `2026-03-20 16:04:56` 由 `李佳沁` 创建并指派给 `王昭`
  - `2026-03-25 19:17:07` 由 `蔡树东` 关联到计划 `CP 7.1.1（0430）`

## 2. 问题现象
- 实际切片只使用了 `2` 种耗材，但导出的 G-code 尾部 `CONFIG_BLOCK` 中，`default_filament_colour` 出现了 `3` 个值。
- 问题场景中，有模型被涂成了其他颜色，导致预览里看到的颜色集合与“默认耗材颜色”不是同一口径。
- 下游在解析 `; default_filament_colour = ...` 时会把这 `3` 个值都当作默认耗材颜色，造成颜色/耗材映射异常。

## 3. 影响范围
- 模块: `G-code 导出 / CONFIG_BLOCK 元数据写入`
- 关键文件:
  - `src/libslic3r/GCode.cpp`
- 受影响流程:
  - 多耗材切片导出 G-code
  - G-code 重新载入后的默认耗材颜色解析
  - 依赖 `default_filament_colour` / `default_filament_type` 的预览与映射逻辑

## 4. 根因分析
- 旧逻辑生成 `default_filament_colour` 的依据，不是“最终 G-code 实际发生出丝的挤出机集合”，而是更早期的“模型 / 配置层推断出的已使用挤出机集合”。
- 在 [`PrintApply.cpp`](/c:/work/6.0/C3DSlicer/src/libslic3r/PrintApply.cpp#L1166) 的 `get_used_extruders(...)` 中，会综合收集：
  - 模型 volume 上声明的挤出机；
  - layer range 中配置的挤出机；
  - support / raft 用到的挤出机；
  - custom tool change 中出现的挤出机。
- 随后在 [`PrintApply.cpp`](/c:/work/6.0/C3DSlicer/src/libslic3r/PrintApply.cpp#L1316) 里，用这组“配置层认为使用过”的挤出机去拼 `default_filament_colour` / `default_filament_type`。
- 这会带来一个偏差：如果某个模型被涂成了其他颜色，或存在额外的颜色/挤出机标记，配置层可能把这个“预览/涂色相关的挤出机”也算进去；但它未必真的在最终 G-code 中形成挤出移动。
- 导出阶段旧逻辑没有再和“最终切片结果”对齐，导致 `CONFIG_BLOCK` 里保留了这个额外颜色，于是双料材场景中写出了 `3` 个 `default_filament_colour` 值。
- 在 [`GCodeProcessor.cpp`](/c:/work/6.0/C3DSlicer/src/libslic3r/GCode/GCodeProcessor.cpp#L3609) 中，解析 `default_filament_colour` 时会按分号直接拆分并写入 `m_result.creality_extruder_colors`，额外颜色会继续传递到后续预览和映射流程。

## 5. 修复策略
- 不再信任旧的“配置层已使用挤出机”结果作为最终默认耗材元数据。
- 在 G-code 导出完成并经过 `m_processor.finalize(...)` 后，直接从 `GCodeProcessorResult::moves` 中统计真实发生过挤出的挤出机：
  - 仅统计 `EMoveType::Extrude`
  - 以及 `EMoveType::Extrude_Alter`
- 基于这组“最终实际渲染到 G-code 的挤出机”，重新对齐：
  - `filament_colour -> default_filament_colour`
  - `filament_type -> default_filament_type`
- 只回写 `CONFIG_BLOCK` 尾部的 `default_filament_colour` / `default_filament_type` 两项，不改动其它配置项。
- 同步更新内存中的 `result.creality_extruder_colors`，确保“文件内容”和“本次导出结果对象”保持一致。
- 若回写失败，则删除临时 G-code 文件并抛出异常，避免导出半成品。

## 6. 代码改动摘要
- 文件: `src/libslic3r/GCode.cpp`
  - 新增 [`collect_rendered_extruder_ids(...)`](/c:/work/6.0/C3DSlicer/src/libslic3r/GCode.cpp#L120)，从最终 `result.moves` 收集真实挤出过的挤出机 ID。
  - 新增 [`build_render_aligned_default_string_values(...)`](/c:/work/6.0/C3DSlicer/src/libslic3r/GCode.cpp#L133)，按真实渲染到 G-code 的挤出机重新对齐颜色/类型数组。
  - 新增 [`rewrite_config_block_tail_values(...)`](/c:/work/6.0/C3DSlicer/src/libslic3r/GCode.cpp#L155)，仅重写 G-code 尾部 `CONFIG_BLOCK` 中指定键值。
  - 新增 [`sync_default_filament_metadata_with_rendered_tools(...)`](/c:/work/6.0/C3DSlicer/src/libslic3r/GCode.cpp#L226)，统一完成元数据对齐与回写。
  - 在 [`GCode::do_export(...)`](/c:/work/6.0/C3DSlicer/src/libslic3r/GCode.cpp#L3161) 的 `m_processor.finalize(...)` 之后调用上述同步逻辑，确保导出结果以“最终真实出丝工具集合”为准。

## 7. 验证清单
- [ ] 双料材工程导出后，`CONFIG_BLOCK` 中 `default_filament_colour` 只包含实际使用的 `2` 个默认耗材颜色。
- [ ] 双料材工程导出后，`CONFIG_BLOCK` 中 `default_filament_type` 数量与 `default_filament_colour` 一致。
- [ ] 存在模型涂色 / 分区涂色时，如果某个颜色没有真正形成最终挤出，不会再把它写进 `default_filament_colour`。
- [ ] 重新加载该 G-code 后，默认耗材颜色数量与实际耗材数量一致，不再出现多出第 `3` 个颜色。
- [ ] 不含该问题的普通多料材工程，导出与重新加载行为保持不变。
- [ ] 回写 `CONFIG_BLOCK` 失败时，导出流程能正确中止，不留下不完整临时文件。

## 8. 风险与回滚
- 风险等级: `低到中`
- 主要风险:
  - 当前对齐逻辑依据的是“最终挤出移动出现过的挤出机 ID”；若后续有业务要求保留“配置里声明但最终未出丝”的默认耗材元数据，需要再明确口径。
  - `build_render_aligned_default_string_values(...)` 目前以 `filament_colour` / `filament_type` 为源数据；若未来默认值来源改成独立配置项，需要同步调整。
- 回滚方案:
  - 回滚 [`GCode.cpp`](/c:/work/6.0/C3DSlicer/src/libslic3r/GCode.cpp) 中新增的 `sync_default_filament_metadata_with_rendered_tools(...)` 及其调用点，恢复旧的导出元数据写入方式。

## 9. 结论
- 该问题本质上是“默认耗材元数据”的口径错位：
  - 旧逻辑按“模型 / 配置推断的已使用挤出机”写；
  - 实际需求应按“最终 G-code 真正出丝的挤出机”写。
- 当模型被涂成其他颜色时，这种口径错位更容易暴露，表现为双料材导出却写出了 `3` 个 `default_filament_colour` 值。
- 本次修复把 `default_filament_colour` / `default_filament_type` 的最终落盘口径统一到了真实导出结果上。
