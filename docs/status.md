# 当前交付与验证状态

更新于 2026-11-20。本页记录 **AgentSphere ESP32** 当前的真实工程进度，严格区分「文档定稿」「编译通过」与「真机实测验收」。

## 当前阶段

**Step 0 工程骨架与微雪 1.75C 真机验板已实测通过**，准备进入 **阶段 A（Issue #2 本地 SoftAP 配网与多 Wi-Fi 管理）**。

## 模块状态总览

| 阶段 / 模块 | 当前状态 | 已确认证据 | 待验证 / 限制 |
| --- | --- | --- | --- |
| 产品功能需求（[requirements.md](requirements.md)） | 已定稿 | 明确 C 端随身定位、本地 SoftAP 配网、6 位配对码认领、10 态状态机、按键/IMU 与圆屏 2 层交互；已确定角色名「小安」与主唤醒词「小安同学」 | 电池休眠/关机时长及唤醒阈值待真机实测定值 |
| 端云通信协议（[protocol.md](protocol.md)） | v1 草案已定稿 | 对齐 AgentSphere 平台契约，定义 HTTPS 引导/认领/OTA 与 WSS + Opus 语音信令 | 待 AgentSphere 服务端补齐 ESP32 通道后端到端联调 |
| 固件技术架构（[architecture.md](architecture.md)） | 已定稿 | 已实测 `waveshare/esp32_s3_touch_amoled_1_75c@3.0.0` + `lvgl@9.6.0` + `esp_codec_dev@1.5.11` 在 ESP-IDF `v6.1` 下解析并编译通过 | `esp-sr >= 2.6` 与 Opus 组件将在后续阶段引入并实测 |
| 开源治理与 GitHub 协作 | 已就绪 | MIT 协议、`.gitignore`、`AGENTS.md`、`CONTRIBUTING.md`、Issue/PR 模板、13 个仓库标签与基础 CI 均已生效 | 后续可补充云端 ESP-IDF 自动编译 CI |
| Step 0 · 1.75C 工程骨架与真机验板（#1） | 真机验收通过 | ESP-IDF `v6.1`（`esp32s3`）构建通过（约 735 KB，`ota_0` 余量 82%）；真机实测通过：I2C 5 芯片（ES8311/AXP2101/ES7210/CST9217/QMI8658）全部在线、466×466 AMOLED 显示与触控光圈/开心表情正常、开机提示音与 `BOOT` 录音回放可出声、AXP2101 充放电与电量（`93% / 4107mV`）及 IMU 读数正常、8MB PSRAM（余 8095 KB）与 SRAM（余 252 KB）稳定；确认 1.75C 无外置 PCF85063 并已自动回退 ESP32-S3 内置 RTC，同时将默认音量提至 95% 改善音量偏小问题 | 后续阶段 A/B 继续调优实际语音播报响度与 IMU 体感阈值 |
| 阶段 A · 本地 SoftAP 配网与多 Wi-Fi（#2） | 构建通过，待真机实测 | ESP-IDF `v6.1`（`esp32s3`）零警告编译通过（`0x159820` / 约 1.35 MB，`ota_0` 余量 66%）；已实现 NVS 多 Wi-Fi（5 个）持久化、开机自动扫描回连与 18s 超时转离线、按需 SoftAP（`AgentSphere-XXXX`）+ Captive Portal DNS + `192.168.4.1` 配网页及圆屏二维码/跳过离线按钮 | 待烧录至 1.75C 真机验证首次开箱配网、手机弹窗/提交、重启自动回连、离线跳过及退出配网内存回收 |
| 阶段 A · Bootstrap 认领 / WSS + Opus（#3、#4） | 待解锁 | 已拆分为独立开发 Issue | 依赖 #2 及服务端 ESP32 通道 |
| 阶段 B · V1.0 日常可用（唤醒/表情/电源/OTA） | 未开始 | — | 依赖阶段 A |
| 阶段 C · 极简英语口语练习场景 | 未开始 | — | 依赖阶段 B 与服务端场景隔离支持 |
