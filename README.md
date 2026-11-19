# AgentSphere ESP32

面向 [AgentSphere](https://github.com/yuyuyulin/AgentSphere-esp32) 生态的开源 ESP32 随身语音伴侣固件项目。

本项目专注于 **ESP32 设备端固件**，旨在将单块 ESP32-S3 开发板打造成具备完整 C 端体验的便携 AI 伴侣：开箱本地自包含配网、6 位短期配对码认领、10 态运行状态机、WSS + Opus 实时全双工语音对话、0ms 本地打断、插电/电池自适应功耗管理与双分区 OTA 防变砖。

## 支持硬件

| 板型 | 状态 | 硬件亮点 |
| --- | --- | --- |
| 微雪 `ESP32-S3-Touch-AMOLED-1.75C` | 首台验证板（开发中） | 1.75 寸 466×466 圆形 AMOLED 触控屏、双麦 + ES7210/ES8311 硬件回声消除、`PWR`/`BOOT` 实体键、AXP2101 电源管理、QMI8658 六轴 IMU、PCF85063 RTC、16MB Flash + 8MB PSRAM |
| 无屏 / 其他屏幕 ESP32-S3 设备 | 架构已预留扩展边界 | 通过 `Board HAL` 与 `Display` 抽象解耦，无屏设备复用同一套配网、状态机、协议与音频管线 |

## 核心特性（规划基线）

- **跨形态本地配网**：设备内置 SoftAP（`AgentSphere-XXXX`）+ Captive Portal + `http://192.168.4.1` 配网页，无需外部 App 或服务端页面，记忆 3~5 个常用 Wi-Fi。
- **安全认领与解绑**：未绑定设备通过临时 `claim_secret` 与 6 位短期配对码在 AgentSphere Web 完成认领，换取可轮换独立凭据，不信任自报 MAC。
- **实时语音与 0ms 打断**：基于 `WSS + Opus`（16kHz 单声道 60ms）流式交互，端侧按 `turn_id` 防串台，按键/触屏/倒扣屏幕即时静音并丢弃迟到回复。
- **随身与桌面双态体验**：插 USB 自动切入桌面免手唤醒态并防 AMOLED 烧屏；电池态支持拿起亮屏、定时息屏与闲置自动关机。
- **通用底座 + 场景菜单**：主屏情感陪伴与日常对话，左滑菜单切入独立上下文场景（首期为极简英语口语练习）。
- **C 端量产底线**：16MB Flash 独立划分双 4MB OTA 槽位（启动失败自动回滚）、独立语音模型区与独立静态资源/皮肤区。

## 文档索引

- [当前交付与验证状态](docs/status.md)：当前阶段、构建与真机验收进度总览
- [产品功能需求](docs/requirements.md)：定位、配网认领、10 态状态机、按键/IMU 与圆屏交互规范
- [端云通信协议（v1 草案）](docs/protocol.md)：HTTPS 引导/认领/OTA 与 WSS + Opus 信令契约
- [固件技术架构](docs/architecture.md)：ESP-IDF v6.1 选型、16MB 分区表、四层解耦目录与双核内存规划
- [贡献指南](CONTRIBUTING.md) · [Agent 协作规范](AGENTS.md) · [Issue 工作流程](docs/development/issue-workflow.md)

## 开发环境要求

- **ESP-IDF**：`v6.1`（目标芯片 `esp32s3`）
- **固件工程初始化**：即将随 Step 0 任务落地，届时可直接运行 `idf.py set-target esp32s3 && idf.py build`。

## 开源协议

本项目基于 [MIT License](LICENSE) 开源。
