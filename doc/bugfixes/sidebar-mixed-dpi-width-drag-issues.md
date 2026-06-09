# Bug 修复记录

## 1. 基本信息
- Bug ID: `内部记录`
- 标题: `双屏混合 DPI 场景下 sidebar 宽度异常、拖拽边界错位与 hint 残影`
- 日期: `2026-03-25`
- 所属产品: `Sanity Print`
- 所属模块: `准备页面 / 右侧 Sidebar / 耗材与参数区域`
- Bug 类型: `界面缩放与交互异常`
- 严重程度: `一般到严重`
- 优先级: `中`
- 当前状态: `已修复`

## 2. 问题现象
- 在 Windows 双屏环境下，如果两个屏幕的缩放比不同，例如一块屏幕为 `100%`，另一块屏幕为 `125%`，主窗口从一块屏幕拖动到另一块屏幕后，右侧 `sidebar` 会出现异常。
- 典型表现包括：
  - `sidebar` 宽度逐渐变宽。
  - `sidebar` 的最小可缩宽度在高缩放屏幕上异常抬高。
  - `sidebar` 边缘的实际拖拽热区与可见边缘不一致，鼠标放在边缘上无法稳定触发拖动。
  - 拖拽过程中会出现白色预测线，且在双屏场景下存在位置偏移和残影。
- 同期还发现耗材区域两个相关问题：
  - 耗材丝下半区域三角图标在缩放切换后刷新不及时。
  - 耗材丝下半按钮点击后，弹框无法正常收起。

## 3. 复现步骤
1. 在 Windows 双屏环境下准备两块缩放比不同的屏幕，例如：
   - 屏幕 A：`1920 x 1080`，缩放 `100%`
   - 屏幕 B：`1600 x 900`，缩放 `125%`
2. 打开 Sanity Print，进入 `Prepare` 页面。
3. 在右侧显示 `sidebar` 的情况下，将主窗口在两块屏幕之间来回拖动。
4. 尝试拖拽 `sidebar` 右侧边缘，观察宽度变化、最小宽度、拖拽热区和预测线表现。
5. 点击耗材丝条目的下半区域，观察弹框展开、收起和三角图标状态。

## 4. 影响范围
- 模块:
  - `Sidebar`
  - `FilamentPanel`
  - `ParamsPanel`
- 关键文件:
  - `src/slic3r/GUI/Plater.cpp`
  - `src/slic3r/GUI/Plater.hpp`
  - `src/slic3r/GUI/FilamentPanel.cpp`
  - `src/slic3r/GUI/ParamsPanel.cpp`
- 受影响流程:
  - 右侧 `sidebar` 在不同 DPI 屏幕之间拖动
  - `sidebar` 宽度调整
  - 耗材丝下半区域弹框展开/收起
  - 耗材丝下半三角图标刷新

## 5. 根因分析
- `wxAUI` 会保存并复用 `sidebar` 的像素级宽度；在混合 DPI 场景下，旧屏幕上的像素宽度会被错误带到新屏幕继续参与布局。
- 旧逻辑在 `idle` 中持续将当前 `sidebar` 像素宽度回写到 `BestSize()`，导致错误宽度被不断固化。
- 在 DPI 切换后，`sidebar` 的 pane 宽度、sash 命中区域和拖拽边界没有完全按新缩放比同步刷新。
- `wxAUI` 默认启用的 hint 预测线机制在双屏混合 DPI 下会产生位置偏移和残影。
- `ParamsPanel` 中存在 `FromDIP(...) * em_unit(...)` 这类重复缩放写法，会额外放大右侧模板区域宽度。
- 耗材丝下半按钮原先的点击逻辑不是标准 toggle，关闭弹框后又会重新弹出。
- 三角图标切换后缺少及时重绘，导致缩放切换后图标状态滞后。

## 6. 修复策略
- 将 `sidebar` 的宽度记忆从“像素宽度驱动”改为“逻辑宽度驱动”，避免不同缩放屏幕之间相互污染。
- 移除会在高缩放屏幕上放大限制效果的 pane 级硬最小宽度约束。
- 停止在 `idle` 中持续更新 `BestSize()`，改为仅在持久化布局时记录当前宽度。
- 在 DPI 切换时同步刷新 `sidebar` 的 sash 宽度和 pane 宽度。
- 关闭 `wxAUI` 默认 hint 预测线，改为 `LIVE_RESIZE`，规避白线偏移和残影。
- 修正 `ParamsPanel` 中的重复缩放计算。
- 修正耗材丝下半按钮展开/收起逻辑，并补齐三角图标刷新。

## 7. 代码改动摘要
- 文件: `src/slic3r/GUI/Plater.hpp`
  - 为 `Sidebar` 增加 `m_last_em_unit`，用于按前后 DPI 比例换算宽度。

- 文件: `src/slic3r/GUI/Plater.cpp`
  - 在 `Sidebar::msw_rescale()` 中：
    - 按当前 DPI 重新计算 `sidebar` 宽度。
    - 同步刷新 `wxAUI` 的 sash 宽度。
    - 只用当前实际宽度参与换算，不再继续依赖旧的 `pane.best_size.x` 缓存。
  - 关闭 `wxAUI` 的 hint 预测线相关 flags，改为 `wxAUI_MGR_LIVE_RESIZE`。
  - 去掉 `idle` 中持续写回 `sidebar.BestSize(...)` 的逻辑。
  - 增加 `sidebar_width_em` 持久化逻辑，按逻辑宽度保存和恢复 `sidebar` 宽度。

- 文件: `src/slic3r/GUI/ParamsPanel.cpp`
  - 去掉 `create_layout_process()` 和 `msw_rescale()` 中 `FromDIP(...) * em_unit(...)` 的重复缩放计算。
  - 在 `msw_rescale()` 末尾补 `Layout()` 和 `Refresh()`。

- 文件: `src/slic3r/GUI/FilamentPanel.cpp`
  - `FilamentButton::SetIcon()` 在更新图标后立即 `Refresh()`。
  - 耗材丝下半按钮改为真正的展开/收起 toggle。
  - 弹框关闭时同步恢复下三角图标。
  - 在 `FilamentItem::msw_rescale()` 中按弹框当前状态重新设置三角图标并刷新。

## 8. 验证清单
- [ ] 在 `100% / 125%` 混合 DPI 双屏环境下，将主窗口在两块屏幕之间来回拖动，`sidebar` 不再持续变宽。
- [ ] 在两块屏幕上分别拖拽 `sidebar` 边缘，最小可缩宽度表现一致，不再在高缩放屏幕上异常抬高。
- [ ] `sidebar` 可见边缘与实际拖拽热区一致，鼠标放在边缘上可正常触发拖动。
- [ ] 拖拽 `sidebar` 时不再出现错位白色预测线和残影。
- [ ] 点击耗材丝下半区域，弹框可正常展开，再次点击可正常收起。
- [ ] 切换缩放比或跨屏拖动后，耗材丝下半三角图标状态刷新正常。

## 9. 风险与回滚
- 风险等级: `低到中`
- 主要风险:
  - 本次修改集中在 `wxAUI` 的 sidebar 宽度同步与 hint 行为，理论影响面较小，但如果后续窗口布局持久化策略调整，需再验证 `sidebar_width_em` 的兼容性。
  - `ParamsPanel` 当前只处理了明确的重复缩放点，若后续还有新的固定宽度控件引入，仍需继续关注高 DPI 下的宽度行为。
- 回滚方案:
  - 回退 `src/slic3r/GUI/Plater.cpp`
  - 回退 `src/slic3r/GUI/Plater.hpp`
  - 回退 `src/slic3r/GUI/ParamsPanel.cpp`
  - 回退 `src/slic3r/GUI/FilamentPanel.cpp`

## 10. 备注
- 会话中一度怀疑 `ParamsPanel` 左侧固定宽度（例如 `260 DIP`）是主要卡点，但最终确认真正导致高缩放屏幕下最小宽度异常抬升的关键原因是 `wxAUI` pane 级别的宽度约束和像素宽度缓存。
