# Bug 修复记录

## 1. 基本信息
- Bug ID: `15382`
- 标题: `附件3mf导入后，选择一个模型，打洞功能和抽壳功能不应该置灰`
- 日期: `2026-03-24`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15382.html`
- 创建人: `冷金辉`
- 当前指派: `钟轩`
- 历史指派:
  - `冷金辉 -> 贺淼`
  - `贺淼 -> 钟轩`
- 最后修改: `贺淼`

## 2. 现象
- 导入附件 `四色.3mf` 后，在准备页面选择单个模型。
- `Drill` 和 `Hollow` 按钮置灰，不可用。
- 预期行为是：只要当前选择能够定位到一个可编辑的模型实例，这两个功能应可用。

## 3. 附件分析
- 附件中的目标对象是 `object id="12"`。
- 该对象由 4 个 `component` 组成，属于组合体对象，不是单一 mesh。
- 这类对象在界面中可能以整对象、多 part 选择等形式出现，但本质上仍然对应单个 `object + instance`。

## 4. 根因分析
- `Drill` 和 `Hollow` 在 FDM 模式下的启用条件原来写得过窄，只允许以下几类选择：
  - `SingleFullInstance`
  - `SingleVolume`
  - `SingleModifier`
- 这会漏掉一部分仍然能够唯一定位到单个实例的合法选择，例如组合体整对象，或者同一实例下的多 part 选择。
- 同时，`Selection::contains_sinking_volumes()` 原实现遍历的是全场景 `GLVolume`，不是当前 selection。
- 结果是：即使当前选中的对象本身正常，只要场景里别的模型被判定为 sinking，`Drill` 和 `Hollow` 也会被一起禁用。

## 5. 条件对比
- 原来的 FDM 条件：
```cpp
selection.is_single_full_instance() || selection.is_single_volume() || selection.is_single_modifier()
```
- 现在的 FDM 条件：
```cpp
selection.is_from_single_instance() && !selection.is_mixed() && !selection.is_wipe_tower()
```
- 两者的差异不只是“多放开了一种类型”，而是判断思路发生了变化：
  - 原条件按 `Selection::EType` 白名单判断。
  - 新条件按“当前选择是否仍然属于单个实例”判断。
- 原条件的含义是：
  - 如果当前选择刚好被 `Selection` 归类成 `SingleFullInstance`、`SingleVolume`、`SingleModifier`，就允许进入 gizmo。
  - 只要选择类型落到别的枚举值，即使本质上仍然只对应一个实例，也会被拒绝。
- 这套判断的问题在于它把“可编辑性”绑定到了少数几个枚举类型上，但 `Drill` / `Hollow` 的实际执行并不是依赖某个固定 `EType`，而是依赖是否能够稳定拿到当前的 `object + instance`。
- 例如附件中的组合体对象：
  - 它不是单一 mesh。
  - 在 UI 中可能以整对象选中，也可能以同一实例下多 part 的形式被选中。
  - 这种选择虽然不一定落在原白名单内，但仍然能够唯一定位到一个实例，逻辑上应该允许 `Drill` / `Hollow`。
- 新条件改成 `selection.is_from_single_instance()` 后，判断口径就和 gizmo 实际需求一致了：
  - 只要当前选择还能唯一定位到一个实例，就允许。
  - 不再依赖它被 `Selection` 具体归类成哪个单选类型。
- 同时增加 `!selection.is_mixed()` 和 `!selection.is_wipe_tower()`，是为了明确保留原本不该放开的场景：
  - `mixed selection` 本身不具备稳定的单实例编辑语义。
  - `wipe tower` 不是目标模型，不应进入 `Drill` / `Hollow`。

## 6. sinking 判定对比
- 原来的实现：
```cpp
for (const GLVolume* v : *m_volumes) {
    if (!ignore_modifiers || !v->is_modifier) {
        if (v->is_sinking())
            return true;
    }
}
```
- 现在的实现：
```cpp
for (unsigned int idx : m_list) {
    const GLVolume* v = (*m_volumes)[idx];
    if ((!ignore_modifiers || !v->is_modifier) && v->is_sinking())
        return true;
}
```
- 这里的核心差异是遍历范围：
  - 原来遍历的是全场景 `m_volumes`。
  - 现在遍历的是当前 selection 的 `m_list`。
- 原实现的问题是：
  - 函数名叫 `contains_sinking_volumes()`，调用位置也在基于 selection 的 gizmo 启用判断里。
  - 但它实际检查的是整个场景。
  - 结果会出现“当前对象没问题，但场景里别的对象下沉，当前 gizmo 也被禁用”的连带误伤。
- 新实现把检查范围缩回当前 selection 后，语义才和函数名、调用上下文一致：
  - 当前选中的对象有 sinking volume，禁用。
  - 当前选中的对象没有 sinking volume，不受其他未选中模型影响。

## 7. 修复策略
- 保持现有代码结构不动，只调整必要判断。
- `GLGizmoDrill.cpp` 和 `GLGizmoHollow.cpp` 中，FDM 启用条件改为：
  - `selection.is_from_single_instance()`
  - `!selection.is_mixed()`
  - `!selection.is_wipe_tower()`
- `Selection.cpp` 中，`contains_sinking_volumes()` 改为只检查当前 selection 中的 volume。

## 8. 代码变更
- 文件: `src/slic3r/GUI/Gizmos/GLGizmoDrill.cpp`
  - 将 FDM 可用条件从固定类型白名单，改为基于单实例选择判断。
- 文件: `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp`
  - 将 FDM 可用条件从固定类型白名单，改为基于单实例选择判断。
  - 保留原有注释代码和函数结构，只改必要逻辑。
- 文件: `src/slic3r/GUI/Selection.cpp`
  - 将 `contains_sinking_volumes()` 从全场景遍历改为仅遍历当前选择。

## 9. 验证
- 已确认附件可正常下载和解包。
- 已确认目标对象 `object 12` 为组合体对象。
- 已完成本地编译验证：
  - `cmake --build build_Release --config Release --target libslic3r_gui -- /m`
  - `cmake --build build_Release --config Release --target SanityPrint_app_gui -- /m`

## 10. 风险与回归点
- 风险较低，本次仅修改 gizmo 启用条件和 selection 的 sinking 判定范围。
- 需要关注：
  - 单实例对象的 `Drill` / `Hollow` 行为保持不变。
  - SLA 模式行为保持不变。
  - wipe tower 和 mixed selection 仍然保持禁用。
  - 当前 selection 中存在 sinking volume 时，仍应继续禁用相关 gizmo。
