# BUG 修复记录 - 15418

【Bug ID】
- 15418
- 禅道链接: https://zentao.creality.com/zentao/bug-view-15418.html
- 标题: 新增参数没翻译
- 产品/模块: Sanity Print / 工艺管理
- 严重程度/优先级: 一般 / 中

【问题现象】
- 新增参数在界面中显示为英文，中文环境下未展示中文文案。
- 与现有参数的本地化体验不一致，影响可读性和理解效率。

【根因分析】
- 新增参数文案仅以英文字符串落地，未完整走国际化链路。
- 参数说明渲染层存在显示边界问题：若不调整 `MarkdownTip` 相关逻辑，UI 参数 `label/tooltip` 页面会出现 `label` 显示不全。
- 具体表现为以下一种或多种情况:
  - 新文案未使用 `_L(...)`（或按场景使用 `_L_ZH(...)`）包装。
  - 新文案未被提取到 `SanityPrint.pot`。
  - `zh_CN` 对应 `po` 未补齐 `msgstr`。
  - `mo` 文件未重新编译，运行时仍加载旧翻译产物。

【修复方案】
1. 代码侧将新增参数文案统一纳入 i18n 调用（推荐 `_L("...")`）。
2. 修复参数提示渲染逻辑，避免 `label/tooltip` 页中 `label` 被截断（`MarkdownTip.cpp`）。
3. 在项目根目录执行提取与合并:
   - `run_gettext.bat --full`
4. 在中文翻译文件补齐新增文案:
   - `localization/i18n/zh_CN/SanityPrint_zh_CN.po`
5. 重新编译语言包:
   - `run_gettext.bat`
6. 启动应用并在中文环境验证参数名称显示。

【改动文件】
- `src/slic3r/GUI/MarkdownTip.cpp`
  - 说明: 该文件改动用于修复 UI 参数 `label/tooltip` 页面的 `label` 显示不全问题；若不改会出现文本截断。
- `src/...`（新增参数所在代码文件，新增或修正 `_L(...)` 调用）
- `localization/i18n/SanityPrint.pot`
- `localization/i18n/zh_CN/SanityPrint_zh_CN.po`
- `resources/i18n/zh_CN/SanityPrint.mo`

【验证结果】
- 禅道问题 `BUG #15418` 已复核，问题为“新增参数没翻译”。
- 修复后在中文环境下，新增参数显示中文文案。
- 英文环境保持原始英文显示，不影响其他语种加载。
- `msgfmt --check-format` 编译通过，未出现格式占位符错误。

【风险与回退】
- 风险:
  - 若误改 `msgid`，可能导致历史翻译无法匹配。
  - 若占位符（如 `%1%`/`%s`）与源文案不一致，可能触发运行时格式问题。
- 回退方案:
  - 回退本次参数文案相关代码变更与 `po/mo` 文件。
  - 重新执行 `run_gettext.bat` 以恢复到回退后的语言包状态。

【分支/提交】
- 当前分支: `release-260330`
- 当前基线提交: `24f0a49be`
- Bug 修复提交: `待补充`（建议提交信息: `fix(i18n): translate new parameter text for bug #15418`）
