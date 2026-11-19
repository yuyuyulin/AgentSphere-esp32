# Agent 协作规范（AGENTS.md）

本仓库为 **公开开源的 ESP32 固件项目**，支持多端、多 AI 编程助手（Antigravity、Claude Code、Codex、Cursor 等）与人类开发者共同协作。凡在本仓库进行代码阅读、修改、测试、提交或 GitHub 操作，须统一遵守以下准则。

---

## 1. 公开开源安全红线（绝对禁止）

本仓库为公开仓库，任何提交前必须自检 `git status` 与 `git diff`，**严禁提交以下内容**：
- 真实生产域名、内网 IP、服务器 SSH 地址或个人本机绝对路径（文档示例统一使用 `api.agentsphere.example` 等 RFC 保留占位域名）；
- Wi-Fi SSID/密码、设备 Token、`claim_secret`、API Key、私钥或未脱敏的抓取日志；
- 构建产物、缓存及临时文件（`build/`、`managed_components/`、`sdkconfig`、`.DS_Store` 等）。

---

## 2. 任务开始前：远端同步与信息核对

1. **同步检查**：
   - 执行 `git fetch origin`，对比当前分支与 `origin/<当前分支>` 状态。
   - 若工作区干净且远端领先，执行 `git pull --rebase`。
   - 若本地存在未提交改动且远端有更新，或同步过程发生任何冲突，**严禁强制覆盖或盲目 `stash pop`**，应立即停止并向用户汇报处理方案。
2. **必读基线文档**：
   - [当前交付状态](docs/status.md)：当前已编译能力与真机实测边界；
   - [产品功能需求](docs/requirements.md)：C 端定位、状态机、按键与 UI 规则；
   - [端云通信协议](docs/protocol.md)：HTTPS 引导/认领/OTA 与 WSS + Opus 契约；
   - [固件技术架构](docs/architecture.md)：16MB 分区表、四层板级解耦与双核内存边界；
   - [Issue 协作流程](docs/development/issue-workflow.md)：领取、分支、PR 与验收回填规范。

---

## 3. 任务进行中：架构边界与验证纪律

1. **严格遵守分层解耦**：
   - 板级引脚、芯片与 BSP 细节必须隔离在 `main/boards/<board>/` 内，核心状态机、配网、协议与音频管线禁止直接耦合特定屏幕或芯片；
   - 保持无屏 ESP32 兼容能力，不在通用层强依赖 LVGL；
   - 不随意改动当前任务范围之外的模块或大面积格式化无关文件。
2. **诚实区分验证层级**：
   - **静态检查 / 文档检查**：CI 与链接/格式检查；
   - **构建验证**：使用 ESP-IDF（当前目标 `v6.1`，target `esp32s3`）执行 `idf.py build` 通过，并记录固件体积与分区余量；
   - **真机实测**：实际烧录至目标开发板（首台为微雪 `ESP32-S3-Touch-AMOLED-1.75C`）后的硬件行为观测。
   - **严禁把“编译通过”写成“真机验收通过”**；未上板测试的项必须在 PR 与状态中明确标注为待真机验收。

---

## 4. 任务完成后：分支、PR 与 GitHub CLI 规范

本仓库采用 **Issue 驱动 + 任务分支 + PR 合入** 流程，**日常开发不得直接推送 `main`**：

1. **统一使用 `gh` CLI**：所有 GitHub 操作（查看/评论/改标签 Issue、创建 PR 等）均通过本地已授权的 `gh` 命令行完成，无需也不应使用浏览器自动化操作 GitHub。
2. **开工登记**：按 [Issue 协作流程](docs/development/issue-workflow.md) 读取目标 Issue，切换互斥标签为 `status:in-progress`，评论声明执行会话、分支名与负责文件清单。
3. **分支命名**：从同步后的 `main` 切出 `agent/issue-<编号>-<slug>`（例如 `agent/issue-1-hw-bringup`）。不在其他 Agent 正在占用的工作目录中切分支；已推送分支如需同步 `origin/main`，使用 `git merge` 而非 `force push`。
4. **提交与 PR**：
   - 定向 `git add` 本次任务文件（严禁盲目 `git add -A` 带入敏感或无关文件）；
   - 使用清晰的 Conventional Commits 风格或简洁中文提交说明（如 `feat(audio): 实现 60ms Opus 编码与打断清队列`）；
   - 推送任务分支后通过 `gh pr create --base main` 自动创建 PR，正文使用 `Refs #<编号>`（不自动关闭仍需真机验收的 Issue），写明变更摘要、验证层级/命令与待验收项；
   - 回填 Issue 评论并将标签切换为 `status:review`。
5. **文档同步**：接口、架构、引脚、分区或能力状态变化时，必须同步更新对应文档与 [docs/status.md](docs/status.md)。
