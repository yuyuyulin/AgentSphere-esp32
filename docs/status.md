# 当前交付与验证状态

更新于 2026-11-19。本页记录 **AgentSphere ESP32** 当前的真实工程进度，严格区分「文档定稿」「编译通过」与「真机实测验收」。

## 当前阶段

**设计基线与开源仓库规范已完成**，即将进入 **Step 0 · 工程骨架搭建与微雪 1.75C 真机验板**。

## 模块状态总览

| 阶段 / 模块 | 当前状态 | 已确认证据 | 待验证 / 限制 |
| --- | --- | --- | --- |
| 产品功能需求（[requirements.md](requirements.md)） | 已定稿 | 明确 C 端随身定位、本地 SoftAP 配网、6 位配对码认领、10 态状态机、按键/IMU 与圆屏 2 层交互 | 电池休眠/关机时长及唤醒阈值待真机实测定值 |
| 端云通信协议（[protocol.md](protocol.md)） | v1 草案已定稿 | 对齐 AgentSphere 平台契约，定义 HTTPS 引导/认领/OTA 与 WSS + Opus 语音信令 | 待 AgentSphere 服务端补齐 ESP32 通道后端到端联调 |
| 固件技术架构（[architecture.md](architecture.md)） | 已定稿 | 明确 ESP-IDF v6.1、16MB 双 OTA/模型/资源分区、四层板级解耦与双核内存边界 | 待实测微雪 1.75C BSP 与 `esp-sr >= 2.6` 在 ESP-IDF v6.1 下的组件解析与编译 |
| 开源治理与 GitHub 协作 | 已就绪 | MIT 协议、`.gitignore`、`AGENTS.md`、`CONTRIBUTING.md`、Issue/PR 模板、互斥状态标签与基础 CI | 固件骨架建立后需在 CI 中补充 `idf.py build` 编译作业 |
| Step 0 · 1.75C 工程骨架与真机验板 | 未开始 | 本地已核实安装 ESP-IDF v6.1（含 `esp32s3`）且开发板在手边 | 待创建 Issue 并完成首版验板固件烧录实测 |
| 阶段 A · 配网 / 认领 / Opus 首轮对话 | 未开始 | — | 依赖 Step 0 与 AgentSphere 服务端 ESP32 通道 |
| 阶段 B · V1.0 日常可用（唤醒/表情/电源/OTA） | 未开始 | — | 依赖阶段 A |
| 阶段 C · 极简英语口语练习场景 | 未开始 | — | 依赖阶段 B 与服务端场景隔离支持 |
