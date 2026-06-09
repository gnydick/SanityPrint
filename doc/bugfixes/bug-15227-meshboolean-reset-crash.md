# Bug 修复说明

## 1. 基本信息

- Bug ID: `15227`
- 标题: `【布尔】多次点击重置，必现崩溃`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15227.html`
- 创建时间: `2026-03-09 20:48:41`
- 修复日期: `2026-03-10`
- 反馈者: `康美樱`
- 指派给: `钟轩`
- 所属产品/模块: `Sanity Print` / `准备页面`
- 所属项目/执行: `Sanity Print` / `CP7.1 布尔新功能移植`
- Bug 类型: `代码错误`
- 严重程度: `致命`
- 优先级: `高`
- Bug 状态: `激活（未确认）`

## 2. 问题现象

- 导入任意模型后，进入 Mesh Boolean（布尔）功能，多次点击 `Reset`，软件必现崩溃。

## 3. 影响范围

- 模块: `MeshBoolean Gizmo`
- 关键文件:
  - `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.cpp`

## 4. 复现步骤（修复前）

1. 选择可用于布尔的模型并进入 Mesh Boolean。
2. 在布尔面板中连续多次点击 `Reset`。
3. 软件崩溃。

## 5. 根因分析

- `Reset` 回调中，会直接尝试执行 `undo_redo_to(m_last_snapshot_time)`。
- 在“未发生有效布尔变更/重复点击重置”的场景下，该回退属于不必要调用，容易进入异常路径并触发崩溃。

## 6. 修复方案

- 仅在布尔模块做局部修复，不改全局 Undo/Redo 行为：
  - 在 `Reset` 回调中增加条件判断。
  - 仅当 `current_snapshot_time > m_last_snapshot_time` 时执行 `undo_redo_to(m_last_snapshot_time)`。
  - 未发生操作时不进行回退，仅刷新布尔列表、颜色覆盖和警告状态。

## 7. 代码改动摘要

- 文件: `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.cpp`
  - 位置: `m_ui->on_reset_operation` 回调
  - 改动点:
    - 增加 `Plater*` 空指针保护。
    - 增加快照时间判断，避免无操作场景下触发回退。
    - 保留 UI 状态刷新（`init_volume_manager` / `restore_list_color_overrides` / `clear_warnings`）。

## 8. 非改动说明

- `src/slic3r/GUI/Plater.cpp` 中全局函数 `Plater::priv::undo_redo_to(size_t)` **未修改**（已保持原实现）。
- 本次修复仅限定在 MeshBoolean 的 Reset 调用路径，避免影响其他模块的 Undo/Redo 语义。

## 9. 验证清单

- [ ] 进入布尔后立即点击 `Reset`，不崩溃。
- [ ] 进入布尔后连续多次点击 `Reset`，不崩溃。
- [ ] 进入布尔后执行一次布尔操作，再点击 `Reset`，可回到进入布尔时状态。
- [ ] 连续多次进入/退出布尔并点击 `Reset`，无崩溃。
- [ ] Undo/Redo 常规流程不受影响。

## 10. 风险与回滚

- 风险等级: `低`
  - 改动仅在布尔重置路径，不触达全局撤销/重做实现。
- 回滚方式:
  - 回退 `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.cpp` 对应改动即可。
