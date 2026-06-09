# Bug 修复记录

## 1. 基本信息
- Bug ID: `15261` / `15262` / `15333` / `15444`
- 禅道链接:
  - `https://zentao.creality.com/zentao/bug-view-15261.html`
  - `https://zentao.creality.com/zentao/bug-view-15262.html`
  - `https://zentao.creality.com/zentao/bug-view-15333.html`
  - `https://zentao.creality.com/zentao/bug-view-15444.html`
- 标题:
  - 15261: `【热更新】未保存的项目弹窗和热更新的安装弹窗存在遮挡`
  - 15262: `【热更新】安装失败了，再次打开软件还出现安装的弹窗，不合理`
  - 15333: `【热更新】把UAC的权限拉到最高，不安装，下次打开还会弹出热更新的窗口`
  - 15444: `【热更新】项目弹窗和热更新的安装弹窗存在遮挡，应该要有先后顺序`
- 日期: `2026-03-23`
- 所属产品: `Sanity Print`
- 所属模块: `其它`
- 所属计划: `CP7.1 软件热更新`
- Bug 类型: `代码错误`
- 严重程度: `一般`
- 优先级: `高`
- 当前状态: `激活`
- 是否确认: `未确认`
- 指派给: `李苏贵`
- 所属执行: `CP7.1 软件热更新`

## 2. 问题现象

**15261 / 15444 — 弹窗遮挡 / 无先后顺序**
- 用户下载热更新包完成后，"更新就绪"弹窗与"检测到未保存项目，是否恢复？"弹窗同时出现，互相遮挡，无显示优先级区分。

**15262 — 安装失败后重新弹窗**
- 用户点击"立即安装"但安装失败（版本号未变），下次打开软件仍会自动弹出热更新安装弹窗，陷入死循环。
- 期望：安装失败后再次打开，不应再自动弹窗；点"检查新版本"时仍可正常弹出。

**15333 — UAC 拒绝后仍重复弹窗**
- 用户将 UAC 权限调至最高，触发用户控制弹窗后点击"否"（安装被系统拒绝），下次打开软件仍弹出"更新就绪"弹窗。
- 本质与 15262 相同：只要用户点击了"立即安装"（无论安装是否成功），均应视为"已处理此版本"，不再自动重复弹窗。

## 3. 禅道复现信息

**15261 / 15444**
1. 打开软件，点击更新版本。
2. 下载过程中打开或编辑 3mf 项目（产生未保存状态）。
3. 热更新下载完成后点击"稍后安装"。
4. 安装失败后（或直接）关闭软件，再次打开。
5. 实际结果：项目恢复弹窗与热更新安装弹窗同时出现，无顺序保障。
6. 期望结果：先弹出"更新就绪"弹窗，用户处理后再弹出项目恢复弹窗。

**15262**
1. 点击更新版本。
2. 热更新后点击"立即安装"。
3. 安装失败后，再次打开切片软件。
4. 实际结果：安装弹窗再次出现，死循环。
5. 期望结果：不再自动弹出热更新安装弹窗；点"检查新版本"时仍正常弹出。

**15333**
1. 把 UAC 权限拉到最高。
2. 点击下载热更新包。
3. 弹出用户控制弹窗后点击"否"。
4. 再次打开切片软件。
5. 实际结果：重新弹出了热更新的窗口（本地存有刚下载的全量包）。
6. 期望结果：按照产品要求，下次打开时不再自动弹出安装弹窗。

## 4. 影响范围
- 模块: `热更新流程 / 启动弹窗时序`
- 关键文件:
  - `src/slic3r/GUI/GUI_App.cpp`
  - `src/slic3r/GUI/GUI_App.hpp`
- 受影响流程:
  - 启动时后台版本检查（by_user = 0）
  - 下载完成后"更新就绪"弹窗（`EVT_APP_UPDATE_COMPLETE`）
  - 启动时 pending 热更新检查（`EVT_SLIC3R_VERSION_ONLINE` pending 路径）
  - 启动时项目恢复弹窗（`EVT_RESTORE_PROJECT`）

## 5. 根因分析

**15262 / 15333 — 安装状态未记录**
- 用户点击"立即安装"后，原有逻辑仅清除 `pending_update.json`，但没有写入任何"已尝试安装"标记。
- 下次启动后台检查时，服务端仍返回相同版本号，pending 路径照常触发，再次弹出"更新就绪"弹窗。

**15261 / 15444 — 弹窗无时序保障**
- 启动时 `trigger_restore_project(1)` 通过 `wxQueueEvent` 立即入队，而版本检查需要 HTTP 往返后才触发 `EVT_SLIC3R_VERSION_ONLINE`。
- 两个事件几乎同时进入事件队列并各自以模态框展示，未做任何互斥或先后保障。

**15444 (激活) — 下载完成状态未及时写入**
- 下载完成后，`update_state.json` 是在用户点击"稍后安装"后才写入，而不是在下载完成时立即写入。
- 如果下载完成后弹出"更新就绪"弹窗时程序异常关闭（未点击任何按钮），`update_state.json` 文件不存在。
- 下次启动时无法检测到有待安装的更新，导致直接显示项目恢复弹窗，而不是先显示热更新安装弹窗。

## 6. 修复策略

**15262 / 15333 — 引入 install_attempted 状态**
- 将 `pending_update.json` 与新增的安装尝试标记合并为单一文件 `update_state.json`，使用 `attempted` 字段区分两种状态：
  - `attempted = false`：用户点了"稍后安装"，下次启动弹"更新就绪"。
  - `attempted = true`：用户点了"立即安装"，下次后台检查对同版本静默跳过。
- 文件路径：`%LOCALAPPDATA%\sanityprint_squirrel\<当前版本>\update_state.json`
- 当服务端推送真正更新的版本号时，清除旧标记并正常进入 `UpdateVersionDialog` 流程。

**15261 / 15444 — 启动时弹窗时序管控**
- 启动时同步读取 `update_state.json`：若存在 `attempted = false` 的 pending 记录，设置标志位 `m_restore_project_deferred = true`，不立即调用 `trigger_restore_project`。
- 待"更新就绪"弹窗被用户处理完毕（无论点"立即安装"还是"稍后"），再通过 `trigger_deferred_restore_project()` 触发项目恢复流程。
- 为覆盖所有出口（HTTP 失败、服务端无更新、服务端返回新版本、待安装缓存包失效等），在 `EVT_SLIC3R_VERSION_ONLINE` 及 `check_new_version_cx_updated` 的每条退出路径均补充调用 `trigger_deferred_restore_project()`。

**15444 (激活) — 下载完成立即写入状态**
- 在 `EVT_APP_UPDATE_COMPLETE` 事件处理中，显示"更新就绪"对话框**之前**，立即调用 `set_pending_app_update()` 写入 `update_state.json`（`attempted=false`）。
- 这样即使程序在显示对话框过程中异常关闭，下次启动时也能通过 `get_pending_app_update_version()` 检测到有待安装的更新。

## 7. 代码改动摘要
- 文件: `src/slic3r/GUI/GUI_App.cpp`
  - 新增底层状态辅助函数：`get_update_state_path()`、`write_update_state()`、`read_update_state()`、`clear_update_state()`，统一读写 `update_state.json`。
  - 对外保留原有函数签名：`set_pending_app_update()`、`clear_pending_app_update()`、`get_pending_app_update_version()` 改为内部调用上述底层函数；新增 `set_install_attempted()`、`get_install_attempted_version()`、`clear_install_attempted()`，操作同一文件中的 `attempted` 字段。
  - `EVT_SLIC3R_VERSION_ONLINE`（by_user=0 路径）：
    - pending 版本匹配：prepare 成功时弹"更新就绪"，点"立即安装"写入 attempted；点"稍后"或 prepare 失败时调 `trigger_deferred_restore_project()`。
    - attempted 版本静默返回：补调 `trigger_deferred_restore_project()` 再 return。
    - 新版本 fall through 到 `UpdateVersionDialog`：对话框关闭后补调 `trigger_deferred_restore_project()`。
  - `EVT_APP_UPDATE_COMPLETE`：
    - **下载完成后立即写入 pending 状态**（`set_pending_app_update`），确保异常关闭后仍能检测到待安装更新。
    - 点"立即安装"写入 attempted；点"稍后"重新写入 pending 并调 `trigger_deferred_restore_project()`。
  - `process_update_packages`（缓存命中、by_user≠0 路径）：点"立即安装"写入 attempted。
  - `check_new_version_cx_updated`：
    - `on_error`、`status != 200`、`hasUpdate = false` 三条出口各通过 `CallAfter` 补调 `trigger_deferred_restore_project()`。
    - `packages.empty()` 时降级发送 `EVT_SLIC3R_VERSION_ONLINE` 事件通知（fallback 到标准版本弹窗），确保用户仍能收到更新提示。
  - 启动时 `load_gl_resources` 回调：若本地 `update_state.json` 存在 pending 记录（`attempted=false`），则设 `m_restore_project_deferred = true`，跳过 `trigger_restore_project(1)`；否则直接调用。
  - 新增 `GUI_App::trigger_deferred_restore_project()`：幂等，flag 为 true 时触发 `trigger_restore_project(1)` 并清除 flag。
- 文件: `src/slic3r/GUI/GUI_App.hpp`
  - 新增成员 `bool m_restore_project_deferred {false}`。
  - 新增函数声明 `void trigger_deferred_restore_project()`。

## 8. 验证清单
- [ ] 点击"立即安装"后安装失败（版本号未变），重新打开软件，后台检查不再自动弹出热更新安装弹窗。
- [ ] 点击"立即安装"后安装失败，点击"检查新版本"，正常弹出热更新弹窗。
- [ ] UAC 弹窗点"否"（安装被系统拒绝），重新打开软件，不再自动弹出热更新安装弹窗。
- [ ] 点击"稍后安装"后重新打开软件，仍正常弹出"更新就绪"弹窗。
- [ ] 服务端有更新版本时，清除旧的 attempted 标记，后台检查正常弹出 `UpdateVersionDialog`。
- [ ] 有 pending 记录时重新启动，先弹出"更新就绪"弹窗，关闭后再弹出项目恢复弹窗。
- [ ] 有 pending 记录但网络不通时，不阻塞项目恢复弹窗（HTTP 超时后自动触发）。
- [ ] 有 pending 记录但缓存包已失效（prepare 失败）时，项目恢复弹窗正常显示。
- [ ] 无 pending 记录时（首次发现新版本、安装成功等），项目恢复弹窗行为与修改前完全一致。
- [ ] **15444 新增验证**：下载完成后弹出"更新就绪"弹窗时异常关闭（任务管理器结束进程），下次启动应先弹出热更新安装窗口，再弹出项目恢复弹窗。

## 9. 风险与回滚
- 风险等级: `低`
- 主要风险:
  - `m_restore_project_deferred` 若在极端情况下未被清除（如进程崩溃），下次启动项目恢复弹窗不受影响（flag 为内存变量，重启自动归零）。
  - `update_state.json` 写入失败（磁盘满等）时，状态无法持久化，行为退化为修复前的重复弹窗；但不会引发崩溃。
- 回滚方案: 回退 `GUI_App.cpp` 中状态辅助函数区（256～380 行）及 `EVT_SLIC3R_VERSION_ONLINE` / `EVT_APP_UPDATE_COMPLETE` 相关修改，恢复原有 `pending_update.json` 单文件逻辑。

## 10. 备注
- `update_state.json` 存放在版本号目录（`%LOCALAPPDATA%\sanityprint_squirrel\<版本>\`）下，软件升级到新版本后目录自然废弃，无需主动清理旧版本遗留文件。
- 本次修复同时覆盖了 15261、15262、15333、15444 四个 Bug，核心改动集中在 `GUI_App.cpp` 的热更新事件处理逻辑中。
