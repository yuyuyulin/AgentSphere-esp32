# 贡献指南（Contributing）

欢迎参与 **AgentSphere ESP32** 开源固件开发！无论是修复 Bug、完善文档、优化语音/电源体验，还是适配新的 ESP32 开发板，都请遵循以下流程以保持仓库清晰与可维护。

## 1. 开发前准备

- 阅读核心文档以对齐产品与架构约束：
  - [产品功能需求](docs/requirements.md)
  - [端云通信协议](docs/protocol.md)
  - [固件技术架构](docs/architecture.md)
  - [当前交付状态](docs/status.md)
- 如使用 AI 编程助手协作，请同时遵守 [AGENTS.md](AGENTS.md) 与 [Issue 协作流程](docs/development/issue-workflow.md)。

## 2. 开发环境

- **框架**：ESP-IDF `v6.1`（目标芯片 `esp32s3`）
- **首台参考硬件**：微雪 `ESP32-S3-Touch-AMOLED-1.75C`（16MB Flash + 8MB Octal PSRAM）

拉取环境并编译的常规命令（工程骨架初始化后生效）：

```sh
# 激活本机 ESP-IDF 环境（按实际安装路径调整）
source "$HOME/.espressif/v6.1/esp-idf/export.sh"

idf.py set-target esp32s3
idf.py build
idf.py -p <串口设备> flash monitor
```

## 3. 提交与 PR 规范

1. **先建/认领 Issue**：大型改动或新板型适配请先在 GitHub Issues 讨论边界与方案。
2. **独立分支开发**：从最新 `main` 切出功能分支（如 `feat/<slug>`、`fix/<slug>` 或 `agent/issue-<编号>-<slug>`），不要直接提交到 `main`。
3. **Commit 风格**：推荐使用 Conventional Commits 前缀，支持中文描述：
   - `feat(board): 适配微雪 1.75C 显示与触控驱动`
   - `fix(audio): 修复打断播报后残留音频帧未清空的问题`
   - `docs(protocol): 补充 OTA 字段校验说明`
4. **提交前自检**：
   - 确保未包含真实 Wi-Fi 密码、Token、私有域名、个人绝对路径或构建产物；
   - 如修改了代码，至少确认本地 `idf.py build` 通过，并在 PR 模板中诚实勾选已执行的验证层级（构建通过 ≠ 真机验收通过）。

## 4. 新增硬件板型适配建议

新板型应以独立目录新增在 `main/boards/<board_name>/` 下，实现 `Board` 抽象接口：
- 有屏设备提供对应 `Display` 实现；
- 无屏设备直接复用 `AudioOnlyDisplay`，无需引入图形库依赖；
- 在 PR 中注明硬件规格（Flash/PSRAM/麦克风/功放/供电管理）与真机实测结果。
