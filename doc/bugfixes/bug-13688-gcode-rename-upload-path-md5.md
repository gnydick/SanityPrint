# Bug Fix Record

## 1. Basic Info
- Bug ID: `13688`
- Title: `【服务器】修改gcode名称后第二个发送给机器，机器不会显示该Gcode，逻辑不对`
- Date: `2026-03-18`
- Reporter: `冷金辉`
- Assignee: `唐俊俊`
- Affected Version: `SanityPrint_7.0.0.3860_Beta`
- Plan: `CP 7.1.0 Release`
- Branch/Commit:

## 2. Symptom
- 用户第一次发送 `556.gcode` 到机器后，第二次仅修改名称为 `788.gcode` 再发送。
- 机器端文件列表不会按新名称显示第二次发送的 G-code。
- 影响：同一份切片内容仅改名再发送时，用户感知为“发送成功但文件名没有更新”。

## 3. Scope
- Module: `发送到创想云 / 设备上传`
- Key files:
  - `src/slic3r/GUI/print_manage/Device/KlipperCXInterface.cpp`
  - `src/slic3r/GUI/print_manage/UploadGcodeToCloud.cpp`
- Affected flows:
  - 设备页发送 G-code 到云端再下发设备
  - Upload Gcode to Cloud 对话框上传路径生成

## 4. Reproduction (Before Fix)
1. 切片完成后进入发送页。
2. 设置 G-code 名称为 `556.gcode` 并发送打印。
3. 再次进入发送页，不改切片内容，只将名称改为 `788.gcode` 后再次发送。
4. 结果：机器文件列表不显示第二次的新名称。

注：以上复现步骤来自禅道页面提取内容。

## 5. Root Cause
- 代码中上传显示名 `target_name` 会随用户重命名变化。
- 但实际上传路径 `target_path` 原先仅使用 `get_file_md5(localFilePath)` 生成，即只和文件内容相关。
- 当文件内容不变、仅文件名变化时，两次上传会得到相同的 OSS `fileKey`/路径，导致服务端或设备侧继续复用旧文件标识，新名称无法体现。

注：本节为根据当前代码修改推断出的根因，不是禅道原文直接描述。

## 6. Fix Strategy
- 保持原有路径规则不变，仍使用 `model/slice/<md5>.gcode.gz`。
- 调整该 `md5` 的生成输入，不再只取文件内容，而是改为：
  - 先取文件内容 MD5
  - 再将 `显示名称（去掉 .gcode） + ":" + 内容MD5` 组合后重新计算一次 MD5
- 这样可以满足：
  - 仅改文件名时，上传路径变化
  - 仅改文件内容时，上传路径也变化
  - 路径格式及服务端约定保持不变

## 7. Code Change Summary
- File: `src/slic3r/GUI/print_manage/Device/KlipperCXInterface.cpp`
  - 新增本地 helper `build_gcode_upload_path(...)`
  - 将上传路径从“纯内容 MD5”改为“名称 + 内容共同参与的 MD5”
- File: `src/slic3r/GUI/print_manage/UploadGcodeToCloud.cpp`
  - 新增同样的 `build_gcode_upload_path(...)`
  - 统一 Upload Gcode to Cloud 入口的路径生成逻辑
- 最终上传路径格式保持为：
  - `model/slice/<md5>.gcode.gz`

## 8. Verification Checklist
- [ ] 同一份 G-code，第一次命名为 `556.gcode` 上传成功。
- [ ] 不改内容，仅改名为 `788.gcode` 再次上传，生成新的 `target_path`。
- [ ] 机器端文件列表显示第二次的新名称。
- [ ] 同名但内容变化时，上传路径仍然变化。
- [ ] 两个入口行为一致：
  - `KlipperCXInterface`
  - `UploadGcodeToCloudDialog`

## 9. Risk / Compatibility
- Risk level: low
- 风险点：
  - 同一内容但不同名称会生成不同上传路径，云端对象去重能力会下降。
  - 如果服务端存在基于历史 `fileKey` 的特殊缓存逻辑，需要关注是否依赖“纯内容 MD5”。
- 兼容性：
  - 路径格式未改，仍为既有 `model/slice/<md5>.gcode.gz`
  - 仅 `md5` 的输入规则发生变化

## 10. Rollback
- 回滚 `KlipperCXInterface.cpp` 和 `UploadGcodeToCloud.cpp` 中的 `build_gcode_upload_path(...)` 逻辑。
- 恢复为仅使用 `get_file_md5(localFilePath)` 生成路径。

## 11. Notes
- 禅道关键信息来源：`https://zentao.creality.com/zentao/bug-view-13688.html`
- 当前未执行完整编译和真机上传验证，验证项仍需联调确认。
