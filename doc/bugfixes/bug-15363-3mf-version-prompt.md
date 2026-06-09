# Bug Fix Record

## 1. Basic Info
- Bug ID: `15363`
- Title: `“较新的3mf版本”提示框的提示语错误，如截图`
- Date: `2026-03-19`
- Reporter: `页面提取存在编码异常，待禅道页面人工确认`
- Assignee: `页面提取存在编码异常，待禅道页面人工确认`
- Branch/Commit:
- ZenTao: `https://zentao.creality.com/zentao/bug-view-15363.html`
- Evidence:
  - Screenshot: `C:\Users\cx2056\Pictures\CodexScreenshots\bug-15363.png`
  - Extracted JSON: `C:\Users\cx2056\Pictures\CodexScreenshots\bug-15363.json`

## 2. Symptom
- 打开由较新版本 Sanity Print 保存的 `3mf` 文件时，会弹出“较新的 3mf 版本”提示框。
- 提示框中的当前版本号/比较结果与用户实际安装版本不一致。
- 直接影响是提示语错误，用户会误判当前安装版本与文件版本的关系。

## 3. Scope
- Module: `版本解析 / 在线更新比较 / 3mf 版本检查`
- Key files:
  - `src/libslic3r/CrealityVersion.hpp`
  - `src/slic3r/GUI/GUI_App.cpp`
  - `src/slic3r/GUI/Plater.cpp`
  - `version.inc`
  - `CMakeLists.txt`
- Affected flows:
  - 3mf 导入版本校验
  - 在线更新版本比较
  - `skip_version` 跳过版本判断

## 4. Reproduction (Before Fix)
1. 安装四段版本号格式的 Sanity Print，例如 `7.1.0.4326`。
2. 打开由较新版本软件保存的 `3mf` 文件。
3. 观察“较新的 3mf 版本”提示框中的版本号比较结果。
4. 结果：提示内容中的版本号与当前安装版本号不一致，或比较逻辑异常。

## 5. Root Cause
- 现有 `Semver` 解析链路只支持三段语义版本，不支持直接解析 `SANITYPRINT_VERSION` 这类四段版本号。
- 代码中多个位置直接对 `SANITYPRINT_VERSION` 做 `Semver::parse()` 或等价比较，导致第四段构建号被忽略或解析失败。
- CMake 侧 `SLIC3R_BUILD_ID` 与 `SANITYPRINT_VERSION` 的来源此前也可能不一致，进一步放大了比较偏差。
- 以上结论来自当前代码改动分析，不是禅道页面原文直接说明。

## 6. Fix Strategy
- 新增专用版本解析 helper，将 `SANITYPRINT_VERSION` 拆成：
  - 前三段：语义版本比较
  - 第四段：构建号比较
- 约定第四段视为 `SLIC3R_BUILD_ID`，当版本字符串缺失第四段时，再回退到编译期 `SLIC3R_BUILD_ID`。
- 只在“需要解析/比较版本”的入口使用该 helper，保持展示字符串仍使用原始版本号。

## 7. Code Change Summary
- File: `src/libslic3r/CrealityVersion.hpp`
  - 新增 `ParsedCrealityVersion`、`parse_creality_version()`、`compare_creality_versions()`。
- File: `src/slic3r/GUI/GUI_App.cpp`
  - `get_version()` 改为通过新 helper 获取前三段 semver。
  - `check_new_version_cx()` 改为先比较 semver，再比较 build id。
  - `check_new_version_sf()` 改为使用统一四段比较逻辑。
  - `skip_version` 判断改为使用统一比较函数，不再做字符串字典序比较。
- File: `src/slic3r/GUI/Plater.cpp`
  - 3mf 导入版本检查不再直接解析四段 `SANITYPRINT_VERSION`。
- File: `version.inc`
  - 优先从 `SANITYPRINT_VERSION` 第四段提取 `SLIC3R_BUILD_ID`。
- File: `CMakeLists.txt`
  - 避免在顶层先把 `SLIC3R_BUILD_ID` 固定到环境变量，统一让 `version.inc` 决定最终来源。

## 8. Verification Checklist
- [x] `cmake --build C:/work/C3DSlicer/out/weiyusuo-release/build --target SanityPrint_app_gui --config Release` 通过。
- [ ] 本地安装四段版本号构建，导入较新版本生成的 `3mf`，确认提示框版本号正确。
- [ ] 在线更新场景中，验证 `7.1.0.4326 < 7.1.0.4364` 判断正确。
- [ ] 验证 `skip_version` 在同前三段、不同第四段时行为符合预期。
- [ ] 验证纯三段版本号仍可正常比较，不受回归影响。

## 9. ZenTao Notes
- 页面标题明确为：`“较新的3mf版本”提示框的提示语错误，如截图`。
- 页面中可识别信息显示：
  - 所属计划：`CP 7.1.0 Release`
  - 影响版本：`SanityPrint_7.1.0.4326_Beta`
- 页面导出的部分字段存在编码异常，因此状态/指派人未在本文中强行填写。

## 10. Rollback / Risk
- Rollback: 移除 `CrealityVersion.hpp` 并恢复 `GUI_App.cpp`、`Plater.cpp`、`version.inc` 的旧版本比较逻辑。
- Risk level: medium-low。
- Side effects to monitor:
  - 线上更新接口若返回非标准版本串，当前 helper 会回退到字符串比较。
  - 同前三段但不同第四段的比较结果会与旧逻辑不同，这是本次修复的预期行为。

## 11. Follow-up
- 建议后续补一个小型版本比较单元测试，覆盖：
  - `7.1.0.4326 < 7.1.0.4364`
  - `7.1.0 < 7.1.0.1`
  - `v7.1.0.4364-beta`
- 建议统一梳理所有 `SANITYPRINT_VERSION` 使用点，继续保持“展示用原串、比较走 helper”的边界。
