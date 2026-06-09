# Bug 修复记录（14766）

## 基本信息
- Bug ID：`14766`
- 禅道链接：`https://zentao.creality.com/zentao/bug-view-14766.html`
- 标题：`添加 boost::log 核心头文件支持`
- 创建时间：`2026-03-03`
- 创建人/反馈来源：
- 当前状态：`激活`
- 严重程度/优先级：`中 / 中`
- 产品/模块：`Sanity Print / 顶部工具栏`
- 所属计划：
- 指派给：
- 关联分支/基线：`release-260330`
- 修复日期：`2026-03-03`

## 问题现象
- 修改分辨率后切换语言导致偶发崩溃

## 复现步骤
1. 偶发

## 根因分析
- 疑似重入

## 修复方案
- 增加重入检查日志

## 代码改动摘要
- `src/slic3r/GUI/BBLTopbar.cpp`
  - 在头文件引用区域添加 `#include <boost/log/core.hpp>`
  - 增加代码重入检查

## 验证清单
- [ ] 项目能够正常编译。

## 风险与回退
- 风险：低，仅添加头文件，不影响现有逻辑。
- 回退：回退文件 `src/slic3r/GUI/BBLTopbar.cpp`和`src/slic3r/GUI/BBLTopbar.hpp`。

## 备注
- 本次修改为依赖头文件补充，确保 boost::log 核心功能可用性。
