# Bug 修复记录

## 1. 基本信息
- Bug ID: `14930`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-14930.html`
- 标题: `【用户反馈】附件文件，没有多色。但有擦拭塔不合理`
- 创建人: `康美樱`
- 指派给: `王文彬`
- 所属产品: `Sanity Print`
- 修复日期: `2026-03-03`

## 2. 问题现象
- 在 `K2Plus` 机型下，单色模型本不应出现擦拭塔，但在准备页会不定期出现擦拭塔。
- 典型表现为：
  - `准备` 页可见擦拭塔；
  - `预览` 页无对应擦拭塔；
  - 添加第二耗材后再删除，准备页仍残留擦拭塔/多色信息。
- 用户确认条件：
  - 非平滑延时摄影；
  - 单耗材打印；
  - 无支撑；
  - 自定义换料 G-code 无 `ToolChange`。

## 3. 重现路径
1. 选择 `K2Plus` 机型并导入附件模型。
2. 在单盘（如 04 盘）保持单色、无支撑、非平滑延时摄影。
3. 观察准备页：出现不合理擦拭塔。
4. 执行“添加第二耗材 -> 删除第二耗材”。
5. 结果：擦拭塔仍可能残留于准备页。

## 4. 根因分析
- 准备页擦拭塔判定与预览页判定口径不一致：
  - 部分路径使用 `get_extruders(true)`，会把自定义换料等信息纳入统计；
  - 预览/切片路径按实际有效挤出机统计，导致“准备有塔、预览无塔”。
- 盘上对象实例统计不严谨：
  - 历史逻辑使用 `contain_instance_totally(obj_idx, 0)`，只看实例 0，可能把不在当前盘的对象挤出机带入统计。
- 删除耗材后的对象/体配置回写存在问题：
  - volume 分支存在配置写入目标错误（写到了 object config）；
  - 删除命中的 volume 挤出机在无替换时回填不合理，导致单色盘仍被判定为多挤出机。

## 5. 修复策略
- 统一擦拭塔判定与显示口径，准备页与预览页均按 `get_extruders(false)` 统计当前盘有效挤出机。
- 在准备页增加防护：
  - 当前盘无可打印实例时不生成擦拭塔；
  - 当前盘无有效挤出机时不生成擦拭塔；
  - 非平滑延时摄影且当前盘单挤出机时不生成擦拭塔。
- 删除耗材逻辑修正：
  - 修正 volume 分支配置写入目标；
  - 删除命中且无替换耗材时，volume 挤出机回退到对象当前挤出机，避免错误残留为多色。
- 统一按“对象任一实例是否在当前盘”参与挤出机统计，避免跨盘污染。
- 验证通过后清理调试日志，保留功能修复代码。

## 6. 代码改动摘要
- `src/slic3r/GUI/GLCanvas3D.cpp`
  - 擦拭塔判定改为使用 `get_extruders(false)`。
  - 增加空盘/无有效挤出机/单色非平滑延时摄影的跳过逻辑。
  - 计算擦拭塔尺寸时显式传入当前盘挤出机数量，避免内部回退到不一致口径。
- `src/slic3r/GUI/3DScene.cpp`
  - 擦拭塔预览颜色来源改为 `get_extruders(false)`，与准备页一致。
- `src/slic3r/GUI/PartPlate.cpp`
  - `get_extruders` / `get_extruders_without_support` / `estimate_wipe_tower_size` 统一改为按“任一实例在当前盘”统计对象。
- `src/slic3r/GUI/GUI_ObjectList.cpp`
  - 修复删除耗材时 volume 配置写入目标错误。
  - 修复无替换耗材时 volume 挤出机回填策略。

## 7. 验证结果
- 用户现场复测结果：`效果 OK`。
- 关键场景验证通过：
  - 单色模型准备页不再错误出现擦拭塔；
  - “添加第二耗材 -> 删除第二耗材”后，不再出现不合理残留塔；
  - 准备页与预览页表现一致。
- 编译验证：
  - `cmake --build build_Release --config Release -j 8` 通过；
  - `SanityPrint_Slicer.dll` / `SanityPrint.exe` 生成成功。

## 8. 风险评估
- 风险等级：`中低`
- 主要风险点：
  - 擦拭塔判定口径调整后，边界场景（支撑、分层换色、平滑延时摄影）需确认无回归。
  - 删除耗材回写策略变更后，历史工程在“对象挤出机/体挤出机/分层挤出机”混用场景需回归验证。
  - 多盘实例统计从“实例0”改为“任一实例”，需关注大工程下性能与一致性。
- 风险结论：
  - 本次改动集中在判定与配置回写，不涉及底层切片路径生成，整体可控。

## 9. 回归关注点
- 多盘场景下的跨盘实例统计是否稳定。
- 含支撑/含分层换色/平滑延时摄影开启场景的擦拭塔行为是否符合预期。
- 删除耗材后对象/体/分层区间挤出机一致性是否持续正确。



## 10. 补充优化记录（2026-03-19）

### 10.1 背景
- 在 14930 主修复后，继续优化了“预览更换耗材丝后返回准备页”的状态一致性。
- 目标保持不变：仅当当前盘存在有效多耗材切换信息时，准备页显示擦拭塔；纯单色盘不显示。

### 10.2 本次追加优化点
- `src/slic3r/GUI/IMSlider.cpp`
  - 在 `post_ticks_changed_event()` 中，预览侧编辑 `ToolChange/Custom/Template/PausePrint` 后立即回写 `model.plates_custom_gcodes[curr_plate_index]`。
  - 同步将对应盘 `slice_result_valid_state` 置为 `false`，确保从预览切回准备页时状态不丢失。
- `src/slic3r/GUI/Plater.cpp`
  - 移除 `EVT_CUSTOMEVT_TICKSCHANGED` 回调中的重复回写与重复置脏逻辑，避免多处写入导致的异步不一致。
  - 保留 `set_plater_dirty(true)` 与 `preview->on_tick_changed(...)` 的主流程。
- `src/slic3r/GUI/GLCanvas3D.cpp`
  - 准备页擦拭塔判定改为使用 `part_plate->get_extruders(true)`，与切片/预览对多耗材来源保持一致。
- `src/slic3r/GUI/PartPlate.cpp`
  - 在 `get_extruders*` 系列函数中，`ToolChange` 仅统计有效挤出机号：`extruder > 0 && extruder <= nums_extruders`。
  - 防止无效挤出机编号被误计入，导致单色盘误显示擦拭塔。

### 10.3 影响范围
- 仅影响擦拭塔“是否显示”的前端/准备页判定与预览编辑后的状态同步。
- 不改变底层切片生成算法，不改动 G-code 生成主路径。
- 影响文件：
  - `src/slic3r/GUI/IMSlider.cpp`
  - `src/slic3r/GUI/Plater.cpp`
  - `src/slic3r/GUI/GLCanvas3D.cpp`
  - `src/slic3r/GUI/PartPlate.cpp`

### 10.4 风险评估
- 风险等级：`低`
- 主要风险点：
  - 预览侧快速切盘、快速切页时的事件时序需持续关注。
  - 历史 3mf 若包含脏 `tool_change` 记录（例如本不期望的盘也有 `tool_change`），准备页会按数据事实显示擦拭塔。

### 10.5 回归建议
1. 单色 STL -> 切片预览 -> 添加“更换耗材丝” -> 返回准备页：应出现擦拭塔。
2. 未添加“更换耗材丝”的单色盘：准备页不应出现擦拭塔。
3. 多盘场景（仅部分盘有 ToolChange）：仅对应盘显示擦拭塔。
4. 导出并重新导入 3mf：准备页擦拭塔显示应与 3mf 内 `custom_gcode_per_layer.xml` 一致。


### 10.6 为什么之前的 3mf 数据看起来不起作用
- 结论：不是 3mf 没保存数据，而是“准备页擦拭塔判定口径”和“预览编辑数据落盘时机”在旧实现里不一致，导致 3mf 中的 `tool_change` 信息没有被准备页及时、正确地用于显示。

- 旧实现的两个关键问题：
  1. 准备页曾按 `get_extruders(false)` 统计挤出机。
     - 该口径只看模型/支撑等几何挤出机，不把 `plates_custom_gcodes` 中的 `ToolChange` 计入。
     - 结果是：即使 3mf 的 `custom_gcode_per_layer.xml` 已保存 `tool_change`，准备页也可能不出擦拭塔（看起来像“3mf 数据不生效”）。
  2. 预览里“更换耗材丝”在旧流程下存在异步窗口。
     - 用户刚编辑完就切回准备页时，`plates_custom_gcodes` 可能尚未及时回写到模型，导致准备页读到旧状态。

- 本次修复后为何恢复生效：
  - 预览编辑后立即回写 `model.plates_custom_gcodes[curr_plate_index]`，并将对应盘切片结果置无效；
  - 准备页改为按 `get_extruders(true)` 判定，并对 `ToolChange` 挤出机号做有效性过滤（`extruder > 0 && extruder <= nums_extruders`）。

- 因此现在的行为是：
  - 3mf 中某盘存在有效 `tool_change` -> 该盘准备页显示擦拭塔；
  - 无有效 `tool_change` 的单色盘 -> 不显示擦拭塔；
  - 避免了“数据已在 3mf 中，但准备页表现不一致”的现象。

### 10.7 补充：ToolChange 高度门槛判定（2026-03-20）
- 问题场景：在预览页添加“更换耗材丝”后，如果换色层 `print_z` 高于模型最终高度（例如换色设在 Z=50，但模型缩放后高度仅 30），该换色实际不会发生。
- 旧行为：仅凭存在 `tool_change` 记录就会被判定为多色，准备页仍显示擦拭塔。
- 期望行为：不可达的换色不应触发多色判定，应按单色处理，不显示擦拭塔。

- 本次处理：
  - 在 `PartPlate::get_extruders(true)` 中，统计 `ToolChange` 时新增高度条件：`item.print_z <= max_model_height + 1e-6`。
  - 其中 `max_model_height` 为当前盘模型可打印对象的最高高度。

- 效果：
  - 仅当换色层高度可达时，`tool_change` 才参与多色判定并触发擦拭塔显示。
  - 当模型缩放导致换色层不可达时，准备页将按单色逻辑显示（不再出现不必要的擦拭塔）。
