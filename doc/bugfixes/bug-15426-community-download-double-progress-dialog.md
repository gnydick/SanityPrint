# Bug 修复记录

## 1. 基本信息
- Bug ID: `15426`
- Bug 标题: `社区个人中心下载并导入模型时，不应该显示两个进度条`
- 记录日期: `2026-04-02`
- 提单人: `冷金辉`
- 处理人: `李苏贵`
- 分支/提交:

## 2. 问题现象
- 从社区个人中心下载 3MF 模型并自动导入时，出现两个进度条对话框同时显示：
  - 下载进度条显示在 99%（还未关闭）
  - 导入进度条已经开始显示
- 影响:
  - 用户界面出现重叠的进度对话框，体验差
  - 给用户造成困惑，不知道当前处于哪个阶段
  - 视觉上不专业，影响产品质感

## 3. 影响范围
- 模块: `社区模型下载 / 3MF 导入`
- 关键文件:
  - `src/slic3r/GUI/ModelDownloader.cpp`
  - `src/slic3r/GUI/DownloadService.cpp`
  - `src/slic3r/GUI/CloudDownloadProgressDialog.cpp`
  - `src/slic3r/GUI/CloudDownloadProgressDialog.hpp`
  - `src/slic3r/GUI/GUI_App.hpp`
  - `src/slic3r/GUI/GUI_App.cpp`
- 涉及流程:
  - 社区个人中心模型下载流程
  - 3MF 文件自动导入流程

## 4. 修复前复现步骤
1. 打开 Sanity Print，进入社区个人中心
2. 选择一个 3MF 模型，点击下载
3. 观察下载进度对话框显示下载进度
4. 当下载接近完成（99%）时，导入进度对话框提前出现
5. 结果:
   - 两个进度对话框同时显示在屏幕上
   - 下载对话框还在 99%，导入对话框已经开始显示"加载文件"

## 5. 原因分析

### 5.1 执行链路
```
DownloadService::on_complete (后台IO线程)
    ↓ 直接调用
ModelDownloader::complete_cb
    ↓ 直接调用
wxGetApp().request_model_download()
    ↓ wxQueueEvent
Plater::import_model_id() → 创建导入进度对话框
```

### 5.2 时序竞争问题
- `complete_cb` 是在 `DownloadService` 的后台 IO 线程（非主线程）中调用的
- 它直接调用 `wxGetApp().request_model_download()`，进而 `wxQueueEvent` 向主线程发送事件
- 下载线程完成时，立即投递 `EVT_IMPORT_MODEL_ID` 事件到主线程队列
- 此时 `CloudDownloadProgressDialog` 的定时器（200ms）还没到达下一次 tick，进度还显示 99%
- 主线程事件队列先处理了 `EVT_IMPORT_MODEL_ID`，开始执行 `import_model_id()`，显示导入进度条
- 而 `CloudDownloadProgressDialog` 的 `ShowModal()` 模态循环还在运行
- **结果**: 两个对话框同时可见

### 5.3 根本原因
后台下载线程直接触发导入操作，而不是让 UI 层（下载进度对话框）在合适的时机（关闭后）再触发导入。

## 6. 修复方案

### 6.1 核心思路
将导入触发时机从"后台线程下载完成时"改为"下载进度对话框关闭后"，确保：
1. 下载进度对话框完全关闭
2. 然后才触发导入操作
3. 导入进度对话框单独显示

### 6.2 具体修改

#### 修改 1: GUI_App.hpp
新增获取下载路径的接口：
```cpp
std::string get_3mf_download_path(const std::string& user_id, const std::string& file_id);
```

#### 修改 2: GUI_App.cpp
实现 `get_3mf_download_path()`，从缓存中查询文件路径。

#### 修改 3: ModelDownloader.cpp
在 `complete_cb` 中**移除**直接调用 `request_model_download`，改为只更新缓存数据（progress=100, path=xxx）。

#### 修改 4: CloudDownloadProgressDialog.cpp
在 `on_timer` 中、确认 `percent >= 100` 后：
1. 停止定时器
2. 获取文件路径
3. **先关闭对话框** (`EndModal`/`Close`)
4. **然后通过 `CallAfter` 触发导入**，确保对话框完全销毁后才显示导入进度条

### 6.3 修复后执行链路
```
下载线程完成 → 只更新缓存数据 (progress=100, path=xxx)
                    ↓
CloudDownloadProgressDialog::on_timer (主线程定时器) 检测到 100%
    ↓
EndModal/Close 关闭下载对话框
    ↓
CallAfter → 下一个事件循环触发导入
    ↓
import_model_id() → 创建导入进度对话框
```

## 7. 代码修改说明

### 文件: `src/slic3r/GUI/GUI_App.hpp`
- 新增 `get_3mf_download_path()` 方法声明

### 文件: `src/slic3r/GUI/GUI_App.cpp`
- 新增 `get_3mf_download_path()` 方法实现
- 从 `ModelDownloader` 缓存中查询指定 fileId 的文件路径

### 文件: `src/slic3r/GUI/ModelDownloader.cpp`
- 在 `start_download_3mf_group` 的 `complete_cb` lambda 中
- 移除 `wxGetApp().request_model_download()` 的直接调用
- 添加注释说明导入由 `CloudDownloadProgressDialog` 在关闭后触发

### 文件: `src/slic3r/GUI/CloudDownloadProgressDialog.cpp`
- 修改 `on_timer()` 方法
- 当 `percent >= 100` 时：
  - 停止定时器
  - 调用 `wxGetApp().get_3mf_download_path()` 获取文件路径
  - 调用 `EndModal(wxID_CLOSE)` 或 `Close()` 关闭对话框
  - 使用 `wxTheApp->CallAfter()` 延迟触发 `wxGetApp().request_model_download()`

### 文件: `src/slic3r/GUI/CloudDownloadProgressDialog.hpp`
- 无需修改

## 8. 验证建议
- [ ] 从社区个人中心下载 3MF 模型
- [ ] 观察下载进度对话框正常显示，到达 100% 后自动关闭
- [ ] 确认下载对话框完全消失后，导入进度对话框才出现
- [ ] 验证整个过程中不会出现两个进度条同时显示的情况
- [ ] 测试取消下载操作，确认不会触发导入
- [ ] 测试下载失败场景，确认不会触发导入

## 9. 调查记录
- ZenTao 页面关键信息:
  - 状态: `激活`
  - 严重程度: `一般`
  - 优先级: `高`
  - 影响版本: `CP 7.1.1（0430）`
  - 所属模块: `准备页面`
- 历史记录:
  - 2026-03-19 冷金辉 创建并指派给贺淼
  - 2026-03-25 梁树永 关联到计划 CP 7.1.1（0430）
  - 2026-03-31 贺淼 指派给李苏贵
- 相关 Bug:
  - Bug #13680: 与本问题类似的双进度条问题

## 10. 风险与回滚
- 回滚方式:
  - 恢复 `ModelDownloader.cpp` 中 `complete_cb` 的 `request_model_download` 调用
  - 恢复 `CloudDownloadProgressDialog.cpp` 中 `on_timer` 的原始实现
  - 移除 `GUI_App.hpp` 和 `GUI_App.cpp` 中的 `get_3mf_download_path` 方法
- 风险等级: `低`
- 需关注的副作用:
  - 确保 `CloudDownloadProgressDialog` 的定时器能正常检测到 100% 进度
  - 确保下载缓存数据正确保存，路径可被查询到
  - 极端情况下（如对话框被强制关闭）可能导致导入未触发

## 11. 后续建议
- 可考虑在下载对话框关闭和导入开始之间添加短暂延迟，提升视觉过渡体验
- 如果后续需要支持批量下载导入，需要重新设计进度显示策略
