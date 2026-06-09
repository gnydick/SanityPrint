SanityPrint 简易更新服务器说明
================================

1. 功能概述
-----------
- 提供一个简易的 HTTP 服务，用于给 SanityPrint 主程序返回更新信息，并托管 RELEASES 和 .nupkg 包。
- 主要接口：
  - GET /api/update/check
  - 静态文件：/releases/RELEASES 和各版本 .nupkg


2. 目录结构
-----------
- tools/update_server/
  - main.go        更新服务器源码
  - go.mod         Go 模块定义
  - readme.txt     本说明文件
  - releases/      存放 RELEASES 和各类 .nupkg 包（需要手动创建和放置）


3. 构建与运行
-------------
在仓库根目录打开终端：

1) 进入目录
   cd tools\update_server

2) 构建（可选）
   go build ./...

3) 运行
   go run .
   或者使用生成的可执行文件：
   .\update_server.exe

默认监听地址： http://localhost:9000


4. 接口说明：/api/update/check
-----------------------------
请求：
- 方法：POST（推荐）。当前实现同时兼容 GET 查询参数，方便浏览器调试。
- POST JSON 体：
  {
    "currentVersion": "6.0.1-alpha",
    "platform": "windows"
  }

响应 JSON 示例：
{
  "hasUpdate": true,
  "latestVersion": "7.0.0-alpha",
  "basePackage": "SanityPrint-6.0.1-alpha-full.nupkg",
  "fullFile": {
    "name": "SanityPrint-7.0.0-alpha-full.nupkg",
    "url": "http://localhost:9000/releases/SanityPrint-7.0.0-alpha-full.nupkg",
    "size": 123456,
    "sha1": "xxxxxxxx..."
  },
  "deltaFiles": [
    {
      "name": "SanityPrint-6.0.1-alpha-to-7.0.0-alpha-delta.nupkg",
      "url": "http://localhost:9000/releases/SanityPrint-6.0.1-alpha-to-7.0.0-alpha-delta.nupkg",
      "size": 654321,
      "sha1": "yyyyyyyy..."
    }
  ]
}

字段含义：
- hasUpdate：是否有可用新版本
- latestVersion：服务器认为的目标版本
- basePackage：调用方当前版本对应的 full 包文件名（用于客户端生成本地 RELEASES 第一行），如不存在可为空
- fullFile：目标版本的 full 包信息（只要 RELEASES 中有 full 记录，这里就一定非空）
- deltaFiles：从 currentVersion 升级到 latestVersion 的所有 delta 包列表，可为空

核心逻辑（当前实现，基于 RELEASES 自动推导，**不再在服务器做“增量/全量”筛选**）：
- 服务器会解析 releases/RELEASES，得到所有版本的 full/delta 记录；
- 结合 currentVersion，按版本号从低到高计算“升级链”（大于 currentVersion 的所有版本）；
- 如果没有比 currentVersion 更新的版本：
-  - 返回 hasUpdate=false，latestVersion 等于 currentVersion，basePackage 为空；
- 如果存在升级链：
  - target = 链中最后一个版本，对应 latestVersion；
  - 如果 target 没有 full 记录，则认为数据不完整，返回错误；
  - fullFile：返回 target 对应的 full 包信息；
  - deltaFiles：返回从 currentVersion 升级到 target 过程中涉及到的**所有** delta 包（不做大小比较也不做策略筛选）；

注意：
- 服务器不再根据包大小决定“本次用 full 还是 delta”，只负责按版本返回 fullFile + 全量 deltaFiles；
- SanityPrint 客户端会在下载前，根据 fullFile.size 与所有 deltaFiles.size 的总和，在本地决定：
  - 若 delta 总大小为 0 或大于 full 包大小：只下载 fullFile；
  - 否则：下载 deltaFiles 中的全部包；
- fullFile 始终提供 last full 包的信息，方便客户端：
  - 做兜底方案（必要时直接下载 full 包）；
  - 在本地生成 RELEASES 等元数据文件时使用。


5. 静态文件托管：/releases
-------------------------
- 服务器会将 tools/update_server/releases 目录映射为：
  http://localhost:9000/releases/

你需要手动准备：
- releases/RELEASES
- releases/SanityPrint-7.0.0-alpha-full.nupkg
- releases/SanityPrint-6.0.1-alpha-to-7.0.0-alpha-delta.nupkg
  （具体文件名只要与 RELEASES 中保持一致即可）

访问示例：
- http://localhost:9000/releases/RELEASES
- http://localhost:9000/releases/SanityPrint-7.0.0-alpha-full.nupkg

测试：
- 浏览器访问（GET 方式，便于快速调试）：
- - http://localhost:9000/api/update/check?currentVersion=6.0.1-alpha&platform=1
-  - 应返回 hasUpdate=true ，并带 full + delta
- - http://localhost:9000/api/update/check?currentVersion=7.0.0-alpha&platform=1
-  - 应返回 hasUpdate=false
- http://localhost:9000/releases/SanityPrint-7.0.0-alpha-full.nupkg
  - 应直接下载你上传的 full 包


6. 可配置项
-----------
当前实现中，更新服务器的行为主要由 releases/RELEASES 和实际存在的 .nupkg 文件决定，版本号与包名不再通过环境变量配置。

仅保留一个环境变量：

- CP_UPDATE_BASE_URL
  - 用于生成文件下载 URL 的基础地址。
  - 默认值： http://localhost:9000
  - 示例：
    - CP_UPDATE_BASE_URL=http://192.168.1.10:9000

注意：
- 对应的 .nupkg 文件必须真实存在于 releases/ 目录，否则：
  - full 包缺失会导致服务器返回错误；
  - delta 包缺失会被忽略（只返回 full 更新方案）。


7. 与 SanityPrint 主程序的配合关系（简要）
-----------------------------------------
- 主程序在检查更新时，调用：
  - GET http://localhost:9000/api/update/check?currentVersion=<当前版本>&platform=<平台标记>
- 解析 JSON：
  - 如果 hasUpdate=false：提示“当前已是最新版本”；
  - 如果 hasUpdate=true：
    - 从 fullFile/deltaFiles 中读取每个需要下载的 .nupkg 的 name/url/size/sha1；
    - 根据约定选择更新策略：
      - 若 deltaFiles 非空：按“增量更新”处理，需要全部下载 deltaFiles 中列出的 delta 包；
      - 若 deltaFiles 为空：按“全量更新”处理，只下载 fullFile 对应的 full 包；
    - 下载每个 .nupkg 时，主程序负责：
      - 检查实际文件大小是否等于 JSON 中的 size；
      - 计算整体 SHA1 是否等于 JSON 中的 sha1；
      - 任意文件校验失败则放弃本轮更新，保留现有版本；
    - 所有 .nupkg 下载成功后，主程序会在本地更新目录中，根据 JSON 提供的包列表，写出一个简化版 RELEASES 文件（每个包一行，包含 sha1 / 文件名 / size），供 updater.exe 使用；
    - 最后，主程序关闭自身并在该目录下启动 updater.exe，由 updater.exe 按 RELEASES + .nupkg 执行本地更新流程。

本 readme 仅描述服务器一端的行为，主程序侧的下载、校验和本地 RELEASES 生成逻辑在 GUI_App.cpp、ReleaseNote.cpp 以及 tools/updater 中实现。
