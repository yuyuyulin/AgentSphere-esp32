# Issue 与多 Agent 协作流程

本仓库通过 GitHub Issues 与 Pull Requests 协调人类开发者与多个 AI 编程助手的并行开发。所有 GitHub 操作统一使用本地已授权的 `gh` CLI 完成，不依赖浏览器自动化。

---

## 1. 信息唯一事实源

| 信息类型 | 维护位置 |
| --- | --- |
| 任务队列、领取状态、阻塞与依赖 | GitHub Issues（本仓库） |
| 当前固件能力与真机验收边界 | [docs/status.md](../status.md) |
| 产品功能与交互规范 | [docs/requirements.md](../requirements.md) |
| 端云通信契约 | [docs/protocol.md](../protocol.md) |
| 固件分层架构与分区设计 | [docs/architecture.md](../architecture.md) |

---

## 2. 互斥状态标签

使用 `.github/labels.json` 定义的 6 个互斥状态标签追踪每个 Issue 的实时阶段，切换状态时必须先移除旧 `status:*` 标签：

- `status:triage`：需求待澄清或待拆分为可执行开发任务；
- `status:ready`：边界、依赖与验收标准已明确，可被领取；
- `status:in-progress`：已由明确开发者或 Agent 会话领取；
- `status:blocked`：存在明确阻塞，评论写清阻塞项与所需配合，禁止自动抢占；
- `status:review`：已创建 PR，等待 CI 检查与代码审阅；
- `status:acceptance`：代码已合入，等待真实开发板烧录实测与验收。

仅在真实硬件/功能验收通过后关闭 Issue；取消的任务标为 `not planned` 并说明原因。

---

## 3. 领取与开工步骤

执行任务前按顺序操作：

1. 同步主干并核对工作区：
   ```sh
   git fetch origin
   git status -sb
   ```
2. 查看 Issue 详情、评论及依赖状态：
   ```sh
   gh issue view <编号> --comments
   ```
3. 核对不存在并发领取或文件冲突后，切换状态并评论登记：
   ```sh
   gh issue edit <编号> --remove-label "status:ready" --add-label "status:in-progress"
   gh issue comment <编号> --body "领取：<Agent/开发者标识>
   状态：in-progress
   基线：<origin/main commit SHA>
   分支：agent/issue-<编号>-<slug>
   负责文件：<预期修改或新增的文件列表>
   依赖核对：<前置 Issue 或硬件条件就绪情况>"
   ```
4. 再次 `gh issue view <编号> --comments` 确认未发生并发领取冲突。
5. 从最新 `origin/main` 切出独立任务分支：
   ```sh
   git checkout -b agent/issue-<编号>-<slug> origin/main
   ```

---

## 4. 开发、验证与提交 PR

1. **按契约实现**：严格限定在 Issue 声明的模块与架构边界内修改；涉及协议、分区或硬件引脚变更时，同步更新 `docs/`。
2. **本地验证**：
   - 文档类改动：核对所有相对链接有效、无敏感信息或私有域名残留；
   - 固件类改动：执行 `idf.py build` 验证 `esp32s3` 目标可干净编译通过，并在可行时烧录真机实测。
3. **定向提交并推送任务分支**：
   ```sh
   git status -sb
   git diff --stat
   git add <本任务涉及文件>
   git commit -m "feat(<模块>): <简明中文描述>"
   git push -u origin agent/issue-<编号>-<slug>
   ```
4. **创建 PR 并回填 Issue**：
   - 使用 `gh pr create --base main --title "..." --body "..."`，正文包含 `Refs #<编号>`、变更摘要、验证命令与层级（静态 / 编译 / 真机），以及待验收项；
   - 在 Issue 评论回填 PR 链接、实际验证结果与剩余待验收项，并将标签更新为 `status:review`：
     ```sh
     gh issue edit <编号> --remove-label "status:in-progress" --add-label "status:review"
     ```

---

## 5. 合入与真机验收

- PR 通过 CI 检查与审阅后合入 `main`。
- 若该任务依赖开发板实机测试（例如屏幕刷新、双麦收音、唤醒命中、充放电切换）且尚未在真机完成验证，合入后将 Issue 标记为 `status:acceptance`，待真机实测通过并记录证据后再关闭 Issue。
- 重要里程碑验收通过后，同步更新 [docs/status.md](../status.md)。
