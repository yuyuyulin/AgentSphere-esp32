# 当前交付与验证状态

更新于 2026-11-19。本页记录 **AgentSphere ESP32** 当前的真实工程进度，严格区分「文档定稿」「编译通过」与「真机实测验收」。

## 当前阶段

**Step 0 工程骨架与微雪 1.75C 验板固件已完成 ESP-IDF `v6.1` 构建验证（PR 评审中，待真机烧录验收）**。

## 模块状态总览

| 阶段 / 模块 | 当前状态 | 已确认证据 | 待验证 / 限制 |
| --- | --- | --- | --- |
| 产品功能需求（[requirements.md](requirements.md)） | 已定稿 | 明确 C 端随身定位、本地 SoftAP 配网、6 位配对码认领、10 态状态机、按键/IMU 与圆屏 2 层交互 | 电池休眠/关机时长及唤醒阈值待真机实测定值 |
| 端云通信协议（[protocol.md](protocol.md)） | v1 草案已定稿 | 对齐 AgentSphere 平台契约，定义 HTTPS 引导/认领/OTA 与 WSS + Opus 语音信令 | 待 AgentSphere 服务端补齐 ESP32 通道后端到端联调 |
| 固件技术架构（[architecture.md](architecture.md)） | 已定稿 | 已实测 `waveshare/esp32_s3_touch_amoled_1_75c@3.0.0` + `lvgl@9.6.0` + `esp_codec_dev@1.5.11` 在 ESP-IDF `v6.1` 下解析并编译通过 | `esp-sr >= 2.6` 与 Opus 组件将在后续阶段引入并实测 |
| 开源治理与 GitHub 协作 | 已就绪 | MIT 协议、`.gitignore`、`AGENTS.md`、`CONTRIBUTING.md`、Issue/PR 模板、13 个仓库标签与基础 CI 均已生效 | 后续可补充云端 ESP-IDF 自动编译 CI |
| Step 0 · 1.75C 工程骨架与真机验板（#1） | 构建通过，待真机验收 | `idf.py build`（ESP-IDF v6.1，`esp32s3`）零错误零警告通过；`agentsphere_esp32.bin` 体积 `0xb5750`（约 726 KB），4MB `ota_0` 余量 82%；实现四层解耦接口、无屏 `AudioOnlyDisplay` 与 1.75C 板级自检 | 待插入 1.75C 开发板烧录，实测 AMOLED 刷新、CST9217 触控、ES7210/ES8311 录音回环、AXP2101/QMI8658/PCF85063 与 `PWR`/`BOOT` 按键 |
| 阶段 A · 配网 / 认领 / Opus 首轮对话（#2~#4） | 待解锁 | 已拆分为独立开发 Issue | 依赖 #1 合入与服务端 ESP32 通道 |
| 阶段 B · V1.0 日常可用（唤醒/表情/电源/OTA） | 未开始 | — | 依赖阶段 A |
| 阶段 C · 极简英语口语练习场景 | 未开始 | — | 依赖阶段 B 与服务端场景隔离支持 |
